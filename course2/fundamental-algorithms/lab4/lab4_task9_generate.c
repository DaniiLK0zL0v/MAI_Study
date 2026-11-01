/*
 * Лабораторная 4, задание 9.3. Генератор файлов заявок для задания 9.1 (без диалога с пользователем).
 *
 * Запуск:  lab4_task9_generate <max_priority> <файл параметров> <файл заявок> [<файл заявок> ...] [--seed N]
 *   аргументы - как у задания 9.1: файл параметров читается (и проверяется) тем же способом, из него берутся
 *   начало и конец моделирования и число отделений; каждый файл заявок создаётся заново.
 *   --seed - начальное число генератора (одинаковый seed - одинаковые файлы); без ключа - текущее время.
 *
 * В каждом файле - случайное число заявок (10..100); у заявки случайные время (целая минута от начала до
 * конца моделирования), приоритет (0..max_priority), отделение (1..n) и текст (1..4 слова из списка).
 * Заявки в файле идут по возрастанию времени, как в настоящем журнале обращений.
 */
#define _POSIX_C_SOURCE 200809L   /* stat (Linux) - до всех #include, см. same_file */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort */
#include <string.h>   /* strcmp, strlen */
#include <limits.h>   /* LLONG_MAX, INT_MAX */
#include <time.h>     /* time - начальное число генератора без --seed */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define MAX_DEPARTMENTS 100
#define MIN_OPERATORS 10
#define MAX_OPERATORS 50
#define MAX_HANDLING_TIME 1000000LL
#define MAX_COEFF_DIGITS 6
#define MAX_COEFF_WHOLE 1000LL
#define MIN_REQUESTS 10               /* сколько заявок в одном файле: от MIN_REQUESTS до MAX_REQUESTS */
#define MAX_REQUESTS 100

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,    /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,    /* строка записана неправильно */
    EARLY_END,       /* файл параметров кончился раньше, чем нашлись все параметры */
    FILE_ERROR,      /* файл не открылся / ошибка чтения или записи */
    ALLOC_ERROR      /* malloc / realloc не смогли выделить память */
} status_code;

/* ===================== случайные числа ===================== */

/* Следующее псевдослучайное 64-битное число: xorshift64 (сдвиги и "исключающее или") */
status_code next_random(unsigned long long *state, unsigned long long *number) {
    if (state == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    *number = x;
    return OK;
}

/* Случайное число от 0 до limit - 1 */
status_code random_below(unsigned long long *state, const unsigned long long limit, unsigned long long *number) {
    if (state == NULL || number == NULL || limit == 0) {
        return INVALID_ARGS;
    }
    unsigned long long r = 0;
    next_random(state, &r);
    *number = r % limit;
    return OK;
}

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

/*
 * "YYYY-MM-DDTHH:MM:SS" или "YYYY-MM-DDTHH:MM" -> секунды от 0001-01-01T00:00:00.
 * Проверяются формат и диапазоны (месяц 1..12, день - по длине месяца, часы 0..23, минуты и секунды 0..59).
 */
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
    /* год: начинаем с оценки снизу (в году не больше 366 дней) и идём вперёд, пока следующий год не позже */
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

/*
 * Коэффициент перегрузки "целая[.дробная]" -> дробь numerator / denominator (denominator = 10^k), без double:
 * "1.25" -> 125 / 100. Так сравнение "очередь >= операторы * коэффициент" - точное, в целых числах.
 * Нужно 1.0 и больше; целая часть - до MAX_COEFF_WHOLE, после точки - до MAX_COEFF_DIGITS цифр.
 */
status_code parse_coeff(const char *s, long long *numerator, long long *denominator) {
    if (s == NULL || numerator == NULL || denominator == NULL) {
        return INVALID_ARGS;
    }
    long long whole = 0;
    long long fraction = 0;
    long long scale = 1;
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
        for (; s[i] >= '0' && s[i] <= '9'; i++) {
            if (i - start == MAX_COEFF_DIGITS) {
                return SYNTAX_ERROR;
            }
            fraction = fraction * 10 + (s[i] - '0');
            scale *= 10;
        }
        if (i == start) {
            return SYNTAX_ERROR;   /* "1." - после точки нет цифр */
        }
    }
    if (s[i] != '\0' || whole < 1) {
        return SYNTAX_ERROR;
    }
    *numerator = whole * scale + fraction;
    *denominator = scale;
    return OK;
}

/* Прочитать строку любой длины (без '\n' и '\r'); *end = 1 - файл кончился (строки нет) */
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

/* ===================== файлы: один и тот же? ===================== */

