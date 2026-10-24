/*
 * Лабораторная 3, задание 8. Многочлены с целыми коэффициентами на односвязном списке.
 *
 * Многочлен - список членов (степень, коэффициент) по убыванию степени, без нулевых коэффициентов;
 * нулевой многочлен - пустой список. Операции: сложение, вычитание, умножение, целочисленное деление,
 * остаток, значение в точке, производная, композиция.
 *
 * Запуск:  lab3_task8 <файл с инструкциями>
 * Инструкции (с учетом регистра), каждая кончается ';', может занимать несколько строк:
 *     Add(f, g)   Sub(f, g)   Mult(f, g)   Div(f, g)   Mod(f, g)   Cmps(f, g)   - f + g, f - g, f * g,
 *                                                         частное, остаток, f(g(x))
 *     Eval(f, точка)   - значение f в целой точке
 *     Diff(f)          - производная
 * Если параметров на один меньше, вместо первого берется сумматор: Add(g) = сумматор + g,
 * Eval(5) - значение сумматора в 5, Diff() - производная сумматора. Сумматор вначале 0.
 * Результат-многочлен печатается и сохраняется в сумматор; Eval печатает число, сумматор не меняет.
 * Комментарии: % - до конца строки; [ ... ] - на несколько строк, вложенные [ ] запрещены.
 * Запись многочлена: 2x^2-x+2, -x^3+5, 7, x^10-x; переменная - x, коэффициенты и степени - целые.
 *
 * Целочисленное деление: на каждом шаге старший коэффициент делимого должен делиться нацело на
 * старший коэффициент делителя (x^5 / (x^2 - 1) - можно; x^2 / 2x - нет: получилось бы x/2) -
 * иначе ошибка. Тогда f = q * g + r, где у r степень меньше степени g.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* strcmp, memcpy */
#include <limits.h>   /* LLONG_MAX, LLONG_MIN */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace, isdigit, isalpha */

#define MAX_DEGREE 100000   /* наибольшая степень: дальше многочлены слишком долго перемножать */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,       /* NULL или неправильные данные в функции */
    OVERFLOW_ERROR,     /* коэффициент или значение не помещается в long long */
    DEGREE_TOO_BIG,     /* степень больше MAX_DEGREE */
    DIVISION_BY_ZERO,   /* деление на нулевой многочлен */
    NOT_DIVISIBLE,      /* старший коэффициент не делится нацело */
    SYNTAX_ERROR,       /* ошибка в записи файла */
    FILE_ERROR,         /* ошибка чтения файла */
    ALLOC_ERROR         /* malloc / realloc не смогли выделить память */
} status_code;

/* Член многочлена: coefficient * x^degree */
typedef struct term {
    long long degree;
    long long coefficient;
    struct term *next;
} term;

typedef struct {
    term *head;   /* старший член; NULL - нулевой многочлен */
} polynomial;

/* ===================== арифметика long long с проверкой переполнения ===================== */

status_code safe_add(const long long a, const long long b, long long *sum) {
    if (sum == NULL) {
        return INVALID_ARGS;
    }
    if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
        return OVERFLOW_ERROR;
    }
    *sum = a + b;
    return OK;
}

status_code safe_multiply(const long long a, const long long b, long long *product) {
    if (product == NULL) {
        return INVALID_ARGS;
    }
    if (a == 0 || b == 0) {
        *product = 0;
        return OK;
    }
    /* |a * b| <= LLONG_MAX проверяем делением; LLONG_MIN отдельно: у него нет положительной пары */
    if ((a == -1 && b == LLONG_MIN) || (b == -1 && a == LLONG_MIN)) {
        return OVERFLOW_ERROR;
    }
    if (a > 0 ? (b > 0 ? a > LLONG_MAX / b : b < LLONG_MIN / a)
              : (b > 0 ? a < LLONG_MIN / b : a < LLONG_MAX / b)) {
        return OVERFLOW_ERROR;
    }
    *product = a * b;
    return OK;
}

/* ===================== многочлен: память и члены ===================== */

status_code poly_init(polynomial *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    p->head = NULL;
    return OK;
}

status_code poly_clear(polynomial *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    while (p->head != NULL) {
        term *next = p->head->next;
        free(p->head);
        p->head = next;
    }
    return OK;
}

