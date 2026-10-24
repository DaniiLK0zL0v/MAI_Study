/*
 * Лабораторная 3, задание 2. Расписание конференции: сколько докладов можно провести в k залах.
 *
 * Есть n заявок: у каждой id, время начала и время окончания (в пределах одного дня).
 * Есть k одинаковых залов. Нужно выбрать как можно больше докладов так, чтобы в каждом зале
 * доклады не пересекались. Доклад, который кончается в момент начала другого, можно провести
 * в том же зале.
 *
 * Запуск:  lab3_task2 < входной_файл      (или ввод с клавиатуры, конец ввода - Ctrl+D / Ctrl+Z)
 * Ввод (стандартный поток ввода):
 *     n k                        - количество заявок (>= 1) и количество залов (>= 1)
 *     id начало конец            - n таких строк; id >= 1 и разные, время HH:MM, начало < конца
 * Вывод: одно число - наибольшее количество докладов.
 * Пример:  3 1 / 1 09:00 10:00 / 2 09:30 10:30 / 3 10:00 11:00   ->   2  (доклады 1 и 3)
 *
 * Главная функция задания - max_reports(заявки, количество, залы, &ответ). Жадный алгоритм:
 *   1) упорядочить доклады по времени окончания (qsort);
 *   2) по очереди ставить каждый доклад в тот зал, который освободился ПОЗЖЕ всех,
 *      но не позже начала доклада ("самый подходящий" зал); если такого зала нет - доклад пропустить.
 * Время - минуты от 00:00 до 23:59, всего 1440 значений. Поэтому залы хранятся не по одному,
 * а массивом free_at[t] - сколько залов освобождается ровно в минуту t (k может быть огромным).
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort */
#include <limits.h>   /* LLONG_MAX */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace */

/* Минут в сутках: время от 0 (00:00) до 1439 (23:59) */
#define MINUTES_IN_DAY 1440

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* в функцию передали NULL или неправильные данные */
    INVALID_NUMBER,    /* строка - не целое неотрицательное число */
    INVALID_TIME,      /* строка - не время HH:MM */
    OVERFLOW_ERROR,    /* число больше LLONG_MAX */
    END_OF_INPUT,      /* строки кончились (сигнал для цикла чтения, не ошибка) */
    NO_DATA,           /* ввод пустой */
    BAD_FIRST_LINE,    /* в первой строке не два слова */
    BAD_N,             /* n - не целое от 1 до LLONG_MAX */
    BAD_K,             /* k - не целое от 1 до LLONG_MAX */
    BAD_REPORT_LINE,   /* в строке заявки не три слова */
    BAD_ID,            /* id - не целое от 1 до LLONG_MAX */
    BAD_START,         /* время начала - не HH:MM */
    BAD_FINISH,        /* время окончания - не HH:MM */
    BAD_ORDER,         /* начало не раньше окончания */
    TOO_FEW_REPORTS,   /* строк с заявками меньше n */
    TOO_MANY_REPORTS,  /* после n заявок есть еще непустые строки */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Одна заявка: время - в минутах от начала суток */
typedef struct {
    long long id;
    int start;    /* 0..1439 */
    int finish;   /* 1..1439, start < finish */
} report;

/* Все, что прочитали из ввода. n и item_count нужны и при ошибке - для сообщения */
typedef struct {
    report *reports;    /* прочитанные заявки */
    size_t item_count;  /* сколько прочитано */
    long long n;        /* сколько заявок обещано в первой строке */
    long long halls;    /* сколько залов */
} input_data;

/* ===================== выбор докладов ===================== */

/* Компаратор для qsort: по времени окончания, при равенстве - по началу.
   Возвращает отрицательное число, 0 или положительное, как требует qsort */
int compare_by_finish(const void *a, const void *b) {
    const report *x = (const report *)a;
    const report *y = (const report *)b;
    if (x->finish != y->finish) {
        return x->finish < y->finish ? -1 : 1;
    }
    if (x->start != y->start) {
        return x->start < y->start ? -1 : 1;
    }
    return 0;
}

/*
 * Главная функция задания. reports не меняется (сортируется копия).
 *
 * Почему жадный выбор дает наибольшее количество:
 * - Доклады перебираются по возрастанию окончания. Доклад, который кончается раньше,
 *   оставляет залу больше времени - его выгодно брать.
 * - Доклад ставится в зал, освободившийся позже всех среди подходящих (но не позже начала).
 *   Залы, освободившиеся раньше, сохраняются для докладов, которые начинаются раньше.
 * - Если подходящего зала нет, значит все k залов заняты докладами, которые кончаются
 *   не позже этого и уже пересекаются с ним - замена ничего не дала бы.
 * Это известный алгоритм для интервального планирования на k машинах; ответ проверен
 * полным перебором на Python (см. тесты).
 */
