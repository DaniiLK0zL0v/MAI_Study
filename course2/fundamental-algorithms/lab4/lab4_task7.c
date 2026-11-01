/*
 * Лабораторная 4, задание 7. Интерпретатор присваиваний на массиве MemoryCell.
 *
 * Запуск:  lab4_task7 <файл с инструкциями>
 * Инструкции (каждая кончается ';', пробелы и переносы строк допускаются):
 *     имя=операнд;                 инициализация или присваивание
 *     имя=операнд оп операнд;      оп - одна из + - * / % (целочисленное деление и остаток)
 *     print имя;                   напечатать значение переменной
 *     print;                       напечатать все объявленные переменные: "имя = значение" (по имени)
 * Операнд - имя переменной или целое число в десятичной записи (можно со знаком: a=-5; b=a*-2;).
 * Имя переменной - только латинские буквы, прописные и строчные различаются.
 *
 * Переменные хранятся в динамическом массиве структур MemoryCell, упорядоченном по имени:
 *   - новая переменная дописывается в конец (ёмкость растёт вдвое - добавление за амортизированную
 *     константу по перевыделению памяти), затем массив сортируется стандартной qsort;
 *   - поиск - дихотомический (двоичный), свой.
 * Использование необъявленной переменной останавливает интерпретатор с сообщением в стандартный вывод
 * (по условию). Так же останавливают работу деление на 0, переполнение и ошибка записи инструкции.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort */
#include <string.h>   /* strcmp, memcpy */
#include <limits.h>   /* LLONG_MAX, LLONG_MIN */

#define START_CAPACITY 4

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,      /* инструкция записана неправильно */
    NOT_DECLARED,      /* переменная не объявлена */
    DIVISION_BY_ZERO,  /* деление или остаток на 0 */
    OVERFLOW_ERROR,    /* число или результат не помещается в long long */
    FILE_ERROR,        /* файл не открылся / ошибка чтения */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Ячейка памяти: имя переменной и её значение */
typedef struct {
    char *name;
    long long value;
} MemoryCell;

/* Массив ячеек, упорядоченный по имени */
typedef struct {
    MemoryCell *cells;
    size_t count;
    size_t capacity;
} memory;

/* ===================== память переменных ===================== */

status_code memory_free(memory *m) {
    if (m == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < m->count; i++) {
        free(m->cells[i].name);
    }
    free(m->cells);
    m->cells = NULL;
    m->count = 0;
    m->capacity = 0;
    return OK;
}

/*
 * Дихотомический поиск переменной по имени (name - не обязательно с нулём в конце: length символов).
 * *index - номер ячейки (если найдена) или место, куда её пришлось бы вставить; *found - нашли ли.
 */
status_code memory_find(const memory *m, const char *name, const size_t length, size_t *index, int *found) {
    if (m == NULL || name == NULL || index == NULL || found == NULL) {
        return INVALID_ARGS;
    }
    size_t low = 0;
    size_t high = m->count;   /* ищем в [low, high) */
    *found = 0;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        /* сравнение имени ячейки с куском name длины length - как strcmp */
        int order = strncmp(m->cells[middle].name, name, length);
        if (order == 0 && m->cells[middle].name[length] != '\0') {
            order = 1;   /* имя ячейки длиннее, а начало совпало: оно "больше" */
        }
        if (order == 0) {
            *index = middle;
            *found = 1;
            return OK;
        }
        if (order < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *index = low;
    return OK;
}

/* Компаратор для qsort: по имени (прототип задан qsort, поэтому возвращает int) */
int compare_cells(const void *a, const void *b) {
    return strcmp(((const MemoryCell *)a)->name, ((const MemoryCell *)b)->name);
}

/*
 * Записать значение переменной: есть - меняем, нет - объявляем: дописываем в конец (ёмкость растёт
 * вдвое, поэтому realloc нужен редко - в среднем O(1)) и сортируем массив qsort.
 */
status_code memory_set(memory *m, const char *name, const size_t length, const long long value) {
    if (m == NULL || name == NULL || length == 0) {
        return INVALID_ARGS;
    }
    size_t index = 0;
    int found = 0;
    memory_find(m, name, length, &index, &found);
    if (found) {
        m->cells[index].value = value;
        return OK;
    }
    if (m->count == m->capacity) {
        size_t capacity = m->capacity == 0 ? START_CAPACITY : m->capacity * 2;
        MemoryCell *tmp = (MemoryCell *)realloc(m->cells, capacity * sizeof(MemoryCell));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        m->cells = tmp;
        m->capacity = capacity;
    }
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(copy, name, length);
    copy[length] = '\0';
    m->cells[m->count].name = copy;
    m->cells[m->count].value = value;
    m->count++;
    qsort(m->cells, m->count, sizeof(MemoryCell), compare_cells);
    return OK;
}

/* ===================== арифметика ===================== */

status_code apply(const char op, const long long a, const long long b, long long *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    switch (op) {
        case '+':
            if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
                return OVERFLOW_ERROR;
            }
            *result = a + b;
            return OK;
        case '-':
            if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
                return OVERFLOW_ERROR;
            }
            *result = a - b;
            return OK;
        case '*':
            if (a != 0 && b != 0) {
                if ((a == -1 && b == LLONG_MIN) || (b == -1 && a == LLONG_MIN)) {
                    return OVERFLOW_ERROR;
                }
                if (a > 0 ? (b > 0 ? a > LLONG_MAX / b : b < LLONG_MIN / a)
                          : (b > 0 ? a < LLONG_MIN / b : a < LLONG_MAX / b)) {
                    return OVERFLOW_ERROR;
                }
            }
            *result = a * b;
            return OK;
        case '/':
        case '%':
            if (b == 0) {
                return DIVISION_BY_ZERO;
            }
            if (a == LLONG_MIN && b == -1) {
                if (op == '/') {
                    return OVERFLOW_ERROR;
                }
                *result = 0;   /* LLONG_MIN % -1 в C - неопределённое поведение, а ответ - 0 */
                return OK;
            }
            *result = op == '/' ? a / b : a % b;
            return OK;
        default:
            return INVALID_ARGS;
    }
}

