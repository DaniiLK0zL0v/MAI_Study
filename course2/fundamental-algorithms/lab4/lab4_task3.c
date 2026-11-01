/*
 * Лабораторная 4, задание 3. Многочлены от нескольких переменных.
 *
 * Многочлен - двусвязный список одночленов (мономов). Одночлен - вещественный коэффициент и бор
 * (префиксное дерево) пар "имя переменной - степень". Степени - целые >= 0; переменных со степенью 0
 * в боре нет никогда. Имя переменной - буквы и цифры, начинается с буквы, регистр не важен (X и x - одно).
 *
 * Запуск:  lab4_task3 <файл с инструкциями> [eps]      (eps > 0, по умолчанию 1e-9)
 * Инструкции (имена - с учётом регистра), каждая кончается ';':
 *     Add(f, g);  Sub(f, g);  Mult(f, g);   - f + g, f - g, f * g
 *     Eval({x:1.5, y:-2});                   - значение в точке (результат - число - тоже в сумматор)
 *     PartDeriv(f, x);                       - частная производная по x
 *     PartInteg(f, x);                       - частная первообразная по x (постоянная = 0)
 *     Cmps(f, x, g);                         - композиция: в f вместо x подставить g
 *     Grad({x:1, y:2});                      - градиент в точке (печатается, сумматор не меняется)
 * Если первого параметра нет (Add(g), Eval({..}), PartDeriv(x), Cmps(x, g)), вместо него берётся
 * сумматор. Сумматор вначале 0; после каждой инструкции печатается его значение.
 * Комментарии: % - до конца строки; [ ... ] - на несколько строк, вложенность любая.
 *
 * Запись одночлена: множители через '*' или '_' (оба значат "умножить"); множитель - число или
 * переменная со степенью ^n. Число может стоять прямо перед именем: 4xy = 4 * xy (xy - одна переменная:
 * имена бывают длинными). Повторы разрешены: 2_x^3_x^-1_-1.5 = -3 * x^2 (внутри одночлена степень может
 * быть отрицательной, но итоговая - нет). Пробелы внутри многочлена игнорируются.
 * Пример:  Add(4*x^2-x_y^2+4*x_y+1, x^2_y^3+2*x_y^2+_y^2-4*x_y);
 *
 * Вещественные числа сравниваются только через eps: коэффициент с |c| < eps считается нулём и удаляется.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, calloc, realloc, free, strtod */
#include <string.h>   /* memcpy, strcmp, strlen */
#include <math.h>     /* fabs, isfinite */

#define ALPHABET 36          /* символы имени: 0-9 и a-z */
#define MAX_DEGREE 10000     /* наибольшая степень переменной */
#define MAX_TERMS 100000     /* наибольшее число одночленов в многочлене */
#define DEFAULT_EPS 1e-9

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,       /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,       /* ошибка в записи многочлена, точки или инструкции */
    UNKNOWN_COMMAND,    /* нет такой инструкции */
    ARGS_COUNT,         /* не то число параметров */
    NEGATIVE_DEGREE,    /* итоговая степень переменной < 0 */
    DEGREE_TOO_BIG,     /* степень больше MAX_DEGREE */
    TOO_MANY_TERMS,     /* больше MAX_TERMS одночленов */
    NO_VALUE,           /* в точке нет значения переменной многочлена */
    DUPLICATE_NAME,     /* в точке одна переменная дважды */
    NUMBER_ERROR,       /* число не помещается в double (inf) */
    COMMENT_ERROR,      /* ']' без '[' или '[' без ']' */
    FILE_ERROR,         /* файл не открылся / ошибка чтения */
    ALLOC_ERROR         /* malloc / realloc не смогли выделить память */
} status_code;

/* Узел бора: степень переменной, имя которой - путь от корня до узла (0 - здесь имя не кончается) */
typedef struct trie_node {
    long long degree;
    struct trie_node *child[ALPHABET];
} trie_node;

/* Пара "имя - степень" (упорядоченный список переменных одночлена для печати и сравнения) */
typedef struct {
    char *name;
    long long degree;
} power;

/* Одночлен: коэффициент и бор переменных; powers - те же переменные списком по алфавиту (кэш) */
typedef struct monomial {
    double coefficient;
    trie_node *vars;
    power *powers;
    size_t count;            /* сколько переменных */
    long long total;         /* сумма степеней */
    struct monomial *prev;
    struct monomial *next;
} monomial;

/* Многочлен: двусвязный список одночленов, упорядоченный (см. monomial_compare); пустой - ноль */
typedef struct {
    monomial *head;
    monomial *tail;
    size_t count;
} polynomial;

/* Точка пространства: значения переменных */
typedef struct {
    char *name;
    double value;
} coordinate;

typedef struct {
    coordinate *items;
    size_t count;
} point;

/* Растущая строка (для текста многочлена) */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} text_buffer;

/* ===================== строка ===================== */

status_code text_init(text_buffer *t) {
    if (t == NULL) {
        return INVALID_ARGS;
    }
    t->data = (char *)malloc(64);
    if (t->data == NULL) {
        return ALLOC_ERROR;
    }
    t->data[0] = '\0';
    t->length = 0;
    t->capacity = 64;
    return OK;
}

