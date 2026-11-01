/*
 * Лабораторная 4, задание 4. Интерпретатор операторов над булевыми векторами.
 *
 * Запуск:  lab4_task4 <файл с операторами> [/trace <файл трассировки>]   (флаг - до или после файла)
 * Переменные A..Z (регистр не важен) - булевы векторы из 64 бит (unsigned long long). Вначале у переменных
 * нет значения: использовать переменную до присваивания - ошибка.
 * Операторы (разделитель ';', пробелы и переносы строк - где угодно, регистр не важен):
 *     A := B <op> C;   поразрядная операция:  +  дизъюнкция (или)        &  конъюнкция (и)
 *                                             -> импликация (B -> C)     <- обратная импликация (C -> B)
 *                                             ~  эквиваленция            <> сложение по модулю 2
 *                                             +> коимпликация (не (B -> C) = B и не C)
 *                                             ?  штрих Шеффера (не и)     !  стрелка Пирса (не или)
 *     X := \W;         поразрядное отрицание
 *     read(D, base);   прочитать со стандартного ввода число в системе счисления base (2..36) в D
 *     write(Q, base);  напечатать Q в системе base
 * Комментарии: % - до конца строки; { ... } - на несколько строк. В условии сказано и "вложенность не
 * допускается", и "вложенные комментарии допускаются" - поддерживаем вложенность (это общий случай).
 *
 * Трассировка: для каждого оператора - его текст, значения переменных до и после, введённые и выведенные
 * значения, ошибки (синтаксические, смысловые - нет значения у переменной, и времени выполнения - неверный
 * ввод). С флагом /trace - в файл трассировки, без флага - в стандартный вывод. Ошибка в операторе не
 * останавливает работу: выполняются следующие операторы; код возврата 1, если ошибки были.
 */
#define _POSIX_C_SOURCE 200809L   /* stat (Linux) - до всех #include, см. same_file */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* strcmp, memcpy */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define VARIABLES 26
#define MAX_TOKEN 128   /* самое длинное слово ввода для read */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,        /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,        /* оператор записан неправильно */
    UNDEFINED_VARIABLE,  /* у переменной нет значения */
    BASE_ERROR,          /* основание не из [2..36] */
    INPUT_ERROR,         /* read: не число в этой системе или не помещается в 64 бита */
    INPUT_END,           /* read: ввод кончился */
    COMMENT_ERROR,       /* '}' без '{' или '{' без '}' */
    SAME_FILE,           /* файл трассировки - это файл с операторами */
    FILE_ERROR,          /* файл не открылся / ошибка чтения */
    ALLOC_ERROR          /* malloc / realloc не смогли выделить память */
} status_code;

/* Поразрядные операции */
typedef enum {
    OP_OR, OP_AND, OP_IMPLICATION, OP_REVERSE_IMPLICATION, OP_EQUIVALENCE, OP_XOR,
    OP_COIMPLICATION, OP_SHEFFER, OP_PIERCE, OP_NOT
} operation;

/* Разобранный оператор */
typedef enum { ASSIGN_BINARY, ASSIGN_NOT, READ, WRITE } statement_kind;

typedef struct {
    statement_kind kind;
    int target;        /* номер переменной слева (или в read/write) */
    int left;          /* операнды (номера переменных) */
    int right;
    operation op;
    int base;          /* read / write */
} statement;

/* Переменные: значения и есть ли значение */
typedef struct {
    unsigned long long value[VARIABLES];
    int defined[VARIABLES];
} memory;

/* ===================== один и тот же файл ===================== */

#ifdef _WIN32
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

/* ===================== операции и системы счисления ===================== */

/* Результат поразрядной операции (для OP_NOT второй операнд не используется) */
status_code apply(const operation op, const unsigned long long b, const unsigned long long c,
                  unsigned long long *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    switch (op) {
        case OP_OR:                  *result = b | c; break;
        case OP_AND:                 *result = b & c; break;
        case OP_IMPLICATION:         *result = ~b | c; break;          /* b -> c  =  не b или c */
        case OP_REVERSE_IMPLICATION: *result = b | ~c; break;          /* c -> b */
        case OP_EQUIVALENCE:         *result = ~(b ^ c); break;        /* 1, где биты равны */
        case OP_XOR:                 *result = b ^ c; break;
        case OP_COIMPLICATION:       *result = b & ~c; break;          /* не (b -> c) */
        case OP_SHEFFER:             *result = ~(b & c); break;
        case OP_PIERCE:              *result = ~(b | c); break;
        case OP_NOT:                 *result = ~b; break;
        default:                     return INVALID_ARGS;
    }
    return OK;
}

