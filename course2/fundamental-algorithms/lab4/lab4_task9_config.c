/*
 * Лабораторная 4, задание 9.2. Создание файла параметров модели для задания 9.1 в диалоге с пользователем.
 *
 * Запуск:  lab4_task9_config <файл параметров>
 *
 * Программа по очереди спрашивает параметры (структуры данных, начало и конец моделирования, время обработки,
 * число отделений, операторы, коэффициент перегрузки), каждый ответ сразу проверяет; на неправильный ответ -
 * сообщение и тот же вопрос снова. Когда все ответы получены, файл записывается в формате задания 9.1.
 * Вопросы печатаются только при вводе с клавиатуры (isatty): при вводе из файла (тесты) видны только
 * сообщения об ошибках и итог.
 */
#define _POSIX_C_SOURCE 200809L   /* isatty, fileno на Linux - до всех #include */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* strcmp, strlen, strcpy */
#ifdef _WIN32
#include <io.h>       /* _isatty, _fileno */
#else
#include <unistd.h>   /* isatty, fileno */
#endif

#define MAX_DEPARTMENTS 100
#define MIN_OPERATORS 10
#define MAX_OPERATORS 50
#define MAX_HANDLING_TIME 1000000LL
#define MAX_COEFF_DIGITS 6
#define MAX_COEFF_WHOLE 1000LL

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,    /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,    /* ответ записан неправильно */
    INPUT_END,       /* ввод кончился раньше, чем получены все ответы */
    FILE_ERROR,      /* файл не открылся / ошибка записи */
    ALLOC_ERROR      /* malloc / realloc не смогли выделить память */
} status_code;

/* Варианты структур данных - те же названия, что понимает задание 9.1 */
const char *HEAP_NAMES[] = {"BinaryHeap", "BinomialHeap", "FibonacciHeap", "SkewHeap", "LeftistHeap", "Treap"};
#define HEAP_KINDS 6
const char *MAP_NAMES[] = {"HashSet", "DynamicArray", "BinarySearchTree", "Trie"};
#define MAP_KINDS 4

/* Ответы пользователя */
typedef struct {
    const char *heap;
    const char *map;
    long long start;            /* секунды от 0001-01-01T00:00:00 */
    long long end;
    long long min_time;
    long long max_time;
    long long department_count;
    long long *operators;       /* department_count чисел */
    char *coeff;                /* коэффициент - как ввёл пользователь (уже проверен) */
} answers;

/* ===================== время ISO 8601 (как в задании 9.1) ===================== */

/* Сколько дней в месяце month года year (високосный: делится на 4, но не на 100, или делится на 400) */
status_code days_in_month(const long long year, const long long month, long long *days) {
    if (days == NULL || month < 1 || month > 12) {
        return INVALID_ARGS;
    }
    const int lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    *days = lengths[month - 1] + (month == 2 && leap ? 1 : 0);
    return OK;
}

/* Сколько дней от 0001-01-01 до 1 января года year */
status_code days_before_year(const long long year, long long *days) {
    if (days == NULL || year < 1) {
        return INVALID_ARGS;
    }
    long long y = year - 1;
    *days = 365 * y + y / 4 - y / 100 + y / 400;
    return OK;
}

/* count цифр подряд с позиции s -> число; SYNTAX_ERROR - не цифры */
status_code fixed_digits(const char *s, const size_t count, long long *value) {
    if (s == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    long long result = 0;
    for (size_t i = 0; i < count; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return SYNTAX_ERROR;
        }
        result = result * 10 + (s[i] - '0');
    }
    *value = result;
    return OK;
}

/* "YYYY-MM-DDTHH:MM:SS" или "YYYY-MM-DDTHH:MM" -> секунды от 0001-01-01T00:00:00 (с проверкой диапазонов) */
status_code parse_time(const char *s, long long *seconds) {
    if (s == NULL || seconds == NULL) {
        return INVALID_ARGS;
    }
    size_t length = strlen(s);
    if ((length != 16 && length != 19) || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
        (length == 19 && s[16] != ':')) {
        return SYNTAX_ERROR;
    }
    long long year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (fixed_digits(s, 4, &year) != OK || fixed_digits(s + 5, 2, &month) != OK ||
        fixed_digits(s + 8, 2, &day) != OK || fixed_digits(s + 11, 2, &hour) != OK ||
        fixed_digits(s + 14, 2, &minute) != OK || (length == 19 && fixed_digits(s + 17, 2, &second) != OK)) {
        return SYNTAX_ERROR;
    }
    long long month_days = 0;
    if (year < 1 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) {
        return SYNTAX_ERROR;
    }
    days_in_month(year, month, &month_days);
    if (day < 1 || day > month_days) {
        return SYNTAX_ERROR;
    }
    /* дни до этого года, затем до этого месяца, затем до этого дня */
    long long days = 0;
    days_before_year(year, &days);
    for (long long m = 1; m < month; m++) {
        long long d = 0;
        days_in_month(year, m, &d);
        days += d;
    }
    days += day - 1;
    *seconds = ((days * 24 + hour) * 60 + minute) * 60 + second;
    return OK;
}

