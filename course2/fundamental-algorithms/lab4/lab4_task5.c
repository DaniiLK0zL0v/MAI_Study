/*
 * Лабораторная 4, задание 5. Выражения: вычисление (--calculate) и таблицы истинности (--table).
 *
 * Запуск:  lab4_task5 --calculate <файл> [<файл> ...]
 *          lab4_task5 --table [--seed N] <файл> [<файл> ...]
 * В каждой строке файла - одно выражение; пустые строки пропускаются (номер выражения = номер строки с 0).
 *
 * --calculate: целые выражения (long long): + - * / (целочисленное деление) % ^ (степень >= 0), скобки,
 *   унарные минус и плюс, переменные (имя - буквы и цифры, начинается с буквы). Значение переменной
 *   спрашивается со стандартного ввода, когда оно впервые понадобится, и запоминается до конца работы.
 *   Для каждого выражения: 1) проверка баланса скобок, 2) дерево выражения, 3) обратная польская запись
 *   (обход дерева), 4) значение по обратной польской записи. Печатается выражение, дерево (со всеми
 *   скобками), обратная польская запись и значение. Ошибочные выражения файла (скобки, запись, деление на 0,
 *   переполнение, ...) выписываются в файл <имя>_errors.txt рядом с исходным: выражение, номер, причина.
 *   Приоритеты: ^ (справа налево) выше унарного минуса, затем * / %, затем + -: -2^2 = -4.
 *
 * --table: булевы формулы: переменные - одна латинская буква (a и A - разные), константы 0 и 1, скобки,
 *   ~ (не), & (и), | (или), -> (импликация), +> (коимпликация), <> (сложение по модулю 2), = (эквиваленция),
 *   ? (штрих Шеффера), ! (функция Вебба - стрелка Пирса). Приоритеты: 3 - ~; 2 - ? ! +> &; 1 - | -> <> =;
 *   двуместные операции - слева направо. Формула -> дерево; значение на каждом наборе переменных считается
 *   по дереву. Таблицы всех формул файла - в файл со случайным именем из букв и цифр в той же папке
 *   (имя печатается). Случайные имена - свой генератор xorshift64, начальное число - --seed N или время.
 *
 * Стеки - свои, на односвязном списке (по условию).
 */
#define _POSIX_C_SOURCE 200809L   /* isatty и fileno на Linux */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* strcmp, strlen, memcpy */
#include <limits.h>   /* LLONG_MAX, LLONG_MIN */
#include <time.h>     /* time - начальное число генератора без --seed */
#ifdef _WIN32
#include <io.h>       /* _isatty, _fileno */
#else
#include <unistd.h>   /* isatty, fileno */
#endif

#define MAX_TABLE_VARIABLES 16   /* 2^16 строк таблицы - больше незачем */
#define NAME_LENGTH 10           /* длина случайного имени файла таблиц (без .txt) */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,        /* NULL или неправильные данные в функции */
    UNBALANCED,          /* скобки не сбалансированы */
    UNKNOWN_SYMBOL,      /* недопустимый символ */
    OPERAND_EXPECTED,    /* на этом месте должно быть число, имя или '(' */
    OPERATOR_EXPECTED,   /* на этом месте должна быть операция или ')' */
    EMPTY_EXPRESSION,    /* в строке нет выражения (только пробелы) */
    NUMBER_TOO_BIG,      /* число не помещается в long long */
    DIVISION_BY_ZERO,    /* деление или остаток на 0 */
    NEGATIVE_POWER,      /* отрицательная степень */
    OVERFLOW_ERROR,      /* результат не помещается в long long */
    NO_VALUE,            /* значение переменной не введено (ввод кончился) */
    WRONG_VALUE,         /* введено не целое число */
    TOO_MANY_VARIABLES,  /* в формуле больше MAX_TABLE_VARIABLES переменных */
    FILE_ERROR,          /* файл не открылся / ошибка чтения или записи */
    ALLOC_ERROR          /* malloc / realloc не смогли выделить память */
} status_code;

/* Операции обоих режимов */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_NEG, OP_POS,          /* --calculate */
    OP_NOT, OP_AND, OP_OR, OP_IMP, OP_COIMP, OP_XOR, OP_EQ, OP_NAND, OP_NOR, /* --table */
    OP_LPAREN                                                                 /* '(' в стеке операций */
} operation;

/* Узел дерева выражения */
typedef enum { NODE_NUMBER, NODE_VARIABLE, NODE_OPERATION } node_kind;

typedef struct expr_node {
    node_kind kind;
    long long number;          /* NODE_NUMBER (в --table - константа 0 или 1) */
    char *name;                /* NODE_VARIABLE */
    operation op;              /* NODE_OPERATION */
    struct expr_node *left;    /* операнд (у одноместной - единственный) */
    struct expr_node *right;   /* второй операнд (у одноместной - NULL) */
} expr_node;

/* Стек на односвязном списке: в узле - одно из значений (узел дерева, число, операция, строка) */
typedef union {
    expr_node *node;
    long long number;
    operation op;
    char *text;
} stack_value;

typedef struct stack_node {
    stack_value value;
    struct stack_node *next;
} stack_node;

typedef struct {
    stack_node *top;
    size_t size;
} stack;

/* Лексема */
typedef enum { TOKEN_NUMBER, TOKEN_NAME, TOKEN_OPERATION, TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_END } token_kind;

typedef struct {
    token_kind kind;
    long long number;
    const char *start;     /* имя - кусок строки */
    size_t length;
    operation op;          /* для TOKEN_OPERATION - двуместный вариант (или ~ в --table) */
} token;

/* Известные значения переменных (--calculate) */
typedef struct {
    char **names;
    long long *values;
    size_t count;
    size_t capacity;
} variables;

/* ===================== стек ===================== */

status_code stack_init(stack *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    s->top = NULL;
    s->size = 0;
    return OK;
}

status_code stack_push(stack *s, const stack_value value) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    stack_node *node = (stack_node *)malloc(sizeof(stack_node));
    if (node == NULL) {
        return ALLOC_ERROR;
    }
    node->value = value;
    node->next = s->top;
    s->top = node;
    s->size++;
    return OK;
}

status_code stack_pop(stack *s, stack_value *value) {
    if (s == NULL || value == NULL || s->top == NULL) {
        return INVALID_ARGS;
    }
    stack_node *node = s->top;
    *value = node->value;
    s->top = node->next;
    s->size--;
    free(node);
    return OK;
}