/* ===================== разбор инструкций ===================== */

typedef struct {
    const char *text;
    size_t position;
    size_t length;
} cursor;

status_code skip_spaces(cursor *c) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    while (c->position < c->length && (c->text[c->position] == ' ' || c->text[c->position] == '\t' ||
                                       c->text[c->position] == '\n' || c->text[c->position] == '\r')) {
        c->position++;
    }
    return OK;
}

int is_letter(const char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

/* Имя (только буквы): *start, *length - кусок текста */
status_code read_name(cursor *c, size_t *start, size_t *length) {
    if (c == NULL || start == NULL || length == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    *start = c->position;
    while (c->position < c->length && is_letter(c->text[c->position])) {
        c->position++;
    }
    *length = c->position - *start;
    return *length == 0 ? SYNTAX_ERROR : OK;
}

/*
 * Операнд: имя объявленной переменной или число со знаком. *bad_start, *bad_length - имя
 * необъявленной переменной (для сообщения).
 */
status_code read_operand(cursor *c, const memory *m, long long *value, size_t *bad_start, size_t *bad_length) {
    if (c == NULL || m == NULL || value == NULL || bad_start == NULL || bad_length == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    if (c->position < c->length && is_letter(c->text[c->position])) {
        size_t start = 0;
        size_t length = 0;
        read_name(c, &start, &length);
        size_t index = 0;
        int found = 0;
        memory_find(m, c->text + start, length, &index, &found);
        if (!found) {
            *bad_start = start;
            *bad_length = length;
            return NOT_DECLARED;
        }
        *value = m->cells[index].value;
        return OK;
    }
    /* число: необязательный знак и цифры; модуль копим отрицательным (у LLONG_MIN нет пары) */
    int negative = 0;
    if (c->position < c->length && (c->text[c->position] == '-' || c->text[c->position] == '+')) {
        negative = c->text[c->position] == '-';
        c->position++;
    }
    size_t digits = c->position;
    long long result = 0;
    while (c->position < c->length && c->text[c->position] >= '0' && c->text[c->position] <= '9') {
        int digit = c->text[c->position] - '0';
        if (result < (LLONG_MIN + digit) / 10) {
            return OVERFLOW_ERROR;
        }
        result = result * 10 - digit;
        c->position++;
    }
    if (c->position == digits) {
        return SYNTAX_ERROR;
    }
    if (!negative) {
        if (result == LLONG_MIN) {
            return OVERFLOW_ERROR;
        }
        result = -result;
    }
    *value = result;
    return OK;
}

/* Что печатать после инструкции */
typedef enum { PRINT_NONE, PRINT_ONE, PRINT_ALL } print_kind;

/*
 * Выполнить одну инструкцию (текст без ';'). *print, *value - что напечатать; *bad_start и *bad_length -
 * где в тексте имя необъявленной переменной.
 */
status_code execute(memory *m, const char *text, const size_t length, print_kind *print, long long *value,
                    size_t *bad_start, size_t *bad_length) {
    if (m == NULL || text == NULL || print == NULL || value == NULL || bad_start == NULL || bad_length == NULL) {
        return INVALID_ARGS;
    }
    cursor c = {text, 0, length};
    *print = PRINT_NONE;
    size_t start = 0;
    size_t name_length = 0;
    status_code sc = read_name(&c, &start, &name_length);
    if (sc != OK) {
        return sc;
    }
    skip_spaces(&c);
    /* print; / print имя;  ("print=..." - присваивание переменной с именем print) */
    if (name_length == 5 && strncmp(text + start, "print", 5) == 0 &&
        (c.position == c.length || c.text[c.position] != '=')) {
        if (c.position == c.length) {
            *print = PRINT_ALL;
            return OK;
        }
        sc = read_name(&c, &start, &name_length);
        skip_spaces(&c);
        if (sc != OK || c.position != c.length) {
            return SYNTAX_ERROR;
        }
        size_t index = 0;
        int found = 0;
        memory_find(m, text + start, name_length, &index, &found);
        if (!found) {
            *bad_start = start;
            *bad_length = name_length;
            return NOT_DECLARED;
        }
        *print = PRINT_ONE;
        *value = m->cells[index].value;
        return OK;
    }
    /* имя = операнд [оп операнд] */
    if (c.position >= c.length || c.text[c.position] != '=') {
        return SYNTAX_ERROR;
    }
    c.position++;
    long long left = 0;
    sc = read_operand(&c, m, &left, bad_start, bad_length);
    if (sc != OK) {
        return sc;
    }
    skip_spaces(&c);
    long long result = left;
    if (c.position < c.length) {
        char op = c.text[c.position];
        if (op != '+' && op != '-' && op != '*' && op != '/' && op != '%') {
            return SYNTAX_ERROR;
        }
        c.position++;
        long long right = 0;
        sc = read_operand(&c, m, &right, bad_start, bad_length);
        if (sc != OK) {
            return sc;
        }
        skip_spaces(&c);
        if (c.position != c.length) {
            return SYNTAX_ERROR;   /* в инструкции только одна операция */
        }
        sc = apply(op, left, right, &result);
        if (sc != OK) {
            return sc;
        }
    }
    return memory_set(m, text + start, name_length, result);
}

/* ===================== вывод ===================== */

/*
 * Напечатать целое число. Вручную, без printf: в сборке mingw (Windows) Dr. Memory ложно сообщает
 * о чтении неинициализированной памяти внутри printf для больших чисел (см. задание 4).
 */
status_code print_number(FILE *out, const long long value) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    /* модуль в беззнаковом типе: у LLONG_MIN нет положительной пары */
    unsigned long long magnitude = value < 0 ? 0ULL - (unsigned long long)value : (unsigned long long)value;
    char digits[24];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude > 0);
    if (value < 0) {
        fputc('-', out);
    }
    while (n > 0) {
        fputc(digits[--n], out);
    }
    return OK;
}

/* print; - все переменные "имя = значение" (массив уже упорядочен по имени) */
status_code print_all(FILE *out, const memory *m) {
    if (out == NULL || m == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < m->count; i++) {
        fputs(m->cells[i].name, out);
        fputs(" = ", out);
        print_number(out, m->cells[i].value);
        fputc('\n', out);
    }
    return OK;
}

status_code error_text(const status_code sc, const char **text) {
    if (text == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case SYNTAX_ERROR:     *text = "wrong instruction"; break;
        case NOT_DECLARED:     *text = "the variable is not declared:"; break;
        case DIVISION_BY_ZERO: *text = "division by zero"; break;
        case OVERFLOW_ERROR:   *text = "the number does not fit in long long"; break;
        default:               *text = "internal error"; break;   /* другие статусы сюда не попадают */
    }
    return OK;
}

/* Прочитать весь файл в память */
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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: lab4_task7 <instructions file>\n");
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

    memory m = {NULL, 0, 0};
    size_t pos = 0;
    size_t number = 0;
    int stopped = 0;
    while (!stopped) {
        while (pos < length && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
            pos++;
        }
        if (pos == length) {
            break;
        }
        number++;
        size_t end = pos;
        while (end < length && text[end] != ';') {
            end++;
        }
        print_kind print = PRINT_NONE;
        long long value = 0;
        size_t bad_start = 0;
        size_t bad_length = 0;
        if (end == length) {
            sc = SYNTAX_ERROR;   /* нет ';' в конце */
        } else {
            sc = execute(&m, text + pos, end - pos, &print, &value, &bad_start, &bad_length);
        }
        if (sc == OK) {
            if (print == PRINT_ONE) {
                print_number(stdout, value);
                fputc('\n', stdout);
            } else if (print == PRINT_ALL) {
                print_all(stdout, &m);
            }
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
            stopped = 1;
        } else {
            /* по условию - сообщение в стандартный вывод и остановка интерпретатора */
            const char *reason = NULL;
            error_text(sc, &reason);
            fputs("Error in instruction ", stdout);
            print_number(stdout, (long long)number);
            fputs(": ", stdout);
            fputs(reason, stdout);
            if (sc == NOT_DECLARED) {
                fputc(' ', stdout);
                fwrite(text + pos + bad_start, 1, bad_length, stdout);   /* имя - кусок текста без нуля в конце */
            }
            fputc('\n', stdout);
            stopped = 1;
        }
        pos = end + 1;
    }
    memory_free(&m);
    free(text);
    return (sc == OK) ? 0 : 1;
}