status_code max_reports(const report *reports, const size_t n, const long long halls, size_t *answer) {
    if (answer == NULL || (reports == NULL && n > 0) || halls < 1) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < n; i++) {
        if (reports[i].start < 0 || reports[i].finish >= MINUTES_IN_DAY || reports[i].start >= reports[i].finish) {
            return INVALID_ARGS;
        }
    }
    *answer = 0;
    if (n == 0) {
        return OK;
    }

    /* копия заявок - ее и сортируем (исходный массив const) */
    if (n > SIZE_MAX / sizeof(report)) {
        return ALLOC_ERROR;
    }
    report *sorted = (report *)malloc(n * sizeof(report));
    if (sorted == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < n; i++) {
        sorted[i] = reports[i];
    }
    qsort(sorted, n, sizeof(report), compare_by_finish);

    /* free_at[t] - сколько залов освобождается в минуту t (их последний доклад кончается в t).
       Вначале все k залов свободны "с 00:00" */
    long long *free_at = (long long *)calloc(MINUTES_IN_DAY, sizeof(long long));
    if (free_at == NULL) {
        free(sorted);
        return ALLOC_ERROR;
    }
    free_at[0] = halls;

    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        /* ищем самое позднее время t <= start, когда освобождается хотя бы один зал */
        int t = sorted[i].start;
        while (t >= 0 && free_at[t] == 0) {
            t--;
        }
        if (t < 0) {
            continue;   /* все залы заняты до начала доклада - пропускаем его */
        }
        /* зал переходит из "свободен с t" в "свободен с finish" */
        free_at[t]--;
        free_at[sorted[i].finish]++;
        count++;
    }

    free(free_at);
    free(sorted);
    *answer = count;
    return OK;
}

/* ===================== ввод (только для main) ===================== */

/* Прочитать одну строку любой длины (без '\n') в динамическую память.
   Буфер начинается с 16 байт и удваивается через realloc, когда кончается место.
   END_OF_INPUT - строк больше нет (файл кончился, а символов не было) */
status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t buf_size = 16;   /* сколько байт выделено */
    size_t length = 0;      /* сколько символов уже прочитано */
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
        /* нужен байт под этот символ и еще один под '\0' в конце */
        if (length + 1 >= buf_size) {
            if (buf_size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            /* realloc - во временную переменную: если он вернет NULL,
               старый блок останется, и мы сможем его освободить */
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

/* Разбить строку на слова (слова разделены пробелами, табуляциями, '\r').
   Строка меняется: после каждого слова пишется '\0', а в words[] кладутся адреса начал слов.
   Больше max_words слов не записываем, но считаем всех: *item_count - сколько слов на самом деле */
status_code split_words(char *line, char **words, const size_t max_words, size_t *item_count) {
    if (line == NULL || words == NULL || item_count == NULL) {
        return INVALID_ARGS;
    }
    size_t k = 0;
    size_t i = 0;
    while (line[i] != '\0') {
        /* пропускаем пробелы перед словом. (unsigned char) - русская буква в char
           отрицательна, а isspace с отрицательным числом - неопределенное поведение */
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
        /* идем до конца слова и ставим после него '\0' */
        while (line[i] != '\0' && !isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] != '\0') {
            line[i] = '\0';
            i++;
        }
    }
    *item_count = k;
    return OK;
}

/* Строка -> целое неотрицательное число long long. Только цифры: "-1", "+5", "1.5", "" -
   INVALID_NUMBER; больше LLONG_MAX - OVERFLOW_ERROR */
status_code parse_number(const char *str, long long *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_NUMBER;
    }
    long long value = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_NUMBER;
        }
        int digit = str[i] - '0';
        /* value * 10 + digit > LLONG_MAX  <=>  value > (LLONG_MAX - digit) / 10 */
        if (value > (LLONG_MAX - digit) / 10) {
            return OVERFLOW_ERROR;
        }
        value = value * 10 + digit;
    }
    *number = value;
    return OK;
}

/* Строка "HH:MM" -> минуты от начала суток. Ровно 5 символов: две цифры часов (00..23),
   двоеточие, две цифры минут (00..59). "9:05", "24:00", "12:60", "12-30" - INVALID_TIME */
status_code parse_time(const char *str, int *minutes) {
    if (str == NULL || minutes == NULL) {
        return INVALID_ARGS;
    }
    /* проверяем символы по одному; str[i] != '\0' проверяется раньше следующего символа,
       поэтому за конец короткой строки не выходим */
    for (int i = 0; i < 5; i++) {
        if (str[i] == '\0') {
            return INVALID_TIME;
        }
        if (i == 2 ? str[i] != ':' : (str[i] < '0' || str[i] > '9')) {
            return INVALID_TIME;
        }
    }
    if (str[5] != '\0') {
        return INVALID_TIME;
    }
    int hours = (str[0] - '0') * 10 + (str[1] - '0');
    int mins = (str[3] - '0') * 10 + (str[4] - '0');
    if (hours > 23 || mins > 59) {
        return INVALID_TIME;
    }
    *minutes = hours * 60 + mins;
    return OK;
}