/* Цифра символа (0-9, A-Z или a-z = 10..35); не цифра - -1 */
status_code digit_value(const char c, int *digit) {
    if (digit == NULL) {
        return INVALID_ARGS;
    }
    if (c >= '0' && c <= '9') {
        *digit = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        *digit = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'z') {
        *digit = c - 'a' + 10;
    } else {
        *digit = -1;
    }
    return OK;
}

/* Строка цифр в системе base -> 64-битное число; переполнение - INPUT_ERROR */
status_code parse_in_base(const char *s, const int base, unsigned long long *value) {
    if (s == NULL || value == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    if (s[0] == '\0') {
        return INPUT_ERROR;
    }
    unsigned long long result = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        int digit = 0;
        digit_value(s[i], &digit);
        if (digit < 0 || digit >= base) {
            return INPUT_ERROR;
        }
        /* result * base + digit <= 2^64 - 1 проверяем до умножения */
        if (result > (~0ULL - (unsigned long long)digit) / (unsigned long long)base) {
            return INPUT_ERROR;
        }
        result = result * (unsigned long long)base + (unsigned long long)digit;
    }
    *value = result;
    return OK;
}

/* Число -> строка в системе base (цифры больше 9 - заглавные буквы); out - не меньше 65 символов */
status_code to_base(unsigned long long value, const int base, char *out, const size_t size) {
    if (out == NULL || base < 2 || base > 36 || size < 65) {
        return INVALID_ARGS;
    }
    char reversed[65];
    size_t n = 0;
    do {
        int digit = (int)(value % (unsigned long long)base);
        reversed[n++] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
        value /= (unsigned long long)base;
    } while (value > 0);
    for (size_t i = 0; i < n; i++) {
        out[i] = reversed[n - 1 - i];
    }
    out[n] = '\0';
    return OK;
}

/* ===================== текст программы ===================== */

/*
 * Убрать комментарии: % - до конца строки; { ... } - любой вложенности. Переносы строк остаются
 * (для номеров строк). *line - строка ошибки.
 */
status_code strip_comments(const char *text, const size_t length, char **clean, size_t *clean_length,
                           size_t *line) {
    if (text == NULL || clean == NULL || clean_length == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    char *out = (char *)malloc(length + 1);
    if (out == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    size_t depth = 0;
    size_t open_line = 0;
    *line = 1;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\n') {
            (*line)++;
            out[n++] = c;
        } else if (depth == 0 && c == '%') {
            while (i + 1 < length && text[i + 1] != '\n') {
                i++;
            }
        } else if (c == '{') {
            if (depth == 0) {
                open_line = *line;
            }
            depth++;
        } else if (c == '}') {
            if (depth == 0) {
                free(out);
                return COMMENT_ERROR;
            }
            depth--;
        } else if (depth == 0) {
            out[n++] = c;
        }
    }
    if (depth > 0) {
        free(out);
        *line = open_line;
        return COMMENT_ERROR;
    }
    out[n] = '\0';
    *clean = out;
    *clean_length = n;
    return OK;
}

/* Курсор по тексту одного оператора */
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
                                       c->text[c->position] == '\n' || c->text[c->position] == '\r' ||
                                       c->text[c->position] == '\v' || c->text[c->position] == '\f')) {
        c->position++;
    }
    return OK;
}

/* Слово из латинских букв в нижнем регистре (до size - 1 символов; длиннее - обрезается) */
status_code read_word(cursor *c, char *out, const size_t size, size_t *length) {
    if (c == NULL || out == NULL || length == NULL || size == 0) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    *length = 0;
    while (c->position < c->length) {
        char ch = c->text[c->position];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        } else if (ch < 'a' || ch > 'z') {
            break;
        }
        if (*length + 1 < size) {
            out[*length] = ch;
        }
        (*length)++;
        c->position++;
    }
    out[*length + 1 < size ? *length : size - 1] = '\0';
    return OK;
}

