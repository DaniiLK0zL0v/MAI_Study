/*
 * Лабораторная 4, задание 2. Интерпретатор операций над целочисленными массивами.
 *
 * Запуск:  lab4_task2 <файл с инструкциями> [--seed N]
 * Массивы называются буквами A..Z (a и A - один массив), вначале все пустые. Команды разделяются ';',
 * прописные и строчные буквы не различаются, пробелы и переносы строк - где угодно:
 *     Load A, in.txt;        - загрузить в A целые числа из файла (неправильные элементы пропускаются)
 *     Save A, out.txt;       - записать A в файл
 *     Rand A, count, lb, rb; - count случайных чисел из отрезка [lb; rb]
 *     Concat A, B;           - A = A, затем B
 *     Free(A);               - A = пустой массив (можно и без скобок: Free A)
 *     Remove A, 2, 7;        - удалить 7 элементов, начиная с индекса 2
 *     Copy A, 4, 10, B;      - B = элементы A с 4 по 10 включительно
 *     Sort A+;  Sort A-;     - по неубыванию / по невозрастанию (qsort)
 *     Shuffle A;             - случайная перестановка (тоже через qsort, см. shuffle_array)
 *     Stats A;               - размер, максимум и минимум (с индексами), самый частый элемент,
 *                              среднее и наибольшее отклонение от среднего
 *     Print A, 3;  Print A, 4, 16;  Print A, all;
 * Индексы - с 0. Ошибка в команде (запись или выполнение) - сообщение с номером команды и строки,
 * дальше выполняются следующие команды; в конце код 1, если ошибки были.
 *
 * Случайные числа - свой генератор xorshift64 (rand по общим правилам лабораторных не используем).
 * Начальное число - ключ --seed N (одинаковый seed - одинаковые числа, это нужно тестам),
 * без ключа - текущее время.
 */
#define _POSIX_C_SOURCE 200809L   /* stat (Linux) - до всех #include, см. same_file */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort */
#include <string.h>   /* memcpy, memmove, strcmp */
#include <limits.h>   /* LLONG_MAX, LLONG_MIN */
#include <time.h>     /* time - начальное число генератора без --seed */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define ARRAYS 26               /* массивы A..Z */
#define MAX_COUNT 10000000LL    /* наибольшее число элементов в Rand (80 МБ) */
#define START_CAPACITY 16

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,       /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,       /* команда записана неправильно */
    UNKNOWN_COMMAND,    /* нет такой команды */
    NUMBER_ERROR,       /* не число или число не помещается в long long */
    INDEX_ERROR,        /* индекс или количество вне массива */
    RANGE_ERROR,        /* lb > rb, count < 0 или слишком большой */
    EMPTY_ARRAY,        /* Stats пустого массива */
    SAME_FILE,          /* Save в файл с инструкциями */
    FILE_ERROR,         /* файл не открылся / ошибка чтения или записи */
    ALLOC_ERROR         /* malloc / realloc не смогли выделить память */
} status_code;

/* Массив переменной длины */
typedef struct {
    long long *data;
    size_t size;
    size_t capacity;
} array;

/* Состояние интерпретатора: 26 массивов и генератор случайных чисел */
typedef struct {
    array arrays[ARRAYS];
    unsigned long long random_state;
    const char *program_path;   /* файл с инструкциями - в него нельзя делать Save */
} machine;

/* "Курсор" по тексту одной команды: где мы сейчас и где конец */
typedef struct {
    const char *text;
    size_t position;
    size_t length;
} cursor;

/* Результат команды для печати: ошибка, предупреждение (Load) или ничего */
typedef struct {
    size_t skipped;   /* Load: сколько неправильных элементов пропущено */
} command_result;

/* ===================== массив ===================== */

status_code array_init(array *a) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    a->data = NULL;
    a->size = 0;
    a->capacity = 0;
    return OK;
}

status_code array_free(array *a) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    free(a->data);
    return array_init(a);
}

