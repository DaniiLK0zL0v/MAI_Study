/*
 * Лабораторная 3, задание 4. Тип String и почтовое отделение (Post / Mail / Address).
 *
 * 1) String - строка в динамической памяти и ее длина. Функции: создать из char *, очистить,
 *    сравнить (сначала по длине, при равной длине - лексикографически), проверить на равенство,
 *    скопировать в существующую String, скопировать в новую String в динамической памяти,
 *    дописать одну String к другой. Демонстрация - команда demo в диалоге.
 * 2) Почтовое отделение: адрес отделения и динамический массив отправлений (Mail), всегда
 *    упорядоченный по индексу получателя, а при равных индексах - по идентификатору.
 *    Все строковые данные хранятся в String.
 *
 * Запуск:  lab3_task4      - сначала вводится адрес отделения, потом команды (по одной в строке):
 *     add             добавить отправление (поля вводятся по одному, см. ask_mail)
 *     remove <id>     удалить отправление
 *     find <id>       показать отправление
 *     list            все отправления (по индексу, затем по id)
 *     deliver <id>    отметить, что отправление вручено
 *     delivered       все врученные - по времени создания, от старых к новым
 *     expired         все неврученные, у которых срок вручения уже прошел (по системному времени)
 *     office          адрес отделения
 *     demo            демонстрация функций String
 *     help, exit
 * Подсказки ("Enter city:") печатаются, только если ввод идет с клавиатуры; при вводе из файла
 * печатаются только ответы на команды.
 *
 * Время в формате "dd:MM:yyyy hh:mm:ss". Поле "время вручения" понимается как срок вручения:
 * по одному времени нельзя различить "доставлено" и "срок истек", поэтому факт вручения
 * отмечает команда deliver.
 */
#define _POSIX_C_SOURCE 200809L   /* для isatty и fileno на Linux (в C99 их нет) */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort, strtod */
#include <string.h>   /* memcpy, strcmp */
#include <limits.h>   /* INT_MAX, LLONG_MAX */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace */
#include <time.h>     /* time, localtime - системное время для команды expired */
#ifdef _WIN32
#include <io.h>       /* _isatty, _fileno */
#else
#include <unistd.h>   /* isatty, fileno */
#endif

#define ID_LENGTH 14       /* почтовый идентификатор - 14 цифр */
#define INDEX_LENGTH 6     /* индекс - 6 цифр */
#define DATETIME_LENGTH 19 /* "dd:MM:yyyy hh:mm:ss" */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    INVALID_VALUE,     /* значение поля не подходит (не число, не та длина, ...) */
    OVERFLOW_ERROR,    /* число или длина строки не помещается в тип */
    NOT_FOUND,         /* нет отправления с таким id */
    DUPLICATE,         /* отправление с таким id уже есть */
    END_OF_INPUT,      /* ввод кончился */
    TIME_ERROR,        /* не удалось узнать системное время */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* ===================================================================== */
/* String                                                                */
/* ===================================================================== */

/* Строка: data - массив символов в динамической памяти (с '\0' в конце), length - сколько символов.
   Пустая String: data == NULL, length == 0 */
typedef struct {
    char *data;
    int length;
} String;