status_code text_append(text_buffer *t, const char *s) {
    if (t == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    size_t n = strlen(s);
    if (t->length + n + 1 > t->capacity) {
        size_t capacity = t->capacity;
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

/* ===================== бор ===================== */

/* Номер символа имени: '0'..'9' -> 0..9, буквы (любой регистр) -> 10..35; иначе -1.
   Цифры раньше букв - тогда обход бора по номерам даёт имена в порядке strcmp (алфавитном). */
status_code char_index(const char c, int *index) {
    if (index == NULL) {
        return INVALID_ARGS;
    }
    if (c >= '0' && c <= '9') {
        *index = c - '0';
    } else if (c >= 'a' && c <= 'z') {
        *index = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
        *index = c - 'A' + 10;
    } else {
        *index = -1;
    }
    return OK;
}

/* Конец имени: позиция первого символа после цепочки букв и цифр, которая начинается в start */
status_code name_end(const char *s, const size_t length, const size_t start, size_t *end) {
    if (s == NULL || end == NULL) {
        return INVALID_ARGS;
    }
    size_t i = start;
    while (i < length) {
        int index = 0;
        char_index(s[i], &index);
        if (index < 0) {
            break;
        }
        i++;
    }
    *end = i;
    return OK;
}

status_code trie_create(trie_node **node) {
    if (node == NULL) {
        return INVALID_ARGS;
    }
    *node = (trie_node *)calloc(1, sizeof(trie_node));   /* calloc: степень 0, детей нет */
    return *node == NULL ? ALLOC_ERROR : OK;
}

/* Освободить весь бор. Без рекурсии: свой стек узлов (в стек кладём детей, пока не дойдём до листьев) */
status_code trie_free(trie_node *root) {
    if (root == NULL) {
        return OK;
    }
    size_t capacity = 16;
    size_t size = 0;
    trie_node **stack = (trie_node **)malloc(capacity * sizeof(trie_node *));
    if (stack == NULL) {
        return ALLOC_ERROR;
    }
    stack[size++] = root;
    while (size > 0) {
        trie_node *node = stack[--size];
        for (int i = 0; i < ALPHABET; i++) {
            if (node->child[i] == NULL) {
                continue;
            }
            if (size == capacity) {
                trie_node **tmp = (trie_node **)realloc(stack, capacity * 2 * sizeof(trie_node *));
                if (tmp == NULL) {
                    free(stack);
                    return ALLOC_ERROR;   /* часть бора не освобождена - но памяти всё равно нет */
                }
                stack = tmp;
                capacity *= 2;
            }
            stack[size++] = node->child[i];
        }
        free(node);
    }
    free(stack);
    return OK;
}

/* Записать степень degree (> 0) переменной name: создаются недостающие узлы пути */
status_code trie_set(trie_node *root, const char *name, const long long degree) {
    if (root == NULL || name == NULL || name[0] == '\0' || degree <= 0) {
        return INVALID_ARGS;
    }
    trie_node *node = root;
    for (size_t i = 0; name[i] != '\0'; i++) {
        int index = 0;
        char_index(name[i], &index);
        if (index < 0) {
            return INVALID_ARGS;
        }
        if (node->child[index] == NULL) {
            status_code sc = trie_create(&node->child[index]);
            if (sc != OK) {
                return sc;
            }
        }
        node = node->child[index];
    }
    node->degree = degree;
    return OK;
}

/* Степень переменной name (0 - переменной в одночлене нет) */
status_code trie_get(const trie_node *root, const char *name, long long *degree) {
    if (root == NULL || name == NULL || degree == NULL) {
        return INVALID_ARGS;
    }
    const trie_node *node = root;
    for (size_t i = 0; name[i] != '\0' && node != NULL; i++) {
        int index = 0;
        char_index(name[i], &index);
        node = index < 0 ? NULL : node->child[index];
    }
    *degree = node == NULL ? 0 : node->degree;
    return OK;
}

/*
 * Все переменные бора списком по алфавиту. Обход в глубину без рекурсии: в стеке - узел и длина имени
 * до него; детей кладём в обратном порядке, чтобы первым достать самого "маленького".
 */
status_code trie_collect(const trie_node *root, power **powers, size_t *count, long long *total) {
    if (root == NULL || powers == NULL || count == NULL || total == NULL) {
        return INVALID_ARGS;
    }
    typedef struct {
        const trie_node *node;
        size_t depth;        /* длина имени до этого узла */
        char symbol;         /* символ ребра, по которому пришли в узел */
    } frame;
    size_t capacity = 16;
    size_t size = 0;
    frame *stack = (frame *)malloc(capacity * sizeof(frame));
    size_t name_capacity = 16;
    char *name = (char *)malloc(name_capacity);
    size_t list_capacity = 4;
    power *list = (power *)malloc(list_capacity * sizeof(power));
    status_code sc = (stack == NULL || name == NULL || list == NULL) ? ALLOC_ERROR : OK;
    size_t found = 0;
    *total = 0;
    if (sc == OK) {
        stack[size].node = root;
        stack[size].depth = 0;
        stack[size].symbol = '\0';
        size++;
    }
    while (sc == OK && size > 0) {
        frame f = stack[--size];
        if (f.depth > 0) {
            /* имя до этого узла: первые depth - 1 символов уже стоят, дописываем свой */
            if (f.depth + 1 > name_capacity) {
                char *tmp = (char *)realloc(name, name_capacity * 2);
                if (tmp == NULL) {
                    sc = ALLOC_ERROR;
                    break;
                }
                name = tmp;
                name_capacity *= 2;
            }
            name[f.depth - 1] = f.symbol;
            name[f.depth] = '\0';
        }
        if (f.node->degree > 0) {
            if (found == list_capacity) {
                power *tmp = (power *)realloc(list, list_capacity * 2 * sizeof(power));
                if (tmp == NULL) {
                    sc = ALLOC_ERROR;
                    break;
                }
                list = tmp;
                list_capacity *= 2;
            }
            list[found].name = (char *)malloc(f.depth + 1);
            if (list[found].name == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            memcpy(list[found].name, name, f.depth);
            list[found].name[f.depth] = '\0';
            list[found].degree = f.node->degree;
            *total += f.node->degree;
            found++;
        }
        for (int i = ALPHABET - 1; i >= 0; i--) {
            if (f.node->child[i] == NULL) {
                continue;
            }
            if (size == capacity) {
                frame *tmp = (frame *)realloc(stack, capacity * 2 * sizeof(frame));
                if (tmp == NULL) {
                    sc = ALLOC_ERROR;
                    break;
                }
                stack = tmp;
                capacity *= 2;
            }
            stack[size].node = f.node->child[i];
            stack[size].depth = f.depth + 1;
            stack[size].symbol = (char)(i < 10 ? '0' + i : 'a' + i - 10);
            size++;
        }
    }
    free(stack);
    free(name);
    if (sc != OK) {
        for (size_t i = 0; i < found; i++) {
            free(list[i].name);
        }
        free(list);
        return sc;
    }
    *powers = list;
    *count = found;
    return OK;
}

/* ===================== одночлен ===================== */

status_code monomial_free(monomial *m) {
    if (m == NULL) {
        return OK;
    }
    trie_free(m->vars);
    for (size_t i = 0; i < m->count; i++) {
        free(m->powers[i].name);
    }
    free(m->powers);
    free(m);
    return OK;
}

/* Новый одночлен: коэффициент и пустой бор (без переменных) */
status_code monomial_new(const double coefficient, monomial **result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    monomial *m = (monomial *)malloc(sizeof(monomial));
    if (m == NULL) {
        return ALLOC_ERROR;
    }
    m->coefficient = coefficient;
    m->powers = NULL;
    m->count = 0;
    m->total = 0;
    m->prev = NULL;
    m->next = NULL;
    status_code sc = trie_create(&m->vars);
    if (sc != OK) {
        free(m);
        return sc;
    }
    *result = m;
    return OK;
}

/* Обновить список переменных по алфавиту (кэш) после того, как бор построен */
status_code monomial_refresh(monomial *m) {
    if (m == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < m->count; i++) {
        free(m->powers[i].name);
    }
    free(m->powers);
    m->powers = NULL;
    m->count = 0;
    return trie_collect(m->vars, &m->powers, &m->count, &m->total);
}

/*
 * Одночлен с коэффициентом coefficient и переменными source (кроме skip, если skip != NULL),
 * степень каждой переменной увеличена на extra из другого списка (для умножения). Удобный "конструктор":
 * powers_a - первый список, powers_b - второй (степени складываются), skip - пропустить переменную.
 */
status_code monomial_build(const double coefficient, const power *powers_a, const size_t count_a,
                           const power *powers_b, const size_t count_b, const char *skip, monomial **result) {
    if (result == NULL || (powers_a == NULL && count_a > 0) || (powers_b == NULL && count_b > 0)) {
        return INVALID_ARGS;
    }
    monomial *m = NULL;
    status_code sc = monomial_new(coefficient, &m);
    /* оба списка упорядочены по алфавиту - сливаем их, как при сортировке слиянием */
    size_t i = 0;
    size_t j = 0;
    while (sc == OK && (i < count_a || j < count_b)) {
        int order = 0;
        if (i == count_a) {
            order = 1;
        } else if (j == count_b) {
            order = -1;
        } else {
            order = strcmp(powers_a[i].name, powers_b[j].name);
        }
        const char *name = order <= 0 ? powers_a[i].name : powers_b[j].name;
        long long degree = 0;
        if (order <= 0) {
            degree += powers_a[i++].degree;
        }
        if (order >= 0) {
            degree += powers_b[j++].degree;
        }
        if (skip != NULL && strcmp(name, skip) == 0) {
            continue;
        }
        if (degree > MAX_DEGREE) {
            sc = DEGREE_TOO_BIG;
        } else if (degree > 0) {
            sc = trie_set(m->vars, name, degree);
        }
    }
    if (sc == OK) {
        sc = monomial_refresh(m);
    }
    if (sc != OK) {
        monomial_free(m);
        return sc;
    }
    *result = m;
    return OK;
}

status_code monomial_copy(const monomial *source, const double coefficient, monomial **result) {
    if (source == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    return monomial_build(coefficient, source->powers, source->count, NULL, 0, NULL, result);
}

/*
 * Порядок одночленов в многочлене: сначала большая суммарная степень; при равной - сравниваем
 * переменные по алфавиту: раньше тот, у кого раньше по алфавиту имя, а при одинаковом имени - больше
 * степень (x^2 раньше x*y). *result < 0 - a раньше b, 0 - одинаковые переменные (подобные), > 0 - позже.
 */
status_code monomial_compare(const monomial *a, const monomial *b, int *result) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    if (a->total != b->total) {
        *result = a->total > b->total ? -1 : 1;
        return OK;
    }
    for (size_t i = 0; i < a->count && i < b->count; i++) {
        int order = strcmp(a->powers[i].name, b->powers[i].name);
        if (order != 0) {
            *result = order < 0 ? -1 : 1;
            return OK;
        }
        if (a->powers[i].degree != b->powers[i].degree) {
            *result = a->powers[i].degree > b->powers[i].degree ? -1 : 1;
            return OK;
        }
    }
    *result = (a->count > b->count) ? -1 : (a->count < b->count ? 1 : 0);
    return OK;
}

/* ===================== многочлен ===================== */

status_code poly_init(polynomial *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    p->head = NULL;
    p->tail = NULL;
    p->count = 0;
    return OK;
}

status_code poly_clear(polynomial *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    monomial *m = p->head;
    while (m != NULL) {
        monomial *next = m->next;
        monomial_free(m);
        m = next;
    }
    return poly_init(p);
}

/* Убрать одночлен из списка (и освободить) */
status_code poly_remove(polynomial *p, monomial *m) {
    if (p == NULL || m == NULL) {
        return INVALID_ARGS;
    }
    if (m->prev != NULL) {
        m->prev->next = m->next;
    } else {
        p->head = m->next;
    }
    if (m->next != NULL) {
        m->next->prev = m->prev;
    } else {
        p->tail = m->prev;
    }
    p->count--;
    return monomial_free(m);
}

/*
 * Прибавить одночлен m к многочлену. m переходит во владение функции (вставляется или освобождается).
 * Есть подобный - складываем коэффициенты (|сумма| < eps - удаляем); нет - вставляем на своё место.
 */
status_code poly_add_monomial(polynomial *p, monomial *m, const double eps) {
    if (p == NULL || m == NULL || eps <= 0) {
        monomial_free(m);
        return INVALID_ARGS;
    }
    monomial *place = p->head;
    int order = 1;
    while (place != NULL) {
        monomial_compare(m, place, &order);
        if (order <= 0) {
            break;
        }
        place = place->next;
    }
    if (place != NULL && order == 0) {
        place->coefficient = place->coefficient + m->coefficient;
        monomial_free(m);
        if (fabs(place->coefficient) < eps) {
            return poly_remove(p, place);
        }
        return OK;
    }
    if (fabs(m->coefficient) < eps) {
        monomial_free(m);   /* нулевой одночлен не храним */
        return OK;
    }
    if (p->count >= MAX_TERMS) {
        monomial_free(m);
        return TOO_MANY_TERMS;
    }
    /* вставка перед place (place == NULL - в конец) */
    m->next = place;
    m->prev = place != NULL ? place->prev : p->tail;
    if (m->prev != NULL) {
        m->prev->next = m;
    } else {
        p->head = m;
    }
    if (place != NULL) {
        place->prev = m;
    } else {
        p->tail = m;
    }
    p->count++;
    return OK;
}

/* result += sign * source (каждый одночлен source по порядку) */
status_code poly_add_to(polynomial *result, const polynomial *source, const double sign, const double eps) {
    if (result == NULL || source == NULL) {
        return INVALID_ARGS;
    }
    for (const monomial *m = source->head; m != NULL; m = m->next) {
        monomial *copy = NULL;
        status_code sc = monomial_copy(m, sign * m->coefficient, &copy);
        if (sc == OK) {
            sc = poly_add_monomial(result, copy, eps);
        }
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

/* result = a + sign * b */
status_code poly_add(const polynomial *a, const polynomial *b, const double sign, polynomial *result,
                     const double eps) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    polynomial r;
    poly_init(&r);
    status_code sc = poly_add_to(&r, a, 1.0, eps);
    if (sc == OK) {
        sc = poly_add_to(&r, b, sign, eps);
    }
    if (sc != OK) {
        poly_clear(&r);
        return sc;
    }
    *result = r;
    return OK;
}

/* result = a * b: каждый одночлен a на каждый одночлен b */
status_code poly_multiply(const polynomial *a, const polynomial *b, polynomial *result, const double eps) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    polynomial r;
    poly_init(&r);
    status_code sc = OK;
    for (const monomial *x = a->head; x != NULL && sc == OK; x = x->next) {
        for (const monomial *y = b->head; y != NULL && sc == OK; y = y->next) {
            monomial *product = NULL;
            sc = monomial_build(x->coefficient * y->coefficient, x->powers, x->count, y->powers, y->count, NULL,
                                &product);
            if (sc == OK) {
                sc = poly_add_monomial(&r, product, eps);
            }
        }
    }
    if (sc != OK) {
        poly_clear(&r);
        return sc;
    }
    *result = r;
    return OK;
}

/* Многочлен-число value (|value| < eps - нулевой многочлен) */
status_code poly_constant(const double value, polynomial *result, const double eps) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    poly_init(result);
    monomial *m = NULL;
    status_code sc = monomial_new(value, &m);
    if (sc == OK) {
        sc = poly_add_monomial(result, m, eps);
    }
    return sc;
}

/* Значение переменной в точке */
status_code point_find(const point *pt, const char *name, double *value, int *found) {
    if (pt == NULL || name == NULL || value == NULL || found == NULL) {
        return INVALID_ARGS;
    }
    *found = 0;
    for (size_t i = 0; i < pt->count; i++) {
        if (strcmp(pt->items[i].name, name) == 0) {
            *value = pt->items[i].value;
            *found = 1;
            break;
        }
    }
    return OK;
}

/* Значение многочлена в точке. *missing - имя переменной без значения (при NO_VALUE) */
status_code poly_evaluate(const polynomial *p, const point *pt, double *value, const char **missing) {
    if (p == NULL || pt == NULL || value == NULL || missing == NULL) {
        return INVALID_ARGS;
    }
    double sum = 0.0;
    for (const monomial *m = p->head; m != NULL; m = m->next) {
        double term = m->coefficient;
        for (size_t i = 0; i < m->count; i++) {
            double x = 0.0;
            int found = 0;
            point_find(pt, m->powers[i].name, &x, &found);
            if (!found) {
                *missing = m->powers[i].name;
                return NO_VALUE;
            }
            /* x^степень повторным умножением (степень не больше MAX_DEGREE) */
            double r = 1.0;
            for (long long k = 0; k < m->powers[i].degree; k++) {
                r *= x;
            }
            term *= r;
        }
        sum += term;
    }
    if (!isfinite(sum)) {
        return NUMBER_ERROR;
    }
    *value = sum;
    return OK;
}

/*
 * Частная производная (integrate = 0) или первообразная (integrate = 1) по переменной name.
 * (c * x^d)' = c*d * x^(d-1) (d = 0 - одночлен пропадает); первообразная: c/(d+1) * x^(d+1).
 */
status_code poly_derive(const polynomial *p, const char *name, const int integrate, polynomial *result,
                        const double eps) {
    if (p == NULL || name == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    polynomial r;
    poly_init(&r);
    status_code sc = OK;
    power one = {(char *)name, 1};
    for (const monomial *m = p->head; m != NULL && sc == OK; m = m->next) {
        long long degree = 0;
        trie_get(m->vars, name, &degree);
        monomial *term = NULL;
        if (integrate) {
            /* степени складываются слиянием списков: x^d * x = x^(d+1) */
            sc = monomial_build(m->coefficient / (double)(degree + 1), m->powers, m->count, &one, 1, NULL, &term);
        } else if (degree > 0) {
            /* сначала без x, потом x^(d-1) */
            sc = monomial_build(m->coefficient * (double)degree, m->powers, m->count, NULL, 0, name, &term);
            if (sc == OK && degree > 1) {
                sc = trie_set(term->vars, name, degree - 1);
                if (sc == OK) {
                    sc = monomial_refresh(term);
                }
                if (sc != OK) {
                    monomial_free(term);
                    term = NULL;
                }
            }
        } else {
            continue;   /* x нет - производная одночлена 0 */
        }
        if (sc == OK) {
            sc = poly_add_monomial(&r, term, eps);
        }
    }
    if (sc != OK) {
        poly_clear(&r);
        return sc;
    }
    *result = r;
    return OK;
}

/* Композиция: в f вместо переменной name подставить g. Каждый одночлен c * x^d * (остальное) -> c * (остальное) * g^d */
status_code poly_compose(const polynomial *f, const char *name, const polynomial *g, polynomial *result,
                         const double eps) {
    if (f == NULL || name == NULL || g == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    polynomial r;
    poly_init(&r);
    status_code sc = OK;
    for (const monomial *m = f->head; m != NULL && sc == OK; m = m->next) {
        long long degree = 0;
        trie_get(m->vars, name, &degree);
        /* rest - одночлен без переменной name */
        polynomial rest;
        poly_init(&rest);
        monomial *without = NULL;
        sc = monomial_build(m->coefficient, m->powers, m->count, NULL, 0, name, &without);
        if (sc == OK) {
            sc = poly_add_monomial(&rest, without, eps);
        }
        /* g^d повторным умножением: power = 1, потом d раз power = power * g */
        polynomial pw;
        sc = sc == OK ? poly_constant(1.0, &pw, eps) : sc;
        for (long long k = 0; k < degree && sc == OK; k++) {
            polynomial next;
            sc = poly_multiply(&pw, g, &next, eps);
            poly_clear(&pw);
            if (sc == OK) {
                pw = next;
            } else {
                poly_init(&pw);
            }
        }
        polynomial term;
        poly_init(&term);
        if (sc == OK) {
            sc = poly_multiply(&rest, &pw, &term, eps);
        }
        if (sc == OK) {
            sc = poly_add_to(&r, &term, 1.0, eps);
        }
        poly_clear(&term);
        poly_clear(&pw);
        poly_clear(&rest);
    }
    if (sc != OK) {
        poly_clear(&r);
        return sc;
    }
    *result = r;
    return OK;
}

/* Все переменные многочлена по алфавиту (без повторов); имена - указатели в одночлены p */
status_code poly_variables(const polynomial *p, const char ***names, size_t *count) {
    if (p == NULL || names == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    size_t capacity = 4;
    size_t size = 0;
    const char **list = (const char **)malloc(capacity * sizeof(char *));
    if (list == NULL) {
        return ALLOC_ERROR;
    }
    for (const monomial *m = p->head; m != NULL; m = m->next) {
        for (size_t i = 0; i < m->count; i++) {
            /* вставка в упорядоченный список, если такого имени ещё нет */
            size_t pos = 0;
            while (pos < size && strcmp(list[pos], m->powers[i].name) < 0) {
                pos++;
            }
            if (pos < size && strcmp(list[pos], m->powers[i].name) == 0) {
                continue;
            }
            if (size == capacity) {
                const char **tmp = (const char **)realloc((void *)list, capacity * 2 * sizeof(char *));
                if (tmp == NULL) {
                    free((void *)list);
                    return ALLOC_ERROR;
                }
                list = tmp;
                capacity *= 2;
            }
            memmove((void *)(list + pos + 1), (const void *)(list + pos), (size - pos) * sizeof(char *));
            list[pos] = m->powers[i].name;
            size++;
        }
    }
    *names = list;
    *count = size;
    return OK;
}

/* Многочлен -> текст: 4*x^2 - x*y^2 + 1; ноль - "0". Числа - %.10g; коэффициент 1 у переменных не пишется */
status_code poly_to_text(const polynomial *p, const double eps, text_buffer *out) {
    if (p == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    out->length = 0;
    out->data[0] = '\0';
    if (p->head == NULL) {
        return text_append(out, "0");
    }
    char number[64];
    status_code sc = OK;
    for (const monomial *m = p->head; m != NULL && sc == OK; m = m->next) {
        double c = m->coefficient;
        if (m == p->head) {
            if (c < 0) {
                sc = text_append(out, "-");
            }
        } else {
            sc = text_append(out, c < 0 ? " - " : " + ");
        }
        double a = fabs(c);
        int need_star = 0;
        /* |c| = 1 у одночлена с переменными не пишем: x, а не 1*x */
        if (m->count == 0 || fabs(a - 1.0) >= eps) {
            snprintf(number, sizeof(number), "%.10g", a);
            if (sc == OK) {
                sc = text_append(out, number);
            }
            need_star = 1;
        }
        for (size_t i = 0; i < m->count && sc == OK; i++) {
            if (need_star) {
                sc = text_append(out, "*");
            }
            if (sc == OK) {
                sc = text_append(out, m->powers[i].name);
            }
            if (sc == OK && m->powers[i].degree > 1) {
                snprintf(number, sizeof(number), "^%lld", m->powers[i].degree);
                sc = text_append(out, number);
            }
            need_star = 1;
        }
    }
    return sc;
}

/* ===================== разбор многочлена и точки ===================== */

/* Временный список множителей-переменных одночлена (при разборе степени могут быть отрицательными) */
typedef struct {
    char *name;
    long long degree;
} factor;

status_code factors_free(factor *list, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(list[i].name);
    }
    free(list);
    return OK;
}

/* Добавить к списку множителей name^degree (одинаковые имена складываются) */
status_code factors_add(factor **list, size_t *count, size_t *capacity, const char *name, const size_t length,
                        const long long degree) {
    if (list == NULL || count == NULL || capacity == NULL || name == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < *count; i++) {
        if (strlen((*list)[i].name) == length && memcmp((*list)[i].name, name, length) == 0) {
            (*list)[i].degree += degree;   /* |степени| <= MAX_DEGREE, их немного - переполнения нет */
            if ((*list)[i].degree > MAX_DEGREE || (*list)[i].degree < -MAX_DEGREE) {
                return DEGREE_TOO_BIG;
            }
            return OK;
        }
    }
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
        factor *tmp = (factor *)realloc(*list, new_capacity * sizeof(factor));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        *list = tmp;
        *capacity = new_capacity;
    }
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(copy, name, length);
    copy[length] = '\0';
    (*list)[*count].name = copy;
    (*list)[*count].degree = degree;
    (*count)++;
    return OK;
}

/* Вещественное число в s[*pos]: цифры, затем необязательно '.' и цифры (без знака и без e) */
status_code read_real(const char *s, const size_t length, size_t *pos, double *value) {
    if (s == NULL || pos == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t start = *pos;
    while (*pos < length && s[*pos] >= '0' && s[*pos] <= '9') {
        (*pos)++;
    }
    size_t digits = *pos - start;
    if (*pos < length && s[*pos] == '.') {
        (*pos)++;
        size_t frac = *pos;
        while (*pos < length && s[*pos] >= '0' && s[*pos] <= '9') {
            (*pos)++;
        }
        digits += *pos - frac;
    }
    if (digits == 0) {
        return SYNTAX_ERROR;
    }
    /* strtod на копии куска: строка числа точно кончится там, где мы решили */
    char buf[512];
    size_t n = *pos - start;
    if (n >= sizeof(buf)) {
        return NUMBER_ERROR;
    }
    memcpy(buf, s + start, n);
    buf[n] = '\0';
    *value = strtod(buf, NULL);
    return isfinite(*value) ? OK : NUMBER_ERROR;
}

/*
 * Разобрать многочлен из строки s (length символов, пробелов уже нет). Каждый одночлен прибавляется к p.
 * Знак '+'/'-' начинает новый одночлен, если не стоит сразу после '*', '_' или '^' (там это знак множителя).
 */
status_code parse_polynomial(const char *s, const size_t length, polynomial *p, const double eps) {
    if (s == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    poly_init(p);
    if (length == 0) {
        return SYNTAX_ERROR;
    }
    size_t pos = 0;
    status_code sc = OK;
    while (pos < length && sc == OK) {
        /* знак одночлена */
        double coefficient = 1.0;
        if (s[pos] == '+' || s[pos] == '-') {
            coefficient = s[pos] == '-' ? -1.0 : 1.0;
            pos++;
        } else if (pos > 0) {
            sc = SYNTAX_ERROR;   /* после одночлена - только + или - */
            break;
        }
        factor *factors = NULL;
        size_t count = 0;
        size_t capacity = 0;
        int factor_count = 0;
        int after_separator = 1;   /* в начале одночлена - как после разделителя */
        int after_number = 0;      /* предыдущий множитель - число без разделителя после него */
        while (pos < length && sc == OK) {
            char ch = s[pos];
            if (ch == '*' || ch == '_') {
                pos++;
                after_separator = 1;
                after_number = 0;
                continue;
            }
            double sign = 1.0;
            if (ch == '+' || ch == '-') {
                if (!after_separator || factor_count == 0) {
                    break;   /* новый одночлен (знак в самом начале одночлена уже съеден выше) */
                }
                sign = ch == '-' ? -1.0 : 1.0;
                pos++;
                if (pos == length) {
                    sc = SYNTAX_ERROR;
                    break;
                }
                ch = s[pos];
            }
            int index = 0;
            char_index(ch, &index);
            if ((ch >= '0' && ch <= '9') || ch == '.') {
                if (after_number) {
                    sc = SYNTAX_ERROR;   /* два числа подряд без '*': "3.4.5" */
                    break;
                }
                double value = 0.0;
                sc = read_real(s, length, &pos, &value);
                if (sc == OK) {
                    coefficient *= sign * value;
                }
                /* сразу за числом может идти имя (4xy = 4 * xy), но знак за числом - уже новый одночлен */
                after_separator = 0;
                after_number = 1;
            } else if (index >= 10) {
                /* имя: буква, затем буквы и цифры; степень ^[знак]цифры */
                size_t start = pos;
                name_end(s, length, start, &pos);
                size_t name_length = pos - start;
                long long degree = 1;
                if (pos < length && s[pos] == '^') {
                    pos++;
                    long long degree_sign = 1;
                    if (pos < length && (s[pos] == '+' || s[pos] == '-')) {
                        degree_sign = s[pos] == '-' ? -1 : 1;
                        pos++;
                    }
                    size_t digits = pos;
                    degree = 0;
                    while (pos < length && s[pos] >= '0' && s[pos] <= '9') {
                        degree = degree * 10 + (s[pos] - '0');
                        if (degree > MAX_DEGREE) {
                            sc = DEGREE_TOO_BIG;
                            break;
                        }
                        pos++;
                    }
                    if (sc == OK && pos == digits) {
                        sc = SYNTAX_ERROR;
                    }
                    degree *= degree_sign;
                }
                /* имя - в нижнем регистре (X и x - одна переменная) */
                char lower[256];
                char *name = name_length < sizeof(lower) ? lower : (char *)malloc(name_length + 1);
                if (sc == OK && name == NULL) {
                    sc = ALLOC_ERROR;
                }
                if (sc == OK) {
                    for (size_t k = 0; k < name_length; k++) {
                        char c = s[start + k];
                        name[k] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
                    }
                    name[name_length] = '\0';
                    sc = factors_add(&factors, &count, &capacity, name, name_length, degree);
                    coefficient *= sign;
                }
                if (name != lower) {
                    free(name);
                }
                after_separator = 0;
                after_number = 0;
            } else {
                sc = SYNTAX_ERROR;
            }
            factor_count++;
        }
        if (sc == OK && factor_count == 0) {
            sc = SYNTAX_ERROR;   /* пустой одночлен: "x+" или "++" */
        }
        /* итоговые степени -> бор одночлена */
        monomial *m = NULL;
        if (sc == OK) {
            sc = monomial_new(coefficient, &m);
        }
        for (size_t i = 0; i < count && sc == OK; i++) {
            if (factors[i].degree < 0) {
                sc = NEGATIVE_DEGREE;
            } else if (factors[i].degree > 0) {
                sc = trie_set(m->vars, factors[i].name, factors[i].degree);
            }
        }
        if (sc == OK) {
            sc = monomial_refresh(m);
        }
        if (sc == OK) {
            sc = poly_add_monomial(p, m, eps);
        } else {
            monomial_free(m);
        }
        factors_free(factors, count);
    }
    if (sc != OK) {
        poly_clear(p);
    }
    return sc;
}

status_code point_free(point *pt) {
    if (pt == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < pt->count; i++) {
        free(pt->items[i].name);
    }
    free(pt->items);
    pt->items = NULL;
    pt->count = 0;
    return OK;
}

/* Точка {x:1.5,y:-2} (пробелов уже нет). Пустая точка {} допустима (многочлен-число) */
status_code parse_point(const char *s, const size_t length, point *pt) {
    if (s == NULL || pt == NULL) {
        return INVALID_ARGS;
    }
    pt->items = NULL;
    pt->count = 0;
    if (length < 2 || s[0] != '{' || s[length - 1] != '}') {
        return SYNTAX_ERROR;
    }
    size_t pos = 1;
    size_t end = length - 1;
    status_code sc = OK;
    while (pos < end && sc == OK) {
        /* имя */
        size_t start = pos;
        int index = 0;
        char_index(s[pos], &index);
        if (index < 10) {
            sc = SYNTAX_ERROR;
            break;
        }
        name_end(s, end, start, &pos);
        size_t name_length = pos - start;
        if (pos >= end || s[pos] != ':') {
            sc = SYNTAX_ERROR;
            break;
        }
        pos++;
        /* значение: до ',' или конца; strtod с проверкой, что прочитано всё */
        size_t value_start = pos;
        while (pos < end && s[pos] != ',') {
            pos++;
        }
        size_t value_length = pos - value_start;
        if (value_length == 0 || value_length >= 512) {
            sc = SYNTAX_ERROR;
            break;
        }
        char buf[512];
        memcpy(buf, s + value_start, value_length);
        buf[value_length] = '\0';
        char *stop = NULL;
        double value = strtod(buf, &stop);
        if (*stop != '\0' || buf[0] == ' ') {
            sc = SYNTAX_ERROR;
            break;
        }
        if (!isfinite(value)) {
            sc = NUMBER_ERROR;
            break;
        }
        char *name = (char *)malloc(name_length + 1);
        if (name == NULL) {
            sc = ALLOC_ERROR;
            break;
        }
        for (size_t k = 0; k < name_length; k++) {
            char c = s[start + k];
            name[k] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        name[name_length] = '\0';
        double old = 0.0;
        int found = 0;
        point_find(pt, name, &old, &found);
        if (found) {
            free(name);
            sc = DUPLICATE_NAME;
            break;
        }
        coordinate *tmp = (coordinate *)realloc(pt->items, (pt->count + 1) * sizeof(coordinate));
        if (tmp == NULL) {
            free(name);
            sc = ALLOC_ERROR;
            break;
        }
        pt->items = tmp;
        pt->items[pt->count].name = name;
        pt->items[pt->count].value = value;
        pt->count++;
        if (pos < end) {
            pos++;   /* ',' */
            if (pos == end) {
                sc = SYNTAX_ERROR;   /* запятая перед '}' */
            }
        }
    }
    if (sc != OK) {
        point_free(pt);
    }
    return sc;
}

/* Имя переменной (параметр PartDeriv, PartInteg, Cmps) -> копия в нижнем регистре */
status_code parse_name(const char *s, const size_t length, char **name) {
    if (s == NULL || name == NULL) {
        return INVALID_ARGS;
    }
    if (length == 0) {
        return SYNTAX_ERROR;
    }
    int index = 0;
    char_index(s[0], &index);
    if (index < 10) {
        return SYNTAX_ERROR;   /* имя начинается только с буквы */
    }
    for (size_t i = 0; i < length; i++) {
        char_index(s[i], &index);
        if (index < 0) {
            return SYNTAX_ERROR;
        }
    }
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < length; i++) {
        copy[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] - 'A' + 'a') : s[i];
    }
    copy[length] = '\0';
    *name = copy;
    return OK;
}

/* ===================== файл: комментарии и инструкции ===================== */

/*
 * Убрать комментарии: % - до конца строки; [ ... ] - вложенные. Переносы строк остаются (для номеров строк).
 * *line - строка ошибки (']' без '[' или незакрытый '[').
 */
status_code strip_comments(const char *text, const size_t length, char **clean, size_t *clean_length, size_t *line) {
    if (text == NULL || clean == NULL || clean_length == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    char *out = (char *)malloc(length + 1);
    if (out == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    size_t depth = 0;          /* глубина вложенности [ ] */
    size_t open_line = 0;      /* строка самого внешнего незакрытого '[' */
    *line = 1;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\n') {
            (*line)++;
            out[n++] = c;
            continue;
        }
        if (depth == 0 && c == '%') {
            while (i + 1 < length && text[i + 1] != '\n') {
                i++;
            }
            continue;
        }
        if (c == '[') {
            if (depth == 0) {
                open_line = *line;
            }
            depth++;
            continue;
        }
        if (c == ']') {
            if (depth == 0) {
                free(out);
                return COMMENT_ERROR;
            }
            depth--;
            continue;
        }
        if (depth == 0) {
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

/* Убрать пробельные символы из куска текста -> новая строка */
status_code without_spaces(const char *s, const size_t length, char **result, size_t *result_length) {
    if (s == NULL || result == NULL || result_length == NULL) {
        return INVALID_ARGS;
    }
    char *out = (char *)malloc(length + 1);
    if (out == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    for (size_t i = 0; i < length; i++) {
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r' && s[i] != '\v' && s[i] != '\f') {
            out[n++] = s[i];
        }
    }
    out[n] = '\0';
    *result = out;
    *result_length = n;
    return OK;
}

/* Состояние интерпретатора */
typedef struct {
    polynomial accumulator;
    double eps;
} machine;

/* Что напечатать после инструкции */
typedef struct {
    int kind;            /* 0 - сумматор, 1 - число (Eval), 2 - градиент (Grad) */
    double value;        /* Eval: значение */
    char **names;        /* Grad: имена переменных (копии) и значения производных */
    double *values;
    size_t count;
} command_output;

status_code output_free(command_output *output) {
    if (output == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < output->count; i++) {
        free(output->names[i]);
    }
    free(output->names);
    free(output->values);
    output->names = NULL;
    output->values = NULL;
    output->count = 0;
    return OK;
}

/* Копия строки в динамической памяти */
status_code copy_string(const char *s, char **copy) {
    if (s == NULL || copy == NULL) {
        return INVALID_ARGS;
    }
    *copy = (char *)malloc(strlen(s) + 1);
    if (*copy == NULL) {
        return ALLOC_ERROR;
    }
    strcpy(*copy, s);
    return OK;
}

/* Градиент многочлена p в точке: имена переменных по алфавиту и значения частных производных */
status_code poly_gradient(const polynomial *p, const point *pt, command_output *output, char **missing,
                          const double eps) {
    if (p == NULL || pt == NULL || output == NULL || missing == NULL) {
        return INVALID_ARGS;
    }
    const char **names = NULL;
    size_t n = 0;
    status_code sc = poly_variables(p, &names, &n);
    if (sc != OK) {
        return sc;
    }
    output->names = n > 0 ? (char **)calloc(n, sizeof(char *)) : NULL;
    output->values = n > 0 ? (double *)malloc(n * sizeof(double)) : NULL;
    if (n > 0 && (output->names == NULL || output->values == NULL)) {
        sc = ALLOC_ERROR;
    }
    for (size_t i = 0; i < n && sc == OK; i++) {
        output->count = i + 1;
        sc = copy_string(names[i], &output->names[i]);
        polynomial d;
        if (sc == OK) {
            sc = poly_derive(p, names[i], 0, &d, eps);
        }
        if (sc == OK) {
            const char *absent = NULL;
            sc = poly_evaluate(&d, pt, &output->values[i], &absent);
            if (sc == NO_VALUE) {
                copy_string(absent, missing);   /* имя указывает в d - копируем до освобождения d */
            }
            poly_clear(&d);
        }
    }
    free((void *)names);
    if (sc != OK) {
        output_free(output);
    }
    return sc;
}

/*
 * Выполнить одну инструкцию command(args). args - текст между скобками без пробелов.
 * Параметры разделены ',' (запятые внутри {...} не считаются). Если параметров на один меньше полного
 * числа, первый операнд - сумматор. *missing - имя переменной без значения (при NO_VALUE, освобождает
 * вызывающий).
 */
status_code execute(machine *m, const char *command, const char *args, const size_t args_length,
                    command_output *output, char **missing) {
    if (m == NULL || command == NULL || args == NULL || output == NULL || missing == NULL) {
        return INVALID_ARGS;
    }
    output->kind = 0;
    output->names = NULL;
    output->values = NULL;
    output->count = 0;
    *missing = NULL;

    /* делим параметры по запятым вне фигурных скобок */
    size_t starts[3];
    size_t lengths[3];
    size_t count = 0;
    size_t start = 0;
    int braces = 0;
    for (size_t i = 0; i <= args_length && args_length > 0; i++) {
        if (i < args_length && args[i] == '{') {
            braces++;
        } else if (i < args_length && args[i] == '}') {
            braces--;
        }
        if (i == args_length || (args[i] == ',' && braces == 0)) {
            if (count == 3) {
                return ARGS_COUNT;
            }
            starts[count] = start;
            lengths[count] = i - start;
            count++;
            start = i + 1;
        }
    }

    int binary = strcmp(command, "Add") == 0 || strcmp(command, "Sub") == 0 || strcmp(command, "Mult") == 0;
    int at_point = strcmp(command, "Eval") == 0 || strcmp(command, "Grad") == 0;
    int by_name = strcmp(command, "PartDeriv") == 0 || strcmp(command, "PartInteg") == 0;
    int compose = strcmp(command, "Cmps") == 0;
    if (!binary && !at_point && !by_name && !compose) {
        return UNKNOWN_COMMAND;
    }
    /* полное число параметров: Cmps - 3, остальные - 2; на один меньше - первый берётся из сумматора */
    size_t full = compose ? 3 : 2;
    if (count != full && count != full - 1) {
        return ARGS_COUNT;
    }

    polynomial first;
    poly_init(&first);
    size_t next = 0;
    status_code sc = OK;
    if (count == full - 1) {
        sc = poly_add_to(&first, &m->accumulator, 1.0, m->eps);
    } else {
        sc = parse_polynomial(args + starts[0], lengths[0], &first, m->eps);
        next = 1;
    }

    polynomial result;
    poly_init(&result);
    if (sc == OK && binary) {
        polynomial second;
        sc = parse_polynomial(args + starts[next], lengths[next], &second, m->eps);
        if (sc == OK) {
            if (strcmp(command, "Mult") == 0) {
                sc = poly_multiply(&first, &second, &result, m->eps);
            } else {
                sc = poly_add(&first, &second, strcmp(command, "Add") == 0 ? 1.0 : -1.0, &result, m->eps);
            }
            poly_clear(&second);
        }
    } else if (sc == OK && at_point) {
        point pt;
        sc = parse_point(args + starts[next], lengths[next], &pt);
        int point_read = (sc == OK);   /* при ошибке parse_point уже освободила точку сама */
        if (sc == OK && strcmp(command, "Eval") == 0) {
            const char *absent = NULL;
            sc = poly_evaluate(&first, &pt, &output->value, &absent);
            if (sc == NO_VALUE) {
                copy_string(absent, missing);
            }
            if (sc == OK) {
                /* число, которое меньше eps по модулю, - это ноль (без "-0") */
                if (fabs(output->value) < m->eps) {
                    output->value = 0.0;
                }
                output->kind = 1;
                sc = poly_constant(output->value, &result, m->eps);   /* результат - в сумматор */
            }
        } else if (sc == OK) {
            sc = poly_gradient(&first, &pt, output, missing, m->eps);
            if (sc == OK) {
                output->kind = 2;
                sc = poly_add_to(&result, &m->accumulator, 1.0, m->eps);   /* сумматор не меняется */
            }
        }
        if (point_read) {
            point_free(&pt);
        }
    } else if (sc == OK && by_name) {
        char *name = NULL;
        sc = parse_name(args + starts[next], lengths[next], &name);
        if (sc == OK) {
            sc = poly_derive(&first, name, strcmp(command, "PartInteg") == 0, &result, m->eps);
            free(name);
        }
    } else if (sc == OK && compose) {
        char *name = NULL;
        polynomial g;
        poly_init(&g);
        sc = parse_name(args + starts[next], lengths[next], &name);
        if (sc == OK) {
            sc = parse_polynomial(args + starts[next + 1], lengths[next + 1], &g, m->eps);
        }
        if (sc == OK) {
            sc = poly_compose(&first, name, &g, &result, m->eps);
        }
        free(name);
        poly_clear(&g);
    }
    poly_clear(&first);

    if (sc == OK) {
        poly_clear(&m->accumulator);
        m->accumulator = result;
    } else {
        poly_clear(&result);
        output_free(output);
    }
    return sc;
}

/* Текст ошибки по статусу */
status_code status_text(const status_code sc, const char **text) {
    if (text == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case SYNTAX_ERROR:    *text = "syntax error"; break;
        case UNKNOWN_COMMAND: *text = "unknown instruction"; break;
        case ARGS_COUNT:      *text = "wrong number of parameters"; break;
        case NEGATIVE_DEGREE: *text = "the degree of a variable is negative"; break;
        case DEGREE_TOO_BIG:  *text = "the degree is too big (more than 10000)"; break;
        case TOO_MANY_TERMS:  *text = "the polynomial is too big (more than 100000 monomials)"; break;
        case NO_VALUE:        *text = "no value for the variable"; break;
        case DUPLICATE_NAME:  *text = "a variable is given twice in the point"; break;
        case NUMBER_ERROR:    *text = "the number is too big"; break;
        case COMMENT_ERROR:   *text = "wrong brackets of a comment"; break;
        case FILE_ERROR:      *text = "can not read the file"; break;
        case ALLOC_ERROR:     *text = "not enough memory"; break;
        default:              *text = "internal error"; break;   /* OK и INVALID_ARGS сюда не попадают */
    }
    return OK;
}

/* Напечатать результат инструкции: значение сумматора, число (Eval) или градиент (Grad) */
status_code print_output(FILE *stream, const char *command, const command_output *output,
                         const polynomial *accumulator, const double eps, text_buffer *text) {
    if (stream == NULL || command == NULL || output == NULL || accumulator == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    if (output->kind == 1) {
        fprintf(stream, "%s: %.10g\n", command, output->value);
        return OK;
    }
    if (output->kind == 2) {
        fprintf(stream, "%s: (", command);
        for (size_t i = 0; i < output->count; i++) {
            double v = fabs(output->values[i]) < eps ? 0.0 : output->values[i];
            fprintf(stream, "%sd/d%s = %.10g", i == 0 ? "" : ", ", output->names[i], v);
        }
        fprintf(stream, ")\n");
        return OK;
    }
    status_code sc = poly_to_text(accumulator, eps, text);
    if (sc == OK) {
        fprintf(stream, "%s: %s\n", command, text->data);
    }
    return sc;
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

/*
 * Выполнить все инструкции текста (комментарии уже убраны). Инструкция: Имя(параметры);
 * Ошибка в инструкции - сообщение с номером инструкции и строки, сумматор не меняется, дальше следующая.
 */
status_code run_program(machine *m, const char *text, const size_t length, size_t *errors) {
    if (m == NULL || text == NULL || errors == NULL) {
        return INVALID_ARGS;
    }
    text_buffer out;
    status_code sc = text_init(&out);
    if (sc != OK) {
        return sc;
    }
    *errors = 0;
    size_t pos = 0;
    size_t line = 1;
    size_t number = 0;
    while (sc == OK) {
        /* пробелы перед инструкцией */
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
        /* конец инструкции - ';' */
        size_t end = pos;
        while (end < length && text[end] != ';') {
            if (text[end] == '\n') {
                line++;
            }
            end++;
        }
        status_code result = OK;
        char command[16] = "";
        char *args = NULL;
        size_t args_length = 0;
        if (end == length) {
            result = SYNTAX_ERROR;   /* нет ';' в конце */
        } else {
            /* Имя(параметры) - без пробелов внутри */
            char *clean = NULL;
            size_t clean_length = 0;
            result = without_spaces(text + pos, end - pos, &clean, &clean_length);
            if (result == OK) {
                size_t k = 0;
                while (k < clean_length && ((clean[k] >= 'A' && clean[k] <= 'Z') || (clean[k] >= 'a' && clean[k] <= 'z'))) {
                    k++;
                }
                /* имя из букв, затем '(' ... ')' до самого конца инструкции */
                if (k == 0 || k >= sizeof(command) || k >= clean_length || clean[k] != '(' ||
                    clean[clean_length - 1] != ')') {
                    result = SYNTAX_ERROR;
                } else {
                    memcpy(command, clean, k);
                    command[k] = '\0';
                    args_length = clean_length - k - 2;
                    args = (char *)malloc(args_length + 1);
                    if (args == NULL) {
                        result = ALLOC_ERROR;
                    } else {
                        memcpy(args, clean + k + 1, args_length);
                        args[args_length] = '\0';
                    }
                }
                free(clean);
            }
        }
        command_output output;
        char *missing = NULL;
        output.count = 0;
        output.names = NULL;
        output.values = NULL;
        if (result == OK) {
            result = execute(m, command, args, args_length, &output, &missing);
        }
        free(args);
        if (result == ALLOC_ERROR) {
            sc = result;
        } else if (result != OK) {
            const char *reason = NULL;
            status_text(result, &reason);
            fflush(stdout);
            fprintf(stderr, "Error in instruction %zu (line %zu): %s%s%s\n", number, start_line, reason,
                    missing != NULL ? " " : "", missing != NULL ? missing : "");
            (*errors)++;
        } else {
            sc = print_output(stdout, command, &output, &m->accumulator, m->eps, &out);
            output_free(&output);
        }
        free(missing);
        pos = end < length ? end + 1 : end;
    }
    free(out.data);
    return sc;
}

int main(int argc, char *argv[]) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: lab4_task3 <instructions file> [eps]\n");
        return 1;
    }
    double eps = DEFAULT_EPS;
    if (argc == 3) {
        char *stop = NULL;
        eps = strtod(argv[2], &stop);
        if (argv[2][0] == '\0' || *stop != '\0' || !isfinite(eps) || eps <= 0 || eps >= 1) {
            fprintf(stderr, "Error: eps must be a number between 0 and 1\n");
            return 1;
        }
    }
    char *text = NULL;
    size_t length = 0;
    status_code sc = read_whole_file(argv[1], &text, &length);
    if (sc != OK) {
        fprintf(stderr, sc == FILE_ERROR ? "Error: can not read file '%s'\n" : "Error: not enough memory%s\n",
                sc == FILE_ERROR ? argv[1] : "");
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

    machine m;
    poly_init(&m.accumulator);
    m.eps = eps;
    size_t errors = 0;
    sc = run_program(&m, clean, clean_length, &errors);
    poly_clear(&m.accumulator);
    free(clean);
    if (sc != OK) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    return errors > 0 ? 1 : 0;
}