/* count цифр числа value (с ведущими нулями) - в out */
status_code put_digits(long long value, const size_t count, char *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = count; i > 0; i--) {
        out[i - 1] = (char)('0' + value % 10);
        value /= 10;
    }
    return OK;
}

/* Секунды от 0001-01-01 -> "YYYY-MM-DDTHH:MM:SS" (out - не меньше 20 символов) */
status_code time_text(const long long seconds, char *out) {
    if (out == NULL || seconds < 0) {
        return INVALID_ARGS;
    }
    long long days = seconds / 86400;
    long long rest = seconds % 86400;
    /* год: оценка снизу (в году не больше 366 дней), затем вперёд, пока следующий год не позже */
    long long year = days / 366 + 1;
    long long next = 0;
    days_before_year(year + 1, &next);
    while (next <= days) {
        year++;
        days_before_year(year + 1, &next);
    }
    long long start = 0;
    days_before_year(year, &start);
    days -= start;
    /* месяц: вычитаем длины месяцев */
    long long month = 1;
    long long length = 0;
    days_in_month(year, month, &length);
    while (days >= length) {
        days -= length;
        month++;
        days_in_month(year, month, &length);
    }
    put_digits(year, 4, out);
    out[4] = '-';
    put_digits(month, 2, out + 5);
    out[7] = '-';
    put_digits(days + 1, 2, out + 8);
    out[10] = 'T';
    put_digits(rest / 3600, 2, out + 11);
    out[13] = ':';
    put_digits(rest / 60 % 60, 2, out + 14);
    out[16] = ':';
    put_digits(rest % 60, 2, out + 17);
    out[19] = '\0';
    return OK;
}

/* ===================== числа и строки ===================== */

/* Строка из цифр -> число в [min, max]; SYNTAX_ERROR - не число или вне диапазона */
status_code parse_number(const char *s, const long long min, const long long max, long long *value) {
    if (s == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    if (s[0] == '\0') {
        return SYNTAX_ERROR;
    }
    long long result = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return SYNTAX_ERROR;
        }
        result = result * 10 + (s[i] - '0');
        if (result > max) {
            return SYNTAX_ERROR;   /* дальше только больше - и без переполнения */
        }
    }
    if (result < min) {
        return SYNTAX_ERROR;
    }
    *value = result;
    return OK;
}

/* Проверка коэффициента перегрузки "целая[.дробная]": 1.0 и больше, до 6 цифр после точки (как в 9.1) */
status_code check_coeff(const char *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    long long whole = 0;
    size_t i = 0;
    for (; s[i] >= '0' && s[i] <= '9'; i++) {
        whole = whole * 10 + (s[i] - '0');
        if (whole > MAX_COEFF_WHOLE) {
            return SYNTAX_ERROR;
        }
    }
    if (i == 0) {
        return SYNTAX_ERROR;
    }
    if (s[i] == '.') {
        i++;
        size_t start = i;
        while (s[i] >= '0' && s[i] <= '9') {
            if (i - start == MAX_COEFF_DIGITS) {
                return SYNTAX_ERROR;
            }
            i++;
        }
        if (i == start) {
            return SYNTAX_ERROR;   /* "1." - после точки нет цифр */
        }
    }
    return s[i] == '\0' && whole >= 1 ? OK : SYNTAX_ERROR;
}