/* Обеспечить место хотя бы под need элементов: ёмкость растёт вдвое (амортизированно O(1) на элемент) */
status_code array_reserve(array *a, const size_t need) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    if (need <= a->capacity) {
        return OK;
    }
    size_t capacity = a->capacity == 0 ? START_CAPACITY : a->capacity;
    while (capacity < need) {
        if (capacity > ((size_t)-1) / 2 / sizeof(long long)) {
            return ALLOC_ERROR;
        }
        capacity *= 2;
    }
    long long *tmp = (long long *)realloc(a->data, capacity * sizeof(long long));
    if (tmp == NULL) {
        return ALLOC_ERROR;
    }
    a->data = tmp;
    a->capacity = capacity;
    return OK;
}

status_code array_push(array *a, const long long value) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = array_reserve(a, a->size + 1);
    if (sc != OK) {
        return sc;
    }
    a->data[a->size++] = value;
    return OK;
}

/* ===================== числа и генератор ===================== */

/*
 * Строка str из length символов -> long long. Необязательный знак, затем только цифры.
 * Переполнение проверяется до умножения (без errno и strtoll).
 */
status_code parse_long_long(const char *str, const size_t length, long long *value) {
    if (str == NULL || value == NULL || length == 0) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    int negative = 0;
    if (str[0] == '+' || str[0] == '-') {
        negative = (str[0] == '-');
        i = 1;
    }
    if (i == length) {
        return NUMBER_ERROR;   /* только знак */
    }
    /* копим модуль как отрицательное число: у LLONG_MIN нет положительной пары */
    long long result = 0;
    for (; i < length; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return NUMBER_ERROR;
        }
        int digit = str[i] - '0';
        if (result < (LLONG_MIN + digit) / 10) {
            return NUMBER_ERROR;   /* result * 10 - digit не поместится */
        }
        result = result * 10 - digit;
    }
    if (!negative) {
        if (result == LLONG_MIN) {
            return NUMBER_ERROR;   /* 9223372036854775808 без минуса не помещается */
        }
        result = -result;
    }
    *value = result;
    return OK;
}

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

/* Случайное число из отрезка [lb; rb] (lb <= rb) */
status_code random_in_range(unsigned long long *state, const long long lb, const long long rb,
                            long long *value) {
    if (state == NULL || value == NULL || lb > rb) {
        return INVALID_ARGS;
    }
    unsigned long long r = 0;
    next_random(state, &r);
    /* длина отрезка rb - lb + 1 в беззнаковой арифметике; 0 - отрезок на все 2^64 значений */
    unsigned long long width = (unsigned long long)rb - (unsigned long long)lb + 1ULL;
    unsigned long long offset = width == 0 ? r : r % width;
    /* lb + offset тоже в беззнаковой арифметике: так нет переполнения со знаком */
    *value = (long long)((unsigned long long)lb + offset);
    return OK;
}

/* ===================== один и тот же файл ===================== */

#ifdef _WIN32
/* Только для Windows: полный путь файла в динамической памяти (см. лабораторную 1) */
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

/*
 * Ведут ли два пути к одному файлу (*equal = 1). Строки сравнивать мало: "in.txt" и "./in.txt" - один файл.
 * Linux: stat, пара (номер устройства, номер файла). Windows: полные пути без учёта регистра.
 * Файла нет - значит не тот же.
 */
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

/* ===================== операции над массивами ===================== */

/* Load: прочитать из файла все целые числа; слово, которое не является числом, пропускается */
status_code load_array(array *a, const char *path, size_t *skipped) {
    if (a == NULL || path == NULL || skipped == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return FILE_ERROR;
    }
    /* читаем в новый массив: при ошибке старое содержимое A не пропадает */
    array loaded;
    array_init(&loaded);
    *skipped = 0;
    status_code sc = OK;
    char word[32];          /* самое длинное число long long - 20 символов со знаком */
    size_t length = 0;
    int too_long = 0;       /* слово длиннее буфера - точно не число */
    int symbol = 0;
    while (sc == OK) {
        symbol = fgetc(file);
        int separator = (symbol == EOF || symbol == ' ' || symbol == '\t' || symbol == '\n' || symbol == '\r' ||
                         symbol == '\v' || symbol == '\f');
        if (!separator) {
            if (length < sizeof(word)) {
                word[length++] = (char)symbol;
            } else {
                too_long = 1;
            }
            continue;
        }
        /* конец слова: число -> в массив, иначе пропускаем */
        if (length > 0 || too_long) {
            long long value = 0;
            if (!too_long && parse_long_long(word, length, &value) == OK) {
                sc = array_push(&loaded, value);
            } else {
                (*skipped)++;
            }
            length = 0;
            too_long = 0;
        }
        if (symbol == EOF) {
            break;
        }
    }
    if (sc == OK && ferror(file)) {
        sc = FILE_ERROR;
    }
    fclose(file);
    if (sc != OK) {
        array_free(&loaded);
        return sc;
    }
    array_free(a);
    *a = loaded;
    return OK;
}