/*
 * Прибавить к многочлену член coefficient * x^degree. Список остается упорядоченным по убыванию
 * степени: член с такой степенью - складываем коэффициенты (ноль - удаляем член), иначе вставляем новый.
 */
status_code poly_add_term(polynomial *p, const long long coefficient, const long long degree) {
    if (p == NULL || degree < 0) {
        return INVALID_ARGS;
    }
    if (degree > MAX_DEGREE) {
        return DEGREE_TOO_BIG;
    }
    if (coefficient == 0) {
        return OK;
    }
    /* link - указатель на поле next предыдущего члена (или на голову) */
    term **link = &p->head;
    while (*link != NULL && (*link)->degree > degree) {
        link = &(*link)->next;
    }
    if (*link != NULL && (*link)->degree == degree) {
        long long sum = 0;
        status_code sc = safe_add((*link)->coefficient, coefficient, &sum);
        if (sc != OK) {
            return sc;
        }
        if (sum == 0) {
            term *zero = *link;
            *link = zero->next;
            free(zero);
        } else {
            (*link)->coefficient = sum;
        }
        return OK;
    }
    term *node = (term *)malloc(sizeof(term));
    if (node == NULL) {
        return ALLOC_ERROR;
    }
    node->degree = degree;
    node->coefficient = coefficient;
    node->next = *link;
    *link = node;
    return OK;
}

/* Копия многочлена (результат пишется в пустой dst) */
status_code poly_copy(const polynomial *src, polynomial *dst) {
    if (src == NULL || dst == NULL) {
        return INVALID_ARGS;
    }
    poly_init(dst);
    term **tail = &dst->head;
    for (const term *t = src->head; t != NULL; t = t->next) {
        term *node = (term *)malloc(sizeof(term));
        if (node == NULL) {
            poly_clear(dst);
            return ALLOC_ERROR;
        }
        node->degree = t->degree;
        node->coefficient = t->coefficient;
        node->next = NULL;
        *tail = node;
        tail = &node->next;
    }
    return OK;
}

/* ===================== операции (результат - новый многочлен в *result) ===================== */

/* a + sign * b, sign = 1 или -1 */
status_code poly_add_signed(const polynomial *a, const polynomial *b, const int sign, polynomial *result) {
    if (a == NULL || b == NULL || result == NULL || (sign != 1 && sign != -1)) {
        return INVALID_ARGS;
    }
    status_code sc = poly_copy(a, result);
    for (const term *t = b->head; t != NULL && sc == OK; t = t->next) {
        long long c = 0;
        sc = safe_multiply(t->coefficient, sign, &c);
        if (sc == OK) {
            sc = poly_add_term(result, c, t->degree);
        }
    }
    if (sc != OK) {
        poly_clear(result);
    }
    return sc;
}

status_code poly_add(const polynomial *a, const polynomial *b, polynomial *result) {
    return poly_add_signed(a, b, 1, result);
}

status_code poly_subtract(const polynomial *a, const polynomial *b, polynomial *result) {
    return poly_add_signed(a, b, -1, result);
}

/* Произведение: каждый член a на каждый член b, одинаковые степени складываются */
status_code poly_multiply(const polynomial *a, const polynomial *b, polynomial *result) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    poly_init(result);
    status_code sc = OK;
    for (const term *x = a->head; x != NULL && sc == OK; x = x->next) {
        for (const term *y = b->head; y != NULL && sc == OK; y = y->next) {
            long long c = 0;
            sc = safe_multiply(x->coefficient, y->coefficient, &c);
            if (sc == OK) {
                /* степени не больше MAX_DEGREE каждая - их сумма помещается в long long */
                sc = poly_add_term(result, c, x->degree + y->degree);
            }
        }
    }
    if (sc != OK) {
        poly_clear(result);
    }
    return sc;
}

/*
 * Деление с остатком: a = quotient * b + remainder, степень остатка меньше степени b.
 * Шаг: старший член остатка / старший член b = c * x^d; quotient += c x^d; remainder -= c x^d * b.
 * Старший член остатка при этом сокращается, степень убывает - цикл конечен.
 */