/* Освободить узлы стека (сами значения - забота вызывающего) */
status_code stack_clear(stack *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    stack_value dummy;
    while (s->top != NULL) {
        stack_pop(s, &dummy);
    }
    return OK;
}

/* ===================== операции ===================== */

/* Сведения об операции: приоритет, ассоциативность справа налево, одноместная ли, запись */
status_code op_info(const operation op, int *priority, int *right_assoc, int *unary, const char **text) {
    if (priority == NULL || right_assoc == NULL || unary == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    *right_assoc = 0;
    *unary = 0;
    switch (op) {
        case OP_ADD:   *priority = 1; *text = "+"; break;
        case OP_SUB:   *priority = 1; *text = "-"; break;
        case OP_MUL:   *priority = 2; *text = "*"; break;
        case OP_DIV:   *priority = 2; *text = "/"; break;
        case OP_MOD:   *priority = 2; *text = "%"; break;
        case OP_NEG:   *priority = 3; *right_assoc = 1; *unary = 1; *text = "neg"; break;
        case OP_POS:   *priority = 3; *right_assoc = 1; *unary = 1; *text = "pos"; break;
        case OP_POW:   *priority = 4; *right_assoc = 1; *text = "^"; break;
        case OP_NOT:   *priority = 3; *right_assoc = 1; *unary = 1; *text = "~"; break;
        case OP_AND:   *priority = 2; *text = "&"; break;
        case OP_NAND:  *priority = 2; *text = "?"; break;
        case OP_NOR:   *priority = 2; *text = "!"; break;
        case OP_COIMP: *priority = 2; *text = "+>"; break;
        case OP_OR:    *priority = 1; *text = "|"; break;
        case OP_IMP:   *priority = 1; *text = "->"; break;
        case OP_XOR:   *priority = 1; *text = "<>"; break;
        case OP_EQ:    *priority = 1; *text = "="; break;
        default:       *priority = 0; *text = "("; break;   /* OP_LPAREN */
    }
    return OK;
}

/* ===================== целая арифметика с проверкой переполнения ===================== */

status_code safe_add(const long long a, const long long b, long long *r) {
    if (r == NULL) {
        return INVALID_ARGS;
    }
    if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
        return OVERFLOW_ERROR;
    }
    *r = a + b;
    return OK;
}

status_code safe_mul(const long long a, const long long b, long long *r) {
    if (r == NULL) {
        return INVALID_ARGS;
    }
    if (a == 0 || b == 0) {
        *r = 0;
        return OK;
    }
    if ((a == -1 && b == LLONG_MIN) || (b == -1 && a == LLONG_MIN)) {
        return OVERFLOW_ERROR;
    }
    if (a > 0 ? (b > 0 ? a > LLONG_MAX / b : b < LLONG_MIN / a) : (b > 0 ? a < LLONG_MIN / b : a < LLONG_MAX / b)) {
        return OVERFLOW_ERROR;
    }
    *r = a * b;
    return OK;
}

/* Значение операции --calculate (для одноместной b не используется) */
status_code calc_apply(const operation op, const long long a, const long long b, long long *r) {
    if (r == NULL) {
        return INVALID_ARGS;
    }
    switch (op) {
        case OP_ADD:
            return safe_add(a, b, r);
        case OP_SUB:
            if (b == LLONG_MIN) {
                return a < 0 ? safe_add(a + 1, LLONG_MAX, r) : OVERFLOW_ERROR;   /* a - LLONG_MIN = a + 2^63 */
            }
            return safe_add(a, -b, r);
        case OP_MUL:
            return safe_mul(a, b, r);
        case OP_DIV:
        case OP_MOD:
            if (b == 0) {
                return DIVISION_BY_ZERO;
            }
            if (a == LLONG_MIN && b == -1) {
                if (op == OP_DIV) {
                    return OVERFLOW_ERROR;   /* 2^63 не помещается */
                }
                *r = 0;                      /* LLONG_MIN % -1 в C - неопределённое поведение, а ответ - 0 */
                return OK;
            }
            *r = op == OP_DIV ? a / b : a % b;
            return OK;
        case OP_POW: {
            if (b < 0) {
                return NEGATIVE_POWER;
            }
            /* быстрое возведение: квадрат основания, степень пополам */
            long long result = 1;
            long long base = a;
            long long exponent = b;
            while (exponent > 0) {
                if (exponent & 1) {
                    status_code sc = safe_mul(result, base, &result);
                    if (sc != OK) {
                        return sc;
                    }
                }
                exponent >>= 1;
                if (exponent > 0) {
                    status_code sc = safe_mul(base, base, &base);
                    if (sc != OK) {
                        return sc;
                    }
                }
            }
            *r = result;
            return OK;
        }
        case OP_NEG:
            if (a == LLONG_MIN) {
                return OVERFLOW_ERROR;
            }
            *r = -a;
            return OK;
        case OP_POS:
            *r = a;
            return OK;
        default:
            return INVALID_ARGS;
    }
}

/* Значение булевой операции (0 или 1) */
status_code bool_apply(const operation op, const int a, const int b, int *r) {
    if (r == NULL) {
        return INVALID_ARGS;
    }
    switch (op) {
        case OP_NOT:   *r = !a; break;
        case OP_AND:   *r = a && b; break;
        case OP_OR:    *r = a || b; break;
        case OP_IMP:   *r = !a || b; break;
        case OP_COIMP: *r = a && !b; break;     /* не (a -> b) */
        case OP_XOR:   *r = a != b; break;
        case OP_EQ:    *r = a == b; break;
        case OP_NAND:  *r = !(a && b); break;
        case OP_NOR:   *r = !(a || b); break;
        default:       return INVALID_ARGS;
    }
    return OK;
}

/* ===================== лексемы ===================== */