/* Строка пустая или из одних пробелов? (такие строки при чтении пропускаем) */
status_code is_blank(const char *line, int *empty) {
    if (line == NULL || empty == NULL) {
        return INVALID_ARGS;
    }
    *empty = 1;
    for (size_t i = 0; line[i] != '\0'; i++) {
        if (!isspace((unsigned char)line[i])) {
            *empty = 0;
            break;
        }
    }
    return OK;
}

/* Прочитать следующую непустую строку. *line_number - номер строки во вводе
   (считаем все строки, и пустые тоже - так номер совпадет с номером в файле) */
status_code read_nonblank_line(FILE *in, char **line, size_t *line_number) {
    if (in == NULL || line == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    while (1) {
        char *str = NULL;
        status_code sc = read_line(in, &str);
        if (sc != OK) {
            return sc;   /* END_OF_INPUT или ALLOC_ERROR */
        }
        (*line_number)++;
        int empty = 0;
        is_blank(str, &empty);
        if (!empty) {
            *line = str;
            return OK;
        }
        free(str);   /* пустая строка не нужна - читаем следующую */
    }
}

/* Разобрать строку заявки "id HH:MM HH:MM" в *item */
status_code parse_report_line(char *line, report *item) {
    if (line == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    char *words[3];
    size_t count = 0;
    split_words(line, words, 3, &count);
    if (count != 3) {
        return BAD_REPORT_LINE;
    }
    long long id = 0;
    if (parse_number(words[0], &id) != OK || id < 1) {
        return BAD_ID;
    }
    int start = 0;
    int finish = 0;
    if (parse_time(words[1], &start) != OK) {
        return BAD_START;
    }
    if (parse_time(words[2], &finish) != OK) {
        return BAD_FINISH;
    }
    if (start >= finish) {
        return BAD_ORDER;
    }
    item->id = id;
    item->start = start;
    item->finish = finish;
    return OK;
}

/*
 * Прочитать весь ввод в data. При ошибке все выделенное освобождается,
 * *line_number - номер строки с ошибкой, data->n и data->item_count остаются для сообщения.
 * Заявки читаются по одной, массив растет вдвое (n в первой строке может быть огромным -
 * выделять сразу n элементов нельзя).
 */
status_code read_input(FILE *in, input_data *data, size_t *line_number) {
    if (in == NULL || data == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    data->reports = NULL;
    data->item_count = 0;
    data->n = 0;
    data->halls = 0;
    *line_number = 0;

    /* 1) первая строка: n k */
    char *line = NULL;
    status_code sc = read_nonblank_line(in, &line, line_number);
    if (sc == END_OF_INPUT) {
        return NO_DATA;
    }
    if (sc != OK) {
        return sc;
    }
    char *words[2];
    size_t count = 0;
    split_words(line, words, 2, &count);
    if (count != 2) {
        sc = BAD_FIRST_LINE;
    } else if (parse_number(words[0], &data->n) != OK || data->n < 1) {
        sc = BAD_N;
    } else if (parse_number(words[1], &data->halls) != OK || data->halls < 1) {
        sc = BAD_K;
    }
    free(line);
    if (sc != OK) {
        return sc;
    }

    /* 2) n строк с заявками */
    size_t buf_size = 0;
    while ((long long)data->item_count < data->n) {
        sc = read_nonblank_line(in, &line, line_number);
        if (sc == END_OF_INPUT) {
            sc = TOO_FEW_REPORTS;
            break;
        }
        if (sc != OK) {
            break;
        }
        report item;
        sc = parse_report_line(line, &item);
        free(line);
        if (sc != OK) {
            break;
        }
        /* место в массиве: удваиваем через временную переменную */
        if (data->item_count == buf_size) {
            size_t new_size = buf_size == 0 ? 16 : buf_size * 2;
            if (new_size > SIZE_MAX / sizeof(report)) {
                sc = ALLOC_ERROR;
                break;
            }
            report *tmp = (report *)realloc(data->reports, new_size * sizeof(report));
            if (tmp == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            data->reports = tmp;
            buf_size = new_size;
        }
        data->reports[data->item_count] = item;
        data->item_count++;
    }

    /* 3) после n заявок больше ничего быть не должно */
    if (sc == OK) {
        sc = read_nonblank_line(in, &line, line_number);
        if (sc == OK) {
            free(line);
            sc = TOO_MANY_REPORTS;
        } else if (sc == END_OF_INPUT) {
            sc = OK;
        }
    }
    if (sc != OK) {
        free(data->reports);
        data->reports = NULL;
        return sc;
    }
    return OK;
}

/* Компаратор для qsort по id - для проверки, что id разные */
int compare_ids(const void *a, const void *b) {
    const long long x = *(const long long *)a;
    const long long y = *(const long long *)b;
    return (x > y) - (x < y);
}

/* Найти повторяющийся id: *has_duplicate = 1 и *duplicate - сам id.
   Копируем id в отдельный массив, сортируем, повторы оказываются рядом */
status_code check_unique_ids(const report *reports, const size_t n, int *has_duplicate, long long *duplicate) {
    if ((reports == NULL && n > 0) || has_duplicate == NULL || duplicate == NULL) {
        return INVALID_ARGS;
    }
    *has_duplicate = 0;
    if (n < 2) {
        return OK;
    }
    if (n > SIZE_MAX / sizeof(long long)) {
        return ALLOC_ERROR;
    }
    long long *ids = (long long *)malloc(n * sizeof(long long));
    if (ids == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < n; i++) {
        ids[i] = reports[i].id;
    }
    qsort(ids, n, sizeof(long long), compare_ids);
    for (size_t i = 1; i < n; i++) {
        if (ids[i] == ids[i - 1]) {
            *has_duplicate = 1;
            *duplicate = ids[i];
            break;
        }
    }
    free(ids);
    return OK;
}

int main(int argc, char *argv[]) {
    (void)argv;
    /* все данные - со стандартного ввода, аргументы не нужны */
    if (argc != 1) {
        fprintf(stderr, "Usage: lab3_task2 < input.txt   (first line: n k, then n lines: id HH:MM HH:MM)\n");
        return 1;
    }

    /* 1) чтение ввода; у каждой ошибки свое сообщение с номером строки */
    input_data data;
    size_t line_number = 0;
    status_code sc = read_input(stdin, &data, &line_number);
    if (sc != OK) {
        switch (sc) {
            case NO_DATA:
                fprintf(stderr, "Error: no input, the first line must be: n k\n");
                break;
            case BAD_FIRST_LINE:
                fprintf(stderr, "Error: line %zu: the first line must be: n k\n", line_number);
                break;
            case BAD_N:
                fprintf(stderr, "Error: line %zu: n must be a whole number from 1 to %lld\n", line_number, LLONG_MAX);
                break;
            case BAD_K:
                fprintf(stderr, "Error: line %zu: k must be a whole number from 1 to %lld\n", line_number, LLONG_MAX);
                break;
            case BAD_REPORT_LINE:
                fprintf(stderr, "Error: line %zu: a report line must be: id start finish\n", line_number);
                break;
            case BAD_ID:
                fprintf(stderr, "Error: line %zu: id must be a whole number from 1 to %lld\n", line_number, LLONG_MAX);
                break;
            case BAD_START:
                fprintf(stderr, "Error: line %zu: start time must be HH:MM (00:00 .. 23:59)\n", line_number);
                break;
            case BAD_FINISH:
                fprintf(stderr, "Error: line %zu: finish time must be HH:MM (00:00 .. 23:59)\n", line_number);
                break;
            case BAD_ORDER:
                fprintf(stderr, "Error: line %zu: start time must be earlier than finish time\n", line_number);
                break;
            case TOO_FEW_REPORTS:
                fprintf(stderr, "Error: expected %lld reports, but found only %zu\n", data.n, data.item_count);
                break;
            case TOO_MANY_REPORTS:
                fprintf(stderr, "Error: line %zu: there are more than %lld reports\n", line_number, data.n);
                break;
            case ALLOC_ERROR:
                fprintf(stderr, "Error: not enough memory\n");
                break;
            default:
                /* INVALID_ARGS и прочие статусы, которых тут быть не может - ошибка в самой программе */
                fprintf(stderr, "Error: internal error\n");
                break;
        }
        return 1;
    }

    /* 2) id должны быть разными */
    int has_duplicate = 0;
    long long duplicate = 0;
    sc = check_unique_ids(data.reports, data.item_count, &has_duplicate, &duplicate);
    if (sc != OK) {
        fprintf(stderr, "Error: not enough memory\n");   /* возможен только ALLOC_ERROR */
        free(data.reports);
        return 1;
    }
    if (has_duplicate) {
        fprintf(stderr, "Error: report id %lld is used more than once\n", duplicate);
        free(data.reports);
        return 1;
    }

    /* 3) сам выбор и вывод */
    size_t answer = 0;
    sc = max_reports(data.reports, data.item_count, data.halls, &answer);
    free(data.reports);
    if (sc == ALLOC_ERROR) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    } else if (sc != OK) {
        fprintf(stderr, "Error: internal error\n");   /* данные уже проверены при чтении */
        return 1;
    }
    printf("%zu\n", answer);
    return 0;
}