status_code poly_divide(const polynomial *a, const polynomial *b, polynomial *quotient, polynomial *remainder) {
    if (a == NULL || b == NULL || quotient == NULL || remainder == NULL) {
        return INVALID_ARGS;
    }
    if (b->head == NULL) {
        return DIVISION_BY_ZERO;
    }
    poly_init(quotient);
    status_code sc = poly_copy(a, remainder);
    const term *lead = b->head;
    while (sc == OK && remainder->head != NULL && remainder->head->degree >= lead->degree) {
        long long top = remainder->head->coefficient;
        if (top % lead->coefficient != 0) {
            sc = NOT_DIVISIBLE;
            break;
        }
        /* LLONG_MIN / -1 не помещается в long long */
        if (top == LLONG_MIN && lead->coefficient == -1) {
            sc = OVERFLOW_ERROR;
            break;
        }
        long long c = top / lead->coefficient;
        long long d = remainder->head->degree - lead->degree;
        sc = poly_add_term(quotient, c, d);
        /* remainder -= c * x^d * b - по членам b */
        for (const term *t = b->head; t != NULL && sc == OK; t = t->next) {
            long long product = 0;
            sc = safe_multiply(c, t->coefficient, &product);
            if (sc == OK) {
                sc = safe_multiply(product, -1, &product);
            }
            if (sc == OK) {
                sc = poly_add_term(remainder, product, t->degree + d);
            }
        }
    }
    if (sc != OK) {
        poly_clear(quotient);
        poly_clear(remainder);
    }
    return sc;
}

/* base^exponent для целых с проверкой переполнения (быстрое возведение: квадраты) */
status_code power(const long long base, long long exponent, long long *result) {
    if (result == NULL || exponent < 0) {
        return INVALID_ARGS;
    }
    long long r = 1;
    long long b = base;
    status_code sc = OK;
    while (exponent > 0 && sc == OK) {
        if (exponent % 2 == 1) {
            sc = safe_multiply(r, b, &r);
        }
        exponent /= 2;
        if (exponent > 0 && sc == OK) {
            sc = safe_multiply(b, b, &b);
        }
    }
    if (sc == OK) {
        *result = r;
    }
    return sc;
}