int is_letter(const char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/*
 * Следующая лексема строки s с позиции *pos. table = 1 - режим --table.
 * UNKNOWN_SYMBOL - недопустимый символ (*pos указывает на него).
 */
status_code next_token(const char *s, const size_t length, size_t *pos, const int table, token *t) {
    if (s == NULL || pos == NULL || t == NULL) {
        return INVALID_ARGS;
    }
    while (*pos < length && (s[*pos] == ' ' || s[*pos] == '\t' || s[*pos] == '\r')) {
        (*pos)++;
    }
    if (*pos == length) {
        t->kind = TOKEN_END;
        return OK;
    }
    char c = s[*pos];
    t->start = s + *pos;
    if (c == '(' || c == ')') {
        t->kind = c == '(' ? TOKEN_LPAREN : TOKEN_RPAREN;
        (*pos)++;
        return OK;
    }
    if (table) {
        if (is_letter(c)) {
            t->kind = TOKEN_NAME;   /* имя переменной - одна буква */
            t->length = 1;
            (*pos)++;
            return OK;
        }
        if (c == '0' || c == '1') {
            t->kind = TOKEN_NUMBER;
            t->number = c - '0';
            (*pos)++;
            return OK;
        }
        /* сначала двухсимвольные операции */
        static const char *const two[] = {"->", "+>", "<>"};
        static const operation two_ops[] = {OP_IMP, OP_COIMP, OP_XOR};
        for (int i = 0; i < 3; i++) {
            if (length - *pos >= 2 && s[*pos] == two[i][0] && s[*pos + 1] == two[i][1]) {
                t->kind = TOKEN_OPERATION;
                t->op = two_ops[i];
                *pos += 2;
                return OK;
            }
        }
        static const char one[] = "&|~=?!";
        static const operation one_ops[] = {OP_AND, OP_OR, OP_NOT, OP_EQ, OP_NAND, OP_NOR};
        for (int i = 0; one[i] != '\0'; i++) {
            if (c == one[i]) {
                t->kind = TOKEN_OPERATION;
                t->op = one_ops[i];
                (*pos)++;
                return OK;
            }
        }
        return UNKNOWN_SYMBOL;
    }
    if (c >= '0' && c <= '9') {
        long long value = 0;
        while (*pos < length && s[*pos] >= '0' && s[*pos] <= '9') {
            int digit = s[*pos] - '0';
            if (value > (LLONG_MAX - digit) / 10) {
                return NUMBER_TOO_BIG;
            }
            value = value * 10 + digit;
            (*pos)++;
        }
        t->kind = TOKEN_NUMBER;
        t->number = value;
        return OK;
    }
    if (is_letter(c)) {
        size_t start = *pos;
        while (*pos < length && (is_letter(s[*pos]) || (s[*pos] >= '0' && s[*pos] <= '9'))) {
            (*pos)++;
        }
        t->kind = TOKEN_NAME;
        t->length = *pos - start;
        return OK;
    }
    static const char ops[] = "+-*/%^";
    static const operation op_codes[] = {OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW};
    for (int i = 0; ops[i] != '\0'; i++) {
        if (c == ops[i]) {
            t->kind = TOKEN_OPERATION;
            t->op = op_codes[i];
            (*pos)++;
            return OK;
        }
    }
    return UNKNOWN_SYMBOL;
}

/* ===================== дерево ===================== */

/* Освободить дерево. Без рекурсии: стек узлов */
status_code tree_free(expr_node *root) {
    if (root == NULL) {
        return OK;
    }
    stack s;
    stack_init(&s);
    stack_value v;
    v.node = root;
    status_code sc = stack_push(&s, v);
    while (sc == OK && s.top != NULL) {
        stack_pop(&s, &v);
        expr_node *node = v.node;
        stack_value child;
        if (node->left != NULL) {
            child.node = node->left;
            sc = stack_push(&s, child);
        }
        if (sc == OK && node->right != NULL) {
            child.node = node->right;
            sc = stack_push(&s, child);
        }
        free(node->name);
        free(node);
    }
    stack_clear(&s);
    return sc;
}

status_code leaf_new(const token *t, expr_node **node) {
    if (t == NULL || node == NULL) {
        return INVALID_ARGS;
    }
    expr_node *n = (expr_node *)calloc(1, sizeof(expr_node));
    if (n == NULL) {
        return ALLOC_ERROR;
    }
    if (t->kind == TOKEN_NUMBER) {
        n->kind = NODE_NUMBER;
        n->number = t->number;
    } else {
        n->kind = NODE_VARIABLE;
        n->name = (char *)malloc(t->length + 1);
        if (n->name == NULL) {
            free(n);
            return ALLOC_ERROR;
        }
        memcpy(n->name, t->start, t->length);
        n->name[t->length] = '\0';
    }
    *node = n;
    return OK;
}

/* Снять операцию со стека операций и её операнды со стека узлов -> новый узел на стек узлов */
status_code reduce(stack *operations, stack *nodes) {
    if (operations == NULL || nodes == NULL || operations->top == NULL) {
        return INVALID_ARGS;
    }
    stack_value v;
    stack_pop(operations, &v);
    operation op = v.op;
    int priority = 0;
    int right_assoc = 0;
    int unary = 0;
    const char *text = NULL;
    op_info(op, &priority, &right_assoc, &unary, &text);
    if (nodes->size < (unary ? 1u : 2u)) {
        return INVALID_ARGS;   /* разбор следит за этим сам - сюда не попадаем */
    }
    expr_node *n = (expr_node *)calloc(1, sizeof(expr_node));
    if (n == NULL) {
        return ALLOC_ERROR;
    }
    n->kind = NODE_OPERATION;
    n->op = op;
    stack_value operand;
    if (unary) {
        stack_pop(nodes, &operand);
        n->left = operand.node;
    } else {
        stack_pop(nodes, &operand);
        n->right = operand.node;
        stack_pop(nodes, &operand);
        n->left = operand.node;
    }
    v.node = n;
    status_code sc = stack_push(nodes, v);
    if (sc != OK) {
        tree_free(n);
    }
    return sc;
}

/* Шаг 1: баланс скобок - каждая ')' закрывает последнюю открытую '(' (свой стек) */
status_code check_brackets(const char *s, const size_t length) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    stack open;
    stack_init(&open);
    status_code sc = OK;
    for (size_t i = 0; i < length && sc == OK; i++) {
        stack_value v;
        if (s[i] == '(') {
            v.number = (long long)i;
            sc = stack_push(&open, v);
        } else if (s[i] == ')') {
            sc = open.top == NULL ? UNBALANCED : stack_pop(&open, &v);
        }
    }
    if (sc == OK && open.top != NULL) {
        sc = UNBALANCED;
    }
    stack_clear(&open);
    return sc;
}

/*
 * Шаг 2: дерево выражения (разбор с приоритетами операций - "алгоритм сортировочной станции" на двух стеках:
 * стек операций и стек готовых поддеревьев). Скобки уже проверены. *where - позиция ошибки в строке.
 */