/* Создать String из обычной строки C: копия символов в динамической памяти */
status_code string_create(const char *src, String *result) {
    if (src == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t length = 0;
    while (src[length] != '\0') {
        length++;
    }
    if (length > (size_t)INT_MAX) {
        return OVERFLOW_ERROR;   /* длина хранится в int */
    }
    char *data = (char *)malloc(length + 1);
    if (data == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(data, src, length + 1);   /* вместе с '\0' */
    result->data = data;
    result->length = (int)length;
    return OK;
}

/* Удалить содержимое: освободить память, String становится пустой */
status_code string_clear(String *str) {
    if (str == NULL) {
        return INVALID_ARGS;
    }
    free(str->data);
    str->data = NULL;
    str->length = 0;
    return OK;
}

/*
 * Отношение порядка: сначала по длине (короче - меньше), при равной длине - лексикографически
 * (как в словаре: по первому различающемуся символу). *order < 0 - a меньше, 0 - равны, > 0 - a больше.
 */
status_code string_compare(const String *a, const String *b, int *order) {
    if (a == NULL || b == NULL || order == NULL) {
        return INVALID_ARGS;
    }
    if (a->length != b->length) {
        *order = a->length < b->length ? -1 : 1;
        return OK;
    }
    /* длины равны: strcmp сравнивает по символам (как unsigned char).
       У очищенной String data == NULL - для сравнения это пустая строка "" */
    int result = strcmp(a->data != NULL ? a->data : "", b->data != NULL ? b->data : "");
    *order = (result > 0) - (result < 0);
    return OK;
}

/* Отношение эквивалентности: *equal = 1, если строки совпадают посимвольно (лексикографически равны) */
status_code string_equal(const String *a, const String *b, int *equal) {
    if (a == NULL || b == NULL || equal == NULL) {
        return INVALID_ARGS;
    }
    int order = 0;
    string_compare(a, b, &order);   /* сначала длина, потом символы - равны, только если order == 0 */
    *equal = order == 0;
    return OK;
}

/* Копировать содержимое src в уже существующую dst (старое содержимое dst освобождается).
   Сначала делаем копию, потом освобождаем старое: при нехватке памяти dst остается целой,
   и копирование строки в саму себя тоже работает */
status_code string_copy(const String *src, String *dst) {
    if (src == NULL || dst == NULL) {
        return INVALID_ARGS;
    }
    String tmp;
    status_code sc = string_create(src->data != NULL ? src->data : "", &tmp);
    if (sc != OK) {
        return sc;
    }
    string_clear(dst);
    *dst = tmp;
    return OK;
}

/* Копировать содержимое src в новую String в динамической памяти.
   Освобождать: string_clear(*result), затем free(*result) */
status_code string_duplicate(const String *src, String **result) {
    if (src == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    String *copy = (String *)malloc(sizeof(String));
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    status_code sc = string_create(src->data != NULL ? src->data : "", copy);
    if (sc != OK) {
        free(copy);
        return sc;
    }
    *result = copy;
    return OK;
}

/* Дописать содержимое src в конец dst. src и dst могут быть одной и той же String */
status_code string_concat(String *dst, const String *src) {
    if (dst == NULL || src == NULL) {
        return INVALID_ARGS;
    }
    if (src->length > INT_MAX - dst->length) {
        return OVERFLOW_ERROR;
    }
    int src_length = src->length;   /* запоминаем до realloc: src может быть тем же, что dst */
    int new_length = dst->length + src_length;
    /* realloc через временную переменную: при неудаче dst не меняется */
    char *data = (char *)realloc(dst->data, (size_t)new_length + 1);
    if (data == NULL) {
        return ALLOC_ERROR;
    }
    /* если src == dst, его данные теперь тоже по адресу data */
    const char *from = (src == dst) ? data : src->data;
    if (src_length > 0) {   /* у пустой src копировать нечего (и data может быть NULL) */
        memcpy(data + dst->length, from, (size_t)src_length);
    }
    data[new_length] = '\0';
    dst->data = data;
    dst->length = new_length;
    return OK;
}

/* ===================================================================== */
/* Почта: структуры                                                       */
/* ===================================================================== */

typedef struct {
    String city;        /* город, непустая строка */
    String street;      /* улица, непустая строка */
    int house;          /* номер дома, натуральное число */
    String building;    /* корпус, строка (может быть пустой) */
    int apartment;      /* номер квартиры, натуральное число */
    String index;       /* индекс получателя - 6 цифр */
} Address;

typedef struct {
    Address to;         /* адрес получателя */
    double weight;      /* вес, неотрицательное число */
    String id;          /* почтовый идентификатор - 14 цифр */
    String created;     /* время создания "dd:MM:yyyy hh:mm:ss" */
    String delivery;    /* время (срок) вручения "dd:MM:yyyy hh:mm:ss" */
    int delivered;      /* 1 - отправление вручено (команда deliver) */
    long long created_key;    /* время создания числом yyyyMMddhhmmss - для сравнений */
    long long delivery_key;   /* срок вручения числом yyyyMMddhhmmss */
} Mail;

typedef struct {
    Address *office;    /* адрес отделения (в динамической памяти) */
    Mail *mails;        /* отправления, упорядочены по индексу, затем по id */
    size_t count;
    size_t capacity;
} Post;

status_code address_clear(Address *address) {
    if (address == NULL) {
        return INVALID_ARGS;
    }
    string_clear(&address->city);
    string_clear(&address->street);
    string_clear(&address->building);
    string_clear(&address->index);
    return OK;
}

status_code mail_clear(Mail *mail) {
    if (mail == NULL) {
        return INVALID_ARGS;
    }
    address_clear(&mail->to);
    string_clear(&mail->id);
    string_clear(&mail->created);
    string_clear(&mail->delivery);
    return OK;
}

/* Пустой адрес: все строки пустые, числа 0 (такой адрес безопасно очищать).
   memset обнуляет и байты выравнивания между полями: иначе при копировании структуры
   копировались бы неинициализированные байты (Dr. Memory сообщает об этом) */
status_code address_init(Address *address) {
    if (address == NULL) {
        return INVALID_ARGS;
    }
    memset(address, 0, sizeof(Address));
    String empty = {NULL, 0};
    address->city = empty;
    address->street = empty;
    address->building = empty;
    address->index = empty;
    address->house = 0;
    address->apartment = 0;
    return OK;
}

/* Пустое отправление - так же, как пустой адрес */
status_code mail_init(Mail *mail) {
    if (mail == NULL) {
        return INVALID_ARGS;
    }
    memset(mail, 0, sizeof(Mail));
    address_init(&mail->to);
    String empty = {NULL, 0};
    mail->weight = 0.0;
    mail->id = empty;
    mail->created = empty;
    mail->delivery = empty;
    mail->delivered = 0;
    mail->created_key = 0;
    mail->delivery_key = 0;
    return OK;
}

status_code post_destroy(Post *post) {
    if (post == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < post->count; i++) {
        mail_clear(&post->mails[i]);
    }
    free(post->mails);
    post->mails = NULL;
    post->count = 0;
    post->capacity = 0;
    if (post->office != NULL) {
        address_clear(post->office);
        free(post->office);
        post->office = NULL;
    }
    return OK;
}

/* ===================================================================== */
/* Проверка значений полей                                                */
/* ===================================================================== */

/* Строка из ровно length цифр */
status_code check_digits(const char *str, const int length) {
    if (str == NULL) {
        return INVALID_ARGS;
    }
    int i = 0;
    for (; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9' || i >= length) {
            return INVALID_VALUE;
        }
    }
    return i == length ? OK : INVALID_VALUE;
}

/* Натуральное число (1..INT_MAX), только цифры */
status_code parse_natural(const char *str, int *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_VALUE;
    }
    long long value = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_VALUE;
        }
        value = value * 10 + (str[i] - '0');
        if (value > INT_MAX) {
            return OVERFLOW_ERROR;
        }
    }
    if (value < 1) {
        return INVALID_VALUE;
    }
    *number = (int)value;
    return OK;
}