/* Save: элементы через пробел, в конце перевод строки */
status_code save_array(const array *a, const char *path, const char *program_path) {
    if (a == NULL || path == NULL || program_path == NULL) {
        return INVALID_ARGS;
    }
    int equal = 0;
    status_code sc = same_file(path, program_path, &equal);
    if (sc != OK) {
        return sc;
    }
    if (equal) {
        return SAME_FILE;   /* fopen "w" стёр бы файл с инструкциями */
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return FILE_ERROR;
    }
    for (size_t i = 0; i < a->size; i++) {
        fprintf(file, i == 0 ? "%lld" : " %lld", a->data[i]);
    }
    fprintf(file, "\n");
    int failed = ferror(file);
    if (fclose(file) != 0 || failed) {
        return FILE_ERROR;
    }
    return OK;
}

/* Rand: count случайных чисел из [lb; rb] (старое содержимое заменяется) */
status_code rand_array(array *a, unsigned long long *state, const long long count, const long long lb,
                       const long long rb) {
    if (a == NULL || state == NULL) {
        return INVALID_ARGS;
    }
    if (count < 0 || count > MAX_COUNT || lb > rb) {
        return RANGE_ERROR;
    }
    array result;
    array_init(&result);
    status_code sc = array_reserve(&result, (size_t)count);
    for (long long i = 0; i < count && sc == OK; i++) {
        long long value = 0;
        random_in_range(state, lb, rb, &value);
        sc = array_push(&result, value);
    }
    if (sc != OK) {
        array_free(&result);
        return sc;
    }
    array_free(a);
    *a = result;
    return OK;
}

/* Concat: дописать b в конец a. a и b могут быть одним массивом (Concat A, A) */
status_code concat_arrays(array *a, const array *b) {
    if (a == NULL || b == NULL) {
        return INVALID_ARGS;
    }
    size_t b_size = b->size;   /* запоминаем до realloc: если a == b, размер изменится */
    status_code sc = array_reserve(a, a->size + b_size);
    if (sc != OK) {
        return sc;
    }
    /* после realloc b->data тоже обновился, если a и b - один массив */
    memmove(a->data + a->size, b->data, b_size * sizeof(long long));
    a->size += b_size;
    return OK;
}

/* Remove: удалить count элементов, начиная с index */
status_code remove_elements(array *a, const long long index, const long long count) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    if (index < 0 || count < 0 || (unsigned long long)index > a->size ||
        (unsigned long long)count > a->size - (size_t)index) {
        return INDEX_ERROR;
    }
    size_t from = (size_t)index;
    size_t n = (size_t)count;
    /* хвост за удалёнными элементами сдвигается влево */
    memmove(a->data + from, a->data + from + n, (a->size - from - n) * sizeof(long long));
    a->size -= n;
    return OK;
}

/* Copy: b = элементы a с from по to включительно. a и b могут совпадать */
status_code copy_elements(const array *a, const long long from, const long long to, array *b) {
    if (a == NULL || b == NULL) {
        return INVALID_ARGS;
    }
    if (from < 0 || from > to || (unsigned long long)to >= a->size) {
        return INDEX_ERROR;
    }
    array result;
    array_init(&result);
    size_t count = (size_t)(to - from + 1);
    status_code sc = array_reserve(&result, count);
    if (sc != OK) {
        return sc;
    }
    memcpy(result.data, a->data + from, count * sizeof(long long));
    result.size = count;
    array_free(b);
    *b = result;
    return OK;
}

/* Компараторы для qsort (их прототип задан qsort, поэтому они возвращают int, а не status_code) */
int compare_ascending(const void *x, const void *y) {
    long long a = *(const long long *)x;
    long long b = *(const long long *)y;
    return (a > b) - (a < b);   /* не a - b: разность может переполниться */
}

int compare_descending(const void *x, const void *y) {
    return compare_ascending(y, x);
}