/* Значение многочлена в целой точке x: сумма coefficient * x^degree */
status_code poly_evaluate(const polynomial *p, const long long x, long long *value) {
    if (p == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    long long sum = 0;
    for (const term *t = p->head; t != NULL; t = t->next) {
        long long xp = 0;
        long long product = 0;
        status_code sc = power(x, t->degree, &xp);
        if (sc == OK) {
            sc = safe_multiply(t->coefficient, xp, &product);
        }
        if (sc == OK) {
            sc = safe_add(sum, product, &sum);
        }
        if (sc != OK) {
            return sc;
        }
    }
    *value = sum;
    return OK;
}

/* Производная: (c x^d)' = c*d x^(d-1); у свободного члена производная 0 */
status_code poly_derivative(const polynomial *p, polynomial *result) {
    if (p == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    poly_init(result);
    status_code sc = OK;
    for (const term *t = p->head; t != NULL && sc == OK; t = t->next) {
        if (t->degree == 0) {
            continue;
        }
        long long c = 0;
        sc = safe_multiply(t->coefficient, t->degree, &c);
        if (sc == OK) {
            sc = poly_add_term(result, c, t->degree - 1);
        }
    }
    if (sc != OK) {
        poly_clear(result);
    }
    return sc;
}

/*
 * Композиция f(g(x)) по схеме Горнера: f = (...((c_n) g^k1 + c_m) g^k2 + ...).
 * Идем по членам f от старшего; между соседними степенями d1 > d2 результат умножается на g^(d1-d2).
 */
status_code poly_compose(const polynomial *f, const polynomial *g, polynomial *result) {
    if (f == NULL || g == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    poly_init(result);
    if (f->head == NULL) {
        return OK;
    }
    /* степень результата = deg f * deg g - проверяем заранее */
    long long deg_g = g->head == NULL ? 0 : g->head->degree;
    if (deg_g > 0 && f->head->degree > MAX_DEGREE / deg_g) {
        return DEGREE_TOO_BIG;
    }
    status_code sc = OK;
    for (const term *t = f->head; t != NULL && sc == OK; t = t->next) {
        /* result = result * g^(разница степеней) + коэффициент; у первого члена result еще 0 */
        long long gap = t->next != NULL ? t->degree - t->next->degree : t->degree;
        sc = poly_add_term(result, t->coefficient, 0);
        for (long long i = 0; i < gap && sc == OK; i++) {
            polynomial next;
            sc = poly_multiply(result, g, &next);
            if (sc == OK) {
                poly_clear(result);
                *result = next;
            }
        }
    }
    if (sc != OK) {
        poly_clear(result);
    }
    return sc;
}

/* ===================== запись и разбор многочлена ===================== */

/* Печать: 2x^2 - x + 2, -x^3, 0. Коэффициент 1 у x не пишется, x^1 пишется как x */
status_code poly_print(FILE *out, const polynomial *p) {
    if (out == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    if (p->head == NULL) {
        fprintf(out, "0");
        return OK;
    }
    for (const term *t = p->head; t != NULL; t = t->next) {
        long long c = t->coefficient;
        int negative = c < 0;
        /* знак: у первого члена "-" вплотную, у остальных " - " / " + " */
        if (t == p->head) {
            if (negative) {
                fputc('-', out);
            }
        } else {
            fputs(negative ? " - " : " + ", out);
        }
        /* модуль коэффициента: для LLONG_MIN модуль не помещается в long long - печатаем через unsigned */
        unsigned long long magnitude = negative ? (unsigned long long)(-(c + 1)) + 1 : (unsigned long long)c;
        if (magnitude != 1 || t->degree == 0) {
            fprintf(out, "%llu", magnitude);
        }
        if (t->degree >= 1) {
            fprintf(out, "x");
        }
        if (t->degree >= 2) {
            fprintf(out, "^%lld", t->degree);
        }
    }
    return OK;
}

/* Целое без знака из text начиная с *pos (только цифры, хотя бы одна); *pos сдвигается */
status_code parse_unsigned(const char *text, size_t *pos, long long *number) {
    if (text == NULL || pos == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    if (!isdigit((unsigned char)text[*pos])) {
        return SYNTAX_ERROR;
    }
    long long value = 0;
    while (isdigit((unsigned char)text[*pos])) {
        int digit = text[*pos] - '0';
        if (value > (LLONG_MAX - digit) / 10) {
            return OVERFLOW_ERROR;
        }
        value = value * 10 + digit;
        (*pos)++;
    }
    *number = value;
    return OK;
}

/* Пропустить пробельные символы */
status_code skip_spaces(const char *text, size_t *pos) {
    if (text == NULL || pos == NULL) {
        return INVALID_ARGS;
    }
    while (text[*pos] != '\0' && isspace((unsigned char)text[*pos])) {
        (*pos)++;
    }
    return OK;
}

/*
 * Разобрать запись многочлена: члены через + и -, член - [коэффициент][x[^степень]].
 * Пробелы между частями допускаются ("2 x ^ 3" - нет: число и x можно разделять,
 * но цифры одного числа - нет). Одинаковые степени складываются.
 */
status_code parse_polynomial(const char *text, polynomial *p) {
    if (text == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    poly_init(p);
    size_t pos = 0;
    skip_spaces(text, &pos);
    if (text[pos] == '\0') {
        return SYNTAX_ERROR;   /* пустая запись */
    }
    int first = 1;
    status_code sc = OK;
    while (sc == OK) {
        skip_spaces(text, &pos);
        if (text[pos] == '\0') {
            break;
        }
        /* знак: у первого члена может не быть, у остальных обязателен */
        long long sign = 1;
        if (text[pos] == '+' || text[pos] == '-') {
            sign = text[pos] == '-' ? -1 : 1;
            pos++;
            skip_spaces(text, &pos);
        } else if (!first) {
            sc = SYNTAX_ERROR;
            break;
        }
        first = 0;
        /* коэффициент (если есть) */
        long long coefficient = 1;
        int has_number = 0;
        if (isdigit((unsigned char)text[pos])) {
            sc = parse_unsigned(text, &pos, &coefficient);
            has_number = 1;
            skip_spaces(text, &pos);
        }
        /* x и степень (если есть) */
        long long degree = 0;
        if (sc == OK && text[pos] == 'x') {
            degree = 1;
            pos++;
            skip_spaces(text, &pos);
            if (text[pos] == '^') {
                pos++;
                skip_spaces(text, &pos);
                sc = parse_unsigned(text, &pos, &degree);
                if (sc == OVERFLOW_ERROR) {
                    sc = DEGREE_TOO_BIG;
                }
            }
        } else if (sc == OK && !has_number) {
            sc = SYNTAX_ERROR;   /* ни числа, ни x - например "+" в конце или "y" */
        }
        if (sc == OK) {
            sc = safe_multiply(coefficient, sign, &coefficient);
        }
        if (sc == OK) {
            sc = poly_add_term(p, coefficient, degree);
        }
    }
    if (sc != OK) {
        poly_clear(p);
    }
    return sc;
}

/* Целая точка для Eval: необязательный знак и цифры */
status_code parse_point(const char *text, long long *x) {
    if (text == NULL || x == NULL) {
        return INVALID_ARGS;
    }
    size_t pos = 0;
    skip_spaces(text, &pos);
    int negative = 0;
    if (text[pos] == '+' || text[pos] == '-') {
        negative = text[pos] == '-';
        pos++;
    }
    long long value = 0;
    status_code sc = parse_unsigned(text, &pos, &value);
    if (sc != OK) {
        return sc;
    }
    skip_spaces(text, &pos);
    if (text[pos] != '\0') {
        return SYNTAX_ERROR;
    }
    *x = negative ? -value : value;
    return OK;
}

/* ===================== файл инструкций ===================== */

/* Прочитать весь файл в строку (буфер удваивается) */
status_code read_file(FILE *in, char **text) {
    if (in == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    size_t size = 256;
    size_t length = 0;
    char *buf = (char *)malloc(size);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    int c = 0;
    while ((c = fgetc(in)) != EOF) {
        if (length + 1 >= size) {
            if (size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            char *tmp = (char *)realloc(buf, size * 2);
            if (tmp == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = tmp;
            size *= 2;
        }
        buf[length] = (char)c;
        length++;
    }
    buf[length] = '\0';
    if (ferror(in)) {
        free(buf);
        return FILE_ERROR;
    }
    *text = buf;
    return OK;
}

/* Где сейчас читаем файл и что было не так (для сообщения об ошибке) */
typedef struct {
    const char *text;
    size_t pos;
    size_t line;            /* номер текущей строки */
    const char *problem;    /* описание синтаксической ошибки */
} reader;

/* Сдвинуться на символ вперед, считая строки */
status_code advance(reader *r) {
    if (r == NULL) {
        return INVALID_ARGS;
    }
    if (r->text[r->pos] == '\n') {
        r->line++;
    }
    r->pos++;
    return OK;
}

/* Пропустить пробелы, переводы строк и комментарии % и [ ]. Ошибки: вложенный или незакрытый [ ] */
status_code skip_blanks(reader *r) {
    if (r == NULL) {
        return INVALID_ARGS;
    }
    while (r->text[r->pos] != '\0') {
        char c = r->text[r->pos];
        if (isspace((unsigned char)c)) {
            advance(r);
        } else if (c == '%') {
            while (r->text[r->pos] != '\0' && r->text[r->pos] != '\n') {
                advance(r);
            }
        } else if (c == '[') {
            advance(r);
            while (r->text[r->pos] != ']') {
                if (r->text[r->pos] == '\0') {
                    r->problem = "comment [ ... ] is not closed";
                    return SYNTAX_ERROR;
                }
                if (r->text[r->pos] == '[') {
                    r->problem = "nested comments [ [ ] ] are not allowed";
                    return SYNTAX_ERROR;
                }
                advance(r);
            }
            advance(r);   /* ']' */
        } else {
            break;
        }
    }
    return OK;
}

/* Одна инструкция: имя и параметры (тексты в динамической памяти) */
typedef struct {
    char name[8];       /* самое длинное имя - "Mult", "Cmps", "Eval", "Diff" */
    char **args;
    size_t arg_count;
    size_t line;        /* строка, где инструкция началась */
} instruction;

status_code instruction_clear(instruction *ins) {
    if (ins == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < ins->arg_count; i++) {
        free(ins->args[i]);
    }
    free(ins->args);
    ins->args = NULL;
    ins->arg_count = 0;
    return OK;
}

/* Добавить параметр text[from..to) */
status_code add_argument(instruction *ins, const char *text, const size_t from, const size_t to) {
    if (ins == NULL || text == NULL || to < from) {
        return INVALID_ARGS;
    }
    char **tmp = (char **)realloc(ins->args, (ins->arg_count + 1) * sizeof(char *));
    if (tmp == NULL) {
        return ALLOC_ERROR;
    }
    ins->args = tmp;
    char *arg = (char *)malloc(to - from + 1);
    if (arg == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(arg, text + from, to - from);
    arg[to - from] = '\0';
    ins->args[ins->arg_count] = arg;
    ins->arg_count++;
    return OK;
}

/*
 * Прочитать следующую инструкцию: Имя ( параметр , параметр ) ;
 * *found = 0 - инструкций больше нет. Пустые скобки - 0 параметров.
 */
status_code read_instruction(reader *r, instruction *ins, int *found) {
    if (r == NULL || ins == NULL || found == NULL) {
        return INVALID_ARGS;
    }
    ins->args = NULL;
    ins->arg_count = 0;
    *found = 0;
    status_code sc = skip_blanks(r);
    if (sc != OK || r->text[r->pos] == '\0') {
        return sc;
    }
    *found = 1;
    ins->line = r->line;
    /* имя: латинские буквы */
    size_t length = 0;
    while (isalpha((unsigned char)r->text[r->pos])) {
        if (length + 1 < sizeof(ins->name)) {
            ins->name[length] = r->text[r->pos];
        }
        length++;
        advance(r);
    }
    if (length == 0 || length + 1 > sizeof(ins->name)) {
        r->problem = "an instruction must start with its name: Add, Sub, Mult, Div, Mod, Eval, Diff, Cmps";
        return SYNTAX_ERROR;
    }
    ins->name[length] = '\0';
    while (r->text[r->pos] == ' ' || r->text[r->pos] == '\t') {
        advance(r);
    }
    if (r->text[r->pos] != '(') {
        r->problem = "'(' expected after the instruction name";
        return SYNTAX_ERROR;
    }
    advance(r);
    /* параметры до ')' через запятые; скобок и комментариев внутри быть не может */
    size_t from = r->pos;
    int empty = 1;   /* пока одни пробелы */
    while (sc == OK) {
        char c = r->text[r->pos];
        if (c == '\0' || c == ';' || c == '(' || c == '%' || c == '[') {
            r->problem = "')' expected: the instruction is not closed";
            sc = SYNTAX_ERROR;
            break;
        }
        if (c == ',' || c == ')') {
            if (!(c == ')' && empty && ins->arg_count == 0)) {
                sc = add_argument(ins, r->text, from, r->pos);
            }
            advance(r);
            from = r->pos;
            if (c == ')') {
                break;
            }
            continue;
        }
        if (!isspace((unsigned char)c)) {
            empty = 0;
        }
        advance(r);
    }
    if (sc == OK) {
        while (r->text[r->pos] == ' ' || r->text[r->pos] == '\t') {
            advance(r);
        }
        if (r->text[r->pos] != ';') {
            r->problem = "';' expected after ')'";
            sc = SYNTAX_ERROR;
        } else {
            advance(r);
        }
    }
    if (sc != OK) {
        instruction_clear(ins);
    }
    return sc;
}

/*
 * Выполнить инструкцию. *accumulator - сумматор; результат-многочлен печатается и попадает в сумматор.
 * Ошибка - статус и *problem (описание для пользователя).
 */
status_code execute(const instruction *ins, polynomial *accumulator, const char **problem) {
    if (ins == NULL || accumulator == NULL || problem == NULL) {
        return INVALID_ARGS;
    }
    const char *binary[6] = {"Add", "Sub", "Mult", "Div", "Mod", "Cmps"};
    int is_binary = 0;
    for (int i = 0; i < 6; i++) {
        if (strcmp(ins->name, binary[i]) == 0) {
            is_binary = 1;
        }
    }
    int is_eval = strcmp(ins->name, "Eval") == 0;
    int is_diff = strcmp(ins->name, "Diff") == 0;
    if (!is_binary && !is_eval && !is_diff) {
        *problem = "unknown instruction (names are case-sensitive: Add, Sub, Mult, Div, Mod, Eval, Diff, Cmps)";
        return SYNTAX_ERROR;
    }
    /* сколько параметров: полное число и на один меньше (тогда первый - сумматор) */
    size_t full = is_diff ? 1 : 2;
    if (ins->arg_count != full && ins->arg_count + 1 != full) {
        *problem = is_diff ? "Diff takes 0 or 1 parameters" : "this instruction takes 1 or 2 parameters";
        return SYNTAX_ERROR;
    }
    int use_accumulator = ins->arg_count + 1 == full;

    /* первый операнд: сумматор или первый параметр */
    polynomial first;
    status_code sc = use_accumulator ? poly_copy(accumulator, &first) : parse_polynomial(ins->args[0], &first);
    if (sc != OK) {
        *problem = sc == SYNTAX_ERROR ? "wrong polynomial" : (sc == DEGREE_TOO_BIG ? "the degree is too big"
                                                               : "a coefficient is too big");
        return sc;
    }
    size_t second_index = use_accumulator ? 0 : 1;

    if (is_eval) {
        long long x = 0;
        long long value = 0;
        sc = parse_point(ins->args[second_index], &x);
        if (sc != OK) {
            *problem = "the point must be a whole number";
        } else {
            sc = poly_evaluate(&first, x, &value);
            if (sc == OK) {
                printf("Eval: %lld\n", value);
            } else {
                *problem = "the value is too big";
            }
        }
        poly_clear(&first);
        return sc;
    }

    polynomial result;
    poly_init(&result);
    if (is_diff) {
        sc = poly_derivative(&first, &result);
    } else {
        polynomial second;
        sc = parse_polynomial(ins->args[second_index], &second);
        if (sc != OK) {
            *problem = sc == SYNTAX_ERROR ? "wrong polynomial" : (sc == DEGREE_TOO_BIG ? "the degree is too big"
                                                                   : "a coefficient is too big");
            poly_clear(&first);
            return sc;
        }
        if (strcmp(ins->name, "Add") == 0) {
            sc = poly_add(&first, &second, &result);
        } else if (strcmp(ins->name, "Sub") == 0) {
            sc = poly_subtract(&first, &second, &result);
        } else if (strcmp(ins->name, "Mult") == 0) {
            sc = poly_multiply(&first, &second, &result);
        } else if (strcmp(ins->name, "Cmps") == 0) {
            sc = poly_compose(&first, &second, &result);
        } else {
            polynomial quotient;
            polynomial remainder;
            sc = poly_divide(&first, &second, &quotient, &remainder);
            if (sc == OK) {
                /* Div - частное, Mod - остаток; ненужное освобождаем */
                int is_div = strcmp(ins->name, "Div") == 0;
                result = is_div ? quotient : remainder;
                poly_clear(is_div ? &remainder : &quotient);
            }
        }
        poly_clear(&second);
    }
    poly_clear(&first);
    if (sc != OK) {
        switch (sc) {
            case DIVISION_BY_ZERO:
                *problem = "division by the zero polynomial";
                break;
            case NOT_DIVISIBLE:
                *problem = "the division is not exact in whole numbers (a leading coefficient is not divisible)";
                break;
            case DEGREE_TOO_BIG:
                *problem = "the degree of the result is too big";
                break;
            case OVERFLOW_ERROR:
                *problem = "a coefficient of the result is too big";
                break;
            default:
                *problem = "not enough memory";
                break;
        }
        return sc;
    }
    printf("%s: ", ins->name);
    poly_print(stdout, &result);
    printf("\n");
    poly_clear(accumulator);
    *accumulator = result;
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: lab3_task8 <instructions file>\n");
        return 1;
    }
    FILE *in = fopen(argv[1], "r");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        return 1;
    }
    char *text = NULL;
    status_code sc = read_file(in, &text);
    fclose(in);
    if (sc != OK) {
        fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: can not read the file\n");
        return 1;
    }

    reader r = {text, 0, 1, NULL};
    /* метка BOM (EF BB BF) в начале файла из Блокнота - пропускаем */
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        r.pos = 3;
    }
    polynomial accumulator;
    poly_init(&accumulator);
    int result = 0;
    while (result == 0) {
        instruction ins;
        int found = 0;
        sc = read_instruction(&r, &ins, &found);
        if (sc != OK) {
            if (sc == ALLOC_ERROR) {
                fprintf(stderr, "Error: not enough memory\n");
            } else {
                fprintf(stderr, "Error: line %zu: %s\n", r.line, r.problem);
            }
            result = 1;
            break;
        }
        if (!found) {
            break;
        }
        const char *problem = NULL;
        sc = execute(&ins, &accumulator, &problem);
        if (sc != OK) {
            fprintf(stderr, "Error: line %zu: %s: %s\n", ins.line, ins.name, problem);
            result = 1;
        }
        instruction_clear(&ins);
    }
    poly_clear(&accumulator);
    free(text);
    return result;
}