/* Прочитать строку любой длины (без '\n' и '\r'); *end = 1 - ввод кончился (строки нет) */
status_code read_line(FILE *file, char **line, int *end) {
    if (file == NULL || line == NULL || end == NULL) {
        return INVALID_ARGS;
    }
    size_t capacity = 64;
    size_t n = 0;
    char *buf = (char *)malloc(capacity);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    int ch = fgetc(file);
    *end = (ch == EOF);
    while (ch != EOF && ch != '\n') {
        if (n + 1 >= capacity) {
            char *tmp = (char *)realloc(buf, capacity * 2);
            if (tmp == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = tmp;
            capacity *= 2;
        }
        buf[n++] = (char)ch;
        ch = fgetc(file);
    }
    if (n > 0 && buf[n - 1] == '\r') {
        n--;
    }
    buf[n] = '\0';
    *line = buf;
    return OK;
}

/* Убрать пробелы и табуляции в начале и в конце (строка меняется; *result - начало) */
status_code trim(char *s, char **result) {
    if (s == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    *result = s;
    return OK;
}

/* Отрезать первое слово: *word - слово (на месте, с '\0' в конце), *rest - что после него */
status_code cut_word(char *s, char **word, char **rest) {
    if (s == NULL || word == NULL || rest == NULL) {
        return INVALID_ARGS;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    *word = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') {
        s++;
    }
    if (*s != '\0') {
        *s++ = '\0';
    }
    *rest = s;
    return OK;
}

/* ===================== диалог ===================== */

/*
 * Задать вопрос и прочитать ответ: вопрос печатается только при вводе с клавиатуры. *line - строка ответа
 * (освобождает вызывающий), *text - она же без пробелов по краям. INPUT_END - ввод кончился.
 */
status_code ask(const char *question, const int interactive, char **line, char **text) {
    if (question == NULL || line == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    if (interactive) {
        printf("%s", question);
        fflush(stdout);
    }
    int end = 0;
    status_code sc = read_line(stdin, line, &end);
    if (sc != OK) {
        return sc;
    }
    if (end) {
        free(*line);
        *line = NULL;
        return INPUT_END;
    }
    return trim(*line, text);
}

/* Выбор из списка: номер (1..count) или название; SYNTAX_ERROR - нет такого */
status_code choose(const char *text, const char **names, const size_t count, const char **result) {
    if (text == NULL || names == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    long long number = 0;
    if (parse_number(text, 1, (long long)count, &number) == OK) {
        *result = names[number - 1];
        return OK;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(text, names[i]) == 0) {
            *result = names[i];
            return OK;
        }
    }
    return SYNTAX_ERROR;
}

/* Вопрос о структуре данных: список вариантов и выбор, пока ответ не подойдёт */
status_code ask_structure(const char *title, const char **names, const size_t count, const int interactive,
                          const char **result) {
    if (title == NULL || names == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        if (interactive) {
            printf("%s:\n", title);
            for (size_t i = 0; i < count; i++) {
                printf("  %zu - %s\n", i + 1, names[i]);
            }
        }
        char *line = NULL;
        char *text = NULL;
        status_code sc = ask("Your choice (number or name): ", interactive, &line, &text);
        if (sc != OK) {
            return sc;
        }
        sc = choose(text, names, count, result);
        free(line);
        if (sc == OK) {
            return OK;
        }
        printf("Error: choose a number from 1 to %zu or a name from the list\n", count);
    }
}

/* Вопрос о времени; not_before >= 0 - время не должно быть раньше not_before (конец не раньше начала) */
status_code ask_time(const char *question, const long long not_before, const int interactive, long long *result) {
    if (question == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        char *line = NULL;
        char *text = NULL;
        status_code sc = ask(question, interactive, &line, &text);
        if (sc != OK) {
            return sc;
        }
        sc = parse_time(text, result);
        free(line);
        if (sc != OK) {
            printf("Error: wrong time, need YYYY-MM-DDTHH:MM:SS (seconds may be omitted)\n");
        } else if (*result < not_before) {
            printf("Error: the end must not be before the start\n");
        } else {
            return OK;
        }
    }
}

/* Вопрос о числе в [min, max] */
status_code ask_number(const char *question, const long long min, const long long max, const int interactive,
                       long long *result) {
    if (question == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        char *line = NULL;
        char *text = NULL;
        status_code sc = ask(question, interactive, &line, &text);
        if (sc != OK) {
            return sc;
        }
        sc = parse_number(text, min, max, result);
        free(line);
        if (sc == OK) {
            return OK;
        }
        printf("Error: need a natural number from %lld to %lld\n", min, max);
    }
}

/* Операторы: одно число (для всех отделений) или по числу на каждое отделение, каждое 10..50 */
status_code ask_operators(answers *a, const int interactive) {
    if (a == NULL || a->operators == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        char *line = NULL;
        char *text = NULL;
        status_code sc = ask("Operators in the departments (10..50): one number for all departments "
                             "or a number for each department: ", interactive, &line, &text);
        if (sc != OK) {
            return sc;
        }
        /* слова ответа по очереди; больше department_count слов - ошибка */
        long long count = 0;
        int good = 1;
        char *rest = text;
        while (good && rest[0] != '\0') {
            char *word = NULL;
            cut_word(rest, &word, &rest);
            if (word[0] == '\0') {
                break;   /* после последнего слова были только пробелы */
            }
            if (count == a->department_count ||
                parse_number(word, MIN_OPERATORS, MAX_OPERATORS, &a->operators[count]) != OK) {
                good = 0;
            } else {
                count++;
            }
        }
        free(line);
        if (good && count == 1) {
            /* одно число - для всех отделений */
            for (long long i = 1; i < a->department_count; i++) {
                a->operators[i] = a->operators[0];
            }
            return OK;
        }
        if (good && count == a->department_count) {
            return OK;
        }
        printf("Error: need one number or %lld numbers from 10 to 50\n", a->department_count);
    }
}

/* Коэффициент перегрузки: проверяется и запоминается текстом */
status_code ask_coeff(answers *a, const int interactive) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        char *line = NULL;
        char *text = NULL;
        status_code sc = ask("Overload coefficient (1.0 or more): ", interactive, &line, &text);
        if (sc != OK) {
            return sc;
        }
        if (check_coeff(text) == OK) {
            a->coeff = (char *)malloc(strlen(text) + 1);
            if (a->coeff == NULL) {
                free(line);
                return ALLOC_ERROR;
            }
            strcpy(a->coeff, text);
            free(line);
            return OK;
        }
        free(line);
        printf("Error: need a number 1.0 or more (up to 6 digits after the point)\n");
    }
}

/* Весь диалог: вопросы по порядку строк файла параметров */
status_code dialog(answers *a, const int interactive) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = ask_structure("Data structure for the requests of a department", HEAP_NAMES, HEAP_KINDS,
                                   interactive, &a->heap);
    if (sc == OK) {
        sc = ask_structure("Data structure for the departments", MAP_NAMES, MAP_KINDS, interactive, &a->map);
    }
    if (sc == OK) {
        sc = ask_time("Start of the model (YYYY-MM-DDTHH:MM:SS): ", 0, interactive, &a->start);
    }
    if (sc == OK) {
        sc = ask_time("End of the model (YYYY-MM-DDTHH:MM:SS): ", a->start, interactive, &a->end);
    }
    if (sc == OK) {
        sc = ask_number("Minimal handling time of a request (minutes): ", 1, MAX_HANDLING_TIME, interactive,
                        &a->min_time);
    }
    if (sc == OK) {
        sc = ask_number("Maximal handling time of a request (minutes): ", a->min_time, MAX_HANDLING_TIME,
                        interactive, &a->max_time);
    }
    if (sc == OK) {
        sc = ask_number("Number of departments (1..100): ", 1, MAX_DEPARTMENTS, interactive, &a->department_count);
    }
    if (sc == OK) {
        a->operators = (long long *)malloc((size_t)a->department_count * sizeof(long long));
        sc = a->operators != NULL ? ask_operators(a, interactive) : ALLOC_ERROR;
    }
    if (sc == OK) {
        sc = ask_coeff(a, interactive);
    }
    return sc;
}