/* Элемент с его случайным ключом - для перемешивания */
typedef struct {
    unsigned long long key;
    long long value;
} keyed_value;

int compare_keys(const void *x, const void *y) {
    unsigned long long a = ((const keyed_value *)x)->key;
    unsigned long long b = ((const keyed_value *)y)->key;
    return (a > b) - (a < b);
}

status_code sort_array(array *a, const int ascending) {
    if (a == NULL) {
        return INVALID_ARGS;
    }
    if (a->size > 1) {
        qsort(a->data, a->size, sizeof(long long), ascending ? compare_ascending : compare_descending);
    }
    return OK;
}

/*
 * Shuffle через qsort (так требует условие): каждому элементу - случайный ключ, сортируем по ключам.
 * Компаратор, который отвечает случайно, использовать нельзя: qsort ждёт непротиворечивых ответов
 * (если a < b и b < c, то a < c), иначе поведение не определено. Ключи же сравниваются честно.
 */
status_code shuffle_array(array *a, unsigned long long *state) {
    if (a == NULL || state == NULL) {
        return INVALID_ARGS;
    }
    if (a->size < 2) {
        return OK;
    }
    keyed_value *pairs = (keyed_value *)malloc(a->size * sizeof(keyed_value));
    if (pairs == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < a->size; i++) {
        next_random(state, &pairs[i].key);
        pairs[i].value = a->data[i];
    }
    qsort(pairs, a->size, sizeof(keyed_value), compare_keys);
    for (size_t i = 0; i < a->size; i++) {
        a->data[i] = pairs[i].value;
    }
    free(pairs);
    return OK;
}

/* Статистика массива (Stats) */
typedef struct {
    size_t size;
    long long max;
    size_t max_index;      /* первое вхождение максимума */
    long long min;
    size_t min_index;
    long long frequent;    /* самый частый элемент (из равных по частоте - наибольший) */
    size_t frequency;
    double average;
    double deviation;      /* наибольшее |элемент - среднее| */
} array_stats;

status_code compute_stats(const array *a, array_stats *stats) {
    if (a == NULL || stats == NULL) {
        return INVALID_ARGS;
    }
    if (a->size == 0) {
        return EMPTY_ARRAY;
    }
    stats->size = a->size;
    stats->max = a->data[0];
    stats->min = a->data[0];
    stats->max_index = 0;
    stats->min_index = 0;
    /*
     * Среднее без переполнения и без потери точности. Сумма элементов может не поместиться в long long,
     * а в double теряются младшие цифры (LLONG_MAX + LLONG_MIN в double даёт 0 вместо -1).
     * Поэтому каждый элемент делим на n с остатком: x = n * q + r. Тогда
     *     среднее = (сумма q) + (сумма r) / n.
     * Сумма q по модулю не больше наибольшего |x| - помещается в long long; |сумма r| < n * n <= 10^14.
     */
    long long n = (long long)a->size;
    long long quotients = 0;
    long long remainders = 0;
    for (size_t i = 0; i < a->size; i++) {
        quotients += a->data[i] / n;
        remainders += a->data[i] % n;
        if (a->data[i] > stats->max) {
            stats->max = a->data[i];
            stats->max_index = i;
        }
        if (a->data[i] < stats->min) {
            stats->min = a->data[i];
            stats->min_index = i;
        }
    }
    /* целую часть остатков переносим в частные: теперь |remainders| < n */
    quotients += remainders / n;
    remainders %= n;
    double fraction = (double)remainders / (double)n;
    stats->average = (double)quotients + fraction;
    /* дальше всех от среднего - максимум или минимум; разность целых частей считаем в double:
       max - quotients в long long может переполниться (9223372036854775807 - (-1)) */
    double up = ((double)stats->max - (double)quotients) - fraction;
    double down = ((double)quotients - (double)stats->min) + fraction;
    stats->deviation = up > down ? up : down;

    /* самый частый: сортируем копию, одинаковые элементы встают рядом, считаем длины серий */
    long long *copy = (long long *)malloc(a->size * sizeof(long long));
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(copy, a->data, a->size * sizeof(long long));
    qsort(copy, a->size, sizeof(long long), compare_ascending);
    stats->frequency = 0;
    size_t i = 0;
    while (i < a->size) {
        size_t j = i;
        while (j < a->size && copy[j] == copy[i]) {
            j++;
        }
        /* >= : серии идут по возрастанию значения, поэтому при равной частоте побеждает большее */
        if (j - i >= stats->frequency) {
            stats->frequency = j - i;
            stats->frequent = copy[i];
        }
        i = j;
    }
    free(copy);
    return OK;
}