#ifdef _WIN32
/* Полный путь файла (Windows, _fullpath); буфер растёт, если путь длинный */
status_code full_path(const char *path, char **full) {
    if (path == NULL || full == NULL) {
        return INVALID_ARGS;
    }
    for (size_t size = 260; size <= 65536; size *= 2) {
        char *buf = (char *)malloc(size);
        if (buf == NULL) {
            return ALLOC_ERROR;
        }
        if (_fullpath(buf, path, size) != NULL) {
            *full = buf;
            return OK;
        }
        free(buf);
    }
    return FILE_ERROR;
}
#endif

/* Ведут ли два пути к одному файлу (как в лабораторной 1): Linux - stat, Windows - полные пути */
status_code same_file(const char *path1, const char *path2, int *equal) {
    if (path1 == NULL || path2 == NULL || equal == NULL) {
        return INVALID_ARGS;
    }
    *equal = 0;
#ifdef _WIN32
    char *full1 = NULL;
    char *full2 = NULL;
    status_code sc1 = full_path(path1, &full1);
    status_code sc2 = full_path(path2, &full2);
    if (sc1 == ALLOC_ERROR || sc2 == ALLOC_ERROR) {
        free(full1);
        free(full2);
        return ALLOC_ERROR;
    }
    if (sc1 == OK && sc2 == OK && _stricmp(full1, full2) == 0) {
        *equal = 1;
    }
    free(full1);
    free(full2);
#else
    struct stat info1;
    struct stat info2;
    if (stat(path1, &info1) == 0 && stat(path2, &info2) == 0 && info1.st_dev == info2.st_dev &&
        info1.st_ino == info2.st_ino) {
        *equal = 1;
    }
#endif
    return OK;
}

/* ===================== файл параметров (как в задании 9.1) ===================== */

const char *HEAP_NAMES[] = {"BinaryHeap", "BinomialHeap", "FibonacciHeap", "SkewHeap", "LeftistHeap", "Treap"};
#define HEAP_KINDS 6
const char *MAP_NAMES[] = {"HashSet", "DynamicArray", "BinarySearchTree", "Trie"};
#define MAP_KINDS 4

typedef struct {
    const char *heap;              /* названия структур (генератору нужны только для проверки файла) */
    const char *map;
    long long start;               /* начало и конец моделирования, секунды */
    long long end;
    long long min_time;            /* время обработки заявки, минуты */
    long long max_time;
    size_t department_count;
    long long *operators;          /* число операторов каждого отделения */
    long long coeff_numerator;     /* коэффициент перегрузки = numerator / denominator */
    long long coeff_denominator;
} model_config;

/* Описание ошибки в файле параметров: номер строки и причина */
typedef struct {
    size_t line;
    const char *reason;
} config_error;

/* Название из списка names (NULL - нет такого) */
status_code find_name(const char *text, const char **names, const size_t count, const char **result) {
    if (text == NULL || names == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    *result = NULL;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(text, names[i]) == 0) {
            *result = names[i];
        }
    }
    return OK;
}

/* Разобрать строку number (0..8) файла параметров; reason - причина ошибки */
status_code config_value(model_config *cfg, const size_t number, char *text, const char **reason) {
    if (cfg == NULL || text == NULL || reason == NULL) {
        return INVALID_ARGS;
    }
    long long value = 0;
    switch (number) {
        case 0:   /* структура для заявок */
            *reason = "wrong data structure for requests "
                      "(BinaryHeap, BinomialHeap, FibonacciHeap, SkewHeap, LeftistHeap or Treap)";
            find_name(text, HEAP_NAMES, HEAP_KINDS, &cfg->heap);
            return cfg->heap != NULL ? OK : SYNTAX_ERROR;
        case 1:   /* структура для отделений */
            *reason = "wrong data structure for departments (HashSet, DynamicArray, BinarySearchTree or Trie)";
            find_name(text, MAP_NAMES, MAP_KINDS, &cfg->map);
            return cfg->map != NULL ? OK : SYNTAX_ERROR;
        case 2:   /* начало */
            *reason = "wrong start time (need YYYY-MM-DDTHH:MM:SS)";
            return parse_time(text, &cfg->start);
        case 3:   /* конец - не раньше начала */
            *reason = "wrong end time (need YYYY-MM-DDTHH:MM:SS, not before the start)";
            if (parse_time(text, &cfg->end) != OK || cfg->end < cfg->start) {
                return SYNTAX_ERROR;
            }
            return OK;
        case 4:   /* минимальное время обработки */
            *reason = "wrong minimal handling time (need a natural number up to 1000000)";
            return parse_number(text, 1, MAX_HANDLING_TIME, &cfg->min_time);
        case 5:   /* максимальное - не меньше минимального */
            *reason = "wrong maximal handling time (need a natural number from the minimal time up to 1000000)";
            return parse_number(text, cfg->min_time, MAX_HANDLING_TIME, &cfg->max_time);
        case 6:   /* число отделений */
            *reason = "wrong number of departments (need 1..100)";
            if (parse_number(text, 1, MAX_DEPARTMENTS, &value) != OK) {
                return SYNTAX_ERROR;
            }
            cfg->department_count = (size_t)value;
            cfg->operators = (long long *)malloc(cfg->department_count * sizeof(long long));
            return cfg->operators != NULL ? OK : ALLOC_ERROR;
        case 7: { /* операторы: ровно department_count чисел через пробел */
            *reason = "wrong numbers of operators (need one number from 10 to 50 for each department)";
            char *rest = text;
            for (size_t i = 0; i < cfg->department_count; i++) {
                char *word = NULL;
                cut_word(rest, &word, &rest);
                if (parse_number(word, MIN_OPERATORS, MAX_OPERATORS, &cfg->operators[i]) != OK) {
                    return SYNTAX_ERROR;
                }
            }
            char *extra = NULL;
            trim(rest, &extra);
            return extra[0] == '\0' ? OK : SYNTAX_ERROR;
        }
        case 8:   /* коэффициент перегрузки */
            *reason = "wrong overload coefficient (need a number 1.0 or more, up to 6 digits after the point)";
            return parse_coeff(text, &cfg->coeff_numerator, &cfg->coeff_denominator);
        default:
            *reason = "extra line after the overload coefficient";
            return SYNTAX_ERROR;
    }
}