/* Записать файл параметров: по строке на параметр, в порядке задания 9.1 */
status_code write_config(const char *path, const answers *a) {
    if (path == NULL || a == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return FILE_ERROR;
    }
    char start[24];
    char end[24];
    time_text(a->start, start);
    time_text(a->end, end);
    fprintf(file, "%s\n%s\n%s\n%s\n%lld\n%lld\n%lld\n", a->heap, a->map, start, end, a->min_time, a->max_time,
            a->department_count);
    /* числа операторов - в одной строке через пробел */
    for (long long i = 0; i < a->department_count; i++) {
        if (i > 0) {
            fputc(' ', file);
        }
        fprintf(file, "%lld", a->operators[i]);
    }
    fprintf(file, "\n%s\n", a->coeff);
    int failed = ferror(file);
    if (fclose(file) != 0) {
        failed = 1;
    }
    return failed ? FILE_ERROR : OK;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: lab4_task9_config <config file>\n");
        return 1;
    }
#ifdef _WIN32
    int interactive = _isatty(_fileno(stdin));
#else
    int interactive = isatty(fileno(stdin));
#endif
    answers a = {NULL, NULL, 0, 0, 0, 0, 0, NULL, NULL};
    /* диалог, затем запись файла */
    status_code sc = dialog(&a, interactive);
    if (sc == OK) {
        sc = write_config(argv[1], &a);
    }
    if (sc == OK) {
        printf("Config saved to '%s'\n", argv[1]);
    } else if (sc == FILE_ERROR) {
        fprintf(stderr, "Error: can not write file '%s'\n", argv[1]);
    } else if (sc == INPUT_END) {
        fprintf(stderr, "Error: the input ended before all parameters were entered\n");
    } else if (sc == ALLOC_ERROR) {
        fprintf(stderr, "Error: not enough memory\n");
    } else {
        /* INVALID_ARGS не бывает ни при каком вводе - только при ошибке в самой программе */
        fprintf(stderr, "Error: internal error\n");
    }
    free(a.operators);
    free(a.coeff);
    return sc == OK ? 0 : 1;
}
