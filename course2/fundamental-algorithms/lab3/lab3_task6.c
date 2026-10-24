/*
 * Лабораторная 3, задание 6. Учет городского транспорта: хранилище маршрутов на односвязных списках.
 *
 * Каждый остановочный пункт пишет свой файл:
 *     x y                                                 - координаты пункта (первая непустая строка)
 *     номер dd.MM.yyyy hh:mm:ss dd.MM.yyyy hh:mm:ss метка   - одна остановка транспорта
 * номер - непустое слово (номер транспортного средства); первое время - прибытие, второе - отправление
 * (не раньше прибытия); метка - start (начальная остановка маршрута), middle (промежуточная) или end (конечная).
 *
 * Запуск:  lab3_task6 <файл1> [<файл2> ...]   - файлы в любом порядке, записи в файлах в любом порядке.
 * Хранилище - односвязный список маршрутов; маршрут - номер транспорта и односвязный список его остановок
 * по возрастанию времени (от start через middle к end).
 * Потом диалог - команды со стандартного ввода:
 *     routes max | routes min   - транспорт, проехавший больше / меньше всех маршрутов
 *     path max   | path min     - транспорт с самым длинным / коротким путем (сумма длин всех его маршрутов)
 *     route max  | route min    - транспорт с самым длинным / коротким маршрутом
 *     stop max   | stop min     - транспорт с самой долгой / короткой остановкой на пункте
 *     idle max                  - транспорт с самым большим временем простоя (сумма всех его остановок)
 *     list, help, exit
 * Длина маршрута - сумма расстояний между соседними остановками (по координатам пунктов, по прямой).
 * Если подходит несколько - берется первый по номеру (номера упорядочены по алфавиту).
 */
#define _POSIX_C_SOURCE 200809L   /* isatty, fileno - функции POSIX */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, strtod */
#include <string.h>   /* strcmp, memcpy */
#include <limits.h>   /* LLONG_MAX */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace */
#include <math.h>     /* sqrt, fabs */
#ifdef _WIN32
#include <io.h>       /* _isatty, _fileno */
#else
#include <unistd.h>   /* isatty, fileno */
#endif

#define TIME_TEXT_SIZE 20   /* "dd.MM.yyyy hh:mm:ss" + '\0' */
#define EPS 1e-9            /* точность сравнения длин путей (передается в функции параметром) */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,       /* NULL или неправильные данные в функции */
    INVALID_VALUE,      /* число, дата или метка в неправильном формате */
    END_OF_INPUT,       /* строки кончились (сигнал для цикла чтения) */
    NO_COORDINATES,     /* в файле нет строки с координатами */
    BAD_COORDINATES,    /* координаты - не два числа */
    BAD_RECORD,         /* в строке остановки не 6 слов */
    BAD_TIME,           /* дата или время не "dd.MM.yyyy hh:mm:ss" */
    BAD_ORDER,          /* отправление раньше прибытия */
    BAD_MARKER,         /* метка не start/middle/end */
    BAD_ROUTE,          /* остановки транспорта не складываются в маршруты start ... end */
    NO_ROUTES,          /* в хранилище нет маршрутов */
    ALLOC_ERROR         /* malloc / realloc не смогли выделить память */
} status_code;

typedef enum {
    MARKER_START,
    MARKER_MIDDLE,
    MARKER_END
} stop_marker;

/* Одна остановка (элемент списка остановок) */
typedef struct stop_node {
    double x;                              /* координаты пункта */
    double y;
    long long arrival;                     /* время прибытия - секунды от 01.01.0001 */
    long long departure;                   /* время отправления */
    char arrival_text[TIME_TEXT_SIZE];     /* то же текстом - для вывода */
    char departure_text[TIME_TEXT_SIZE];
    stop_marker marker;
    struct stop_node *next;
} stop_node;