/* Что должно быть в строке number - для сообщения "файл кончился раньше" */
const char *CONFIG_NAMES[] = {"the data structure for requests", "the data structure for departments",
                              "the start time", "the end time", "the minimal handling time",
                              "the maximal handling time", "the number of departments",
                              "the numbers of operators", "the overload coefficient"};
#define CONFIG_LINES 9

/*
 * Прочитать файл параметров. Пустые строки пропускаются; строки - по порядку из условия.
 * SYNTAX_ERROR - ошибка, *error - строка и причина; EARLY_END - не все параметры; FILE_ERROR - файл не открылся.
 */
status_code read_config(const char *path, model_config *cfg, config_error *error) {
    if (path == NULL || cfg == NULL || error == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return FILE_ERROR;
    }
    size_t number = 0;        /* сколько параметров уже прочитано */
    size_t line_number = 0;
    status_code sc = OK;
    for (;;) {
        char *line = NULL;
        int end = 0;
        sc = read_line(file, &line, &end);
        if (sc != OK) {
            break;
        }
        if (end) {
            free(line);
            break;
        }
        line_number++;
        char *text = NULL;
        trim(line, &text);
        if (text[0] != '\0') {
            sc = config_value(cfg, number, text, &error->reason);
            number++;
        }
        free(line);
        if (sc != OK) {
            error->line = line_number;
            break;
        }
    }
    if (ferror(file) && sc == OK) {
        sc = FILE_ERROR;
    }
    fclose(file);
    /* файл кончился раньше, чем нашлись все параметры */
    if (sc == OK && number < CONFIG_LINES) {
        error->line = line_number + 1;
        error->reason = CONFIG_NAMES[number];
        return EARLY_END;
    }
    return sc;
}

/* ===================== генерация ===================== */

/* Слова для текстов заявок */
const char *WORDS[] = {"my", "internet", "printer", "does", "not", "work", "please", "help",
                       "the", "account", "is", "blocked", "password", "lost", "slow", "again"};
#define WORD_COUNT 16