/* Имя переменной: одна буква, за ней не буква и не цифра */
status_code read_variable(cursor *c, int *index) {
    if (c == NULL || index == NULL) {
        return INVALID_ARGS;
    }
    char word[4];
    size_t length = 0;
    size_t start = c->position;
    read_word(c, word, sizeof(word), &length);
    if (length != 1 || (c->position < c->length && c->text[c->position] >= '0' && c->text[c->position] <= '9')) {
        c->position = start;
        return SYNTAX_ERROR;
    }
    *index = word[0] - 'a';
    return OK;
}

status_code expect(cursor *c, const char *token) {
    if (c == NULL || token == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    size_t n = strlen(token);
    if (c->length - c->position < n || memcmp(c->text + c->position, token, n) != 0) {
        return SYNTAX_ERROR;
    }
    c->position += n;
    return OK;
}

/* Поразрядная операция: сначала двухсимвольные (->, <-, <>, +>), потом односимвольные */
status_code read_operation(cursor *c, operation *op) {
    if (c == NULL || op == NULL) {
        return INVALID_ARGS;
    }
    static const char *const names[] = {"->", "<-", "<>", "+>", "+", "&", "~", "?", "!"};
    static const operation ops[] = {OP_IMPLICATION, OP_REVERSE_IMPLICATION, OP_XOR, OP_COIMPLICATION,
                                    OP_OR, OP_AND, OP_EQUIVALENCE, OP_SHEFFER, OP_PIERCE};
    skip_spaces(c);
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        size_t n = strlen(names[i]);
        if (c->length - c->position >= n && memcmp(c->text + c->position, names[i], n) == 0) {
            c->position += n;
            *op = ops[i];
            return OK;
        }
    }
    return SYNTAX_ERROR;
}

/* Основание: десятичное число (проверка диапазона 2..36 - отдельно, это смысловая ошибка) */
status_code read_base(cursor *c, int *base) {
    if (c == NULL || base == NULL) {
        return INVALID_ARGS;
    }
    skip_spaces(c);
    size_t start = c->position;
    long long value = 0;
    while (c->position < c->length && c->text[c->position] >= '0' && c->text[c->position] <= '9') {
        if (value < 1000) {
            value = value * 10 + (c->text[c->position] - '0');   /* дальше 1000 копить незачем: и так > 36 */
        }
        c->position++;
    }
    if (c->position == start) {
        return SYNTAX_ERROR;
    }
    *base = (int)value;
    return OK;
}

/*
 * Разобрать один оператор (текст между ';'). Возвращает SYNTAX_ERROR с пояснением *message
 * или BASE_ERROR (основание вне 2..36).
 */