/* Неотрицательное вещественное число: цифры и не больше одной точки ("2", "0.5", "1.", ".5").
   Знаки, экспонента, "inf", "nan" не принимаются; само число считает strtod */
status_code parse_weight(const char *str, double *weight) {
    if (str == NULL || weight == NULL) {
        return INVALID_ARGS;
    }
    int digits = 0;
    int dots = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            digits++;
        } else if (str[i] == '.') {
            dots++;
        } else {
            return INVALID_VALUE;
        }
    }
    if (digits == 0 || dots > 1) {
        return INVALID_VALUE;
    }
    double value = strtod(str, NULL);
    /* очень длинное число может не поместиться в double - strtod вернет бесконечность;
       бесконечность больше любого конечного числа */
    if (value > 1e300) {
        return OVERFLOW_ERROR;
    }
    *weight = value;
    return OK;
}

/* Високосный год: делится на 4, но не на 100, или делится на 400 */
status_code is_leap_year(const int year, int *leap) {
    if (leap == NULL) {
        return INVALID_ARGS;
    }
    *leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return OK;
}

/*
 * Проверить время "dd:MM:yyyy hh:mm:ss" и получить ключ для сравнения:
 * число yyyyMMddhhmmss - у более позднего времени ключ больше.
 */
status_code parse_datetime(const char *str, long long *key) {
    if (str == NULL || key == NULL) {
        return INVALID_ARGS;
    }
    /* шаблон: 'd' - цифра, остальное - ровно этот символ */
    const char pattern[] = "dd:dd:dddd dd:dd:dd";
    for (int i = 0; i < DATETIME_LENGTH; i++) {
        if (str[i] == '\0') {
            return INVALID_VALUE;
        }
        if (pattern[i] == 'd' ? (str[i] < '0' || str[i] > '9') : str[i] != pattern[i]) {
            return INVALID_VALUE;
        }
    }
    if (str[DATETIME_LENGTH] != '\0') {
        return INVALID_VALUE;
    }
    /* число из цифр с позиции from, count штук */
    int part[6];
    const int from[6] = {0, 3, 6, 11, 14, 17};
    const int count[6] = {2, 2, 4, 2, 2, 2};
    for (int p = 0; p < 6; p++) {
        part[p] = 0;
        for (int j = 0; j < count[p]; j++) {
            part[p] = part[p] * 10 + (str[from[p] + j] - '0');
        }
    }
    int day = part[0], month = part[1], year = part[2], hour = part[3], minute = part[4], second = part[5];
    if (year < 1 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) {
        return INVALID_VALUE;
    }
    const int days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = 0;
    is_leap_year(year, &leap);
    int max_day = days_in_month[month - 1] + (month == 2 && leap ? 1 : 0);
    if (day < 1 || day > max_day) {
        return INVALID_VALUE;
    }
    *key = ((((year * 100LL + month) * 100 + day) * 100 + hour) * 100 + minute) * 100 + second;
    return OK;
}