/* Один маршрут (элемент основного списка): от start до end */
typedef struct route_node {
    char *vehicle;           /* номер транспорта */
    stop_node *stops;        /* остановки по возрастанию времени */
    size_t stop_count;
    struct route_node *next;
} route_node;

/* Временный список: все остановки одного транспорта (до разбиения на маршруты) */
typedef struct vehicle_node {
    char *number;
    stop_node *stops;
    struct vehicle_node *next;
} vehicle_node;

/* Сводка по одному транспорту - для ответов на вопросы */
typedef struct {
    const char *vehicle;
    size_t routes;              /* сколько маршрутов */
    double path;                /* сумма длин всех маршрутов */
    double longest_route;
    double shortest_route;
    long long longest_stop;     /* секунды */
    long long shortest_stop;
    long long idle;             /* сумма всех остановок, секунды */
    int has_stop;               /* 1 - longest_stop и shortest_stop уже заполнены */
} vehicle_stats;

/* ===================== память ===================== */

status_code free_stops(stop_node *stop) {
    while (stop != NULL) {
        stop_node *next = stop->next;
        free(stop);
        stop = next;
    }
    return OK;
}

status_code free_routes(route_node *route) {
    while (route != NULL) {
        route_node *next = route->next;
        free(route->vehicle);
        free_stops(route->stops);
        free(route);
        route = next;
    }
    return OK;
}

status_code free_vehicles(vehicle_node *vehicle) {
    while (vehicle != NULL) {
        vehicle_node *next = vehicle->next;
        free(vehicle->number);
        free_stops(vehicle->stops);
        free(vehicle);
        vehicle = next;
    }
    return OK;
}