status_code parse_statement(const char *text, const size_t length, statement *st, const char **message) {
    if (text == NULL || st == NULL || message == NULL) {
        return INVALID_ARGS;
    }
    cursor c = {text, 0, length};
    *message = NULL;
    char word[8];
    size_t word_length = 0;
    size_t start = c.position;
    read_word(&c, word, sizeof(word), &word_length);
    skip_spaces(&c);
    int call = (c.position < c.length && c.text[c.position] == '(');
    if (call && (strcmp(word, "read") == 0 || strcmp(word, "write") == 0) && word_length <= 5) {
        /* read(D, base) / write(Q, base) */
        st->kind = word[0] == 'r' ? READ : WRITE;
        status_code sc = expect(&c, "(");
        if (sc == OK) sc = read_variable(&c, &st->target);
        if (sc == OK) sc = expect(&c, ",");
        if (sc == OK) sc = read_base(&c, &st->base);
        if (sc == OK) sc = expect(&c, ")");
        skip_spaces(&c);
        if (sc == OK && c.position != c.length) sc = SYNTAX_ERROR;
        if (sc != OK) {
            *message = st->kind == READ ? "expected: read(D, base);" : "expected: write(Q, base);";
            return sc;
        }
        return (st->base < 2 || st->base > 36) ? BASE_ERROR : OK;
    }
    /* присваивание: X := B op C  или  X := \W */
    c.position = start;
    status_code sc = read_variable(&c, &st->target);
    if (sc == OK) sc = expect(&c, ":=");
    if (sc != OK) {
        *message = "expected: A := B op C;  A := \\B;  read(D, base);  or  write(Q, base);";
        return SYNTAX_ERROR;
    }
    skip_spaces(&c);
    if (c.position < c.length && c.text[c.position] == '\\') {
        c.position++;
        st->kind = ASSIGN_NOT;
        st->op = OP_NOT;
        sc = read_variable(&c, &st->left);
        st->right = st->left;
    } else {
        st->kind = ASSIGN_BINARY;
        sc = read_variable(&c, &st->left);
        if (sc == OK) {
            sc = read_operation(&c, &st->op);
            if (sc != OK) {
                *message = "unknown operation (+ & -> <- ~ <> +> ? !)";
                return SYNTAX_ERROR;
            }
        }
        if (sc == OK) sc = read_variable(&c, &st->right);
    }
    skip_spaces(&c);
    if (sc == OK && c.position != c.length) sc = SYNTAX_ERROR;
    if (sc != OK) {
        *message = "expected: A := B op C;  or  A := \\B;";
    }
    return sc;
}

/* ===================== выполнение и трассировка ===================== */

/* Значение переменной для трассировки: число или "undefined" */
status_code variable_text(const memory *mem, const int index, char *out, const size_t size) {
    if (mem == NULL || out == NULL || index < 0 || index >= VARIABLES || size < 4 + 65) {   /* "A = " и до 64 цифр с нулём */
        return INVALID_ARGS;
    }
    /* "A = " и число (или undefined). Строка собирается strcpy/strcat и своей to_base, без snprintf:
       snprintf в сборке mingw даёт ложные сообщения Dr. Memory о чтении неинициализированной памяти
       внутри самой snprintf (на её собственных переменных) */
    out[0] = (char)('A' + index);
    out[1] = '\0';
    strcat(out, " = ");
    if (mem->defined[index]) {
        to_base(mem->value[index], 10, out + 4, size - 4);
    } else {
        strcat(out, "undefined");
    }
    return OK;
}

/* Строка "before:" - переменные оператора без повторов: цель, затем операнды */
status_code print_variables(FILE *trace, const char *title, const memory *mem, const int *indexes,
                            const size_t count) {
    if (trace == NULL || title == NULL || mem == NULL || indexes == NULL) {
        return INVALID_ARGS;
    }
    fprintf(trace, "    %s", title);
    for (size_t i = 0; i < count; i++) {
        int repeated = 0;
        for (size_t j = 0; j < i; j++) {
            if (indexes[j] == indexes[i]) {
                repeated = 1;
            }
        }
        if (repeated) {
            continue;
        }
        char text[80];
        variable_text(mem, indexes[i], text, sizeof(text));
        fprintf(trace, "%s%s", i == 0 ? "" : ", ", text);
    }
    fprintf(trace, "\n");
    return OK;
}

/* Прочитать следующее слово стандартного ввода (до пробела); *end = 1 - ввод кончился */
status_code read_input_word(FILE *input, char *word, const size_t size, int *end, int *too_long) {
    if (input == NULL || word == NULL || end == NULL || too_long == NULL || size == 0) {
        return INVALID_ARGS;
    }
    int ch = fgetc(input);
    while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ch = fgetc(input);
    }
    *end = (ch == EOF);
    *too_long = 0;
    size_t n = 0;
    while (ch != EOF && ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
        if (n + 1 < size) {
            word[n++] = (char)ch;
        } else {
            *too_long = 1;
        }
        ch = fgetc(input);
    }
    word[n] = '\0';
    return OK;
}

/*
 * Выполнить разобранный оператор. Всё, что видно пользователю (трассировка, write), печатается здесь
 * в переданные потоки - вычисления (apply, parse_in_base, to_base) отделены от вывода.
 */