/* Текущее системное (местное) время в виде того же ключа yyyyMMddhhmmss */
status_code current_time_key(long long *key) {
    if (key == NULL) {
        return INVALID_ARGS;
    }
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        return TIME_ERROR;
    }
    /* localtime возвращает указатель на свою внутреннюю структуру - сразу забираем поля */
    const struct tm *local = localtime(&now);
    if (local == NULL) {
        return TIME_ERROR;
    }
    *key = (((((local->tm_year + 1900) * 100LL + local->tm_mon + 1) * 100 + local->tm_mday) * 100
             + local->tm_hour) * 100 + local->tm_min) * 100 + local->tm_sec;
    return OK;
}

/* ===================================================================== */
/* Почта: операции с данными (ничего не печатают)                         */
/* ===================================================================== */

/* Порядок хранения: по индексу, при равных индексах - по id. Индексы и id - строки цифр
   одной длины, поэтому string_compare (по длине, потом по символам) сравнивает их как числа */
status_code mail_order(const Mail *a, const Mail *b, int *order) {
    if (a == NULL || b == NULL || order == NULL) {
        return INVALID_ARGS;
    }
    string_compare(&a->to.index, &b->to.index, order);
    if (*order == 0) {
        string_compare(&a->id, &b->id, order);
    }
    return OK;
}

/* Найти отправление по id: *position - его номер в массиве */
status_code post_find(const Post *post, const String *id, size_t *position) {
    if (post == NULL || id == NULL || position == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < post->count; i++) {
        int equal = 0;
        string_equal(&post->mails[i].id, id, &equal);
        if (equal) {
            *position = i;
            return OK;
        }
    }
    return NOT_FOUND;
}

/*
 * Добавить отправление. Массив всегда упорядочен: новое вставляется на свое место
 * (остальные сдвигаются), поэтому порядок верен в любой момент.
 * Строки *mail переходят в массив (после успеха вызывающий их не освобождает).
 */
status_code post_add(Post *post, Mail *mail) {
    if (post == NULL || mail == NULL) {
        return INVALID_ARGS;
    }
    size_t found = 0;
    if (post_find(post, &mail->id, &found) == OK) {
        return DUPLICATE;
    }
    if (post->count == post->capacity) {
        size_t new_capacity = post->capacity == 0 ? 8 : post->capacity * 2;
        if (new_capacity > SIZE_MAX / sizeof(Mail)) {
            return ALLOC_ERROR;
        }
        Mail *tmp = (Mail *)realloc(post->mails, new_capacity * sizeof(Mail));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        post->mails = tmp;
        post->capacity = new_capacity;
    }
    /* место вставки: первое отправление, которое "больше" нового */
    size_t position = 0;
    while (position < post->count) {
        int order = 0;
        mail_order(&post->mails[position], mail, &order);
        if (order > 0) {
            break;
        }
        position++;
    }
    for (size_t i = post->count; i > position; i--) {
        post->mails[i] = post->mails[i - 1];
    }
    post->mails[position] = *mail;
    post->count++;
    return OK;
}

/* Удалить отправление по id (порядок остальных не меняется) */
status_code post_remove(Post *post, const String *id) {
    if (post == NULL || id == NULL) {
        return INVALID_ARGS;
    }
    size_t position = 0;
    status_code sc = post_find(post, id, &position);
    if (sc != OK) {
        return sc;
    }
    mail_clear(&post->mails[position]);
    for (size_t i = position; i + 1 < post->count; i++) {
        post->mails[i] = post->mails[i + 1];
    }
    post->count--;
    return OK;
}

/* Компаратор для qsort (массив указателей на Mail): по времени создания, при равенстве - по id */
int compare_by_created(const void *a, const void *b) {
    const Mail *x = *(const Mail *const *)a;
    const Mail *y = *(const Mail *const *)b;
    if (x->created_key != y->created_key) {
        return x->created_key < y->created_key ? -1 : 1;
    }
    int order = 0;
    string_compare(&x->id, &y->id, &order);
    return order;
}

/*
 * Выбрать отправления: want_delivered = 1 - врученные; 0 - неврученные с прошедшим сроком
 * (срок раньше now_key). Результат - массив указателей (освобождает вызывающий),
 * упорядоченный по времени создания от старых к новым.
 */