status_code build_tree(const char *s, const size_t length, const int table, expr_node **root, size_t *where) {
    if (s == NULL || root == NULL || where == NULL) {
        return INVALID_ARGS;
    }
    stack operations;
    stack nodes;
    stack_init(&operations);
    stack_init(&nodes);
    size_t pos = 0;
    int expect_operand = 1;   /* сейчас ждём операнд (число, имя, '(' или одноместную операцию) */
    status_code sc = OK;
    int tokens = 0;
    while (sc == OK) {
        token t;
        *where = pos;
        sc = next_token(s, length, &pos, table, &t);
        if (sc != OK) {
            *where = (size_t)(t.start - s);
            break;
        }
        if (t.kind == TOKEN_END) {
            if (tokens == 0) {
                sc = EMPTY_EXPRESSION;
            } else if (expect_operand) {
                sc = OPERAND_EXPECTED;   /* выражение кончилось на операции: "2 +" */
                *where = length;
            }
            break;
        }
        tokens++;
        *where = (size_t)(t.start - s);
        stack_value v;
        if (expect_operand) {
            if (t.kind == TOKEN_NUMBER || t.kind == TOKEN_NAME) {
                expr_node *leaf = NULL;
                sc = leaf_new(&t, &leaf);
                if (sc == OK) {
                    v.node = leaf;
                    sc = stack_push(&nodes, v);
                    if (sc != OK) {
                        tree_free(leaf);
                    }
                }
                expect_operand = 0;
            } else if (t.kind == TOKEN_LPAREN) {
                v.op = OP_LPAREN;
                sc = stack_push(&operations, v);
            } else if (t.kind == TOKEN_OPERATION &&
                       (t.op == OP_NOT || (!table && (t.op == OP_SUB || t.op == OP_ADD)))) {
                /* одноместная: ~ или минус/плюс перед операндом */
                v.op = t.op == OP_SUB ? OP_NEG : (t.op == OP_ADD ? OP_POS : OP_NOT);
                sc = stack_push(&operations, v);
            } else {
                sc = OPERAND_EXPECTED;
            }
            continue;
        }
        /* ждём операцию или ')' */
        if (t.kind == TOKEN_RPAREN) {
            while (sc == OK && operations.top != NULL && operations.top->value.op != OP_LPAREN) {
                sc = reduce(&operations, &nodes);
            }
            if (sc == OK) {
                stack_pop(&operations, &v);   /* '(' - есть: скобки проверены */
            }
        } else if (t.kind == TOKEN_OPERATION && t.op != OP_NOT) {
            int priority = 0;
            int right_assoc = 0;
            int unary = 0;
            const char *text = NULL;
            op_info(t.op, &priority, &right_assoc, &unary, &text);
            /* снимаем операции, которые должны выполниться раньше: выше приоритетом
               или равного приоритета при вычислении слева направо */
            while (sc == OK && operations.top != NULL && operations.top->value.op != OP_LPAREN) {
                int top_priority = 0;
                int top_right = 0;
                int top_unary = 0;
                op_info(operations.top->value.op, &top_priority, &top_right, &top_unary, &text);
                if (top_priority > priority || (top_priority == priority && !right_assoc)) {
                    sc = reduce(&operations, &nodes);
                } else {
                    break;
                }
            }
            if (sc == OK) {
                v.op = t.op;
                sc = stack_push(&operations, v);
            }
            expect_operand = 1;
        } else {
            sc = OPERATOR_EXPECTED;   /* "2 3", "x (", "~" после операнда */
        }
    }
    /* конец: снимаем оставшиеся операции */
    while (sc == OK && operations.top != NULL) {
        sc = reduce(&operations, &nodes);
    }
    if (sc == OK) {
        stack_value v;
        stack_pop(&nodes, &v);
        *root = v.node;
    }
    /* при ошибке - освободить всё, что успели построить */
    while (nodes.top != NULL) {
        stack_value v;
        stack_pop(&nodes, &v);
        tree_free(v.node);
    }
    stack_clear(&operations);
    return sc;
}

/*
 * Шаг 3: обратная польская запись - узлы дерева в порядке обхода "левое, правое, сам" (без рекурсии:
 * обход "сам, правое, левое" стеком даёт обратный порядок, его разворачиваем). Массив освобождает вызывающий.
 */
status_code postorder(expr_node *root, expr_node ***order, size_t *count) {
    if (root == NULL || order == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    size_t capacity = 16;
    size_t n = 0;
    expr_node **list = (expr_node **)malloc(capacity * sizeof(expr_node *));
    if (list == NULL) {
        return ALLOC_ERROR;
    }
    stack s;
    stack_init(&s);
    stack_value v;
    v.node = root;
    status_code sc = stack_push(&s, v);
    while (sc == OK && s.top != NULL) {
        stack_pop(&s, &v);
        if (n == capacity) {
            expr_node **tmp = (expr_node **)realloc(list, capacity * 2 * sizeof(expr_node *));
            if (tmp == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            list = tmp;
            capacity *= 2;
        }
        list[n++] = v.node;
        stack_value child;
        if (v.node->left != NULL) {
            child.node = v.node->left;
            sc = stack_push(&s, child);
        }
        if (sc == OK && v.node->right != NULL) {
            child.node = v.node->right;
            sc = stack_push(&s, child);
        }
    }
    stack_clear(&s);
    if (sc != OK) {
        free(list);
        return sc;
    }
    /* "сам, правое, левое" задом наперёд = "левое, правое, сам" */
    for (size_t i = 0; i < n / 2; i++) {
        expr_node *tmp = list[i];
        list[i] = list[n - 1 - i];
        list[n - 1 - i] = tmp;
    }
    *order = list;
    *count = n;
    return OK;
}

/*
 * Целое число -> текст (out - не меньше 24 символов). Вручную, без printf/snprintf: в сборке mingw
 * Dr. Memory ложно сообщает о чтении неинициализированной памяти внутри них. Цифры - с конца.
 */
status_code number_to_text(const long long value, char *out, const size_t size) {
    if (out == NULL || size < 24) {
        return INVALID_ARGS;
    }
    /* модуль в беззнаковом типе: у LLONG_MIN нет положительной пары в long long */
    unsigned long long magnitude = value < 0 ? 0ULL - (unsigned long long)value : (unsigned long long)value;
    char digits[24];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude > 0);
    size_t k = 0;
    if (value < 0) {
        out[k++] = '-';
    }
    for (size_t i = 0; i < n; i++) {
        out[k++] = digits[n - 1 - i];
    }
    out[k] = '\0';
    return OK;
}

/* Текст одного узла: число, имя или знак операции (в обратной польской записи) */
status_code node_text(const expr_node *node, char *out, const size_t size) {
    if (node == NULL || out == NULL || size < 32) {
        return INVALID_ARGS;
    }
    if (node->kind == NODE_NUMBER) {
        return number_to_text(node->number, out, size);
    }
    if (node->kind == NODE_VARIABLE) {
        if (strlen(node->name) >= size) {
            return INVALID_ARGS;
        }
        strcpy(out, node->name);
        return OK;
    }
    int priority = 0;
    int right_assoc = 0;
    int unary = 0;
    const char *text = NULL;
    op_info(node->op, &priority, &right_assoc, &unary, &text);
    strcpy(out, text);
    return OK;
}

/* Растущая строка */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} text_buffer;