status_code execute(memory *mem, const statement *st, FILE *input, FILE *output, FILE *trace,
                    int *undefined) {
    if (mem == NULL || st == NULL || input == NULL || output == NULL || trace == NULL || undefined == NULL) {
        return INVALID_ARGS;
    }
    int involved[3] = {st->target, st->left, st->right};
    size_t involved_count = st->kind == ASSIGN_BINARY ? 3 : (st->kind == ASSIGN_NOT ? 2 : 1);
    print_variables(trace, "before: ", mem, involved, involved_count);

    if (st->kind == WRITE) {
        if (!mem->defined[st->target]) {
            *undefined = st->target;
            return UNDEFINED_VARIABLE;
        }
        char digits[65];
        to_base(mem->value[st->target], st->base, digits, sizeof(digits));
        fprintf(output, "%s\n", digits);
        fprintf(trace, "    output: %s\n", digits);
        return OK;
    }
    if (st->kind == READ) {
        char word[MAX_TOKEN + 1];
        int end = 0;
        int too_long = 0;
        read_input_word(input, word, sizeof(word), &end, &too_long);
        if (end) {
            return INPUT_END;
        }
        fprintf(trace, "    input: %s%s\n", word, too_long ? "..." : "");
        unsigned long long value = 0;
        if (too_long || parse_in_base(word, st->base, &value) != OK) {
            return INPUT_ERROR;
        }
        mem->value[st->target] = value;
        mem->defined[st->target] = 1;
    } else {
        /* операнды должны иметь значение (смысловая ошибка) */
        if (!mem->defined[st->left]) {
            *undefined = st->left;
            return UNDEFINED_VARIABLE;
        }
        if (st->kind == ASSIGN_BINARY && !mem->defined[st->right]) {
            *undefined = st->right;
            return UNDEFINED_VARIABLE;
        }
        unsigned long long result = 0;
        apply(st->op, mem->value[st->left], mem->value[st->right], &result);
        mem->value[st->target] = result;
        mem->defined[st->target] = 1;
    }
    print_variables(trace, "after:  ", mem, involved, 1);
    return OK;
}

/* Текст оператора для трассировки: пробельные символы подряд -> один пробел, по краям - без пробелов */
status_code compact_text(const char *text, const size_t length, char **result) {
    if (text == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    char *out = (char *)malloc(length + 1);
    if (out == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    int space = 0;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            space = 1;
            continue;
        }
        if (space && n > 0) {
            out[n++] = ' ';
        }
        space = 0;
        out[n++] = c;
    }
    out[n] = '\0';
    *result = out;
    return OK;
}

status_code status_text(const status_code sc, const char **text) {
    if (text == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case SYNTAX_ERROR:       *text = "syntax error"; break;
        case UNDEFINED_VARIABLE: *text = "semantic error: a variable has no value"; break;   /* имя - в run_program */
        case BASE_ERROR:         *text = "semantic error: the base must be from 2 to 36"; break;
        case INPUT_ERROR:        *text = "runtime error: the input is not a number in this base or is too big"; break;
        case INPUT_END:          *text = "runtime error: the input has ended"; break;
        default:                 *text = "internal error"; break;   /* другие статусы сюда не попадают */
    }
    return OK;
}