status_code post_select(const Post *post, const int want_delivered, const long long now_key,
                        const Mail ***result, size_t *count) {
    if (post == NULL || result == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    *result = NULL;
    *count = 0;
    if (post->count == 0) {
        return OK;
    }
    const Mail **list = (const Mail **)malloc(post->count * sizeof(const Mail *));
    if (list == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    for (size_t i = 0; i < post->count; i++) {
        const Mail *mail = &post->mails[i];
        int take = 0;
        if (want_delivered) {
            take = mail->delivered;
        } else {
            take = !mail->delivered && mail->delivery_key < now_key;
        }
        if (take) {
            list[n] = mail;
            n++;
        }
    }
    qsort(list, n, sizeof(const Mail *), compare_by_created);
    *result = list;
    *count = n;
    return OK;
}

/* ===================================================================== */
/* Ввод                                                                   */
/* ===================================================================== */

/* Прочитать строку любой длины (без '\n'); буфер удваивается realloc.
   END_OF_INPUT - ввод кончился и символов не было */
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

/* Пропустить метку BOM (байты EF BB BF) в самом начале ввода - ее пишет Блокнот в файлы UTF-8.
   Если первый байт не EF, возвращаем его обратно во ввод */
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
    /* EF есть - дальше должны быть BB BF. Обычная буква с EF не начинается (в UTF-8 это
       начало трехбайтового символа), поэтому неполную метку просто пропускаем */
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

/* Убрать пробелы (и '\r' от файлов Windows) в начале и в конце строки - на месте */
status_code trim(char *str) {
    if (str == NULL) {
        return INVALID_ARGS;
    }
    size_t start = 0;
    while (str[start] != '\0' && isspace((unsigned char)str[start])) {
        start++;
    }
    size_t end = start;
    size_t last = start;   /* позиция после последнего непробельного символа */
    while (str[end] != '\0') {
        if (!isspace((unsigned char)str[end])) {
            last = end + 1;
        }
        end++;
    }
    size_t length = last - start;
    for (size_t i = 0; i < length; i++) {
        str[i] = str[start + i];
    }
    str[length] = '\0';
    return OK;
}

/* Состояние диалога: откуда читать и печатать ли подсказки */
typedef struct {
    FILE *in;
    int interactive;   /* 1 - ввод с клавиатуры: печатаем подсказки */
} dialog;

/* Виды полей - чтобы одна функция ask_field проверяла любое поле */
typedef enum {
    FIELD_NONEMPTY,    /* непустая строка */
    FIELD_ANY,         /* любая строка (корпус) */
    FIELD_NATURAL,     /* натуральное число */
    FIELD_INDEX,       /* 6 цифр */
    FIELD_WEIGHT,      /* неотрицательное вещественное */
    FIELD_ID,          /* 14 цифр */
    FIELD_DATETIME     /* dd:MM:yyyy hh:mm:ss */
} field_kind;

/*
 * Спросить поле: подсказка (если ввод с клавиатуры), чтение строки, проверка по виду поля.
 * Неправильное значение - сообщение об ошибке и повторный вопрос. Строка - в *text,
 * число - в *number или *real (для своих видов полей).
 */
status_code ask_field(const dialog *d, const char *prompt, const field_kind kind,
                      String *text, int *number, double *real) {
    if (d == NULL || prompt == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    while (1) {
        if (d->interactive) {
            printf("%s: ", prompt);
            fflush(stdout);
        }
        char *line = NULL;
        status_code sc = read_line(d->in, &line);
        if (sc != OK) {
            return sc;   /* END_OF_INPUT или ALLOC_ERROR */
        }
        trim(line);
        status_code check = OK;
        const char *problem = NULL;   /* текст ошибки для пользователя */
        long long key = 0;
        switch (kind) {
            case FIELD_NONEMPTY:
                if (line[0] == '\0') {
                    check = INVALID_VALUE;
                    problem = "must not be empty";
                }
                break;
            case FIELD_ANY:
                break;
            case FIELD_NATURAL:
                check = parse_natural(line, number);
                problem = "must be a whole number from 1 to 2147483647";
                break;
            case FIELD_INDEX:
                check = check_digits(line, INDEX_LENGTH);
                problem = "must be exactly 6 digits";
                break;
            case FIELD_WEIGHT:
                check = parse_weight(line, real);
                problem = "must be a number 0 or more (digits and one dot)";
                break;
            case FIELD_ID:
                check = check_digits(line, ID_LENGTH);
                problem = "must be exactly 14 digits";
                break;
            case FIELD_DATETIME:
                check = parse_datetime(line, &key);
                problem = "must be a real date and time dd:MM:yyyy hh:mm:ss";
                break;
        }
        if (check != OK) {
            fprintf(stderr, "Error: %s %s, try again\n", prompt, problem);
            free(line);
            continue;
        }
        sc = string_create(line, text);
        free(line);
        return sc;
    }
}

/* Спросить все поля адреса */
status_code ask_address(const dialog *d, Address *address) {
    if (d == NULL || address == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = ask_field(d, "City", FIELD_NONEMPTY, &address->city, NULL, NULL);
    String tmp = {NULL, 0};
    if (sc == OK) {
        sc = ask_field(d, "Street", FIELD_NONEMPTY, &address->street, NULL, NULL);
    }
    if (sc == OK) {
        /* число хранится в int, строку от ask_field сразу освобождаем */
        sc = ask_field(d, "House", FIELD_NATURAL, &tmp, &address->house, NULL);
        string_clear(&tmp);
    }
    if (sc == OK) {
        sc = ask_field(d, "Building", FIELD_ANY, &address->building, NULL, NULL);
    }
    if (sc == OK) {
        sc = ask_field(d, "Apartment", FIELD_NATURAL, &tmp, &address->apartment, NULL);
        string_clear(&tmp);
    }
    if (sc == OK) {
        sc = ask_field(d, "Index", FIELD_INDEX, &address->index, NULL, NULL);
    }
    return sc;
}

/* Спросить все поля отправления. Срок вручения не раньше времени создания */
status_code ask_mail(const dialog *d, Mail *mail) {
    if (d == NULL || mail == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = ask_address(d, &mail->to);
    String tmp = {NULL, 0};
    if (sc == OK) {
        sc = ask_field(d, "Weight", FIELD_WEIGHT, &tmp, NULL, &mail->weight);
        string_clear(&tmp);
    }
    if (sc == OK) {
        sc = ask_field(d, "Id", FIELD_ID, &mail->id, NULL, NULL);
    }
    if (sc == OK) {
        sc = ask_field(d, "Created", FIELD_DATETIME, &mail->created, NULL, NULL);
    }
    if (sc == OK) {
        parse_datetime(mail->created.data, &mail->created_key);   /* строка уже проверена */
    }
    while (sc == OK) {
        sc = ask_field(d, "Delivery", FIELD_DATETIME, &mail->delivery, NULL, NULL);
        if (sc != OK) {
            break;
        }
        parse_datetime(mail->delivery.data, &mail->delivery_key);
        if (mail->delivery_key >= mail->created_key) {
            break;
        }
        fprintf(stderr, "Error: Delivery must not be earlier than Created, try again\n");
        string_clear(&mail->delivery);
    }
    return sc;
}

/* ===================================================================== */
/* Вывод                                                                  */
/* ===================================================================== */

status_code print_address(FILE *out, const Address *a) {
    if (out == NULL || a == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "%s, %s, %s, house %d", a->index.data, a->city.data, a->street.data, a->house);
    if (a->building.length > 0) {
        fprintf(out, ", building %s", a->building.data);
    }
    fprintf(out, ", apartment %d", a->apartment);
    return OK;
}

/* Подробно об одном отправлении. now_key - текущее время (для статуса "expired") */
status_code print_mail(FILE *out, const Mail *m, const long long now_key) {
    if (out == NULL || m == NULL) {
        return INVALID_ARGS;
    }
    const char *status = m->delivered ? "delivered" : (m->delivery_key < now_key ? "expired" : "in transit");
    fprintf(out, "Mail %s\n", m->id.data);
    fprintf(out, "  to: ");
    print_address(out, &m->to);
    fprintf(out, "\n  weight: %g\n", m->weight);
    fprintf(out, "  created: %s\n", m->created.data);
    fprintf(out, "  delivery: %s\n", m->delivery.data);
    fprintf(out, "  status: %s\n", status);
    return OK;
}

/* Коротко: одна строка на отправление */
status_code print_mail_short(FILE *out, const Mail *m) {
    if (out == NULL || m == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "%s  index %s  created %s  delivery %s%s\n", m->id.data, m->to.index.data,
            m->created.data, m->delivery.data, m->delivered ? "  delivered" : "");
    return OK;
}

status_code print_help(FILE *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Commands: add, remove <id>, find <id>, list, deliver <id>, delivered, expired,\n");
    fprintf(out, "          office, demo, help, exit\n");
    return OK;
}

/* Демонстрация всех функций String */
status_code string_demo(FILE *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    String a = {NULL, 0};
    String b = {NULL, 0};
    String c = {NULL, 0};
    String *d = NULL;
    status_code sc = string_create("Hello", &a);
    if (sc == OK) {
        sc = string_create("World!", &b);
    }
    if (sc == OK) {
        sc = string_create("Hello", &c);
    }
    if (sc == OK) {
        int order_ab = 0, order_ba = 0, order_ac = 0, equal_ac = 0, equal_ab = 0;
        string_compare(&a, &b, &order_ab);
        string_compare(&b, &a, &order_ba);
        string_compare(&a, &c, &order_ac);
        string_equal(&a, &c, &equal_ac);
        string_equal(&a, &b, &equal_ab);
        fprintf(out, "String demonstration\n");
        fprintf(out, "create: a = \"%s\" (length %d), b = \"%s\" (length %d), c = \"%s\"\n",
                a.data, a.length, b.data, b.length, c.data);
        fprintf(out, "compare(a, b) = %d, compare(b, a) = %d, compare(a, c) = %d\n", order_ab, order_ba, order_ac);
        fprintf(out, "equal(a, c) = %s, equal(a, b) = %s\n", equal_ac ? "yes" : "no", equal_ab ? "yes" : "no");
        sc = string_duplicate(&a, &d);
    }
    if (sc == OK) {
        fprintf(out, "duplicate a into new d: d = \"%s\"\n", d->data);
        sc = string_copy(&b, &a);
    }
    if (sc == OK) {
        fprintf(out, "copy b into a: a = \"%s\"\n", a.data);
        sc = string_concat(&a, &c);
    }
    if (sc == OK) {
        fprintf(out, "concat a + c: a = \"%s\" (length %d)\n", a.data, a.length);
        sc = string_concat(&c, &c);
    }
    if (sc == OK) {
        fprintf(out, "concat c + c: c = \"%s\"\n", c.data);
        string_clear(&a);
        fprintf(out, "clear a: a.data = %s, a.length = %d\n", a.data == NULL ? "NULL" : a.data, a.length);
    }
    string_clear(&a);
    string_clear(&b);
    string_clear(&c);
    if (d != NULL) {
        string_clear(d);
        free(d);
    }
    return sc;
}

/* ===================================================================== */
/* Диалог                                                                 */
/* ===================================================================== */

/* Разделить строку команды на слово команды и аргумент (после пробела). Строка меняется */
status_code split_command(char *line, char **command, char **argument) {
    if (line == NULL || command == NULL || argument == NULL) {
        return INVALID_ARGS;
    }
    trim(line);
    *command = line;
    *argument = NULL;
    size_t i = 0;
    while (line[i] != '\0' && !isspace((unsigned char)line[i])) {
        i++;
    }
    if (line[i] != '\0') {
        line[i] = '\0';
        char *rest = line + i + 1;
        trim(rest);
        *argument = rest;
    }
    return OK;
}

/*
 * Выполнить одну команду. *finish = 1 - команда exit.
 * Возвращает ALLOC_ERROR / END_OF_INPUT (в середине add) - тогда диалог заканчивается;
 * ошибки пользователя (нет такого id и т.п.) печатаются здесь и диалог продолжается.
 */
status_code run_command(const dialog *d, Post *post, char *line, int *finish) {
    if (d == NULL || post == NULL || line == NULL || finish == NULL) {
        return INVALID_ARGS;
    }
    char *command = NULL;
    char *argument = NULL;
    split_command(line, &command, &argument);
    int has_argument = argument != NULL && argument[0] != '\0';
    *finish = 0;

    if (command[0] == '\0') {
        return OK;   /* пустая строка - ничего не делаем */
    }
    /* команды без аргумента */
    if (strcmp(command, "add") == 0 || strcmp(command, "list") == 0 || strcmp(command, "delivered") == 0
        || strcmp(command, "expired") == 0 || strcmp(command, "office") == 0 || strcmp(command, "demo") == 0
        || strcmp(command, "help") == 0 || strcmp(command, "exit") == 0) {
        if (has_argument) {
            fprintf(stderr, "Error: command '%s' takes no argument\n", command);
            return OK;
        }
    } else if (strcmp(command, "remove") == 0 || strcmp(command, "find") == 0 || strcmp(command, "deliver") == 0) {
        /* команды с id */
        if (!has_argument || check_digits(argument, ID_LENGTH) != OK) {
            fprintf(stderr, "Error: command '%s' needs an id of exactly 14 digits\n", command);
            return OK;
        }
    } else {
        fprintf(stderr, "Error: unknown command '%s' (type help)\n", command);
        return OK;
    }

    if (strcmp(command, "exit") == 0) {
        *finish = 1;
        return OK;
    }
    if (strcmp(command, "help") == 0) {
        return print_help(stdout);
    }
    if (strcmp(command, "demo") == 0) {
        return string_demo(stdout);
    }
    if (strcmp(command, "office") == 0) {
        printf("Office: ");
        print_address(stdout, post->office);
        printf("\n");
        return OK;
    }
    if (strcmp(command, "add") == 0) {
        Mail mail;
        mail_init(&mail);
        status_code sc = ask_mail(d, &mail);
        if (sc == OK) {
            sc = post_add(post, &mail);
            if (sc == OK) {
                printf("Added mail %s\n", mail.id.data);   /* строки теперь в массиве */
                return OK;
            }
        }
        if (sc == DUPLICATE) {
            fprintf(stderr, "Error: mail %s already exists, not added\n", mail.id.data);
            sc = OK;
        }
        mail_clear(&mail);
        return sc;   /* END_OF_INPUT (ввод кончился посреди add) или ALLOC_ERROR */
    }
    if (strcmp(command, "list") == 0) {
        printf("Mails: %zu\n", post->count);
        for (size_t i = 0; i < post->count; i++) {
            print_mail_short(stdout, &post->mails[i]);
        }
        return OK;
    }

    /* остальные команды работают с id или со временем */
    long long now_key = 0;
    if (current_time_key(&now_key) != OK) {
        fprintf(stderr, "Error: can not get the system time\n");
        return OK;
    }
    if (strcmp(command, "delivered") == 0 || strcmp(command, "expired") == 0) {
        int want_delivered = strcmp(command, "delivered") == 0;
        const Mail **list = NULL;
        size_t count = 0;
        status_code sc = post_select(post, want_delivered, now_key, &list, &count);
        if (sc != OK) {
            return sc;   /* ALLOC_ERROR */
        }
        printf("%s: %zu\n", want_delivered ? "Delivered" : "Expired", count);
        for (size_t i = 0; i < count; i++) {
            print_mail(stdout, list[i], now_key);
        }
        free(list);
        return OK;
    }

    String id = {NULL, 0};
    status_code sc = string_create(argument, &id);
    if (sc != OK) {
        return sc;
    }
    size_t position = 0;
    if (strcmp(command, "remove") == 0) {
        sc = post_remove(post, &id);
        if (sc == OK) {
            printf("Removed mail %s\n", argument);
        }
    } else {
        sc = post_find(post, &id, &position);
        if (sc == OK && strcmp(command, "find") == 0) {
            print_mail(stdout, &post->mails[position], now_key);
        } else if (sc == OK) {
            /* deliver */
            if (post->mails[position].delivered) {
                printf("Mail %s is already delivered\n", argument);
            } else {
                post->mails[position].delivered = 1;
                printf("Mail %s is delivered\n", argument);
            }
        }
    }
    string_clear(&id);
    if (sc == NOT_FOUND) {
        fprintf(stderr, "Error: no mail with id %s\n", argument);
        sc = OK;
    }
    return sc;
}

int main(int argc, char *argv[]) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "Usage: lab3_task4   (commands are read from the input, type help)\n");
        return 1;
    }
    dialog d;
    d.in = stdin;
#ifdef _WIN32
    d.interactive = _isatty(_fileno(stdin));
#else
    d.interactive = isatty(fileno(stdin));
#endif

    Post post = {NULL, NULL, 0, 0};
    /* 1) адрес отделения */
    post.office = (Address *)malloc(sizeof(Address));
    if (post.office == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    address_init(post.office);   /* все поля пустые */
    if (d.interactive) {
        printf("Post office address\n");
    }
    skip_bom(d.in);
    status_code sc = ask_address(&d, post.office);
    if (sc != OK) {
        if (sc == END_OF_INPUT) {
            fprintf(stderr, "Error: the input ended before the post office address was entered\n");
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
        } else {
            fprintf(stderr, "Error: internal error\n");
        }
        post_destroy(&post);
        return 1;
    }
    if (d.interactive) {
        print_help(stdout);
    }

    /* 2) команды, пока не exit или конец ввода */
    int result = 0;
    int finish = 0;
    while (!finish) {
        if (d.interactive) {
            printf("> ");
            fflush(stdout);
        }
        char *line = NULL;
        sc = read_line(d.in, &line);
        if (sc == END_OF_INPUT) {
            break;
        }
        if (sc == OK) {
            sc = run_command(&d, &post, line, &finish);
            free(line);
        }
        if (sc == END_OF_INPUT) {
            fprintf(stderr, "Error: the input ended in the middle of a mail, it is not added\n");
            result = 1;
            break;
        }
        if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
            result = 1;
            break;
        }
        if (sc != OK) {
            fprintf(stderr, "Error: internal error\n");   /* других статусов здесь не бывает */
            result = 1;
            break;
        }
    }
    post_destroy(&post);
    return result;
}
