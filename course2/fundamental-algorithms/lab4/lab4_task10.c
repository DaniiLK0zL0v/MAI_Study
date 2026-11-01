/*
 * Лабораторная 4, задание 10. Интерпретатор с настраиваемым синтаксисом и отладчиком.
 *
 * Запуск:  lab4_task10 [<файл настроек>] <файл программы> [--base_assign N] [--base_input N] [--base_output N]
 *                      [--debug | -d | /debug]
 *   основания - от 2 до 36, по умолчанию 10: base_assign - константы в программе, base_input - ввод (input),
 *   base_output - вывод (output). Без файла настроек берётся последний файл настроек, с которым работала
 *   программа (его полный путь запоминается в файле lab4_task10_settings.txt рядом с программой).
 *   --debug: однострочный комментарий "# BREAKPOINT" - точка останова, дальше диалог отладчика.
 *
 * Переменные - 32-битные беззнаковые (uint32_t), хранятся в боре. Имя - латинские буквы, цифры и '_', не с цифры;
 * прописные и строчные буквы в именах различаются. Команды: not, input, output (унарные); add, mult, sub, pow,
 * div, rem, xor, and, or (бинарные); = (присваивание). Названия команд и цифры констант - без учёта регистра.
 * Константа начинается с цифры (при base_assign 16 "FF" - имя переменной, константа - "0FF").
 *
 * Файл настроек: строки "<команда> <синоним>" (действует последний синоним команды) и правила записи:
 *   left= / right=   - куда записывается результат: var = expr  или  expr = var;
 *   op()  - команда перед скобками с аргументами: add(a, b), not(a);   (для унарных и бинарных)
 *   ()op  - аргументы в скобках перед командой:  (a, b)add, (a)not;   (для унарных и бинарных)
 *   (op)  - бинарная команда между аргументами:  a add b              (только для бинарных).
 * По умолчанию: left= и op(). Комментарии - # до конца строки и [ ... ] (вложенные) - и в настройках, и в программе.
 *
 * Инструкции разделяются ';'. Инструкция - присваивание (переменной - значение выражения), input (прочитать
 * переменную) или output (напечатать значение). Каждая инструкция сначала разбирается в дерево, потом
 * выполняется; ошибка (синтаксическая или времени выполнения) - сообщение с номером строки и остановка.
 */
#define _POSIX_C_SOURCE 200809L   /* getcwd, isatty, fileno на Linux - до всех #include */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, _fullpath (Windows) */
#include <string.h>   /* strlen, strcmp, strcpy, memcpy, strchr */
#include <stdint.h>   /* uint32_t */
#ifdef _WIN32
#include <io.h>       /* _isatty, _fileno */
#else
#include <unistd.h>   /* getcwd, isatty, fileno */
#endif

#define MAX_NESTING 500                     /* наибольшая вложенность выражения (глубина рекурсии разбора) */
#define MAX_NODES 2000                      /* наибольшее число узлов дерева одной инструкции (глубина рекурсии
                                               вычисления и освобождения не больше числа узлов) */
#define STATE_FILE "lab4_task10_settings.txt"
#define MAX_SHOWN 40                        /* сколько символов слова показывать в сообщении об ошибке */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,      /* ошибка записи (в программе, настройках, ответе пользователя) */
    RUNTIME_ERROR,     /* ошибка выполнения: необъявленная переменная, деление на 0, неправильный ввод */
    INPUT_END,         /* стандартный ввод кончился */
    STOPPED,           /* в отладчике выбрано "завершить работу интерпретатора" */
    FILE_ERROR,        /* файл не открылся / ошибка чтения или записи */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Команды интерпретатора (порядок - как в ORIGINAL_NAMES) */
typedef enum {
    CMD_NOT, CMD_INPUT, CMD_OUTPUT,
    CMD_ADD, CMD_MULT, CMD_SUB, CMD_POW, CMD_DIV, CMD_REM, CMD_XOR, CMD_AND, CMD_OR,
    CMD_ASSIGN,
    COMMAND_COUNT
} command;

const char *ORIGINAL_NAMES[COMMAND_COUNT] = {"not", "input", "output", "add", "mult", "sub", "pow", "div", "rem",
                                             "xor", "and", "or", "="};

/* Где стоят аргументы: op() - после команды в скобках, (op) - вокруг команды, ()op - перед командой в скобках */
typedef enum { PREFIX, INFIX, POSTFIX } syntax_kind;

/* Настройки синтаксиса */
typedef struct {
    char *names[COMMAND_COUNT];   /* как сейчас пишется каждая команда (копии строк) */
    int result_left;              /* 1 - left= (var = expr), 0 - right= (expr = var) */
    syntax_kind unary;            /* PREFIX или POSTFIX */
    syntax_kind binary;           /* PREFIX, INFIX или POSTFIX */
} settings;

/* ===================== символы и строки ===================== */

/* Буква латинского алфавита */
status_code is_letter(const char c, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    return OK;
}

/* Символ слова: буква, цифра или '_' */
status_code is_word_char(const char c, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    is_letter(c, result);
    *result = *result || (c >= '0' && c <= '9') || c == '_';
    return OK;
}

/* Символ "значка" (из таких состоят синонимы вроде <> или ->): видимый ASCII, не символ слова и не ( ) , ; # [ ] */
status_code is_symbol_char(const char c, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    int word = 0;
    is_word_char(c, &word);
    *result = c > ' ' && c < 127 && !word && strchr("(),;#[]", c) == NULL;
    return OK;
}

/* Пробельный символ: пробел, табуляция, перевод строки, возврат каретки */
status_code is_space(const char c, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    return OK;
}

/* Буква в нижнем регистре (остальные символы не меняются) */
status_code lower(const char c, char *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    return OK;
}

/* Равны ли строка s (length символов) и строка t без учёта регистра букв */
status_code same_ignore_case(const char *s, const size_t length, const char *t, int *equal) {
    if (s == NULL || t == NULL || equal == NULL) {
        return INVALID_ARGS;
    }
    *equal = 0;
    if (strlen(t) != length) {
        return OK;
    }
    for (size_t i = 0; i < length; i++) {
        char a = 0;
        char b = 0;
        lower(s[i], &a);
        lower(t[i], &b);
        if (a != b) {
            return OK;
        }
    }
    *equal = 1;
    return OK;
}