/* ===================== печать ===================== */

status_code print_stats(FILE *stream, const char name, const array_stats *stats) {
    if (stream == NULL || stats == NULL) {
        return INVALID_ARGS;
    }
    fprintf(stream, "Stats %c:\n", name);
    fprintf(stream, "  size: %zu\n", stats->size);
    fprintf(stream, "  max: %lld (index %zu)\n", stats->max, stats->max_index);
    fprintf(stream, "  min: %lld (index %zu)\n", stats->min, stats->min_index);
    fprintf(stream, "  most frequent: %lld (%zu times)\n", stats->frequent, stats->frequency);
    fprintf(stream, "  average: %.6f\n", stats->average);
    fprintf(stream, "  max deviation from average: %.6f\n", stats->deviation);
    return OK;
}

/* Элементы с from по to включительно через пробел (массив и границы уже проверены) */
status_code print_range(FILE *stream, const array *a, const size_t from, const size_t to) {
    if (stream == NULL || a == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = from; i <= to && i < a->size; i++) {
        fprintf(stream, i == from ? "%lld" : " %lld", a->data[i]);
    }
    return OK;
}

/* ===================== разбор команды ===================== */

status_code skip_spaces(cursor *c) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    while (c->position < c->length) {
        char ch = c->text[c->position];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\v' && ch != '\f') {
            break;
        }
        c->position++;
    }
    return OK;
}

/* Слово из латинских букв (имя команды или "all"), переведённое в нижний регистр в out (до size-1 символов) */
status_code read_word(cursor *c, char *out, const size_t size) {
    if (c == NULL || out == NULL || size == 0) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    size_t length = 0;
    while (c->position < c->length) {
        char ch = c->text[c->position];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        } else if (ch < 'a' || ch > 'z') {
            break;
        }
        /* слишком длинное слово обрезается: оно длиннее любой команды и всё равно ни с чем не совпадёт */
        if (length + 1 < size) {
            out[length] = ch;
        }
        length++;
        c->position++;
    }
    out[length + 1 < size ? length : size - 1] = '\0';
    return length == 0 ? SYNTAX_ERROR : OK;
}

/* Имя массива: одна латинская буква (после неё не должна идти буква или цифра) -> номер 0..25 */
status_code read_array_name(cursor *c, int *index) {
    if (c == NULL || index == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    if (c->position >= c->length) {
        return SYNTAX_ERROR;
    }
    char ch = c->text[c->position];
    if (ch >= 'a' && ch <= 'z') {
        *index = ch - 'a';
    } else if (ch >= 'A' && ch <= 'Z') {
        *index = ch - 'A';
    } else {
        return SYNTAX_ERROR;
    }
    c->position++;
    if (c->position < c->length) {
        char next = c->text[c->position];
        if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9')) {
            return SYNTAX_ERROR;   /* "AB" - не имя массива */
        }
    }
    return OK;
}

/* Ожидаемый символ (после пробелов) */
status_code expect_char(cursor *c, const char expected) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    if (c->position >= c->length || c->text[c->position] != expected) {
        return SYNTAX_ERROR;
    }
    c->position++;
    return OK;
}

/* Целое число (после пробелов): знак и цифры */
status_code read_number(cursor *c, long long *value) {
    if (c == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    size_t start = c->position;
    if (c->position < c->length && (c->text[c->position] == '+' || c->text[c->position] == '-')) {
        c->position++;
    }
    while (c->position < c->length && c->text[c->position] >= '0' && c->text[c->position] <= '9') {
        c->position++;
    }
    if (c->position == start) {
        return SYNTAX_ERROR;
    }
    return parse_long_long(c->text + start, c->position - start, value);
}

/* Конец команды: дальше только пробелы */
status_code expect_end(cursor *c) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    return c->position == c->length ? OK : SYNTAX_ERROR;
}