/* Выполнить все операторы текста (без комментариев). *errors - сколько операторов с ошибкой */
status_code run_program(memory *mem, const char *text, const size_t length, FILE *input, FILE *output, FILE *trace,
                        const int errors_to_stderr, size_t *errors) {
    if (mem == NULL || text == NULL || input == NULL || output == NULL || trace == NULL || errors == NULL) {
        return INVALID_ARGS;
    }
    *errors = 0;
    size_t pos = 0;
    size_t line = 1;
    size_t number = 0;
    while (1) {
        while (pos < length && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
            if (text[pos] == '\n') {
                line++;
            }
            pos++;
        }
        if (pos == length) {
            break;
        }
        number++;
        size_t start_line = line;
        size_t end = pos;
        while (end < length && text[end] != ';') {
            if (text[end] == '\n') {
                line++;
            }
            end++;
        }
        char *shown = NULL;
        status_code sc = compact_text(text + pos, end - pos, &shown);
        if (sc != OK) {
            return sc;
        }
        fprintf(trace, "%zu (line %zu): %s\n", number, start_line, shown);
        free(shown);

        statement st;
        const char *message = NULL;
        int undefined = 0;   /* номер переменной без значения (при UNDEFINED_VARIABLE) */
        if (end == length) {
            sc = SYNTAX_ERROR;
            message = "';' expected at the end";
        } else {
            sc = parse_statement(text + pos, end - pos, &st, &message);
        }
        if (sc == OK) {
            sc = execute(mem, &st, input, output, trace, &undefined);
        }
        if (sc != OK) {
            const char *reason = NULL;
            status_text(sc, &reason);
            /* у переменной без значения в сообщении - её имя */
            char text[160];
            if (sc == UNDEFINED_VARIABLE) {
                /* strcpy/strcat вместо snprintf с %s - см. variable_text */
                strcpy(text, "semantic error: variable ");
                size_t n = strlen(text);
                text[n] = (char)('A' + undefined);
                text[n + 1] = '\0';
                strcat(text, " has no value");
            } else {
                strcpy(text, reason);   /* все тексты status_text короче 160 символов */
            }
            fprintf(trace, "    error: %s%s%s\n", text, message != NULL ? ": " : "", message != NULL ? message : "");
            if (errors_to_stderr) {
                fprintf(stderr, "Error in statement %zu (line %zu): %s%s%s\n", number, start_line, text,
                        message != NULL ? ": " : "", message != NULL ? message : "");
            }
            (*errors)++;
        }
        fflush(output);
        pos = end < length ? end + 1 : end;
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
    /* аргументы: файл и необязательно "/trace <файл>" (до или после) */
    const char *program_path = NULL;
    const char *trace_path = NULL;
    int wrong = 0;
    for (int i = 1; i < argc && !wrong; i++) {
        if (strcmp(argv[i], "/trace") == 0) {
            if (trace_path != NULL || i + 1 >= argc) {
                wrong = 1;
            } else {
                trace_path = argv[++i];
            }
        } else if (program_path == NULL) {
            program_path = argv[i];
        } else {
            wrong = 1;
        }
    }
    if (wrong || program_path == NULL) {
        fprintf(stderr, "Usage: lab4_task4 <statements file> [/trace <trace file>]\n");
        return 1;
    }

    char *text = NULL;
    size_t length = 0;
    status_code sc = read_whole_file(program_path, &text, &length);
    if (sc != OK) {
        fprintf(stderr, sc == FILE_ERROR ? "Error: can not read file '%s'\n" : "Error: not enough memory%s\n",
                sc == FILE_ERROR ? program_path : "");
        return 1;
    }
    char *clean = NULL;
    size_t clean_length = 0;
    size_t line = 0;
    sc = strip_comments(text, length, &clean, &clean_length, &line);
    free(text);
    if (sc != OK) {
        if (sc == COMMENT_ERROR) {
            fprintf(stderr, "Error: wrong brackets of a comment in line %zu\n", line);
        } else {
            fprintf(stderr, "Error: not enough memory\n");
        }
        return 1;
    }

    FILE *trace = stdout;
    if (trace_path != NULL) {
        int equal = 0;
        same_file(trace_path, program_path, &equal);
        if (equal) {
            fprintf(stderr, "Error: the trace file must not be the statements file\n");
            free(clean);
            return 1;
        }
        trace = fopen(trace_path, "w");
        if (trace == NULL) {
            fprintf(stderr, "Error: can not open trace file '%s'\n", trace_path);
            free(clean);
            return 1;
        }
    }

    memory mem;
    for (int i = 0; i < VARIABLES; i++) {
        mem.value[i] = 0;
        mem.defined[i] = 0;
    }
    size_t errors = 0;
    sc = run_program(&mem, clean, clean_length, stdin, stdout, trace, trace_path != NULL, &errors);
    free(clean);
    if (trace != stdout && fclose(trace) != 0) {
        fprintf(stderr, "Error: can not write trace file '%s'\n", trace_path);
        return 1;
    }
    if (sc != OK) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    return errors > 0 ? 1 : 0;
}