status_code text_append(text_buffer *t, const char *s) {
    if (t == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    size_t n = strlen(s);
    if (t->data == NULL || t->length + n + 1 > t->capacity) {
        size_t capacity = t->capacity == 0 ? 64 : t->capacity;
        while (t->length + n + 1 > capacity) {
            capacity *= 2;
        }
        char *tmp = (char *)realloc(t->data, capacity);
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        t->data = tmp;
        t->capacity = capacity;
    }
    memcpy(t->data + t->length, s, n + 1);
    t->length += n;
    return OK;
}

/* Обратная польская запись строкой: элементы через пробел */
status_code postfix_text(expr_node *const *order, const size_t count, char **result) {
    if (order == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    text_buffer t = {NULL, 0, 0};
    status_code sc = OK;
    for (size_t i = 0; i < count && sc == OK; i++) {
        char *piece = (char *)malloc(order[i]->kind == NODE_VARIABLE ? strlen(order[i]->name) + 32 : 32);
        if (piece == NULL) {
            sc = ALLOC_ERROR;
            break;
        }
        sc = node_text(order[i], piece, order[i]->kind == NODE_VARIABLE ? strlen(order[i]->name) + 32 : 32);
        if (sc == OK && i > 0) {
            sc = text_append(&t, " ");
        }
        if (sc == OK) {
            sc = text_append(&t, piece);
        }
        free(piece);
    }
    if (sc != OK) {
        free(t.data);
        return sc;
    }
    *result = t.data;
    return OK;
}

/*
 * Дерево строкой со всеми скобками: (2 + (3 * x)), (-x), (~A). Строится по обратной польской записи
 * стеком строк: лист - его текст; операция - склейка текстов операндов.
 */
status_code tree_text(expr_node *const *order, const size_t count, char **result) {
    if (order == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    stack s;
    stack_init(&s);
    status_code sc = OK;
    for (size_t i = 0; i < count && sc == OK; i++) {
        const expr_node *node = order[i];
        text_buffer t = {NULL, 0, 0};
        char piece[64];
        if (node->kind != NODE_OPERATION) {
            if (node->kind == NODE_VARIABLE) {
                sc = text_append(&t, node->name);
            } else {
                node_text(node, piece, sizeof(piece));
                sc = text_append(&t, piece);
            }
        } else {
            int priority = 0;
            int right_assoc = 0;
            int unary = 0;
            const char *op = NULL;
            op_info(node->op, &priority, &right_assoc, &unary, &op);
            stack_value a;
            stack_value b;
            if (unary) {
                stack_pop(&s, &a);
                /* одноместные: (-x), (+x), (~A) */
                const char *sign = node->op == OP_NEG ? "-" : (node->op == OP_POS ? "+" : "~");
                sc = text_append(&t, "(");
                if (sc == OK) sc = text_append(&t, sign);
                if (sc == OK) sc = text_append(&t, a.text);
                if (sc == OK) sc = text_append(&t, ")");
                free(a.text);
            } else {
                stack_pop(&s, &b);
                stack_pop(&s, &a);
                sc = text_append(&t, "(");
                if (sc == OK) sc = text_append(&t, a.text);
                if (sc == OK) sc = text_append(&t, " ");
                if (sc == OK) sc = text_append(&t, op);
                if (sc == OK) sc = text_append(&t, " ");
                if (sc == OK) sc = text_append(&t, b.text);
                if (sc == OK) sc = text_append(&t, ")");
                free(a.text);
                free(b.text);
            }
        }
        if (sc == OK) {
            stack_value v;
            v.text = t.data;
            sc = stack_push(&s, v);
        }
        if (sc != OK) {
            free(t.data);
        }
    }
    stack_value v;
    v.text = NULL;
    if (sc == OK) {
        stack_pop(&s, &v);
        *result = v.text;
    }
    while (s.top != NULL) {
        stack_pop(&s, &v);
        free(v.text);
    }
    return sc;
}

/* ===================== переменные (--calculate) ===================== */

status_code variables_free(variables *vars) {
    if (vars == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < vars->count; i++) {
        free(vars->names[i]);
    }
    free(vars->names);
    free(vars->values);
    vars->names = NULL;
    vars->values = NULL;
    vars->count = 0;
    vars->capacity = 0;
    return OK;
}

status_code variables_find(const variables *vars, const char *name, long long *value, int *found) {
    if (vars == NULL || name == NULL || value == NULL || found == NULL) {
        return INVALID_ARGS;
    }
    *found = 0;
    for (size_t i = 0; i < vars->count; i++) {
        if (strcmp(vars->names[i], name) == 0) {
            *value = vars->values[i];
            *found = 1;
            break;
        }
    }
    return OK;
}

status_code variables_add(variables *vars, const char *name, const long long value) {
    if (vars == NULL || name == NULL) {
        return INVALID_ARGS;
    }
    if (vars->count == vars->capacity) {
        size_t capacity = vars->capacity == 0 ? 4 : vars->capacity * 2;
        char **names = (char **)realloc(vars->names, capacity * sizeof(char *));
        if (names == NULL) {
            return ALLOC_ERROR;
        }
        vars->names = names;
        long long *values = (long long *)realloc(vars->values, capacity * sizeof(long long));
        if (values == NULL) {
            return ALLOC_ERROR;
        }
        vars->values = values;
        vars->capacity = capacity;
    }
    char *copy = (char *)malloc(strlen(name) + 1);
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    strcpy(copy, name);
    vars->names[vars->count] = copy;
    vars->values[vars->count] = value;
    vars->count++;
    return OK;
}

/* Прочитать со стандартного ввода значение переменной: слово до пробела -> целое число */
status_code ask_value(FILE *input, const int interactive, const char *name, long long *value) {
    if (input == NULL || name == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    if (interactive) {
        fprintf(stderr, "%s = ", name);
    }
    char word[32];
    size_t n = 0;
    int too_long = 0;
    int ch = fgetc(input);
    while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ch = fgetc(input);
    }
    if (ch == EOF) {
        return NO_VALUE;
    }
    while (ch != EOF && ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
        if (n + 1 < sizeof(word)) {
            word[n++] = (char)ch;
        } else {
            too_long = 1;
        }
        ch = fgetc(input);
    }
    word[n] = '\0';
    if (too_long) {
        return WRONG_VALUE;
    }
    /* знак и цифры, переполнение - до умножения (копим отрицательное: у LLONG_MIN нет пары) */
    size_t i = (word[0] == '-' || word[0] == '+') ? 1 : 0;
    if (word[i] == '\0') {
        return WRONG_VALUE;
    }
    long long result = 0;
    for (; word[i] != '\0'; i++) {
        if (word[i] < '0' || word[i] > '9') {
            return WRONG_VALUE;
        }
        int digit = word[i] - '0';
        if (result < (LLONG_MIN + digit) / 10) {
            return WRONG_VALUE;
        }
        result = result * 10 - digit;
    }
    if (word[0] != '-') {
        if (result == LLONG_MIN) {
            return WRONG_VALUE;
        }
        result = -result;
    }
    *value = result;
    return OK;
}

/* Шаг 4: значение по обратной польской записи (стек чисел). *bad - имя переменной без значения */
status_code evaluate_postfix(expr_node *const *order, const size_t count, variables *vars, FILE *input,
                             const int interactive, long long *value, const char **bad) {
    if (order == NULL || vars == NULL || input == NULL || value == NULL || bad == NULL) {
        return INVALID_ARGS;
    }
    stack s;
    stack_init(&s);
    status_code sc = OK;
    for (size_t i = 0; i < count && sc == OK; i++) {
        const expr_node *node = order[i];
        stack_value v;
        if (node->kind == NODE_NUMBER) {
            v.number = node->number;
        } else if (node->kind == NODE_VARIABLE) {
            int found = 0;
            variables_find(vars, node->name, &v.number, &found);
            if (!found) {
                sc = ask_value(input, interactive, node->name, &v.number);
                if (sc == OK) {
                    sc = variables_add(vars, node->name, v.number);
                } else {
                    *bad = node->name;
                }
            }
        } else {
            int priority = 0;
            int right_assoc = 0;
            int unary = 0;
            const char *text = NULL;
            op_info(node->op, &priority, &right_assoc, &unary, &text);
            stack_value a;
            stack_value b;
            b.number = 0;
            if (!unary) {
                stack_pop(&s, &b);
            }
            stack_pop(&s, &a);
            sc = calc_apply(node->op, a.number, b.number, &v.number);
        }
        if (sc == OK) {
            sc = stack_push(&s, v);
        }
    }
    if (sc == OK) {
        stack_value v;
        stack_pop(&s, &v);
        *value = v.number;
    }
    stack_clear(&s);
    return sc;
}

/* Значение булевой формулы на наборе: значения переменных - по буквам (values['A'..'z']) */
status_code evaluate_bool(expr_node *const *order, const size_t count, const int *values, int *result) {
    if (order == NULL || values == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    stack s;
    stack_init(&s);
    status_code sc = OK;
    for (size_t i = 0; i < count && sc == OK; i++) {
        const expr_node *node = order[i];
        stack_value v;
        if (node->kind == NODE_NUMBER) {
            v.number = node->number;
        } else if (node->kind == NODE_VARIABLE) {
            v.number = values[(unsigned char)node->name[0]];
        } else {
            int priority = 0;
            int right_assoc = 0;
            int unary = 0;
            const char *text = NULL;
            op_info(node->op, &priority, &right_assoc, &unary, &text);
            stack_value a;
            stack_value b;
            b.number = 0;
            if (!unary) {
                stack_pop(&s, &b);
            }
            stack_pop(&s, &a);
            int r = 0;
            sc = bool_apply(node->op, (int)a.number, (int)b.number, &r);
            v.number = r;
        }
        if (sc == OK) {
            sc = stack_push(&s, v);
        }
    }
    if (sc == OK) {
        stack_value v;
        stack_pop(&s, &v);
        *result = (int)v.number;
    }
    stack_clear(&s);
    return sc;
}

/* ===================== сообщения ===================== */

status_code error_text(const status_code sc, const char **text) {
    if (text == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case UNBALANCED:         *text = "unbalanced brackets"; break;
        case UNKNOWN_SYMBOL:     *text = "unknown symbol"; break;
        case OPERAND_EXPECTED:   *text = "an operand is expected"; break;
        case OPERATOR_EXPECTED:  *text = "an operation is expected"; break;
        case EMPTY_EXPRESSION:   *text = "empty expression"; break;
        case NUMBER_TOO_BIG:     *text = "a number is too big"; break;
        case DIVISION_BY_ZERO:   *text = "division by zero"; break;
        case NEGATIVE_POWER:     *text = "negative power"; break;
        case OVERFLOW_ERROR:     *text = "overflow (the result does not fit in long long)"; break;
        case NO_VALUE:           *text = "no value for the variable"; break;
        case WRONG_VALUE:        *text = "wrong value for the variable"; break;
        case TOO_MANY_VARIABLES: *text = "too many variables (more than 16)"; break;
        default:                 *text = "internal error"; break;   /* другие статусы сюда не попадают */
    }
    return OK;
}

/* Полная причина ошибки: текст статуса и уточнение (позиция или имя переменной) */
status_code describe_error(const status_code sc, const size_t where, const char *name, char *out, const size_t size) {
    if (out == NULL || size < 128) {
        return INVALID_ARGS;
    }
    const char *text = NULL;
    error_text(sc, &text);
    strcpy(out, text);
    if ((sc == NO_VALUE || sc == WRONG_VALUE) && name != NULL && strlen(name) + strlen(out) + 2 < size) {
        strcat(out, " ");
        strcat(out, name);
    } else if (sc == UNKNOWN_SYMBOL || sc == OPERAND_EXPECTED || sc == OPERATOR_EXPECTED || sc == NUMBER_TOO_BIG) {
        /* позиция - номер символа с 1 */
        char number[24];
        number_to_text((long long)where + 1, number, sizeof(number));
        strcat(out, " at position ");
        strcat(out, number);
    }
    return OK;
}

/* ===================== файлы ===================== */

/* Прочитать строку любой длины (без '\n'); *end = 1 - файл кончился */
status_code read_line(FILE *file, char **line, size_t *length, int *end) {
    if (file == NULL || line == NULL || length == NULL || end == NULL) {
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
        n--;   /* конец строки Windows */
    }
    buf[n] = '\0';
    if (ferror(file)) {
        free(buf);
        return FILE_ERROR;
    }
    *line = buf;
    *length = n;
    return OK;
}

/* Строка из одних пробелов? */
status_code is_blank(const char *s, const size_t length, int *blank) {
    if (s == NULL || blank == NULL) {
        return INVALID_ARGS;
    }
    *blank = 1;
    for (size_t i = 0; i < length; i++) {
        if (s[i] != ' ' && s[i] != '\t') {
            *blank = 0;
            break;
        }
    }
    return OK;
}

/* Имя файла ошибок: "dir/name.txt" -> "dir/name_errors.txt" (без .txt - просто дописываем) */
status_code errors_path(const char *path, char **result) {
    if (path == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t n = strlen(path);
    size_t base = (n >= 4 && strcmp(path + n - 4, ".txt") == 0) ? n - 4 : n;
    char *out = (char *)malloc(base + strlen("_errors.txt") + 1);
    if (out == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(out, path, base);
    strcpy(out + base, "_errors.txt");
    *result = out;
    return OK;
}

/* Разобранное выражение: дерево и порядок обхода */
typedef struct {
    expr_node *root;
    expr_node **order;
    size_t count;
} parsed;

/* Шаги 1-3 для одной строки. *where - позиция ошибки */
status_code parse_expression(const char *line, const size_t length, const int table, parsed *p, size_t *where) {
    if (line == NULL || p == NULL || where == NULL) {
        return INVALID_ARGS;
    }
    p->root = NULL;
    p->order = NULL;
    p->count = 0;
    *where = 0;
    status_code sc = check_brackets(line, length);
    if (sc == OK) {
        sc = build_tree(line, length, table, &p->root, where);
    }
    if (sc == OK) {
        sc = postorder(p->root, &p->order, &p->count);
        if (sc != OK) {
            tree_free(p->root);
            p->root = NULL;
        }
    }
    return sc;
}

status_code parsed_free(parsed *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    tree_free(p->root);
    free(p->order);
    p->root = NULL;
    p->order = NULL;
    p->count = 0;
    return OK;
}

/* --calculate для одного файла: печать в out, ошибки - в файл <имя>_errors.txt */
status_code calculate_file(const char *path, variables *vars, const int interactive, FILE *out) {
    if (path == NULL || vars == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return FILE_ERROR;
    }
    fprintf(out, "File: %s\n", path);
    FILE *errors = NULL;          /* файл ошибок открывается при первой ошибке */
    char *error_file = NULL;
    size_t error_count = 0;
    status_code sc = OK;
    size_t index = 0;
    while (sc == OK) {
        char *line = NULL;
        size_t length = 0;
        int end = 0;
        sc = read_line(file, &line, &length, &end);
        if (sc != OK || end) {
            free(line);
            break;
        }
        int blank = 0;
        is_blank(line, length, &blank);
        if (!blank) {
            fprintf(out, "%zu: %s\n", index, line);
            parsed p;
            size_t where = 0;
            status_code result = parse_expression(line, length, 0, &p, &where);
            const char *bad = NULL;
            char *tree = NULL;
            char *postfix = NULL;
            long long value = 0;
            if (result == OK) {
                result = tree_text(p.order, p.count, &tree);
            }
            if (result == OK) {
                result = postfix_text(p.order, p.count, &postfix);
            }
            if (result == OK) {
                fprintf(out, "   tree: %s\n   postfix: %s\n", tree, postfix);
                fflush(out);
                result = evaluate_postfix(p.order, p.count, vars, stdin, interactive, &value, &bad);
            }
            if (result == OK) {
                char number[24];
                number_to_text(value, number, sizeof(number));
                fprintf(out, "   value: %s\n", number);
            } else if (result == ALLOC_ERROR) {
                sc = result;
            } else {
                char reason[256];
                describe_error(result, where, bad, reason, sizeof(reason));
                fprintf(out, "   error: %s\n", reason);
                if (errors == NULL) {
                    sc = errors_path(path, &error_file);
                    if (sc == OK) {
                        errors = fopen(error_file, "w");
                        if (errors == NULL) {
                            sc = FILE_ERROR;
                        }
                    }
                }
                if (errors != NULL) {
                    /* записи разделяются пустой строкой (перед каждой, кроме первой) */
                    fprintf(errors, "%sexpression: %s\nindex: %zu\nreason: %s\n", error_count > 0 ? "\n" : "", line,
                            index, reason);
                    error_count++;
                }
            }
            free(tree);
            free(postfix);
            parsed_free(&p);
        }
        free(line);
        index++;
    }
    fclose(file);
    if (errors != NULL) {
        if (fclose(errors) != 0 && sc == OK) {
            sc = FILE_ERROR;
        }
        if (sc == OK) {
            fprintf(out, "Errors are written to %s\n", error_file);
        }
    }
    free(error_file);
    return sc;
}

/* Следующее псевдослучайное число: xorshift64 */
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

/* Путь к файлу таблиц: папка исходного файла + случайное имя из букв и цифр + ".txt" (такого файла ещё нет) */
status_code table_path(const char *source, unsigned long long *state, char **result) {
    if (source == NULL || state == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    size_t dir = strlen(source);
    while (dir > 0 && source[dir - 1] != '/' && source[dir - 1] != '\\') {
        dir--;
    }
    char *out = (char *)malloc(dir + NAME_LENGTH + 5);
    if (out == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(out, source, dir);
    for (int attempt = 0; attempt < 100; attempt++) {
        for (int i = 0; i < NAME_LENGTH; i++) {
            unsigned long long r = 0;
            next_random(state, &r);
            out[dir + i] = alphabet[r % 62];
        }
        strcpy(out + dir + NAME_LENGTH, ".txt");
        FILE *exists = fopen(out, "r");
        if (exists == NULL) {
            *result = out;
            return OK;
        }
        fclose(exists);   /* такой файл уже есть - пробуем другое имя */
    }
    free(out);
    return FILE_ERROR;
}

/* Таблица истинности одной формулы - в файл table */
status_code write_table(FILE *table, const parsed *p) {
    if (table == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    /* переменные формулы по алфавиту (по коду символа: сначала A-Z, потом a-z) */
    int used[256] = {0};
    for (size_t i = 0; i < p->count; i++) {
        if (p->order[i]->kind == NODE_VARIABLE) {
            used[(unsigned char)p->order[i]->name[0]] = 1;
        }
    }
    char names[MAX_TABLE_VARIABLES];
    int n = 0;
    for (int c = 0; c < 256; c++) {
        if (used[c]) {
            if (n == MAX_TABLE_VARIABLES) {
                return TOO_MANY_VARIABLES;
            }
            names[n++] = (char)c;
        }
    }
    for (int i = 0; i < n; i++) {
        fprintf(table, "%c ", names[i]);
    }
    fprintf(table, "| result\n");
    /* наборы по порядку: число mask от 0 до 2^n - 1, первая переменная - старший бит */
    int values[256] = {0};
    for (unsigned long mask = 0; mask < (1UL << n); mask++) {
        for (int i = 0; i < n; i++) {
            values[(unsigned char)names[i]] = (int)((mask >> (n - 1 - i)) & 1UL);
            fprintf(table, "%d ", values[(unsigned char)names[i]]);
        }
        int result = 0;
        status_code sc = evaluate_bool(p->order, p->count, values, &result);
        if (sc != OK) {
            return sc;
        }
        fprintf(table, "| %d\n", result);
    }
    return OK;
}

/* --table для одного файла: все таблицы - в один файл со случайным именем */
status_code table_file(const char *path, unsigned long long *state, FILE *out) {
    if (path == NULL || state == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return FILE_ERROR;
    }
    char *result_path = NULL;
    status_code sc = table_path(path, state, &result_path);
    FILE *table = NULL;
    if (sc == OK) {
        table = fopen(result_path, "w");
        if (table == NULL) {
            sc = FILE_ERROR;
        }
    }
    size_t index = 0;
    size_t formulas = 0;
    while (sc == OK) {
        char *line = NULL;
        size_t length = 0;
        int end = 0;
        sc = read_line(file, &line, &length, &end);
        if (sc != OK || end) {
            free(line);
            break;
        }
        int blank = 0;
        is_blank(line, length, &blank);
        if (!blank) {
            /* формулы разделяются пустой строкой (перед каждой, кроме первой) */
            fprintf(table, "%sFormula %zu: %s\n", formulas > 0 ? "\n" : "", index, line);
            formulas++;
            parsed p;
            size_t where = 0;
            status_code result = parse_expression(line, length, 1, &p, &where);
            char *tree = NULL;
            if (result == OK) {
                result = tree_text(p.order, p.count, &tree);
            }
            if (result == OK) {
                fprintf(table, "tree: %s\n", tree);
                result = write_table(table, &p);
            }
            if (result == ALLOC_ERROR) {
                sc = result;
            } else if (result != OK) {
                char reason[256];
                describe_error(result, where, NULL, reason, sizeof(reason));
                fprintf(table, "error: %s\n", reason);
            }
            free(tree);
            parsed_free(&p);
        }
        free(line);
        index++;
    }
    fclose(file);
    if (table != NULL && fclose(table) != 0 && sc == OK) {
        sc = FILE_ERROR;
    }
    if (sc == OK) {
        fprintf(out, "Truth tables for %s: %s\n", path, result_path);
    }
    free(result_path);
    return sc;
}

int main(int argc, char *argv[]) {
    /* ключ режима (ровно один), необязательный --seed N, затем файлы */
    int mode = 0;   /* 1 - --calculate, 2 - --table */
    int wrong = 0;
    int files = 0;
    unsigned long long seed = (unsigned long long)time(NULL);
    int has_seed = 0;
    for (int i = 1; i < argc && !wrong; i++) {
        if (strcmp(argv[i], "--calculate") == 0 || strcmp(argv[i], "--table") == 0) {
            wrong = mode != 0;
            mode = argv[i][2] == 'c' ? 1 : 2;
        } else if (strcmp(argv[i], "--seed") == 0) {
            if (has_seed || i + 1 >= argc) {
                wrong = 1;
                break;
            }
            const char *s = argv[++i];
            unsigned long long v = 0;
            wrong = s[0] == '\0';
            for (size_t k = 0; s[k] != '\0' && !wrong; k++) {
                if (s[k] < '0' || s[k] > '9' || v > (~0ULL - 9) / 10) {
                    wrong = 1;
                } else {
                    v = v * 10 + (unsigned long long)(s[k] - '0');
                }
            }
            seed = v;
            has_seed = 1;
        } else {
            files++;
        }
    }
    if (wrong || mode == 0 || files == 0 || (has_seed && mode != 2)) {
        fprintf(stderr, "Usage: lab4_task5 --calculate <file> [<file> ...]\n");
        fprintf(stderr, "       lab4_task5 --table [--seed N] <file> [<file> ...]\n");
        return 1;
    }

#ifdef _WIN32
    int interactive = _isatty(_fileno(stdin));
#else
    int interactive = isatty(fileno(stdin));
#endif
    unsigned long long state = seed ^ 0x9E3779B97F4A7C15ULL;
    if (state == 0) {
        state = 1;
    }
    variables vars = {NULL, NULL, 0, 0};
    int failed = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--calculate") == 0 || strcmp(argv[i], "--table") == 0) {
            continue;
        }
        if (strcmp(argv[i], "--seed") == 0) {
            i++;
            continue;
        }
        status_code sc = mode == 1 ? calculate_file(argv[i], &vars, interactive, stdout)
                                   : table_file(argv[i], &state, stdout);
        fflush(stdout);
        if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not read or write files for '%s'\n", argv[i]);
            failed = 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: not enough memory\n");
            failed = 1;
            break;
        }
    }
    variables_free(&vars);
    return failed;
}