/* Путь к файлу - весь остаток команды без пробелов по краям; копия в динамической памяти */
status_code read_path(cursor *c, char **path) {
    if (c == NULL || path == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    size_t end = c->length;
    while (end > c->position && (c->text[end - 1] == ' ' || c->text[end - 1] == '\t' ||
                                 c->text[end - 1] == '\n' || c->text[end - 1] == '\r')) {
        end--;
    }
    if (end == c->position) {
        return SYNTAX_ERROR;
    }
    char *copy = (char *)malloc(end - c->position + 1);
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(copy, c->text + c->position, end - c->position);
    copy[end - c->position] = '\0';
    c->position = c->length;
    *path = copy;
    return OK;
}

/* ===================== выполнение команды ===================== */

/*
 * Разобрать и выполнить одну команду (текст между ';'). Печать результата (Print, Stats) - в stream.
 * result - предупреждения (Load: сколько пропущено). *message - пояснение к ошибке (может быть NULL).
 */
status_code execute_command(machine *m, const char *text, const size_t length, FILE *stream,
                            command_result *result, const char **message) {
    if (m == NULL || text == NULL || stream == NULL || result == NULL || message == NULL) {
        return INVALID_ARGS;
    }
    cursor c = {text, 0, length};
    char command[16];
    int x = 0;
    int y = 0;
    long long n1 = 0;
    long long n2 = 0;
    long long n3 = 0;
    result->skipped = 0;
    *message = NULL;

    if (read_word(&c, command, sizeof(command)) != OK) {
        *message = "a command name expected";
        return SYNTAX_ERROR;
    }
    if (strcmp(command, "load") == 0 || strcmp(command, "save") == 0) {
        /* Load A, путь  /  Save A, путь */
        char *path = NULL;
        status_code sc = read_array_name(&c, &x);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_path(&c, &path);
        if (sc != OK) {
            *message = "expected: Load A, file;  or  Save A, file;";
            return sc;
        }
        if (command[0] == 'l') {
            sc = load_array(&m->arrays[x], path, &result->skipped);
        } else {
            sc = save_array(&m->arrays[x], path, m->program_path);
        }
        free(path);
        return sc;
    }
    if (strcmp(command, "rand") == 0) {
        status_code sc = read_array_name(&c, &x);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_number(&c, &n1);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_number(&c, &n2);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_number(&c, &n3);
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Rand A, count, lb, rb;";
            return sc;
        }
        return rand_array(&m->arrays[x], &m->random_state, n1, n2, n3);
    }
    if (strcmp(command, "concat") == 0) {
        status_code sc = read_array_name(&c, &x);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_array_name(&c, &y);
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Concat A, B;";
            return sc;
        }
        return concat_arrays(&m->arrays[x], &m->arrays[y]);
    }
    if (strcmp(command, "free") == 0) {
        /* Free(A) или Free A */
        skip_spaces(&c);
        int brackets = (c.position < c.length && c.text[c.position] == '(');
        status_code sc = OK;
        if (brackets) sc = expect_char(&c, '(');
        if (sc == OK) sc = read_array_name(&c, &x);
        if (sc == OK && brackets) sc = expect_char(&c, ')');
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Free(A);";
            return sc;
        }
        return array_free(&m->arrays[x]);
    }
    if (strcmp(command, "remove") == 0) {
        status_code sc = read_array_name(&c, &x);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_number(&c, &n1);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_number(&c, &n2);
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Remove A, index, count;";
            return sc;
        }
        return remove_elements(&m->arrays[x], n1, n2);
    }
    if (strcmp(command, "copy") == 0) {
        status_code sc = read_array_name(&c, &x);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_number(&c, &n1);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_number(&c, &n2);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc == OK) sc = read_array_name(&c, &y);
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Copy A, from, to, B;";
            return sc;
        }
        return copy_elements(&m->arrays[x], n1, n2, &m->arrays[y]);
    }
    if (strcmp(command, "sort") == 0) {
        /* Sort A+ / Sort A- */
        status_code sc = read_array_name(&c, &x);
        int ascending = 0;
        if (sc == OK) {
            skip_spaces(&c);
            if (c.position < c.length && (c.text[c.position] == '+' || c.text[c.position] == '-')) {
                ascending = (c.text[c.position] == '+');
                c.position++;
            } else {
                sc = SYNTAX_ERROR;
            }
        }
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Sort A+;  or  Sort A-;";
            return sc;
        }
        return sort_array(&m->arrays[x], ascending);
    }
    if (strcmp(command, "shuffle") == 0 || strcmp(command, "stats") == 0) {
        status_code sc = read_array_name(&c, &x);
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Shuffle A;  or  Stats A;";
            return sc;
        }
        if (command[1] == 'h') {
            return shuffle_array(&m->arrays[x], &m->random_state);
        }
        array_stats stats;
        sc = compute_stats(&m->arrays[x], &stats);
        if (sc == OK) {
            print_stats(stream, (char)('A' + x), &stats);
        }
        return sc;
    }
    if (strcmp(command, "print") == 0) {
        /* Print A, i  /  Print A, from, to  /  Print A, all */
        status_code sc = read_array_name(&c, &x);
        if (sc == OK) sc = expect_char(&c, ',');
        if (sc != OK) {
            *message = "expected: Print A, index;  Print A, from, to;  or  Print A, all;";
            return sc;
        }
        const array *a = &m->arrays[x];
        cursor saved = c;
        char word[8];
        if (read_word(&c, word, sizeof(word)) == OK) {
            if (strcmp(word, "all") != 0 || expect_end(&c) != OK) {
                *message = "expected: Print A, all;";
                return SYNTAX_ERROR;
            }
            fprintf(stream, "%c (%zu):", 'A' + x, a->size);
            if (a->size > 0) {
                fprintf(stream, " ");
                print_range(stream, a, 0, a->size - 1);
            }
            fprintf(stream, "\n");
            return OK;
        }
        c = saved;
        sc = read_number(&c, &n1);
        int two = 0;
        if (sc == OK) {
            skip_spaces(&c);
            if (c.position < c.length && c.text[c.position] == ',') {
                two = 1;
                c.position++;
                sc = read_number(&c, &n2);
            }
        }
        if (sc == OK) sc = expect_end(&c);
        if (sc != OK) {
            *message = "expected: Print A, index;  Print A, from, to;  or  Print A, all;";
            return sc;
        }
        if (!two) {
            if (n1 < 0 || (unsigned long long)n1 >= a->size) {
                return INDEX_ERROR;
            }
            fprintf(stream, "%c[%lld] = %lld\n", 'A' + x, n1, a->data[n1]);
            return OK;
        }
        if (n1 < 0 || n1 > n2 || (unsigned long long)n2 >= a->size) {
            return INDEX_ERROR;
        }
        fprintf(stream, "%c[%lld..%lld]: ", 'A' + x, n1, n2);
        print_range(stream, a, (size_t)n1, (size_t)n2);
        fprintf(stream, "\n");
        return OK;
    }
    return UNKNOWN_COMMAND;
}