/* Подходит ли строка (length символов) как имя переменной: буквы, цифры, '_', первая - не цифра */
status_code valid_name(const char *s, const size_t length, int *result) {
    if (s == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    *result = length > 0 && !(s[0] >= '0' && s[0] <= '9');
    for (size_t i = 0; i < length && *result; i++) {
        is_word_char(s[i], result);
    }
    return OK;
}

/* Копия length символов строки s (с '\0' в конце) */
status_code copy_part(const char *s, const size_t length, char **result) {
    if (s == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(copy, s, length);
    copy[length] = '\0';
    *result = copy;
    return OK;
}

/* Прочитать весь файл в строку (с '\0' в конце) */
status_code read_file(const char *path, char **text) {
    if (path == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return FILE_ERROR;
    }
    size_t capacity = 1024;
    size_t size = 0;
    char *buf = (char *)malloc(capacity);
    if (buf == NULL) {
        fclose(file);
        return ALLOC_ERROR;
    }
    int ch = 0;
    while ((ch = fgetc(file)) != EOF) {
        /* место для символа и для '\0': буфер растёт вдвое */
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
        buf[size++] = (char)ch;
    }
    int failed = ferror(file);
    fclose(file);
    if (failed) {
        free(buf);
        return FILE_ERROR;
    }
    buf[size] = '\0';
    *text = buf;
    return OK;
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

/* ===================== числа в системах счисления ===================== */

/* Значение цифры: '0'..'9' -> 0..9, буквы (любого регистра) -> 10..35; *digit = 36 - не цифра */
status_code digit_value(const char c, int *digit) {
    if (digit == NULL) {
        return INVALID_ARGS;
    }
    char l = 0;
    lower(c, &l);
    if (c >= '0' && c <= '9') {
        *digit = c - '0';
    } else if (l >= 'a' && l <= 'z') {
        *digit = l - 'a' + 10;
    } else {
        *digit = 36;
    }
    return OK;
}

/*
 * Строка (length символов) -> число в системе счисления base. SYNTAX_ERROR - пусто или не цифра этой системы;
 * RUNTIME_ERROR - не помещается в 32 бита. Переполнение проверяется до умножения: value > (MAX - d) / base.
 */
status_code parse_in_base(const char *s, const size_t length, const int base, uint32_t *value) {
    if (s == NULL || value == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    if (length == 0) {
        return SYNTAX_ERROR;
    }
    uint32_t result = 0;
    int too_big = 0;
    for (size_t i = 0; i < length; i++) {
        int d = 0;
        digit_value(s[i], &d);
        if (d >= base) {
            return SYNTAX_ERROR;
        }
        if (result > (UINT32_MAX - (uint32_t)d) / (uint32_t)base) {
            too_big = 1;   /* дальше цифры всё равно проверяем: сначала - ошибка записи */
        } else {
            result = result * (uint32_t)base + (uint32_t)d;
        }
    }
    if (too_big) {
        return RUNTIME_ERROR;
    }
    *value = result;
    return OK;
}

/* Число -> текст в системе счисления base (цифры больше 9 - заглавные буквы); out - не меньше 33 символов */
status_code number_in_base(uint32_t value, const int base, char *out) {
    if (out == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char reversed[33];
    size_t n = 0;
    do {
        reversed[n++] = digits[value % (uint32_t)base];
        value /= (uint32_t)base;
    } while (value > 0);
    for (size_t i = 0; i < n; i++) {
        out[i] = reversed[n - 1 - i];
    }
    out[n] = '\0';
    return OK;
}

/* Основание из аргумента командной строки: число от 2 до 36 */
status_code parse_base(const char *s, int *base) {
    if (s == NULL || base == NULL) {
        return INVALID_ARGS;
    }
    uint32_t value = 0;
    size_t length = strlen(s);
    for (size_t i = 0; i < length; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return SYNTAX_ERROR;
        }
    }
    if (length > 2 || parse_in_base(s, length, 10, &value) != OK || value < 2 || value > 36) {
        return SYNTAX_ERROR;
    }
    *base = (int)value;
    return OK;
}

/* ===================== римские цифры и цекендорфово представление (для отладчика) ===================== */

/*
 * Римское число -> значение (1..3999). Разбор жадный: на каждом шаге - самая большая из 13 записей
 * (M, CM, D, CD, C, XC, L, XL, X, IX, V, IV, I), с которой начинается остаток строки; затем значение
 * переводится обратно в римскую запись и сравнивается со строкой - так отбрасываются неправильные
 * записи вроде IIII, IC или VX (у каждого числа одна правильная запись).
 */
status_code parse_roman(const char *s, uint32_t *value) {
    if (s == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    const char *symbols[13] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    const uint32_t values[13] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    size_t length = strlen(s);
    if (length == 0 || length > 15) {
        return SYNTAX_ERROR;   /* самая длинная правильная запись - MMMDCCCLXXXVIII, 15 символов */
    }
    /* разбор: сумма значений найденных записей */
    uint32_t result = 0;
    size_t i = 0;
    while (i < length) {
        size_t k = 0;
        while (k < 13 && strncmp(s + i, symbols[k], strlen(symbols[k])) != 0) {
            k++;
        }
        if (k == 13) {
            return SYNTAX_ERROR;   /* не римская цифра */
        }
        result += values[k];
        i += strlen(symbols[k]);
    }
    if (result > 3999) {
        return SYNTAX_ERROR;
    }
    /* обратный перевод - должен совпасть со строкой */
    char check[16];
    size_t n = 0;
    uint32_t rest = result;
    for (size_t k = 0; k < 13; k++) {
        while (rest >= values[k]) {
            for (size_t j = 0; symbols[k][j] != '\0'; j++) {
                check[n++] = symbols[k][j];
            }
            rest -= values[k];
        }
    }
    check[n] = '\0';
    if (strcmp(check, s) != 0) {
        return SYNTAX_ERROR;
    }
    *value = result;
    return OK;
}

/*
 * Цекендорфово представление -> значение (как в лабораторной 2): символы '0'/'1' - коэффициенты при числах
 * Фибоначчи 1, 2, 3, 5, 8, ... (от младшего), в конце - лишняя '1' (признак конца: "11" бывает только там).
 * Ноль - "01". Две единицы подряд до конца - не цекендорфово. RUNTIME_ERROR - больше 32 бит.
 */
status_code parse_zeckendorf(const char *s, uint32_t *value) {
    if (s == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t length = strlen(s);
    if (length < 2 || s[length - 1] != '1' || s[length - 2] != '1') {
        if (!(length == 2 && s[0] == '0' && s[1] == '1')) {
            return SYNTAX_ERROR;
        }
    }
    unsigned long long result = 0;
    unsigned long long fib = 1;       /* текущее число Фибоначчи */
    unsigned long long next = 2;      /* следующее */
    for (size_t i = 0; i + 1 < length; i++) {
        if (s[i] != '0' && s[i] != '1') {
            return SYNTAX_ERROR;
        }
        /* две единицы подряд - только последние два символа */
        if (s[i] == '1' && i + 2 < length && s[i + 1] == '1') {
            return SYNTAX_ERROR;
        }
        if (s[i] == '1') {
            if (fib > UINT32_MAX) {
                return RUNTIME_ERROR;
            }
            result += fib;
            if (result > UINT32_MAX) {
                return RUNTIME_ERROR;
            }
        }
        /* следующее число Фибоначчи (после 2^33 дальше не считаем: единица там - уже переполнение) */
        if (fib <= UINT32_MAX) {
            unsigned long long tmp = fib + next;
            fib = next;
            next = tmp;
        }
    }
    *value = (uint32_t)result;
    return OK;
}

/* ===================== переменные в боре ===================== */

/* Узел бора: символ, первый ребёнок, следующий брат (братья - по возрастанию символа); значение, если есть */
typedef struct var_node {
    char symbol;
    struct var_node *child;
    struct var_node *sibling;
    int known;          /* здесь кончается имя "известной" переменной */
    uint32_t value;
} var_node;

typedef struct {
    var_node *root;      /* корень - пустой символ */
    size_t nodes;        /* сколько узлов (для стека при обходе) */
    size_t longest;      /* самое длинное имя, которое было в боре (для буфера при обходе) */
} var_trie;

status_code trie_init(var_trie *t) {
    if (t == NULL) {
        return INVALID_ARGS;
    }
    t->root = (var_node *)calloc(1, sizeof(var_node));
    if (t->root == NULL) {
        return ALLOC_ERROR;
    }
    t->nodes = 1;
    t->longest = 0;
    return OK;
}

/* Найти узел имени (length символов); *node = NULL - такого пути нет */
status_code trie_find(const var_trie *t, const char *name, const size_t length, var_node **node) {
    if (t == NULL || name == NULL || node == NULL) {
        return INVALID_ARGS;
    }
    var_node *x = t->root;
    for (size_t i = 0; i < length && x != NULL; i++) {
        var_node *c = x->child;
        while (c != NULL && c->symbol < name[i]) {
            c = c->sibling;
        }
        x = (c != NULL && c->symbol == name[i]) ? c : NULL;
    }
    *node = x;
    return OK;
}

/* Известна ли переменная; если да - её значение */
status_code trie_get(const var_trie *t, const char *name, const size_t length, int *known, uint32_t *value) {
    if (t == NULL || name == NULL || known == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    var_node *x = NULL;
    trie_find(t, name, length, &x);
    *known = x != NULL && x->known;
    if (*known) {
        *value = x->value;
    }
    return OK;
}

/* Присвоить (и объявить, если не была известна): недостающие узлы вставляются на место по порядку символов */
status_code trie_set(var_trie *t, const char *name, const size_t length, const uint32_t value) {
    if (t == NULL || name == NULL || length == 0) {
        return INVALID_ARGS;
    }
    var_node *x = t->root;
    for (size_t i = 0; i < length; i++) {
        var_node **link = &x->child;
        while (*link != NULL && (*link)->symbol < name[i]) {
            link = &(*link)->sibling;
        }
        if (*link == NULL || (*link)->symbol != name[i]) {
            var_node *node = (var_node *)calloc(1, sizeof(var_node));
            if (node == NULL) {
                return ALLOC_ERROR;
            }
            node->symbol = name[i];
            node->sibling = *link;
            *link = node;
            t->nodes++;
        }
        x = *link;
    }
    x->known = 1;
    x->value = value;
    if (length > t->longest) {
        t->longest = length;
    }
    return OK;
}

/*
 * "Отменить объявление": снять отметку и удалить узлы, которые больше не нужны (нет ни значения, ни детей),
 * снизу вверх. Путь от корня запоминается в массиве: в нём length + 1 узел. *removed = 0 - переменной не было.
 */
status_code trie_remove(var_trie *t, const char *name, const size_t length, int *removed) {
    if (t == NULL || name == NULL || removed == NULL) {
        return INVALID_ARGS;
    }
    *removed = 0;
    var_node **path = (var_node **)malloc((length + 1) * sizeof(var_node *));
    if (path == NULL) {
        return ALLOC_ERROR;
    }
    /* спуск с запоминанием пути */
    path[0] = t->root;
    size_t depth = 0;
    while (depth < length) {
        var_node *c = path[depth]->child;
        while (c != NULL && c->symbol < name[depth]) {
            c = c->sibling;
        }
        if (c == NULL || c->symbol != name[depth]) {
            break;
        }
        path[++depth] = c;
    }
    if (depth == length && length > 0 && path[length]->known) {
        *removed = 1;
        path[length]->known = 0;
        /* подъём: лист без значения убирается из списка детей родителя */
        for (size_t k = length; k > 0; k--) {
            var_node *x = path[k];
            if (x->known || x->child != NULL) {
                break;
            }
            var_node **link = &path[k - 1]->child;
            while (*link != x) {
                link = &(*link)->sibling;
            }
            *link = x->sibling;
            free(x);
            t->nodes--;
        }
    }
    free(path);
    return OK;
}

/*
 * Все известные переменные по алфавиту (по кодам символов): "имя = значение (16)". Без рекурсии: в стеке -
 * пары (узел, глубина); снимаем узел, его символ - в имя на место depth; затем в стек брат (той же глубины)
 * и первый ребёнок (глубже) - ребёнок снимется раньше брата, поэтому порядок - как в словаре.
 */
status_code trie_print(const var_trie *t, FILE *out, size_t *count) {
    if (t == NULL || out == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    *count = 0;
    var_node **stack = (var_node **)malloc(t->nodes * sizeof(var_node *));
    size_t *depths = (size_t *)malloc(t->nodes * sizeof(size_t));
    char *name = (char *)malloc(t->longest + 1);
    if (stack == NULL || depths == NULL || name == NULL) {
        free(stack);
        free(depths);
        free(name);
        return ALLOC_ERROR;
    }
    size_t top = 0;
    if (t->root->child != NULL) {
        stack[top] = t->root->child;
        depths[top++] = 0;
    }
    while (top > 0) {
        var_node *x = stack[--top];
        size_t depth = depths[top];
        name[depth] = x->symbol;
        if (x->known) {
            char text[33];
            number_in_base(x->value, 16, text);
            fprintf(out, "%.*s = %s\n", (int)(depth + 1), name, text);
            (*count)++;
        }
        if (x->sibling != NULL) {
            stack[top] = x->sibling;
            depths[top++] = depth;
        }
        if (x->child != NULL) {
            stack[top] = x->child;
            depths[top++] = depth + 1;
        }
    }
    free(stack);
    free(depths);
    free(name);
    return OK;
}

/* Освободить бор без рекурсии: дети и братья узла - в стек */
status_code trie_free(var_trie *t) {
    if (t == NULL || t->root == NULL) {
        return OK;
    }
    var_node **stack = (var_node **)malloc(t->nodes * sizeof(var_node *));
    if (stack == NULL) {
        return ALLOC_ERROR;
    }
    size_t top = 0;
    stack[top++] = t->root;
    while (top > 0) {
        var_node *x = stack[--top];
        if (x->child != NULL) {
            stack[top++] = x->child;
        }
        if (x->sibling != NULL) {
            stack[top++] = x->sibling;
        }
        free(x);
    }
    free(stack);
    t->root = NULL;
    return OK;
}

/* ===================== файл настроек ===================== */

/*
 * Убрать комментарии: '#' - до конца строки, '[' ... ']' - с любой вложенностью. Переводы строк остаются
 * (номера строк не сдвигаются). SYNTAX_ERROR: ']' без '[' или '[' без ']'; *line - где.
 */
status_code strip_comments(const char *text, char **result, size_t *line, const char **reason) {
    if (text == NULL || result == NULL || line == NULL || reason == NULL) {
        return INVALID_ARGS;
    }
    size_t length = strlen(text);
    char *out = (char *)malloc(length + 1);
    if (out == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    size_t depth = 0;          /* глубина вложенности [ ] */
    size_t open_line = 0;      /* где открыт внешний '[' */
    *line = 1;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\n') {
            out[n++] = c;
            (*line)++;
        } else if (c == '[') {
            if (depth == 0) {
                open_line = *line;
            }
            depth++;
        } else if (c == ']') {
            if (depth == 0) {
                free(out);
                *reason = "']' without '['";
                return SYNTAX_ERROR;
            }
            depth--;
        } else if (depth == 0 && c == '#') {
            while (i + 1 < length && text[i + 1] != '\n') {
                i++;
            }
        } else if (depth == 0) {
            out[n++] = c;
        }
    }
    if (depth > 0) {
        free(out);
        *line = open_line;
        *reason = "'[' without ']'";
        return SYNTAX_ERROR;
    }
    out[n] = '\0';
    *result = out;
    return OK;
}

/* Синоним годится: слово (буквы, цифры, '_', не с цифры) или значок (только символы-значки) */
status_code valid_synonym(const char *s, int *result) {
    if (s == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t length = strlen(s);
    valid_name(s, length, result);
    if (*result) {
        return OK;
    }
    *result = length > 0;
    for (size_t i = 0; i < length && *result; i++) {
        is_symbol_char(s[i], result);
    }
    return OK;
}

/* Настройки по умолчанию: оригинальные названия, left=, op() */
status_code settings_init(settings *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    s->result_left = 1;
    s->unary = PREFIX;
    s->binary = PREFIX;
    for (int i = 0; i < COMMAND_COUNT; i++) {
        s->names[i] = NULL;
    }
    for (int i = 0; i < COMMAND_COUNT; i++) {
        status_code sc = copy_part(ORIGINAL_NAMES[i], strlen(ORIGINAL_NAMES[i]), &s->names[i]);
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

status_code settings_free(settings *s) {
    if (s == NULL) {
        return OK;
    }
    for (int i = 0; i < COMMAND_COUNT; i++) {
        free(s->names[i]);
        s->names[i] = NULL;
    }
    return OK;
}

/* Описание ошибки в файле настроек */
typedef struct {
    size_t line;           /* 0 - ошибка не в одной строке (два одинаковых названия) */
    const char *reason;
    int first;             /* для одинаковых названий - какие команды */
    int second;
} settings_error;

/*
 * Одна строка настроек (уже без комментариев), слова разделены пробелами: одно слово - правило записи
 * (left= right= op() ()op (op)), два слова - "команда синоним". Регистр букв не важен.
 */
status_code settings_line(settings *s, char *line, const char **reason) {
    if (s == NULL || line == NULL || reason == NULL) {
        return INVALID_ARGS;
    }
    /* разрезать на слова: words[0], words[1], сколько всего - count */
    char *words[3] = {NULL, NULL, NULL};
    size_t count = 0;
    char *p = line;
    int space = 0;
    while (count < 3) {
        /* пропустить пробелы перед словом */
        is_space(*p, &space);
        while (*p != '\0' && space) {
            p++;
            is_space(*p, &space);
        }
        if (*p == '\0') {
            break;
        }
        /* слово - до пробела или конца строки */
        words[count++] = p;
        while (*p != '\0' && !space) {
            p++;
            is_space(*p, &space);
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    if (count == 0) {
        return OK;
    }
    if (count == 3) {
        *reason = "too many words (need a rule or '<command> <synonym>')";
        return SYNTAX_ERROR;
    }
    if (count == 1) {
        /* правило записи */
        int equal = 0;
        const char *rules[5] = {"left=", "right=", "op()", "()op", "(op)"};
        for (int k = 0; k < 5; k++) {
            same_ignore_case(words[0], strlen(words[0]), rules[k], &equal);
            if (!equal) {
                continue;
            }
            if (k == 0 || k == 1) {
                s->result_left = (k == 0);
            } else if (k == 2) {
                s->unary = PREFIX;
                s->binary = PREFIX;
            } else if (k == 3) {
                s->unary = POSTFIX;
                s->binary = POSTFIX;
            } else {
                s->binary = INFIX;
            }
            return OK;
        }
        *reason = "unknown rule (need left=, right=, op(), ()op or (op))";
        return SYNTAX_ERROR;
    }
    /* синоним: команда - оригинальное название */
    int cmd = -1;
    for (int i = 0; i < COMMAND_COUNT && cmd < 0; i++) {
        int equal = 0;
        same_ignore_case(words[0], strlen(words[0]), ORIGINAL_NAMES[i], &equal);
        if (equal) {
            cmd = i;
        }
    }
    if (cmd < 0) {
        *reason = "unknown command (need not, input, output, add, mult, sub, pow, div, rem, xor, and, or or =)";
        return SYNTAX_ERROR;
    }
    int good = 0;
    valid_synonym(words[1], &good);
    if (!good) {
        *reason = "wrong synonym (need a name of letters, digits and '_' or a sign like <>)";
        return SYNTAX_ERROR;
    }
    char *copy = NULL;
    status_code sc = copy_part(words[1], strlen(words[1]), &copy);
    if (sc != OK) {
        return sc;
    }
    /* действует последний синоним: прежнее название (и оригинальное тоже) больше не годится */
    free(s->names[cmd]);
    s->names[cmd] = copy;
    return OK;
}

/* Прочитать файл настроек; SYNTAX_ERROR - ошибка (*error - где и что), FILE_ERROR - файл не читается */
status_code load_settings(const char *path, settings *s, settings_error *error) {
    if (path == NULL || s == NULL || error == NULL) {
        return INVALID_ARGS;
    }
    char *text = NULL;
    status_code sc = read_file(path, &text);
    if (sc != OK) {
        return sc;
    }
    char *clean = NULL;
    sc = strip_comments(text, &clean, &error->line, &error->reason);
    free(text);
    if (sc != OK) {
        return sc;
    }
    /* строки по очереди: '\n' заменяется на '\0', строка разбирается */
    char *line = clean;
    error->line = 1;
    while (sc == OK && line != NULL) {
        char *end = strchr(line, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        sc = settings_line(s, line, &error->reason);
        if (sc == OK && end != NULL) {
            error->line++;
            line = end + 1;
        } else if (sc == OK) {
            line = NULL;
        }
    }
    free(clean);
    if (sc != OK) {
        return sc;
    }
    /* у двух команд не может быть одинакового названия (слова - без учёта регистра) */
    for (int i = 0; i < COMMAND_COUNT; i++) {
        for (int j = i + 1; j < COMMAND_COUNT; j++) {
            int equal = 0;
            same_ignore_case(s->names[i], strlen(s->names[i]), s->names[j], &equal);
            if (equal) {
                error->line = 0;
                error->first = i;
                error->second = j;
                return SYNTAX_ERROR;
            }
        }
    }
    return OK;
}

/* ===================== запоминание последнего файла настроек ===================== */

/* Путь файла-памяти: папка программы (из argv[0]) + STATE_FILE */
status_code state_path(const char *program, char **result) {
    if (program == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t dir = 0;   /* длина папки вместе с последним '/' или '\' */
    for (size_t i = 0; program[i] != '\0'; i++) {
        if (program[i] == '/' || program[i] == '\\') {
            dir = i + 1;
        }
    }
    char *path = (char *)malloc(dir + strlen(STATE_FILE) + 1);
    if (path == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(path, program, dir);
    strcpy(path + dir, STATE_FILE);
    *result = path;
    return OK;
}

/* Полный путь файла (чтобы файл нашёлся, даже если программу запустят из другой папки) */
status_code absolute_path(const char *path, char **result) {
    if (path == NULL || result == NULL) {
        return INVALID_ARGS;
    }
#ifdef _WIN32
    for (size_t size = 260; size <= 65536; size *= 2) {
        char *buf = (char *)malloc(size);
        if (buf == NULL) {
            return ALLOC_ERROR;
        }
        if (_fullpath(buf, path, size) != NULL) {
            *result = buf;
            return OK;
        }
        free(buf);
    }
    return FILE_ERROR;
#else
    if (path[0] == '/') {
        return copy_part(path, strlen(path), result);
    }
    /* текущая папка + '/' + путь; буфер для getcwd растёт, пока папка не поместится */
    for (size_t size = 256; size <= 65536; size *= 2) {
        char *buf = (char *)malloc(size + strlen(path) + 2);
        if (buf == NULL) {
            return ALLOC_ERROR;
        }
        if (getcwd(buf, size) != NULL) {
            strcat(buf, "/");
            strcat(buf, path);
            *result = buf;
            return OK;
        }
        free(buf);
    }
    return FILE_ERROR;
#endif
}

/* Запомнить файл настроек: его полный путь - одной строкой в файл-память */
status_code remember_settings(const char *state, const char *settings_file) {
    if (state == NULL || settings_file == NULL) {
        return INVALID_ARGS;
    }
    char *full = NULL;
    status_code sc = absolute_path(settings_file, &full);
    if (sc != OK) {
        return sc;
    }
    FILE *file = fopen(state, "w");
    if (file == NULL) {
        free(full);
        return FILE_ERROR;
    }
    fprintf(file, "%s\n", full);
    free(full);
    int failed = ferror(file);
    if (fclose(file) != 0) {
        failed = 1;
    }
    return failed ? FILE_ERROR : OK;
}

/* Вспомнить последний файл настроек: FILE_ERROR - памяти нет (программа ещё не запускалась с настройками) */
status_code recall_settings(const char *state, char **settings_file) {
    if (state == NULL || settings_file == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(state, "r");
    if (file == NULL) {
        return FILE_ERROR;
    }
    char *line = NULL;
    int end = 0;
    status_code sc = read_line(file, &line, &end);
    fclose(file);
    if (sc != OK) {
        return sc;
    }
    if (end || line[0] == '\0') {
        free(line);
        return FILE_ERROR;
    }
    *settings_file = line;
    return OK;
}

/* ===================== интерпретатор: слова программы ===================== */

/* Виды слов (лексем) программы */
typedef enum { T_WORD, T_COMMAND, T_LPAREN, T_RPAREN, T_COMMA, T_SEMICOLON, T_END } token_kind;

typedef struct {
    token_kind kind;
    const char *start;     /* где в тексте программы */
    size_t length;
    size_t line;
    int command;           /* T_COMMAND: какая команда */
} token;

/* Узел дерева выражения: переменная, константа или команда с одним-двумя аргументами */
typedef enum { N_VARIABLE, N_CONSTANT, N_CALL } node_kind;

typedef struct node {
    node_kind kind;
    int command;              /* N_CALL */
    uint32_t value;           /* N_CONSTANT */
    const char *name;         /* N_VARIABLE: имя - прямо в тексте программы (он живёт до конца работы) */
    size_t length;
    size_t line;
    struct node *args[2];
} node;

/* Всё состояние интерпретатора */
typedef struct {
    settings cfg;
    int base_assign;
    int base_input;
    int base_output;
    int debug;               /* --debug: точки останова работают */
    int interactive;         /* ввод с клавиатуры: печатать подсказки отладчика */
    var_trie vars;
    const char *text;        /* текст программы */
    size_t pos;              /* где сейчас разбор */
    size_t line;
    token current;           /* подсмотренное следующее слово */
    int has_current;
    size_t nodes;            /* сколько узлов в дереве текущей инструкции */
} interpreter;

/* Ошибка в программе: "Error: line N: <before><слово><after>" в stderr; возвращает code */
status_code fail(const size_t line, const char *before, const char *word, const size_t length, const char *after,
                 const status_code code) {
    if (before == NULL) {
        return INVALID_ARGS;
    }
    fprintf(stderr, "Error: line %zu: %s", line, before);
    if (word != NULL) {
        /* длинное слово показываем не целиком */
        fprintf(stderr, "%.*s%s", (int)(length > MAX_SHOWN ? MAX_SHOWN : length), word,
                length > MAX_SHOWN ? "..." : "");
    }
    fprintf(stderr, "%s\n", after != NULL ? after : "");
    return code;
}

status_code debugger(interpreter *in, const size_t line);

/*
 * Пропустить пробелы и комментарии. Однострочный комментарий, текст которого (без пробелов по краям) -
 * BREAKPOINT, при --debug - точка останова: отладчик запускается прямо здесь, до разбора следующего слова.
 */
status_code skip_blank(interpreter *in) {
    if (in == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        char c = in->text[in->pos];
        int space = 0;
        is_space(c, &space);
        if (space) {
            if (c == '\n') {
                in->line++;
            }
            in->pos++;
        } else if (c == '#') {
            /* однострочный комментарий: его текст - от '#' до конца строки */
            size_t start = ++in->pos;
            while (in->text[in->pos] != '\0' && in->text[in->pos] != '\n') {
                in->pos++;
            }
            size_t end = in->pos;
            while (start < end && (in->text[start] == ' ' || in->text[start] == '\t')) {
                start++;
            }
            while (end > start && (in->text[end - 1] == ' ' || in->text[end - 1] == '\t' ||
                                   in->text[end - 1] == '\r')) {
                end--;
            }
            if (in->debug && end - start == 10 && strncmp(in->text + start, "BREAKPOINT", 10) == 0) {
                status_code sc = debugger(in, in->line);
                if (sc != OK) {
                    return sc;
                }
            }
        } else if (c == '[') {
            /* многострочный комментарий с любой вложенностью */
            size_t depth = 0;
            size_t open_line = in->line;
            do {
                char k = in->text[in->pos];
                if (k == '\0') {
                    return fail(open_line, "'[' without ']'", NULL, 0, NULL, SYNTAX_ERROR);
                }
                if (k == '[') {
                    depth++;
                } else if (k == ']') {
                    depth--;
                } else if (k == '\n') {
                    in->line++;
                }
                in->pos++;
            } while (depth > 0);
        } else if (c == ']') {
            return fail(in->line, "']' without '['", NULL, 0, NULL, SYNTAX_ERROR);
        } else {
            return OK;
        }
    }
}

/* Слово - название какой команды (без учёта регистра); *cmd = -1 - не команда */
status_code find_command(const interpreter *in, const char *s, const size_t length, int *cmd) {
    if (in == NULL || s == NULL || cmd == NULL) {
        return INVALID_ARGS;
    }
    *cmd = -1;
    for (int i = 0; i < COMMAND_COUNT; i++) {
        int equal = 0;
        same_ignore_case(s, length, in->cfg.names[i], &equal);
        if (equal) {
            *cmd = i;
        }
    }
    return OK;
}

/*
 * Прочитать следующее слово программы. Слово из букв, цифр и '_' - имя, константа или команда (если так
 * называется команда). Значки: самое длинное название команды-значка, с которого начинается текст
 * (например, при командах <> и < в тексте "<>" - команда <>).
 */
status_code read_token(interpreter *in, token *t) {
    if (in == NULL || t == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = skip_blank(in);
    if (sc != OK) {
        return sc;
    }
    const char *s = in->text + in->pos;
    t->start = s;
    t->line = in->line;
    t->command = -1;
    t->length = 1;
    int word = 0;
    int symbol = 0;
    is_word_char(s[0], &word);
    is_symbol_char(s[0], &symbol);
    if (s[0] == '\0') {
        t->kind = T_END;
        t->length = 0;
    } else if (s[0] == '(' || s[0] == ')' || s[0] == ',' || s[0] == ';') {
        t->kind = s[0] == '(' ? T_LPAREN : s[0] == ')' ? T_RPAREN : s[0] == ',' ? T_COMMA : T_SEMICOLON;
    } else if (word) {
        /* слово: до первого символа не из слова */
        size_t n = 0;
        while (word) {
            n++;
            is_word_char(s[n], &word);
        }
        t->length = n;
        find_command(in, s, n, &t->command);
        t->kind = t->command >= 0 ? T_COMMAND : T_WORD;
    } else if (symbol) {
        /* значок: самое длинное подходящее название команды */
        size_t best = 0;
        for (int i = 0; i < COMMAND_COUNT; i++) {
            size_t n = strlen(in->cfg.names[i]);
            int is_sign = 0;
            is_symbol_char(in->cfg.names[i][0], &is_sign);
            if (is_sign && n > best && strncmp(s, in->cfg.names[i], n) == 0) {
                best = n;
                t->command = i;
            }
        }
        if (best == 0) {
            return fail(in->line, "unknown sign '", s, 1, "'", SYNTAX_ERROR);
        }
        t->kind = T_COMMAND;
        t->length = best;
    } else {
        /* управляющий символ или не ASCII (например, русская буква вне комментария) - печатаем его код:
           половина символа UTF-8 на экране была бы мусором */
        fprintf(stderr, "Error: line %zu: unknown symbol with code %d\n", in->line, (int)(unsigned char)s[0]);
        return SYNTAX_ERROR;
    }
    in->pos += t->length;
    return OK;
}

/* Подсмотреть следующее слово (не забирая его) */
status_code peek(interpreter *in, token **t) {
    if (in == NULL || t == NULL) {
        return INVALID_ARGS;
    }
    if (!in->has_current) {
        status_code sc = read_token(in, &in->current);
        if (sc != OK) {
            return sc;
        }
        in->has_current = 1;
    }
    *t = &in->current;
    return OK;
}

/* Забрать следующее слово */
status_code next(interpreter *in, token *t) {
    if (in == NULL || t == NULL) {
        return INVALID_ARGS;
    }
    token *p = NULL;
    status_code sc = peek(in, &p);
    if (sc != OK) {
        return sc;
    }
    *t = *p;
    in->has_current = 0;
    return OK;
}

/* Сообщение "неожиданное слово" (или конец программы) */
status_code unexpected(const token *t, const char *expected) {
    if (t == NULL || expected == NULL) {
        return INVALID_ARGS;
    }
    if (t->kind == T_END) {
        fprintf(stderr, "Error: line %zu: unexpected end of the program (%s)\n", t->line, expected);
        return SYNTAX_ERROR;
    }
    /* длинное слово показываем не целиком (первые MAX_SHOWN символов) */
    fprintf(stderr, "Error: line %zu: unexpected '%.*s' (%s)\n", t->line,
            (int)(t->length > MAX_SHOWN ? MAX_SHOWN : t->length), t->start, expected);
    return SYNTAX_ERROR;
}

/* Забрать слово, которое обязано быть вида kind (иначе - ошибка "expected") */
status_code expect(interpreter *in, const token_kind kind, const char *expected) {
    if (in == NULL || expected == NULL) {
        return INVALID_ARGS;
    }
    token t;
    status_code sc = next(in, &t);
    if (sc != OK) {
        return sc;
    }
    return t.kind == kind ? OK : unexpected(&t, expected);
}

/* ===================== интерпретатор: дерево выражения ===================== */

/* Новый пустой узел; узлов в инструкции не больше MAX_NODES (иначе дерево слишком глубокое для рекурсии) */
status_code new_node(interpreter *in, const size_t line, node **result) {
    if (in == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    if (++in->nodes > MAX_NODES) {
        return fail(line, "the instruction is too long (more than 2000 commands and operands)", NULL, 0, NULL,
                    SYNTAX_ERROR);
    }
    node *x = (node *)calloc(1, sizeof(node));
    if (x == NULL) {
        return ALLOC_ERROR;
    }
    *result = x;
    return OK;
}

/* Новый узел-команда с аргументами a и b (b = NULL у унарной) */
status_code new_call(interpreter *in, const int cmd, node *a, node *b, const size_t line, node **result) {
    if (in == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    node *x = NULL;
    status_code sc = new_node(in, line, &x);
    if (sc != OK) {
        return sc;
    }
    x->kind = N_CALL;
    x->command = cmd;
    x->args[0] = a;
    x->args[1] = b;
    x->line = line;
    *result = x;
    return OK;
}

/* Освободить дерево (рекурсия: глубина не больше MAX_NESTING - так ограничен разбор) */
status_code free_tree(node *x) {
    if (x == NULL) {
        return OK;
    }
    free_tree(x->args[0]);
    free_tree(x->args[1]);
    free(x);
    return OK;
}

/* Унарная ли команда (not, input, output) */
status_code is_unary(const int cmd, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = cmd == CMD_NOT || cmd == CMD_INPUT || cmd == CMD_OUTPUT;
    return OK;
}

status_code parse_expression(interpreter *in, const size_t depth, node **result);

/*
 * Команда, записанная перед аргументами - op(): name(e) у унарной, name(e1, e2) у бинарной.
 * Можно только если так задано в настройках; '=' внутри выражения - ошибка.
 */
status_code parse_prefix_call(interpreter *in, const token *t, const size_t depth, node **result) {
    if (in == NULL || t == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    int unary = 0;
    is_unary(t->command, &unary);
    if (t->command == CMD_ASSIGN || (unary && in->cfg.unary != PREFIX) || (!unary && in->cfg.binary != PREFIX)) {
        return unexpected(t, "with these settings the command can not stand here");
    }
    status_code sc = expect(in, T_LPAREN, "'(' after the command expected");
    if (sc != OK) {
        return sc;
    }
    node *a = NULL;
    node *b = NULL;
    sc = parse_expression(in, depth + 1, &a);
    if (sc == OK && !unary) {
        sc = expect(in, T_COMMA, "',' expected");
        if (sc == OK) {
            sc = parse_expression(in, depth + 1, &b);
        }
    }
    if (sc == OK) {
        sc = expect(in, T_RPAREN, "')' expected");
    }
    if (sc == OK) {
        sc = new_call(in, t->command, a, b, t->line, result);
    }
    if (sc != OK) {
        free_tree(a);
        free_tree(b);
    }
    return sc;
}

/*
 * Скобка: (e1, e2)op - бинарная ()op; (e)op - унарная ()op; иначе (e) - просто скобки для группировки
 * (нужны при записи (op): (a add b) mult c).
 */
status_code parse_parenthesis(interpreter *in, const size_t depth, node **result) {
    if (in == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    node *a = NULL;
    node *b = NULL;
    token t;
    status_code sc = parse_expression(in, depth + 1, &a);
    if (sc == OK) {
        sc = next(in, &t);
    }
    if (sc == OK && t.kind == T_COMMA && in->cfg.binary == POSTFIX) {
        /* (e1, e2)op: после скобки - бинарная команда */
        sc = parse_expression(in, depth + 1, &b);
        if (sc == OK) {
            sc = expect(in, T_RPAREN, "')' expected");
        }
        if (sc == OK) {
            sc = next(in, &t);
        }
        int unary = 1;
        if (sc == OK) {
            is_unary(t.command, &unary);
        }
        if (sc == OK && (t.kind != T_COMMAND || unary || t.command == CMD_ASSIGN)) {
            sc = unexpected(&t, "a binary command after '(a, b)' expected");
        }
        if (sc == OK) {
            sc = new_call(in, t.command, a, b, t.line, result);
        }
    } else if (sc == OK && t.kind == T_RPAREN) {
        /* (e)op - если унарные пишутся после аргумента и дальше унарная команда; иначе скобки */
        token *after = NULL;
        sc = peek(in, &after);
        int unary = 0;
        if (sc == OK && after->kind == T_COMMAND) {
            is_unary(after->command, &unary);
        }
        if (sc == OK && unary && in->cfg.unary == POSTFIX) {
            next(in, &t);
            sc = new_call(in, t.command, a, NULL, t.line, result);
        } else if (sc == OK) {
            *result = a;
            return OK;
        }
    } else if (sc == OK) {
        sc = unexpected(&t, in->cfg.binary == POSTFIX ? "',' or ')' expected" : "')' expected");
    }
    if (sc != OK) {
        free_tree(a);
        free_tree(b);
    }
    return sc;
}

/* Операнд: константа (с цифры), переменная, команда op() или выражение в скобках */
status_code parse_operand(interpreter *in, const size_t depth, node **result) {
    if (in == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    if (depth > MAX_NESTING) {
        return fail(in->line, "the expression is nested too deeply", NULL, 0, NULL, SYNTAX_ERROR);
    }
    token t;
    status_code sc = next(in, &t);
    if (sc != OK) {
        return sc;
    }
    if (t.kind == T_COMMAND) {
        return parse_prefix_call(in, &t, depth, result);
    }
    if (t.kind == T_LPAREN) {
        return parse_parenthesis(in, depth, result);
    }
    if (t.kind != T_WORD) {
        return unexpected(&t, "a variable, a constant or a command expected");
    }
    node *x = NULL;
    sc = new_node(in, t.line, &x);
    if (sc != OK) {
        return sc;
    }
    x->line = t.line;
    if (t.start[0] >= '0' && t.start[0] <= '9') {
        /* константа в системе base_assign */
        x->kind = N_CONSTANT;
        sc = parse_in_base(t.start, t.length, in->base_assign, &x->value);
        if (sc == SYNTAX_ERROR) {
            sc = fail(t.line, "wrong constant '", t.start, t.length, "' for base_assign", SYNTAX_ERROR);
        } else if (sc == RUNTIME_ERROR) {
            sc = fail(t.line, "constant '", t.start, t.length, "' does not fit in 32 bits", SYNTAX_ERROR);
        }
    } else {
        x->kind = N_VARIABLE;
        x->name = t.start;
        x->length = t.length;
    }
    if (sc != OK) {
        free(x);
        return sc;
    }
    *result = x;
    return OK;
}

/* Выражение: операнд, а при записи (op) - цепочка "операнд команда операнд ..." слева направо */
status_code parse_expression(interpreter *in, const size_t depth, node **result) {
    if (in == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    node *left = NULL;
    status_code sc = parse_operand(in, depth, &left);
    while (sc == OK && in->cfg.binary == INFIX) {
        token *t = NULL;
        sc = peek(in, &t);
        int unary = 1;
        if (sc == OK && t->kind == T_COMMAND) {
            is_unary(t->command, &unary);
        }
        if (sc != OK || t->kind != T_COMMAND || unary || t->command == CMD_ASSIGN) {
            break;   /* не бинарная команда - выражение кончилось */
        }
        token op;
        next(in, &op);
        node *right = NULL;
        sc = parse_operand(in, depth + 1, &right);
        node *call = NULL;
        if (sc == OK) {
            sc = new_call(in, op.command, left, right, op.line, &call);
        }
        if (sc != OK) {
            free_tree(right);
            break;
        }
        left = call;
    }
    if (sc != OK) {
        free_tree(left);
        return sc;
    }
    *result = left;
    return OK;
}

/* input и output - только вся инструкция целиком, не внутри выражения */
status_code check_io(const node *x, const int is_root) {
    if (x == NULL || x->kind != N_CALL) {
        return OK;
    }
    if ((x->command == CMD_INPUT || x->command == CMD_OUTPUT) && !is_root) {
        return fail(x->line, "input and output can not be inside an expression", NULL, 0, NULL, SYNTAX_ERROR);
    }
    status_code sc = check_io(x->args[0], 0);
    if (sc == OK) {
        sc = check_io(x->args[1], 0);
    }
    return sc;
}

/* ===================== интерпретатор: выполнение ===================== */

/* Быстрое возведение в степень по модулю 2^32: квадраты основания и множители там, где в степени бит 1 */
status_code power(uint32_t base, uint32_t exponent, uint32_t *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    uint32_t r = 1;
    while (exponent > 0) {
        if (exponent & 1u) {
            r *= base;        /* uint32_t: переполнение - это и есть остаток от деления на 2^32 */
        }
        base *= base;
        exponent >>= 1;
    }
    *result = r;
    return OK;
}

/* Значение выражения (рекурсия по дереву: глубина не больше MAX_NESTING) */
status_code evaluate(interpreter *in, const node *x, uint32_t *value) {
    if (in == NULL || x == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    if (x->kind == N_CONSTANT) {
        *value = x->value;
        return OK;
    }
    if (x->kind == N_VARIABLE) {
        int known = 0;
        trie_get(&in->vars, x->name, x->length, &known, value);
        return known ? OK : fail(x->line, "variable '", x->name, x->length, "' is not declared", RUNTIME_ERROR);
    }
    uint32_t a = 0;
    uint32_t b = 0;
    status_code sc = evaluate(in, x->args[0], &a);
    if (sc == OK && x->args[1] != NULL) {
        sc = evaluate(in, x->args[1], &b);
    }
    if (sc != OK) {
        return sc;
    }
    /* арифметика uint32_t - по модулю 2^32 */
    switch (x->command) {
        case CMD_NOT:  *value = ~a; break;
        case CMD_ADD:  *value = a + b; break;
        case CMD_MULT: *value = a * b; break;
        case CMD_SUB:  *value = a - b; break;
        case CMD_POW:  power(a, b, value); break;
        case CMD_XOR:  *value = a ^ b; break;
        case CMD_AND:  *value = a & b; break;
        case CMD_OR:   *value = a | b; break;
        case CMD_DIV:
        case CMD_REM:
            if (b == 0) {
                return fail(x->line, "division by zero", NULL, 0, NULL, RUNTIME_ERROR);
            }
            *value = x->command == CMD_DIV ? a / b : a % b;
            break;
        default:
            /* input/output внутри выражения отсеяны check_io - сюда не попадают */
            return INVALID_ARGS;
    }
    return OK;
}

/* input(x): строка стандартного ввода - число в системе base_input - в переменную x */
status_code do_input(interpreter *in, const node *target, const size_t line) {
    if (in == NULL || target == NULL) {
        return INVALID_ARGS;
    }
    char *text = NULL;
    int end = 0;
    status_code sc = read_line(stdin, &text, &end);
    if (sc != OK) {
        return sc;
    }
    if (end) {
        free(text);
        return fail(line, "input: the standard input ended", NULL, 0, NULL, RUNTIME_ERROR);
    }
    char *s = NULL;
    trim(text, &s);
    uint32_t value = 0;
    sc = parse_in_base(s, strlen(s), in->base_input, &value);
    if (sc == SYNTAX_ERROR) {
        sc = fail(line, "input: '", s, strlen(s), "' is not a number in base_input", RUNTIME_ERROR);
    } else if (sc == RUNTIME_ERROR) {
        sc = fail(line, "input: '", s, strlen(s), "' does not fit in 32 bits", RUNTIME_ERROR);
    } else {
        sc = trie_set(&in->vars, target->name, target->length, value);
    }
    free(text);
    return sc;
}

/*
 * Одна инструкция до ';': разобрать в дерево, проверить, выполнить.
 *   left=:  <переменная> = <выражение>;     right=:  <выражение> = <переменная>;
 *   или input(<переменная>); / output(<выражение>); (в записи унарных команд из настроек).
 */
status_code run_statement(interpreter *in) {
    if (in == NULL) {
        return INVALID_ARGS;
    }
    node *expr = NULL;
    node *target = NULL;
    token t;
    in->nodes = 0;   /* счётчик узлов - для каждой инструкции заново */
    status_code sc = parse_expression(in, 0, &expr);
    if (sc == OK) {
        sc = next(in, &t);
    }
    if (sc == OK && t.kind == T_COMMAND && t.command == CMD_ASSIGN) {
        /* присваивание: с другой стороны от '=' - переменная */
        if (in->cfg.result_left) {
            target = expr;
            expr = NULL;
            sc = parse_expression(in, 0, &expr);
        } else {
            sc = parse_operand(in, 0, &target);
        }
        if (sc == OK && target->kind != N_VARIABLE) {
            sc = fail(t.line, "only a variable can get the result of '", in->cfg.names[CMD_ASSIGN],
                      strlen(in->cfg.names[CMD_ASSIGN]), "'", SYNTAX_ERROR);
        }
        if (sc == OK) {
            sc = expect(in, T_SEMICOLON, "';' expected");
        }
        if (sc == OK) {
            sc = check_io(expr, 0);
        }
        uint32_t value = 0;
        if (sc == OK) {
            sc = evaluate(in, expr, &value);
        }
        if (sc == OK) {
            sc = trie_set(&in->vars, target->name, target->length, value);
        }
    } else if (sc == OK && t.kind == T_SEMICOLON) {
        /* input или output */
        if (expr->kind != N_CALL || (expr->command != CMD_INPUT && expr->command != CMD_OUTPUT)) {
            sc = fail(t.line, "the instruction does nothing (need an assignment, input or output)", NULL, 0, NULL,
                      SYNTAX_ERROR);
        }
        if (sc == OK) {
            sc = check_io(expr, 1);
        }
        if (sc == OK && expr->command == CMD_INPUT) {
            if (expr->args[0]->kind != N_VARIABLE) {
                sc = fail(expr->line, "input needs a variable", NULL, 0, NULL, SYNTAX_ERROR);
            } else {
                sc = do_input(in, expr->args[0], expr->line);
            }
        } else if (sc == OK) {
            uint32_t value = 0;
            sc = evaluate(in, expr->args[0], &value);
            if (sc == OK) {
                char text[33];
                number_in_base(value, in->base_output, text);
                printf("%s\n", text);
            }
        }
    } else if (sc == OK) {
        sc = unexpected(&t, "';' or an assignment expected");
    }
    free_tree(expr);
    free_tree(target);
    return sc;
}

/* Вся программа: инструкции по очереди до конца текста */
status_code run_program(interpreter *in) {
    if (in == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        token *t = NULL;
        status_code sc = peek(in, &t);
        if (sc != OK) {
            return sc;
        }
        if (t->kind == T_END) {
            return OK;
        }
        sc = run_statement(in);
        if (sc != OK) {
            return sc;
        }
        fflush(stdout);
    }
}

/* ===================== отладчик ===================== */

/* Вопрос отладчика: подсказка (только с клавиатуры), строка ответа без пробелов по краям; INPUT_END - конец ввода */
status_code ask(const interpreter *in, const char *question, char **line, char **answer) {
    if (in == NULL || question == NULL || line == NULL || answer == NULL) {
        return INVALID_ARGS;
    }
    if (in->interactive) {
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
    return trim(*line, answer);
}

/* Значение и дамп памяти: байты значения в порядке в памяти, каждый - 8 двоичных цифр, через пробел */
status_code print_variable(const char *name, const uint32_t value) {
    if (name == NULL) {
        return INVALID_ARGS;
    }
    char text[33];
    number_in_base(value, 16, text);
    printf("%s = %s\nmemory:", name, text);
    const unsigned char *bytes = (const unsigned char *)&value;
    for (size_t i = 0; i < sizeof(value); i++) {
        putchar(' ');
        for (int bit = 7; bit >= 0; bit--) {
            putchar((bytes[i] >> bit) & 1u ? '1' : '0');
        }
    }
    putchar('\n');
    return OK;
}

/* Ошибка отладчика - в стандартный вывод; переменные не меняются, диалог продолжается */
status_code debug_error(const char *before, const char *word, const char *after) {
    if (before == NULL) {
        return INVALID_ARGS;
    }
    printf("Debugger error: %s%s%s\n", before, word != NULL ? word : "", after != NULL ? after : "");
    return OK;
}

/* Спросить имя переменной; must_be_known: 1 - должна быть известна, 0 - не должна (и имя должно годиться) */
status_code ask_name(interpreter *in, const int must_be_known, char **line, char **name, int *good) {
    if (in == NULL || line == NULL || name == NULL || good == NULL) {
        return INVALID_ARGS;
    }
    *good = 0;
    status_code sc = ask(in, "Variable name: ", line, name);
    if (sc != OK) {
        return sc;
    }
    size_t length = strlen(*name);
    int valid = 0;
    int cmd = -1;
    int known = 0;
    uint32_t value = 0;
    valid_name(*name, length, &valid);
    find_command(in, *name, length, &cmd);
    if (valid && cmd < 0) {
        trie_get(&in->vars, *name, length, &known, &value);
    }
    if (!valid || cmd >= 0) {
        debug_error("'", *name, "' is not a variable name");
    } else if (must_be_known && !known) {
        debug_error("variable '", *name, "' is not known");
    } else if (!must_be_known && known) {
        debug_error("variable '", *name, "' is already known");
    } else {
        *good = 1;
    }
    return OK;
}

/* Одно действие отладчика по номеру choice (1..5); *line_name / *line_value - строки ответов для free */
status_code debug_action(interpreter *in, const int choice) {
    if (in == NULL) {
        return INVALID_ARGS;
    }
    char *line = NULL;
    char *line2 = NULL;
    char *line3 = NULL;
    char *name = NULL;
    char *answer = NULL;
    int good = 0;
    size_t count = 0;
    uint32_t value = 0;
    status_code sc = OK;
    switch (choice) {
        case 1:   /* значение и дамп памяти */
            sc = ask_name(in, 1, &line, &name, &good);
            if (sc == OK && good) {
                int known = 0;
                trie_get(&in->vars, name, strlen(name), &known, &value);
                print_variable(name, value);
            }
            break;
        case 2:   /* все известные переменные */
            sc = trie_print(&in->vars, stdout, &count);
            if (sc == OK && count == 0) {
                printf("no variables\n");
            }
            break;
        case 3:   /* новое значение (16-ричное) */
            sc = ask_name(in, 1, &line, &name, &good);
            if (sc == OK && good) {
                sc = ask(in, "New value (base 16): ", &line2, &answer);
            }
            if (sc == OK && good) {
                status_code parsed = parse_in_base(answer, strlen(answer), 16, &value);
                if (parsed == OK) {
                    sc = trie_set(&in->vars, name, strlen(name), value);
                    print_variable(name, value);
                } else {
                    debug_error("'", answer, parsed == RUNTIME_ERROR ? "' does not fit in 32 bits"
                                                                     : "' is not a number in base 16");
                }
            }
            break;
        case 4:   /* объявить: значение - цекендорфово или римское */
            sc = ask_name(in, 0, &line, &name, &good);
            if (sc == OK && good) {
                sc = ask(in, "Value as 1 - Zeckendorf representation, 2 - Roman numerals: ", &line2, &answer);
            }
            if (sc == OK && good) {
                if (strcmp(answer, "1") != 0 && strcmp(answer, "2") != 0) {
                    debug_error("choose 1 or 2", NULL, NULL);
                    break;
                }
                int roman = strcmp(answer, "2") == 0;
                sc = ask(in, roman ? "Roman number: " : "Zeckendorf representation: ", &line3, &answer);
                if (sc == OK) {
                    status_code parsed = roman ? parse_roman(answer, &value) : parse_zeckendorf(answer, &value);
                    if (parsed == OK) {
                        sc = trie_set(&in->vars, name, strlen(name), value);
                        print_variable(name, value);
                    } else if (parsed == RUNTIME_ERROR) {
                        debug_error("'", answer, "' does not fit in 32 bits");
                    } else {
                        debug_error("'", answer, roman ? "' is not a Roman number from I to MMMCMXCIX"
                                                       : "' is not a Zeckendorf representation");
                    }
                }
            }
            break;
        case 5:   /* отменить объявление */
            sc = ask_name(in, 1, &line, &name, &good);
            if (sc == OK && good) {
                int removed = 0;
                sc = trie_remove(&in->vars, name, strlen(name), &removed);
                if (sc == OK) {
                    printf("variable '%s' is undeclared\n", name);
                }
            }
            break;
        default:
            sc = INVALID_ARGS;
    }
    free(line);
    free(line2);
    free(line3);
    return sc;
}

/*
 * Диалог отладчика в точке останова: действия 1..5 - и диалог продолжается, 6 - продолжить выполнение,
 * 7 - завершить интерпретатор (STOPPED). Конец ввода - как "продолжить".
 */
status_code debugger(interpreter *in, const size_t line) {
    if (in == NULL) {
        return INVALID_ARGS;
    }
    fflush(stdout);
    printf("Breakpoint at line %zu\n", line);
    for (;;) {
        if (in->interactive) {
            printf("1 - print a variable (base 16) and its memory dump\n"
                   "2 - print all known variables\n"
                   "3 - change a variable (base 16)\n"
                   "4 - declare a variable (Zeckendorf representation or Roman numerals)\n"
                   "5 - undeclare a variable\n"
                   "6 - continue the program\n"
                   "7 - stop the interpreter\n");
        }
        char *line_text = NULL;
        char *answer = NULL;
        status_code sc = ask(in, "Your choice: ", &line_text, &answer);
        if (sc == INPUT_END) {
            return OK;
        }
        if (sc != OK) {
            return sc;
        }
        int choice = (strlen(answer) == 1 && answer[0] >= '1' && answer[0] <= '7') ? answer[0] - '0' : 0;
        free(line_text);
        if (choice == 6) {
            return OK;
        }
        if (choice == 7) {
            return STOPPED;
        }
        if (choice == 0) {
            debug_error("choose a number from 1 to 7", NULL, NULL);
            continue;
        }
        sc = debug_action(in, choice);
        if (sc == INPUT_END) {
            return OK;
        }
        if (sc != OK) {
            return sc;
        }
    }
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    const char *usage = "Usage: lab4_task10 [<settings file>] <program file> [--base_assign N] [--base_input N] "
                        "[--base_output N] [--debug | -d | /debug]";
    interpreter in;
    memset(&in, 0, sizeof(in));
    in.base_assign = 10;
    in.base_input = 10;
    in.base_output = 10;
    /* аргументы: ключи где угодно, остальное - файлы (один или два) */
    const char *files[2] = {NULL, NULL};
    size_t count = 0;
    int wrong = 0;
    int seen[3] = {0, 0, 0};   /* какие основания уже заданы */
    for (int i = 1; i < argc && !wrong; i++) {
        const char *keys[3] = {"--base_assign", "--base_input", "--base_output"};
        int *places[3] = {&in.base_assign, &in.base_input, &in.base_output};
        int key = -1;
        for (int k = 0; k < 3; k++) {
            if (strcmp(argv[i], keys[k]) == 0) {
                key = k;
            }
        }
        if (key >= 0) {
            if (i + 1 == argc || seen[key]) {
                wrong = 1;
            } else if (parse_base(argv[++i], places[key]) != OK) {
                fprintf(stderr, "Error: %s must be a number from 2 to 36\n", keys[key] + 2);
                return 1;
            }
            seen[key] = 1;
        } else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "/debug") == 0) {
            in.debug = 1;
        } else if (count < 2) {
            files[count++] = argv[i];
        } else {
            wrong = 1;
        }
    }
    if (wrong || count == 0) {
        fprintf(stderr, "%s\n", usage);
        return 1;
    }
#ifdef _WIN32
    in.interactive = _isatty(_fileno(stdin));
#else
    in.interactive = isatty(fileno(stdin));
#endif

    /* файл настроек: данный или запомненный */
    char *state = NULL;
    char *remembered = NULL;
    const char *settings_file = count == 2 ? files[0] : NULL;
    const char *program_file = files[count - 1];
    status_code sc = state_path(argv[0], &state);
    if (sc == OK && settings_file == NULL) {
        sc = recall_settings(state, &remembered);
        if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: no settings file is given and none is remembered\n");
            free(state);
            return 1;
        }
        settings_file = remembered;
    }
    settings_error error = {0, NULL, 0, 0};
    if (sc == OK) {
        sc = settings_init(&in.cfg);
    }
    if (sc == OK) {
        sc = load_settings(settings_file, &in.cfg, &error);
        if (sc == FILE_ERROR && remembered != NULL) {
            /* "если это возможно": запомненный файл могли удалить или переместить */
            fprintf(stderr, "Error: can not read the remembered settings file '%s'\n", settings_file);
        } else if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not read file '%s'\n", settings_file);
        } else if (sc == SYNTAX_ERROR && error.line > 0) {
            fprintf(stderr, "Error: settings '%s' line %zu: %s\n", settings_file, error.line, error.reason);
        } else if (sc == SYNTAX_ERROR) {
            fprintf(stderr, "Error: settings '%s': commands '%s' and '%s' have the same name '%s'\n", settings_file,
                    ORIGINAL_NAMES[error.first], ORIGINAL_NAMES[error.second], in.cfg.names[error.first]);
        }
    }

    /* программа */
    char *text = NULL;
    if (sc == OK) {
        sc = read_file(program_file, &text);
        if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not read file '%s'\n", program_file);
        }
    }
    int remember = 0;   /* настройки прочитаны - запомнить их при завершении */
    if (sc == OK) {
        remember = 1;
        sc = trie_init(&in.vars);
    }
    if (sc == OK) {
        in.text = text;
        in.line = 1;
        sc = run_program(&in);
    }
    fflush(stdout);
    /* при завершении - запомнить файл настроек (ошибка программы этому не мешает) */
    if (remember && remember_settings(state, settings_file) != OK) {
        fprintf(stderr, "Warning: can not remember the settings file in '%s'\n", state);
    }
    trie_free(&in.vars);
    settings_free(&in.cfg);
    free(text);
    free(state);
    free(remembered);
    if (sc == ALLOC_ERROR) {
        fprintf(stderr, "Error: not enough memory\n");
    } else if (sc == INVALID_ARGS) {
        /* INVALID_ARGS не бывает ни при какой программе - только при ошибке в самом интерпретаторе */
        fprintf(stderr, "Error: internal error\n");
    }
    /* остальные ошибки уже описаны; STOPPED - пользователь сам завершил работу в отладчике */
    return (sc == OK || sc == STOPPED) ? 0 : 1;
}