/* Компаратор qsort (его прототип задан qsort): времена по возрастанию */
int compare_times(const void *a, const void *b) {
    long long x = *(const long long *)a;
    long long y = *(const long long *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/*
 * Создать один файл заявок. Порядок случайных чисел: число заявок, все времена, затем для каждой заявки
 * приоритет, отделение, число слов и слова. *count - сколько заявок записано.
 */
status_code generate_file(const char *path, const model_config *cfg, const long long max_priority,
                          unsigned long long *state, size_t *count) {
    if (path == NULL || cfg == NULL || state == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long r = 0;
    random_below(state, MAX_REQUESTS - MIN_REQUESTS + 1, &r);
    size_t n = MIN_REQUESTS + (size_t)r;
    /* времена: целые минуты от начала до конца моделирования, по возрастанию */
    long long *times = (long long *)malloc(n * sizeof(long long));
    if (times == NULL) {
        return ALLOC_ERROR;
    }
    unsigned long long minutes = (unsigned long long)((cfg->end - cfg->start) / 60) + 1;
    for (size_t i = 0; i < n; i++) {
        random_below(state, minutes, &r);
        times[i] = cfg->start + (long long)r * 60;
    }
    qsort(times, n, sizeof(long long), compare_times);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        free(times);
        return FILE_ERROR;
    }
    /* строки заявок:  <время> <приоритет> <отделение> "<текст>" */
    for (size_t i = 0; i < n; i++) {
        char text[24];
        time_text(times[i], text);
        unsigned long long priority = 0;
        unsigned long long department = 0;
        unsigned long long words = 0;
        random_below(state, (unsigned long long)max_priority + 1, &priority);
        random_below(state, cfg->department_count, &department);
        random_below(state, 4, &words);
        fprintf(file, "%s %lld %lld \"", text, (long long)priority, (long long)department + 1);
        /* текст - от 1 до 4 слов через пробел */
        for (unsigned long long k = 0; k <= words; k++) {
            random_below(state, WORD_COUNT, &r);
            if (k > 0) {
                fputc(' ', file);
            }
            fputs(WORDS[r], file);
        }
        fprintf(file, "\"\n");
    }
    free(times);
    int failed = ferror(file);
    if (fclose(file) != 0) {
        failed = 1;
    }
    *count = n;
    return failed ? FILE_ERROR : OK;
}

int main(int argc, char *argv[]) {
    const char *usage = "Usage: lab4_task9_generate <max_priority> <config file> <requests file> ... [--seed N]";
    /* разбор аргументов: --seed где угодно, остальное - по порядку */
    const char *seed_text = NULL;
    const char **files = (const char **)malloc((size_t)argc * sizeof(char *));
    if (files == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    size_t count = 0;
    int wrong = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0) {
            if (i + 1 == argc || seed_text != NULL) {
                wrong = 1;   /* нет значения или ключ повторяется */
            } else {
                seed_text = argv[++i];
            }
        } else {
            files[count++] = argv[i];
        }
    }
    if (wrong || count < 3) {
        fprintf(stderr, "%s\n", usage);
        free(files);
        return 1;
    }

    /* max_priority и начальное число генератора */
    long long max_priority = 0;
    if (parse_number(files[0], 1, INT_MAX, &max_priority) != OK) {
        fprintf(stderr, "Error: max_priority must be a natural number (up to 2147483647)\n");
        free(files);
        return 1;
    }
    long long seed = (long long)time(NULL);
    if (seed_text != NULL && parse_number(seed_text, 0, LLONG_MAX, &seed) != OK) {
        fprintf(stderr, "Error: the seed must be a number\n");
        free(files);
        return 1;
    }
    /* у xorshift состояние 0 "залипает" (всегда 0), поэтому подмешиваем постоянную */
    unsigned long long state = (unsigned long long)seed ^ 0x9E3779B97F4A7C15ULL;
    if (state == 0) {
        state = 0x9E3779B97F4A7C15ULL;
    }

    /* файл параметров - так же, как в задании 9.1 */
    model_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_error error = {0, NULL};
    status_code sc = read_config(files[1], &cfg, &error);
    if (sc != OK) {
        if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not read file '%s'\n", files[1]);
        } else if (sc == SYNTAX_ERROR) {
            fprintf(stderr, "Error: config '%s' line %zu: %s\n", files[1], error.line, error.reason);
        } else if (sc == EARLY_END) {
            fprintf(stderr, "Error: config '%s' ended too early: no %s\n", files[1], error.reason);
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
        } else {
            /* INVALID_ARGS не бывает ни при каком файле - только при ошибке в самой программе */
            fprintf(stderr, "Error: internal error\n");
        }
        free(cfg.operators);
        free(files);
        return 1;
    }

    /* файлы заявок по очереди; файл не должен совпадать с файлом параметров или с уже созданным */
    int result = 0;
    for (size_t i = 2; i < count && result == 0; i++) {
        int equal = 0;
        for (size_t j = 1; j < i && !equal; j++) {
            same_file(files[i], files[j], &equal);
        }
        if (equal) {
            fprintf(stderr, "Error: file '%s' is the config file or is given twice\n", files[i]);
            result = 1;
            break;
        }
        size_t n = 0;
        sc = generate_file(files[i], &cfg, max_priority, &state, &n);
        if (sc == OK) {
            printf("file '%s': %zu requests\n", files[i], n);
        } else if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not write file '%s'\n", files[i]);
            result = 1;
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
            result = 1;
        } else {
            /* INVALID_ARGS не бывает ни при каком вводе - только при ошибке в самой программе */
            fprintf(stderr, "Error: internal error\n");
            result = 1;
        }
    }
    free(cfg.operators);
    free(files);
    return result;
}