status_code copy_string(const char *str, char **copy) {
    if (str == NULL || copy == NULL) {
        return INVALID_ARGS;
    }
    size_t length = strlen(str);
    char *result = (char *)malloc(length + 1);
    if (result == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(result, str, length + 1);
    *copy = result;
    return OK;
}

/* ===================== время ===================== */

/* Високосный год: делится на 4, но не на 100, или делится на 400 */
status_code is_leap_year(const long long year, int *leap) {
    if (leap == NULL) {
        return INVALID_ARGS;
    }
    *leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return OK;
}

/*
 * "dd.MM.yyyy" и "hh:mm:ss" -> секунды от 01.01.0001 00:00:00.
 * Дни: 365 на каждый прошедший год + високосные дни прошедших лет + дни прошедших месяцев + день.
 * text - то же время одной строкой для вывода.
 */
status_code parse_time(const char *date, const char *clock, long long *seconds, char text[TIME_TEXT_SIZE]) {
    if (date == NULL || clock == NULL || seconds == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    /* шаблоны: 'd' - цифра, остальное - ровно этот символ */
    const char date_pattern[] = "dd.dd.dddd";
    const char clock_pattern[] = "dd:dd:dd";
    for (int i = 0; i < 10; i++) {
        if (date[i] == '\0' || (date_pattern[i] == 'd' ? (date[i] < '0' || date[i] > '9') : date[i] != date_pattern[i])) {
            return INVALID_VALUE;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (clock[i] == '\0' || (clock_pattern[i] == 'd' ? (clock[i] < '0' || clock[i] > '9') : clock[i] != clock_pattern[i])) {
            return INVALID_VALUE;
        }
    }
    if (date[10] != '\0' || clock[8] != '\0') {
        return INVALID_VALUE;
    }
    long long day = (date[0] - '0') * 10 + (date[1] - '0');
    long long month = (date[3] - '0') * 10 + (date[4] - '0');
    long long year = (date[6] - '0') * 1000 + (date[7] - '0') * 100 + (date[8] - '0') * 10 + (date[9] - '0');
    long long hour = (clock[0] - '0') * 10 + (clock[1] - '0');
    long long minute = (clock[3] - '0') * 10 + (clock[4] - '0');
    long long second = (clock[6] - '0') * 10 + (clock[7] - '0');
    const int days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = 0;
    is_leap_year(year, &leap);
    if (year < 1 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) {
        return INVALID_VALUE;
    }
    if (day < 1 || day > days_in_month[month - 1] + (month == 2 && leap ? 1 : 0)) {
        return INVALID_VALUE;
    }
    long long y = year - 1;   /* сколько полных лет прошло */
    long long days = 365 * y + y / 4 - y / 100 + y / 400;
    for (long long m = 1; m < month; m++) {
        days += days_in_month[m - 1] + (m == 2 && leap ? 1 : 0);
    }
    days += day - 1;
    *seconds = ((days * 24 + hour) * 60 + minute) * 60 + second;
    memcpy(text, date, 10);
    text[10] = ' ';
    memcpy(text + 11, clock, 8);
    text[19] = '\0';
    return OK;
}

/* ===================== разбор строк ===================== */

/* Строка -> вещественное число: знак, цифры, не больше одной точки, необязательная экспонента
   (e или E, знак, цифры). "inf", "nan", "0x1p3" не принимаются; само число считает strtod */
status_code parse_double(const char *str, double *value) {
    if (str == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    if (str[i] == '+' || str[i] == '-') {
        i++;
    }
    int digits = 0;
    int dot = 0;
    while ((str[i] >= '0' && str[i] <= '9') || str[i] == '.') {
        if (str[i] == '.') {
            if (dot) {
                return INVALID_VALUE;
            }
            dot = 1;
        } else {
            digits++;
        }
        i++;
    }
    if (digits == 0) {
        return INVALID_VALUE;
    }
    if (str[i] == 'e' || str[i] == 'E') {
        i++;
        if (str[i] == '+' || str[i] == '-') {
            i++;
        }
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_VALUE;
        }
        while (str[i] >= '0' && str[i] <= '9') {
            i++;
        }
    }
    if (str[i] != '\0') {
        return INVALID_VALUE;
    }
    double result = strtod(str, NULL);
    /* слишком большое число strtod превращает в бесконечность - такое не подходит */
    if (!(result <= 1e300 && result >= -1e300)) {
        return INVALID_VALUE;
    }
    *value = result;
    return OK;
}

/* Разбить строку на слова (пробелы, табуляции, '\r'); *count - сколько слов на самом деле */
status_code split_words(char *line, char **words, const size_t max_words, size_t *count) {
    if (line == NULL || words == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    size_t k = 0;
    size_t i = 0;
    while (line[i] != '\0') {
        while (line[i] != '\0' && isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] == '\0') {
            break;
        }
        if (k < max_words) {
            words[k] = &line[i];
        }
        k++;
        while (line[i] != '\0' && !isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] != '\0') {
            line[i] = '\0';
            i++;
        }
    }
    *count = k;
    return OK;
}

/* Прочитать строку любой длины (без '\n'); END_OF_INPUT - строк больше нет */
status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t buf_size = 16;
    size_t length = 0;
    char *buf = (char *)malloc(buf_size);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    int c = fgetc(in);
    if (c == EOF) {
        free(buf);
        return END_OF_INPUT;
    }
    while (c != EOF && c != '\n') {
        if (length + 1 >= buf_size) {
            if (buf_size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            char *new_ptr = (char *)realloc(buf, buf_size * 2);
            if (new_ptr == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = new_ptr;
            buf_size *= 2;
        }
        buf[length] = (char)c;
        length++;
        c = fgetc(in);
    }
    buf[length] = '\0';
    *line = buf;
    return OK;
}

/* Пропустить метку BOM (EF BB BF) в начале файла - ее пишет Блокнот */
status_code skip_bom(FILE *in) {
    if (in == NULL) {
        return INVALID_ARGS;
    }
    int c = fgetc(in);
    if (c != 0xEF) {
        if (c != EOF) {
            ungetc(c, in);
        }
        return OK;
    }
    c = fgetc(in);
    if (c == 0xBB) {
        c = fgetc(in);
        if (c != 0xBF && c != EOF) {
            ungetc(c, in);
        }
    } else if (c != EOF) {
        ungetc(c, in);
    }
    return OK;
}

/* ===================== сборка хранилища ===================== */

/* Найти транспорт в списке (упорядочен по номеру) или вставить новый на свое место */
status_code find_or_add_vehicle(vehicle_node **head, const char *number, vehicle_node **vehicle) {
    if (head == NULL || number == NULL || vehicle == NULL) {
        return INVALID_ARGS;
    }
    /* link - указатель на поле next предыдущего элемента (или на голову): через него вставляем */
    vehicle_node **link = head;
    while (*link != NULL && strcmp((*link)->number, number) < 0) {
        link = &(*link)->next;
    }
    if (*link != NULL && strcmp((*link)->number, number) == 0) {
        *vehicle = *link;
        return OK;
    }
    vehicle_node *node = (vehicle_node *)malloc(sizeof(vehicle_node));
    if (node == NULL) {
        return ALLOC_ERROR;
    }
    if (copy_string(number, &node->number) != OK) {
        free(node);
        return ALLOC_ERROR;
    }
    node->stops = NULL;
    node->next = *link;
    *link = node;
    *vehicle = node;
    return OK;
}

/* Вставить остановку в список по возрастанию времени прибытия (при равном - по отправлению) */
status_code insert_stop(stop_node **head, stop_node *stop) {
    if (head == NULL || stop == NULL) {
        return INVALID_ARGS;
    }
    stop_node **link = head;
    while (*link != NULL && ((*link)->arrival < stop->arrival
                             || ((*link)->arrival == stop->arrival && (*link)->departure <= stop->departure))) {
        link = &(*link)->next;
    }
    stop->next = *link;
    *link = stop;
    return OK;
}

/* Разобрать строку остановки "номер дата время дата время метка" и добавить в список транспорта */
status_code add_record(vehicle_node **vehicles, char *line, const double x, const double y) {
    if (vehicles == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    char *words[6];
    size_t count = 0;
    split_words(line, words, 6, &count);
    if (count != 6) {
        return BAD_RECORD;
    }
    stop_node *stop = (stop_node *)malloc(sizeof(stop_node));
    if (stop == NULL) {
        return ALLOC_ERROR;
    }
    stop->x = x;
    stop->y = y;
    stop->next = NULL;
    status_code sc = OK;
    if (parse_time(words[1], words[2], &stop->arrival, stop->arrival_text) != OK
        || parse_time(words[3], words[4], &stop->departure, stop->departure_text) != OK) {
        sc = BAD_TIME;
    } else if (stop->departure < stop->arrival) {
        sc = BAD_ORDER;
    } else if (strcmp(words[5], "start") == 0) {
        stop->marker = MARKER_START;
    } else if (strcmp(words[5], "middle") == 0) {
        stop->marker = MARKER_MIDDLE;
    } else if (strcmp(words[5], "end") == 0) {
        stop->marker = MARKER_END;
    } else {
        sc = BAD_MARKER;
    }
    vehicle_node *vehicle = NULL;
    if (sc == OK) {
        sc = find_or_add_vehicle(vehicles, words[0], &vehicle);
    }
    if (sc != OK) {
        free(stop);
        return sc;
    }
    return insert_stop(&vehicle->stops, stop);
}

/*
 * Прочитать один файл пункта: координаты, потом остановки. *line_number - номер строки с ошибкой.
 */
status_code read_stop_file(FILE *in, vehicle_node **vehicles, size_t *line_number) {
    if (in == NULL || vehicles == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    *line_number = 0;
    skip_bom(in);
    int have_coordinates = 0;
    double x = 0.0;
    double y = 0.0;
    status_code sc = OK;
    while (sc == OK) {
        char *line = NULL;
        sc = read_line(in, &line);
        if (sc == END_OF_INPUT) {
            sc = have_coordinates ? OK : NO_COORDINATES;
            break;
        }
        if (sc != OK) {
            break;
        }
        (*line_number)++;
        char *words[2];
        size_t count = 0;
        if (!have_coordinates) {
            split_words(line, words, 2, &count);
            if (count == 0) {
                free(line);   /* пустая строка */
                continue;
            }
            if (count != 2 || parse_double(words[0], &x) != OK || parse_double(words[1], &y) != OK) {
                sc = BAD_COORDINATES;
            }
            have_coordinates = 1;
        } else {
            /* пустая строка? (split_words меняет строку - проверяем по первому непробельному символу) */
            size_t i = 0;
            while (line[i] != '\0' && isspace((unsigned char)line[i])) {
                i++;
            }
            if (line[i] != '\0') {
                sc = add_record(vehicles, line, x, y);
            }
        }
        free(line);
    }
    return sc;
}

/*
 * Разбить остановки каждого транспорта на маршруты и собрать основной список.
 * Остановки транспорта уже по времени; правильная последовательность:
 *     start middle* end   start middle* end   ...
 * Соседние остановки не должны пересекаться по времени (прибытие не раньше прошлого отправления).
 * Список vehicles разбирается: остановки переходят в маршруты, сами узлы транспорта освобождаются.
 * *bad_vehicle - номер транспорта с ошибкой (копия, освобождает вызывающий).
 */
status_code build_routes(vehicle_node **vehicles, route_node **routes, char **bad_vehicle) {
    if (vehicles == NULL || routes == NULL || bad_vehicle == NULL) {
        return INVALID_ARGS;
    }
    *routes = NULL;
    *bad_vehicle = NULL;
    route_node **tail = routes;   /* куда прицепить следующий маршрут */
    status_code sc = OK;
    while (*vehicles != NULL && sc == OK) {
        vehicle_node *vehicle = *vehicles;
        while (vehicle->stops != NULL && sc == OK) {
            stop_node *first = vehicle->stops;
            if (first->marker != MARKER_START) {
                sc = BAD_ROUTE;
                break;
            }
            /* ищем конец маршрута: первую остановку end */
            stop_node *last = first;
            size_t count = 1;
            while (last->marker != MARKER_END) {
                stop_node *next = last->next;
                if (next == NULL || next->marker == MARKER_START || next->arrival < last->departure) {
                    sc = BAD_ROUTE;   /* маршрут не кончился, начался новый или остановки пересекаются */
                    break;
                }
                last = next;
                count++;
            }
            if (sc != OK) {
                break;
            }
            /* следующий маршрут не может начаться раньше, чем кончился этот */
            if (last->next != NULL && last->next->arrival < last->departure) {
                sc = BAD_ROUTE;
                break;
            }
            route_node *route = (route_node *)malloc(sizeof(route_node));
            if (route == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            if (copy_string(vehicle->number, &route->vehicle) != OK) {
                free(route);
                sc = ALLOC_ERROR;
                break;
            }
            /* отрезаем остановки first..last от списка транспорта и отдаем маршруту */
            vehicle->stops = last->next;
            last->next = NULL;
            route->stops = first;
            route->stop_count = count;
            route->next = NULL;
            *tail = route;
            tail = &route->next;
        }
        if (sc == BAD_ROUTE) {
            copy_string(vehicle->number, bad_vehicle);   /* не хватит памяти - сообщим без номера */
            break;
        }
        if (sc != OK) {
            break;
        }
        /* транспорт разобран полностью - удаляем его узел */
        *vehicles = vehicle->next;
        vehicle->next = NULL;
        free_vehicles(vehicle);
    }
    if (sc != OK) {
        free_routes(*routes);
        *routes = NULL;
    }
    return sc;
}

/* ===================== вопросы к хранилищу ===================== */

/* Длина маршрута: сумма расстояний между соседними остановками */
status_code route_length(const route_node *route, double *length) {
    if (route == NULL || length == NULL) {
        return INVALID_ARGS;
    }
    double total = 0.0;
    for (const stop_node *s = route->stops; s != NULL && s->next != NULL; s = s->next) {
        double dx = s->next->x - s->x;
        double dy = s->next->y - s->y;
        total += sqrt(dx * dx + dy * dy);
    }
    *length = total;
    return OK;
}

/*
 * Сводка по каждому транспорту. Маршруты одного транспорта в списке идут подряд
 * (транспорт упорядочен по номеру), поэтому сводка собирается одним проходом.
 * eps - точность сравнения длин (самый длинный / короткий маршрут транспорта).
 */
status_code collect_stats(const route_node *routes, const double eps, vehicle_stats **stats, size_t *count) {
    if (stats == NULL || count == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    *stats = NULL;
    *count = 0;
    size_t capacity = 0;
    vehicle_stats *result = NULL;
    for (const route_node *r = routes; r != NULL; r = r->next) {
        /* новый транспорт - новая запись сводки */
        if (*count == 0 || strcmp(result[*count - 1].vehicle, r->vehicle) != 0) {
            if (*count == capacity) {
                size_t new_capacity = capacity == 0 ? 8 : capacity * 2;
                vehicle_stats *tmp = (vehicle_stats *)realloc(result, new_capacity * sizeof(vehicle_stats));
                if (tmp == NULL) {
                    free(result);
                    *count = 0;
                    return ALLOC_ERROR;
                }
                result = tmp;
                capacity = new_capacity;
            }
            vehicle_stats empty = {r->vehicle, 0, 0.0, 0.0, 0.0, 0, 0, 0, 0};
            result[*count] = empty;
            (*count)++;
        }
        vehicle_stats *v = &result[*count - 1];
        double length = 0.0;
        route_length(r, &length);
        if (v->routes == 0 || length > v->longest_route + eps) {
            v->longest_route = length;
        }
        if (v->routes == 0 || length < v->shortest_route - eps) {
            v->shortest_route = length;
        }
        v->routes++;
        v->path += length;
        for (const stop_node *s = r->stops; s != NULL; s = s->next) {
            long long duration = s->departure - s->arrival;
            /* первая остановка транспорта сразу и самая долгая, и самая короткая */
            if (!v->has_stop || duration > v->longest_stop) {
                v->longest_stop = duration;
            }
            if (!v->has_stop || duration < v->shortest_stop) {
                v->shortest_stop = duration;
            }
            v->has_stop = 1;
            v->idle += duration;
        }
    }
    *stats = result;
    return OK;
}

/* Какой вопрос задан */
typedef enum {
    ASK_ROUTES,   /* число маршрутов */
    ASK_PATH,     /* сумма длин всех маршрутов */
    ASK_ROUTE,    /* самый длинный / короткий маршрут */
    ASK_STOP,     /* самая долгая / короткая остановка */
    ASK_IDLE      /* общее время остановок */
} question;

/* Значение сводки для вопроса. Целые величины тоже отдаем как double - выбор максимума
   ниже один для всех; целые до 2^53 переводятся в double точно */
status_code stat_value(const vehicle_stats *v, const question q, const int want_max, double *value) {
    if (v == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    switch (q) {
        case ASK_ROUTES:
            *value = (double)v->routes;
            break;
        case ASK_PATH:
            *value = v->path;
            break;
        case ASK_ROUTE:
            *value = want_max ? v->longest_route : v->shortest_route;
            break;
        case ASK_STOP:
            *value = (double)(want_max ? v->longest_stop : v->shortest_stop);
            break;
        case ASK_IDLE:
            *value = (double)v->idle;
            break;
    }
    return OK;
}

/*
 * Ответ на вопрос: транспорт с наибольшим (want_max) или наименьшим значением.
 * Новый лидер - только если лучше больше чем на eps: при равенстве остается первый по номеру.
 */
status_code answer_question(const route_node *routes, const question q, const int want_max, const double eps,
                            const char **vehicle, double *value) {
    if (vehicle == NULL || value == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    vehicle_stats *stats = NULL;
    size_t count = 0;
    status_code sc = collect_stats(routes, eps, &stats, &count);
    if (sc != OK) {
        return sc;
    }
    if (count == 0) {
        free(stats);
        return NO_ROUTES;
    }
    size_t best = 0;
    double best_value = 0.0;
    stat_value(&stats[0], q, want_max, &best_value);
    for (size_t i = 1; i < count; i++) {
        double current = 0.0;
        stat_value(&stats[i], q, want_max, &current);
        if (want_max ? current > best_value + eps : current < best_value - eps) {
            best = i;
            best_value = current;
        }
    }
    *vehicle = stats[best].vehicle;   /* строка принадлежит маршруту, не сводке */
    *value = best_value;
    free(stats);
    return OK;
}

/* ===================== вывод ===================== */

/* Секунды -> "часы:мм:сс" (часов может быть больше 24) */
status_code print_duration(FILE *out, const long long seconds) {
    if (out == NULL || seconds < 0) {
        return INVALID_ARGS;
    }
    fprintf(out, "%lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    return OK;
}

status_code print_routes(FILE *out, const route_node *routes) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    const char *names[3] = {"start", "middle", "end"};
    size_t count = 0;
    for (const route_node *r = routes; r != NULL; r = r->next) {
        count++;
    }
    fprintf(out, "Routes: %zu\n", count);
    for (const route_node *r = routes; r != NULL; r = r->next) {
        double length = 0.0;
        route_length(r, &length);
        fprintf(out, "%s: %zu stops, length %.2f\n", r->vehicle, r->stop_count, length);
        for (const stop_node *s = r->stops; s != NULL; s = s->next) {
            fprintf(out, "  (%g, %g) %s - %s %s\n", s->x, s->y, s->arrival_text, s->departure_text, names[s->marker]);
        }
    }
    return OK;
}

status_code print_help(FILE *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Commands: routes max|min, path max|min, route max|min, stop max|min, idle max,\n");
    fprintf(out, "          list, help, exit\n");
    return OK;
}

/* ===================== диалог ===================== */

status_code run_command(const route_node *routes, char *line, int *finish) {
    if (line == NULL || finish == NULL) {
        return INVALID_ARGS;
    }
    *finish = 0;
    char *words[3];
    size_t count = 0;
    split_words(line, words, 3, &count);
    if (count == 0) {
        return OK;
    }
    if (count == 1 && strcmp(words[0], "exit") == 0) {
        *finish = 1;
        return OK;
    }
    if (count == 1 && strcmp(words[0], "help") == 0) {
        return print_help(stdout);
    }
    if (count == 1 && strcmp(words[0], "list") == 0) {
        return print_routes(stdout, routes);
    }
    /* вопрос: слово и max/min */
    const char *names[5] = {"routes", "path", "route", "stop", "idle"};
    int found = -1;
    for (int i = 0; i < 5; i++) {
        if (strcmp(words[0], names[i]) == 0) {
            found = i;
        }
    }
    if (found < 0) {
        fprintf(stderr, "Error: unknown command '%s' (type help)\n", words[0]);
        return OK;
    }
    int is_max = count == 2 && strcmp(words[1], "max") == 0;
    int is_min = count == 2 && strcmp(words[1], "min") == 0;
    if (!is_max && !(is_min && found != ASK_IDLE)) {
        /* у простоя спрашивается только наибольший */
        if (found == ASK_IDLE) {
            fprintf(stderr, "Error: use: idle max\n");
        } else {
            fprintf(stderr, "Error: use: %s max or %s min\n", words[0], words[0]);
        }
        return OK;
    }
    const char *vehicle = NULL;
    double value = 0.0;
    status_code sc = answer_question(routes, (question)found, is_max, EPS, &vehicle, &value);
    if (sc == NO_ROUTES) {
        fprintf(stderr, "Error: there are no routes\n");
        return OK;
    }
    if (sc != OK) {
        return sc;   /* ALLOC_ERROR */
    }
    printf("%s %s: %s, ", words[0], words[1], vehicle);
    switch ((question)found) {
        case ASK_ROUTES:
            printf("%.0f routes\n", value);
            break;
        case ASK_PATH:
        case ASK_ROUTE:
            printf("length %.2f\n", value);
            break;
        case ASK_STOP:
        case ASK_IDLE:
            print_duration(stdout, (long long)value);
            printf("\n");
            break;
    }
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: lab3_task6 <stop file> [<stop file> ...]\n");
        return 1;
    }

    /* 1) все файлы пунктов: остановки раскладываются по транспорту и по времени */
    vehicle_node *vehicles = NULL;
    for (int i = 1; i < argc; i++) {
        FILE *in = fopen(argv[i], "r");
        if (in == NULL) {
            fprintf(stderr, "Error: can not open file '%s'\n", argv[i]);
            free_vehicles(vehicles);
            return 1;
        }
        size_t line_number = 0;
        status_code sc = read_stop_file(in, &vehicles, &line_number);
        fclose(in);
        if (sc != OK) {
            switch (sc) {
                case NO_COORDINATES:
                    fprintf(stderr, "Error: file '%s': no coordinates line\n", argv[i]);
                    break;
                case BAD_COORDINATES:
                    fprintf(stderr, "Error: file '%s' line %zu: coordinates must be two numbers: x y\n", argv[i], line_number);
                    break;
                case BAD_RECORD:
                    fprintf(stderr, "Error: file '%s' line %zu: must be: number date time date time marker\n", argv[i], line_number);
                    break;
                case BAD_TIME:
                    fprintf(stderr, "Error: file '%s' line %zu: time must be dd.MM.yyyy hh:mm:ss\n", argv[i], line_number);
                    break;
                case BAD_ORDER:
                    fprintf(stderr, "Error: file '%s' line %zu: departure is earlier than arrival\n", argv[i], line_number);
                    break;
                case BAD_MARKER:
                    fprintf(stderr, "Error: file '%s' line %zu: marker must be start, middle or end\n", argv[i], line_number);
                    break;
                case ALLOC_ERROR:
                    fprintf(stderr, "Error: not enough memory\n");
                    break;
                default:
                    fprintf(stderr, "Error: internal error\n");
                    break;
            }
            free_vehicles(vehicles);
            return 1;
        }
    }

    /* 2) маршруты */
    route_node *routes = NULL;
    char *bad_vehicle = NULL;
    status_code sc = build_routes(&vehicles, &routes, &bad_vehicle);
    free_vehicles(vehicles);   /* после успеха список уже пуст; после ошибки - остаток */
    if (sc == BAD_ROUTE) {
        fprintf(stderr, "Error: stops of vehicle %s do not form routes start ... end without overlaps\n",
                bad_vehicle != NULL ? bad_vehicle : "?");
        free(bad_vehicle);
        return 1;
    }
    if (sc != OK) {
        fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: internal error\n");
        return 1;
    }

    /* 3) диалог */
#ifdef _WIN32
    int interactive = _isatty(_fileno(stdin));
#else
    int interactive = isatty(fileno(stdin));
#endif
    if (interactive) {
        print_help(stdout);
    }
    int result = 0;
    int finish = 0;
    while (!finish) {
        if (interactive) {
            printf("> ");
            fflush(stdout);
        }
        char *line = NULL;
        sc = read_line(stdin, &line);
        if (sc == END_OF_INPUT) {
            break;
        }
        if (sc == OK) {
            sc = run_command(routes, line, &finish);
            free(line);
        }
        if (sc != OK) {
            fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: internal error\n");
            result = 1;
            break;
        }
    }
    free_routes(routes);
    return result;
}