/* Текст ошибки по статусу */
status_code status_text(const status_code sc, const char **text) {
    if (text == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case SYNTAX_ERROR:    *text = "syntax error"; break;
        case UNKNOWN_COMMAND: *text = "unknown command"; break;
        case NUMBER_ERROR:    *text = "the number is wrong or too big"; break;
        case INDEX_ERROR:     *text = "index or count is out of the array"; break;
        case RANGE_ERROR:     *text = "wrong count or range (need 0 <= count <= 10000000 and lb <= rb)"; break;
        case EMPTY_ARRAY:     *text = "the array is empty"; break;
        case SAME_FILE:       *text = "can not save into the instructions file"; break;
        case FILE_ERROR:      *text = "can not open, read or write the file"; break;
        case ALLOC_ERROR:     *text = "not enough memory"; break;
        default:              *text = "internal error"; break;   /* OK и INVALID_ARGS сюда не попадают */
    }
    return OK;
}

/* ===================== файл с инструкциями ===================== */

/* Прочитать весь файл в память (строка с нулём в конце) */
status_code read_whole_file(const char *path, char **text, size_t *length) {
    if (path == NULL || text == NULL || length == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return FILE_ERROR;
    }
    size_t capacity = 256;
    size_t size = 0;
    char *buf = (char *)malloc(capacity);
    if (buf == NULL) {
        fclose(file);
        return ALLOC_ERROR;
    }
    int symbol = 0;
    while ((symbol = fgetc(file)) != EOF) {
        if (size + 1 >= capacity) {
            char *tmp = (char *)realloc(buf, capacity * 2);
            if (tmp == NULL) {
                free(buf);
                fclose(file);
                return ALLOC_ERROR;
            }
            buf = tmp;
            capacity *= 2;
        }
        buf[size++] = (char)symbol;
    }
    int failed = ferror(file);
    fclose(file);
    if (failed) {
        free(buf);
        return FILE_ERROR;
    }
    buf[size] = '\0';
    *text = buf;
    *length = size;
    return OK;
}

/*
 * Выполнить все команды из текста программы. *errors - сколько команд завершилось ошибкой.
 * Ошибки и предупреждения печатаются в err, результаты команд - в out.
 */
status_code run_program(machine *m, const char *text, const size_t length, FILE *out, FILE *err, size_t *errors) {
    if (m == NULL || text == NULL || out == NULL || err == NULL || errors == NULL) {
        return INVALID_ARGS;
    }
    *errors = 0;
    size_t start = 0;
    size_t line = 1;          /* номер строки, на которой сейчас позиция start */
    size_t number = 0;        /* номер команды */
    while (start < length) {
        /* пропускаем пробелы перед командой (и считаем строки) */
        while (start < length && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' ||
                                  text[start] == '\r')) {
            if (text[start] == '\n') {
                line++;
            }
            start++;
        }
        if (start == length) {
            break;
        }
        size_t end = start;
        size_t lines_inside = 0;
        while (end < length && text[end] != ';') {
            if (text[end] == '\n') {
                lines_inside++;
            }
            end++;
        }
        number++;
        if (end == length) {
            fprintf(err, "Error in command %zu (line %zu): ';' expected at the end\n", number, line);
            (*errors)++;
            break;
        }
        command_result result;
        const char *message = NULL;
        status_code sc = execute_command(m, text + start, end - start, out, &result, &message);
        fflush(out);
        if (sc == ALLOC_ERROR) {
            return sc;   /* памяти нет - дальше работать нельзя */
        }
        if (sc != OK) {
            const char *reason = NULL;
            status_text(sc, &reason);
            fprintf(err, "Error in command %zu (line %zu): %s%s%s\n", number, line, reason,
                    message != NULL ? ": " : "", message != NULL ? message : "");
            (*errors)++;
        } else if (result.skipped > 0) {
            fprintf(err, "Warning in command %zu (line %zu): %zu wrong elements skipped\n", number, line,
                    result.skipped);
        }
        line += lines_inside;
        start = end + 1;
    }
    return OK;
}

int main(int argc, char *argv[]) {
    /* аргументы: файл и необязательный --seed N */
    unsigned long long seed = (unsigned long long)time(NULL);
    if (argc == 4 && strcmp(argv[2], "--seed") == 0) {
        long long value = 0;
        if (parse_long_long(argv[3], strlen(argv[3]), &value) != OK || value < 0) {
            fprintf(stderr, "Error: the seed must be a non-negative integer\n");
            return 1;
        }
        seed = (unsigned long long)value;
    } else if (argc != 2) {
        fprintf(stderr, "Usage: lab4_task2 <instructions file> [--seed N]\n");
        return 1;
    }

    char *text = NULL;
    size_t length = 0;
    status_code sc = read_whole_file(argv[1], &text, &length);
    if (sc != OK) {
        fprintf(stderr, sc == FILE_ERROR ? "Error: can not read file '%s'\n" : "Error: not enough memory%s\n",
                sc == FILE_ERROR ? argv[1] : "");
        return 1;
    }

    machine m;
    for (int i = 0; i < ARRAYS; i++) {
        array_init(&m.arrays[i]);
    }
    /* у xorshift состояние 0 - "залипает" (всегда 0), поэтому подмешиваем постоянную */
    m.random_state = seed ^ 0x9E3779B97F4A7C15ULL;
    if (m.random_state == 0) {
        m.random_state = 1;
    }
    m.program_path = argv[1];

    size_t errors = 0;
    sc = run_program(&m, text, length, stdout, stderr, &errors);
    for (int i = 0; i < ARRAYS; i++) {
        array_free(&m.arrays[i]);
    }
    free(text);
    if (sc != OK) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    return errors > 0 ? 1 : 0;
}
