/*
 * Лабораторная 4, задание 9.1. Модель центра клиентской поддержки.
 *
 * Запуск:  lab4_task9 <max_priority> <файл параметров> <файл заявок> [<файл заявок> ...] [--log <файл>] [--seed N]
 *   max_priority - натуральное число, приоритет заявки - от 0 (низший) до max_priority (высший);
 *   --log  - журнал в файл (без ключа - в стандартный вывод);
 *   --seed - начальное число генератора (имена операторов и время обработки); без ключа - текущее время.
 *          lab4_task9 --self-test [--seed N]
 *   самопроверка: все операции интерфейса у всех шести очередей и четырёх словарей (в модели нужны не все).
 *
 * Файл параметров (по строке на параметр, пустые строки пропускаются):
 *   структура для заявок отделения: BinaryHeap, BinomialHeap, FibonacciHeap, SkewHeap, LeftistHeap или Treap;
 *   структура "идентификатор отделения -> его очередь": HashSet, DynamicArray, BinarySearchTree или Trie;
 *   начало и конец моделирования (ISO 8601: 2026-11-01T09:00:00, секунды можно не писать);
 *   минимальное и максимальное время обработки заявки (минуты); число отделений (1..100);
 *   числа операторов отделений через пробел (каждое 10..50); коэффициент перегрузки (1.0 и больше).
 * Строка файла заявок:  <время> <приоритет> <идентификатор отделения> "<текст>"   (отделения - 1, 2, ..., n).
 *
 * Время дискретно, шаг - минута: заявка, пришедшая между шагами, поступает на ближайшем следующем шаге.
 * На каждом шаге: 1) операторы, у которых кончилось время, освобождаются; 2) свободные операторы берут заявки;
 * 3) поступают новые заявки (по одной): заявка кладётся в очередь отделения, свободные операторы её берут;
 * если в очереди стало не меньше, чем operators * overload_coeff, отделение перегружено - вся его очередь
 * переходит (meld) наименее загруженному отделению, которое после этого само не станет перегруженным.
 * Порядок в очереди: больший приоритет раньше, при равном - раньше поступившая (меньший номер заявки).
 * Шаги, на которых ничего не происходит, пропускаются: сразу к ближайшему поступлению или окончанию обработки.
 *
 * Шесть очередей и четыре словаря реализуют общие интерфейсы - таблицы указателей на функции (heap_ops,
 * map_ops); остальная программа работает только через интерфейс и не знает, какая структура выбрана.
 */
#define _POSIX_C_SOURCE 200809L   /* stat (Linux) - до всех #include, см. same_file */

#include <stdio.h>
#include <stdlib.h>   /* malloc, calloc, realloc, free, qsort */
#include <string.h>   /* strcmp, strlen, strcpy, memcpy, memmove */
#include <limits.h>   /* LLONG_MAX, INT_MAX */
#include <time.h>     /* time - начальное число генератора без --seed */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define MAX_DEPARTMENTS 100
#define MIN_OPERATORS 10
#define MAX_OPERATORS 50
#define MAX_HANDLING_TIME 1000000LL   /* минут (почти 2 года) - больше не нужно, и нет переполнений */
#define MAX_COEFF_DIGITS 6            /* знаков после точки в коэффициенте перегрузки */
#define MAX_COEFF_WHOLE 1000LL        /* наибольшая целая часть коэффициента */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,    /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,    /* строка записана неправильно */
    KEY_EXISTS,      /* в словаре уже есть такой ключ */
    EARLY_END,       /* файл параметров кончился раньше, чем нашлись все параметры */
    FILE_ERROR,      /* файл не открылся / ошибка чтения или записи */
    ALLOC_ERROR      /* malloc / realloc не смогли выделить память */
} status_code;

/* Заявка */
typedef struct {
    long long id;          /* номер заявки - по порядку поступления в систему (1, 2, ...); 0 - ещё не поступила */
    long long arrival;     /* время из файла (секунды от 0001-01-01T00:00:00) */
    long long step;        /* шаг модели, на котором заявка поступает (arrival, округлённое вверх до минуты) */
    int priority;
    size_t department;     /* индекс отделения (0..n-1) */
    size_t order;          /* номер строки среди всех файлов - при равном времени раньше записанная */
    char *text;
} request;

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

/* ===================== порядок заявок ===================== */

/* a важнее b: больше приоритет, при равном - раньше поступила (меньше номер) */
status_code more_important(const request *a, const request *b, int *result) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    *result = a->priority > b->priority || (a->priority == b->priority && a->id < b->id);
    return OK;
}

/* ===================== двоичная куча (BinaryHeap) ===================== */

/*
 * Двоичная куча в массиве: у элемента i дети 2i+1 и 2i+2, родитель (i-1)/2; каждый элемент важнее своих детей,
 * поэтому самый важный - items[0].
 */
typedef struct {
    request **items;
    size_t size;
    size_t capacity;
} binary_heap;

status_code binary_create(void **heap) {
    if (heap == NULL) {
        return INVALID_ARGS;
    }
    binary_heap *h = (binary_heap *)calloc(1, sizeof(binary_heap));
    if (h == NULL) {
        return ALLOC_ERROR;
    }
    *heap = h;
    return OK;
}

/* Место хотя бы для need элементов: ёмкость растёт вдвое (realloc через временную переменную) */
status_code binary_reserve(binary_heap *h, const size_t need) {
    if (h == NULL) {
        return INVALID_ARGS;
    }
    if (need <= h->capacity) {
        return OK;
    }
    size_t capacity = h->capacity == 0 ? 16 : h->capacity;
    while (capacity < need) {
        capacity *= 2;
    }
    request **tmp = (request **)realloc(h->items, capacity * sizeof(request *));
    if (tmp == NULL) {
        return ALLOC_ERROR;
    }
    h->items = tmp;
    h->capacity = capacity;
    return OK;
}

/* Подъём: элемент меняется с родителем, пока он важнее родителя */
status_code binary_sift_up(binary_heap *h, size_t i) {
    if (h == NULL || i >= h->size) {
        return INVALID_ARGS;
    }
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        int better = 0;
        more_important(h->items[i], h->items[parent], &better);
        if (!better) {
            break;
        }
        request *tmp = h->items[i];
        h->items[i] = h->items[parent];
        h->items[parent] = tmp;
        i = parent;
    }
    return OK;
}

/* Спуск: элемент меняется с самым важным ребёнком, пока ребёнок важнее его */
status_code binary_sift_down(binary_heap *h, size_t i) {
    if (h == NULL) {
        return INVALID_ARGS;
    }
    for (;;) {
        size_t best = i;
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        int better = 0;
        if (left < h->size) {
            more_important(h->items[left], h->items[best], &better);
            if (better) {
                best = left;
            }
        }
        if (right < h->size) {
            more_important(h->items[right], h->items[best], &better);
            if (better) {
                best = right;
            }
        }
        if (best == i) {
            break;
        }
        request *tmp = h->items[i];
        h->items[i] = h->items[best];
        h->items[best] = tmp;
        i = best;
    }
    return OK;
}

/* Добавить: в конец массива и поднять */
status_code binary_insert(void *heap, request *item) {
    binary_heap *h = (binary_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = binary_reserve(h, h->size + 1);
    if (sc != OK) {
        return sc;
    }
    h->items[h->size++] = item;
    return binary_sift_up(h, h->size - 1);
}

/* Самая важная заявка (не удаляя); *item = NULL - куча пуста */
status_code binary_top(const void *heap, request **item) {
    const binary_heap *h = (const binary_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    *item = h->size > 0 ? h->items[0] : NULL;
    return OK;
}

/* Извлечь самую важную: на её место - последний элемент, и спустить его */
status_code binary_extract(void *heap, request **item) {
    binary_heap *h = (binary_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    if (h->size == 0) {
        *item = NULL;
        return OK;
    }
    *item = h->items[0];
    h->items[0] = h->items[--h->size];
    return binary_sift_down(h, 0);
}

/*
 * Слияние с разрушением: элементы b дописываются в массив a (b становится пустой), затем куча a
 * перестраивается целиком (спуск всех элементов, у которых есть дети, от последнего к первому) - O(n).
 */
status_code binary_meld(void *heap_a, void *heap_b) {
    binary_heap *a = (binary_heap *)heap_a;
    binary_heap *b = (binary_heap *)heap_b;
    if (a == NULL || b == NULL || a == b) {
        return INVALID_ARGS;
    }
    status_code sc = binary_reserve(a, a->size + b->size);
    if (sc != OK) {
        return sc;
    }
    if (b->size > 0) {
        memcpy(a->items + a->size, b->items, b->size * sizeof(request *));
    }
    a->size += b->size;
    b->size = 0;
    for (size_t i = a->size / 2; i-- > 0;) {
        binary_sift_down(a, i);
    }
    return OK;
}

status_code binary_size(const void *heap, size_t *size) {
    const binary_heap *h = (const binary_heap *)heap;
    if (h == NULL || size == NULL) {
        return INVALID_ARGS;
    }
    *size = h->size;
    return OK;
}

/* Все заявки кучи (в любом порядке) - в массив out, в нём должно быть место для size заявок */
status_code binary_collect(const void *heap, request **out) {
    const binary_heap *h = (const binary_heap *)heap;
    if (h == NULL || (out == NULL && h->size > 0)) {
        return INVALID_ARGS;
    }
    if (h->size > 0) {
        memcpy(out, h->items, h->size * sizeof(request *));
    }
    return OK;
}

status_code binary_destroy(void *heap) {
    binary_heap *h = (binary_heap *)heap;
    if (h == NULL) {
        return OK;
    }
    free(h->items);
    free(h);
    return OK;
}

/* ===================== кучи из узлов: общий узел и обход ===================== */

/*
 * Один вид узла для пяти куч из узлов (биномиальной, фибоначчиевой, косой, левосторонней, декартова дерева):
 *   left  - левый ребёнок; у биномиальной и фибоначчиевой - первый ребёнок;
 *   right - правый ребёнок; у биномиальной - следующий брат; у фибоначчиевой - следующий узел в кольце братьев;
 *   prev  - только у фибоначчиевой: предыдущий узел в кольце;
 *   rank  - степень (число детей) у биномиальной и фибоначчиевой, ранг (длина правого пути) у левосторонней;
 *   weight - только у декартова дерева: случайный ключ кучи.
 */
typedef struct heap_node {
    request *item;
    struct heap_node *left;
    struct heap_node *right;
    struct heap_node *prev;
    size_t rank;
    unsigned long long weight;
} heap_node;

/* Куча из узлов: root - корень (у биномиальной - первый корень списка, у фибоначчиевой - самый важный корень) */
typedef struct {
    heap_node *root;
    size_t size;
    unsigned long long random_state;   /* генератор весов декартова дерева (свой, не общий генератор модели) */
} node_heap;

status_code node_heap_create(void **heap) {
    if (heap == NULL) {
        return INVALID_ARGS;
    }
    node_heap *h = (node_heap *)calloc(1, sizeof(node_heap));
    if (h == NULL) {
        return ALLOC_ERROR;
    }
    h->random_state = 0x2545F4914F6CDD1DULL;
    *heap = h;
    return OK;
}

status_code new_node(request *item, heap_node **node) {
    if (item == NULL || node == NULL) {
        return INVALID_ARGS;
    }
    heap_node *x = (heap_node *)calloc(1, sizeof(heap_node));
    if (x == NULL) {
        return ALLOC_ERROR;
    }
    x->item = item;
    *node = x;
    return OK;
}

/*
 * Все узлы кучи - в новый массив *nodes (h->size штук; освобождает вызывающий). Без рекурсии: в стеке лежат
 * "начала цепочек"; цепочка - узел, его right, right от right, ...; она кончается на NULL или (у фибоначчиевой
 * кучи, где братья - кольцо) снова на своём начале. У каждого узла цепочки его left - начало новой цепочки.
 * Так один обход годится и для двоичных деревьев (left/right), и для списков братьев, и для колец.
 */
status_code nodes_walk(const node_heap *h, heap_node ***nodes) {
    if (h == NULL || nodes == NULL) {
        return INVALID_ARGS;
    }
    *nodes = NULL;
    if (h->size == 0) {
        return OK;
    }
    heap_node **result = (heap_node **)malloc(h->size * sizeof(heap_node *));
    heap_node **stack = (heap_node **)malloc(h->size * sizeof(heap_node *));
    if (result == NULL || stack == NULL) {
        free(result);
        free(stack);
        return ALLOC_ERROR;
    }
    size_t count = 0;
    size_t top = 0;
    stack[top++] = h->root;
    while (top > 0) {
        heap_node *start = stack[--top];
        heap_node *x = start;
        do {
            /* узлов больше, чем size, быть не может - иначе куча испорчена (ошибка в самой программе) */
            if (count == h->size) {
                free(result);
                free(stack);
                return INVALID_ARGS;
            }
            result[count++] = x;
            if (x->left != NULL) {
                stack[top++] = x->left;
            }
            x = x->right;
        } while (x != NULL && x != start);
    }
    free(stack);
    *nodes = result;
    return OK;
}

status_code node_heap_size(const void *heap, size_t *size) {
    const node_heap *h = (const node_heap *)heap;
    if (h == NULL || size == NULL) {
        return INVALID_ARGS;
    }
    *size = h->size;
    return OK;
}

/* Все заявки кучи - в массив out (место для size заявок) */
status_code node_heap_collect(const void *heap, request **out) {
    const node_heap *h = (const node_heap *)heap;
    if (h == NULL || (out == NULL && h->size > 0)) {
        return INVALID_ARGS;
    }
    heap_node **nodes = NULL;
    status_code sc = nodes_walk(h, &nodes);
    if (sc != OK) {
        return sc;
    }
    for (size_t i = 0; i < h->size; i++) {
        out[i] = nodes[i]->item;
    }
    free(nodes);
    return OK;
}

/* Освободить кучу: сначала собрать все узлы обходом, потом освободить (заявки не освобождаются - они не её) */
status_code node_heap_destroy(void *heap) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL) {
        return OK;
    }
    heap_node **nodes = NULL;
    status_code sc = nodes_walk(h, &nodes);
    if (sc != OK) {
        return sc;
    }
    for (size_t i = 0; i < h->size; i++) {
        free(nodes[i]);
    }
    free(nodes);
    free(h);
    return OK;
}

/* Слияние куч из узлов одного вида: a и b - кучи; после слияния всё в a, b пустая */
status_code node_heap_take_all(node_heap *a, node_heap *b, heap_node *new_root) {
    if (a == NULL || b == NULL) {
        return INVALID_ARGS;
    }
    a->root = new_root;
    a->size += b->size;
    b->root = NULL;
    b->size = 0;
    return OK;
}

/* ===================== биномиальная куча (BinomialHeap) ===================== */

/*
 * Список корней биномиальных деревьев по возрастанию степени (right - следующий корень); у дерева степени k
 * 2^k узлов, дети корня - деревья степеней k-1, ..., 0 (left - старший ребёнок, дальше братья по right).
 */

/* Связать два дерева одной степени: менее важный корень становится старшим ребёнком более важного */
status_code binomial_link(heap_node *child, heap_node *parent) {
    if (child == NULL || parent == NULL) {
        return INVALID_ARGS;
    }
    child->right = parent->left;
    parent->left = child;
    parent->rank++;
    return OK;
}

/*
 * Объединение двух списков корней: 1) слить списки по возрастанию степени; 2) пройти по списку и связать
 * соседние деревья одинаковой степени (как сложение двоичных чисел с переносом).
 */
status_code binomial_union(heap_node *a, heap_node *b, heap_node **result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    /* 1) слияние упорядоченных списков */
    heap_node *head = NULL;
    heap_node **tail = &head;
    while (a != NULL && b != NULL) {
        if (a->rank <= b->rank) {
            *tail = a;
            a = a->right;
        } else {
            *tail = b;
            b = b->right;
        }
        tail = &(*tail)->right;
    }
    *tail = a != NULL ? a : b;
    if (head == NULL) {
        *result = NULL;
        return OK;
    }
    /* 2) связывание одинаковых степеней */
    heap_node *prev = NULL;
    heap_node *x = head;
    heap_node *next = x->right;
    while (next != NULL) {
        int same_three = next->right != NULL && next->right->rank == x->rank;
        if (x->rank != next->rank || same_three) {
            prev = x;           /* степени разные (или впереди три одинаковых) - идём дальше */
            x = next;
        } else {
            int better = 0;
            more_important(x->item, next->item, &better);
            if (better) {
                x->right = next->right;
                binomial_link(next, x);
            } else {
                if (prev == NULL) {
                    head = next;
                } else {
                    prev->right = next;
                }
                binomial_link(x, next);
                x = next;
            }
        }
        next = x->right;
    }
    *result = head;
    return OK;
}

/* Добавить: куча из одного узла объединяется с данной */
status_code binomial_insert(void *heap, request *item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *node = NULL;
    status_code sc = new_node(item, &node);
    if (sc != OK) {
        return sc;
    }
    h->size++;
    return binomial_union(h->root, node, &h->root);
}

/* Самый важный корень и корень перед ним (*before = NULL - он первый) */
status_code binomial_best(const node_heap *h, heap_node **best, heap_node **before) {
    if (h == NULL || best == NULL || before == NULL) {
        return INVALID_ARGS;
    }
    *best = h->root;
    *before = NULL;
    if (h->root == NULL) {
        return OK;
    }
    heap_node *prev = h->root;
    for (heap_node *x = h->root->right; x != NULL; prev = x, x = x->right) {
        int better = 0;
        more_important(x->item, (*best)->item, &better);
        if (better) {
            *best = x;
            *before = prev;
        }
    }
    return OK;
}

status_code binomial_top(const void *heap, request **item) {
    const node_heap *h = (const node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *best = NULL;
    heap_node *before = NULL;
    binomial_best(h, &best, &before);
    *item = best != NULL ? best->item : NULL;
    return OK;
}

/* Извлечь: убрать самый важный корень из списка, его детей (в обратном порядке - по возрастанию) слить с кучей */
status_code binomial_extract(void *heap, request **item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *best = NULL;
    heap_node *before = NULL;
    binomial_best(h, &best, &before);
    if (best == NULL) {
        *item = NULL;
        return OK;
    }
    if (before == NULL) {
        h->root = best->right;
    } else {
        before->right = best->right;
    }
    /* дети идут от старшего к младшему - разворачиваем список, чтобы степени возрастали */
    heap_node *reversed = NULL;
    heap_node *c = best->left;
    while (c != NULL) {
        heap_node *next = c->right;
        c->right = reversed;
        reversed = c;
        c = next;
    }
    *item = best->item;
    free(best);
    h->size--;
    return binomial_union(h->root, reversed, &h->root);
}

status_code binomial_meld(void *heap_a, void *heap_b) {
    node_heap *a = (node_heap *)heap_a;
    node_heap *b = (node_heap *)heap_b;
    if (a == NULL || b == NULL || a == b) {
        return INVALID_ARGS;
    }
    heap_node *root = NULL;
    binomial_union(a->root, b->root, &root);
    return node_heap_take_all(a, b, root);
}

/* ===================== фибоначчиева куча (FibonacciHeap) ===================== */

/*
 * Корни - кольцо (right - следующий, prev - предыдущий), root - самый важный корень. Добавление и слияние -
 * просто склеить кольца, O(1). Вся работа - при извлечении: дети извлечённого корня становятся корнями,
 * затем корни одинаковой степени связываются попарно (consolidate), пока степени всех корней не станут разными.
 * Пометки узлов (mark) нужны только для уменьшения ключа и удаления произвольного узла - этих операций
 * в интерфейсе очереди нет, поэтому пометок нет.
 */

/* Склеить два кольца в одно: a -> b -> ... -> (последний из b) -> (следующий за a) -> ... -> a */
status_code ring_join(heap_node *a, heap_node *b) {
    if (a == NULL || b == NULL) {
        return INVALID_ARGS;
    }
    heap_node *a_next = a->right;
    heap_node *b_prev = b->prev;
    a->right = b;
    b->prev = a;
    b_prev->right = a_next;
    a_next->prev = b_prev;
    return OK;
}

/* Добавить кольцо корней в кучу (корни уже в кольце) и обновить самый важный корень */
status_code fibonacci_add_roots(node_heap *h, heap_node *ring) {
    if (h == NULL || ring == NULL) {
        return INVALID_ARGS;
    }
    if (h->root == NULL) {
        h->root = ring;
        return OK;
    }
    ring_join(h->root, ring);
    int better = 0;
    more_important(ring->item, h->root->item, &better);
    if (better) {
        h->root = ring;
    }
    return OK;
}

status_code fibonacci_insert(void *heap, request *item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *node = NULL;
    status_code sc = new_node(item, &node);
    if (sc != OK) {
        return sc;
    }
    node->right = node;   /* кольцо из одного узла */
    node->prev = node;
    h->size++;
    return fibonacci_add_roots(h, node);
}

status_code fibonacci_top(const void *heap, request **item) {
    const node_heap *h = (const node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    *item = h->root != NULL ? h->root->item : NULL;
    return OK;
}

/*
 * Уплотнение: корни по очереди кладутся в таблицу по степени; если место уже занято деревом той же степени,
 * два дерева связываются (менее важный корень - в кольцо детей более важного), степень растёт на 1, и
 * попытка повторяется. В конце из таблицы собирается новое кольцо корней. roots и table - заранее выделенные
 * массивы (на h->size и limit мест), чтобы на середине работы не случилось ошибки памяти.
 */
status_code fibonacci_consolidate(node_heap *h, heap_node **roots, heap_node **table, const size_t limit) {
    if (h == NULL || roots == NULL || table == NULL || h->root == NULL) {
        return INVALID_ARGS;
    }
    /* корни - в массив (кольцо будет разобрано) */
    size_t n = 0;
    heap_node *x = h->root;
    do {
        roots[n++] = x;
        x = x->right;
    } while (x != h->root && n < h->size);
    for (size_t i = 0; i < limit; i++) {
        table[i] = NULL;
    }
    for (size_t i = 0; i < n; i++) {
        heap_node *tree = roots[i];
        tree->right = tree;   /* пока каждое дерево - отдельное кольцо из одного корня */
        tree->prev = tree;
        size_t d = tree->rank;
        while (d < limit && table[d] != NULL) {
            heap_node *other = table[d];
            table[d] = NULL;
            int better = 0;
            more_important(other->item, tree->item, &better);
            if (better) {
                heap_node *tmp = tree;
                tree = other;
                other = tmp;
            }
            /* other - ребёнок tree */
            if (tree->left == NULL) {
                tree->left = other;
            } else {
                ring_join(tree->left, other);
            }
            tree->rank++;
            d++;
        }
        /* степень больше границы невозможна (граница с запасом) - ошибка в самой программе */
        if (d >= limit) {
            return INVALID_ARGS;
        }
        table[d] = tree;
    }
    /* новое кольцо корней */
    h->root = NULL;
    for (size_t d = 0; d < limit; d++) {
        if (table[d] != NULL) {
            fibonacci_add_roots(h, table[d]);
        }
    }
    return OK;
}

status_code fibonacci_extract(void *heap, request **item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *z = h->root;
    if (z == NULL) {
        *item = NULL;
        return OK;
    }
    /* память для уплотнения - до изменения кучи. Степень корня не больше 1.44*log2(n) < 2*log2(n) + 2 */
    size_t limit = 2;
    for (size_t s = h->size; s > 0; s /= 2) {
        limit += 2;
    }
    heap_node **roots = (heap_node **)malloc(h->size * sizeof(heap_node *));
    heap_node **table = (heap_node **)malloc(limit * sizeof(heap_node *));
    if (roots == NULL || table == NULL) {
        free(roots);
        free(table);
        return ALLOC_ERROR;
    }
    /* дети z - в кольцо корней */
    if (z->left != NULL) {
        ring_join(z, z->left);
        z->left = NULL;
    }
    /* убрать z из кольца; z был один и без детей - куча пуста, иначе корни уплотняются */
    status_code sc = OK;
    h->size--;
    if (z->right == z) {
        h->root = NULL;
    } else {
        z->prev->right = z->right;
        z->right->prev = z->prev;
        h->root = z->right;
        sc = fibonacci_consolidate(h, roots, table, limit);
    }
    *item = z->item;
    free(z);
    free(roots);
    free(table);
    return sc;
}

status_code fibonacci_meld(void *heap_a, void *heap_b) {
    node_heap *a = (node_heap *)heap_a;
    node_heap *b = (node_heap *)heap_b;
    if (a == NULL || b == NULL || a == b) {
        return INVALID_ARGS;
    }
    if (b->root != NULL) {
        fibonacci_add_roots(a, b->root);
    }
    return node_heap_take_all(a, b, a->root);
}

/* ===================== косая куча (SkewHeap) ===================== */

/*
 * Двоичное дерево, каждый узел важнее детей. Слияние: идём вниз от корней двух куч, каждый раз берём более
 * важный корень, его правое поддерево сливаем дальше со второй кучей, а детей меняем местами (новое слитое
 * поддерево - левое). Без рекурсии: link - куда прицепить следующий узел пути.
 * Рекурсия здесь опасна: правый путь косой кучи бывает длиной в n узлов.
 */
status_code skew_merge(heap_node *a, heap_node *b, heap_node **result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    heap_node *root = NULL;
    heap_node **link = &root;
    while (a != NULL && b != NULL) {
        int better = 0;
        more_important(b->item, a->item, &better);
        if (better) {
            heap_node *tmp = a;
            a = b;
            b = tmp;
        }
        /* a - более важный корень: он встаёт на место link; старое правое поддерево - дальше на слияние */
        *link = a;
        heap_node *rest = a->right;
        a->right = a->left;
        link = &a->left;
        a = rest;
    }
    *link = a != NULL ? a : b;
    *result = root;
    return OK;
}

status_code skew_insert(void *heap, request *item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *node = NULL;
    status_code sc = new_node(item, &node);
    if (sc != OK) {
        return sc;
    }
    h->size++;
    return skew_merge(h->root, node, &h->root);
}

/* Корень - самый важный (так у косой, левосторонней кучи) */
status_code root_top(const void *heap, request **item) {
    const node_heap *h = (const node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    *item = h->root != NULL ? h->root->item : NULL;
    return OK;
}

/* Извлечь: корень убрать, слить его поддеревья */
status_code skew_extract(void *heap, request **item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *top = h->root;
    if (top == NULL) {
        *item = NULL;
        return OK;
    }
    *item = top->item;
    skew_merge(top->left, top->right, &h->root);
    free(top);
    h->size--;
    return OK;
}

status_code skew_meld(void *heap_a, void *heap_b) {
    node_heap *a = (node_heap *)heap_a;
    node_heap *b = (node_heap *)heap_b;
    if (a == NULL || b == NULL || a == b) {
        return INVALID_ARGS;
    }
    heap_node *root = NULL;
    skew_merge(a->root, b->root, &root);
    return node_heap_take_all(a, b, root);
}

/* ===================== левосторонняя куча (LeftistHeap) ===================== */

/*
 * Ранг узла - длина пути вправо до пустого места (у пустого 0, у листа 1). В левосторонней куче ранг левого
 * ребёнка не меньше ранга правого, поэтому правый путь короткий: не больше log2(n+1) узлов. Слияние идёт по
 * правым путям обеих куч (как у косой), затем на обратном пути у каждого узла пути ранги детей сравниваются
 * (меньший - вправо) и ранг пересчитывается. Путь запоминается в массиве: в нём не больше rank(a) + rank(b) узлов.
 */
size_t leftist_rank(const heap_node *x) {
    return x != NULL ? x->rank : 0;
}

status_code leftist_merge(heap_node *a, heap_node *b, heap_node **result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    heap_node **path = (heap_node **)malloc((leftist_rank(a) + leftist_rank(b) + 1) * sizeof(heap_node *));
    if (path == NULL) {
        return ALLOC_ERROR;
    }
    size_t k = 0;
    heap_node *root = NULL;
    heap_node **link = &root;
    /* вниз по правым путям: более важный корень встаёт на место, дальше сливаем его правое поддерево */
    while (a != NULL && b != NULL) {
        int better = 0;
        more_important(b->item, a->item, &better);
        if (better) {
            heap_node *tmp = a;
            a = b;
            b = tmp;
        }
        *link = a;
        path[k++] = a;
        link = &a->right;
        a = a->right;
    }
    *link = a != NULL ? a : b;
    /* обратно вверх: восстановить "левосторонность" и ранги */
    while (k > 0) {
        heap_node *x = path[--k];
        if (leftist_rank(x->left) < leftist_rank(x->right)) {
            heap_node *tmp = x->left;
            x->left = x->right;
            x->right = tmp;
        }
        x->rank = leftist_rank(x->right) + 1;
    }
    free(path);
    *result = root;
    return OK;
}

status_code leftist_insert(void *heap, request *item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *node = NULL;
    status_code sc = new_node(item, &node);
    if (sc != OK) {
        return sc;
    }
    node->rank = 1;
    sc = leftist_merge(h->root, node, &h->root);
    if (sc != OK) {
        free(node);
        return sc;
    }
    h->size++;
    return OK;
}

status_code leftist_extract(void *heap, request **item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *top = h->root;
    if (top == NULL) {
        *item = NULL;
        return OK;
    }
    heap_node *root = NULL;
    status_code sc = leftist_merge(top->left, top->right, &root);
    if (sc != OK) {
        return sc;   /* куча не изменилась */
    }
    *item = top->item;
    h->root = root;
    free(top);
    h->size--;
    return OK;
}

status_code leftist_meld(void *heap_a, void *heap_b) {
    node_heap *a = (node_heap *)heap_a;
    node_heap *b = (node_heap *)heap_b;
    if (a == NULL || b == NULL || a == b) {
        return INVALID_ARGS;
    }
    heap_node *root = NULL;
    status_code sc = leftist_merge(a->root, b->root, &root);
    if (sc != OK) {
        return sc;
    }
    return node_heap_take_all(a, b, root);
}

/* ===================== декартово дерево (Treap) ===================== */

/*
 * Двоичное дерево поиска по заявкам (слева - менее важные, справа - более важные) и одновременно куча по
 * случайному весу (вес родителя не меньше весов детей) - поэтому дерево в среднем сбалансировано.
 * Самая важная заявка - самый правый узел. Без рекурсии: link - указатель на место в дереве.
 */

/* Разрезать дерево t: в *less - заявки менее важные, чем key, в *more - остальные */
status_code treap_split(heap_node *t, const request *key, heap_node **less, heap_node **more) {
    if (key == NULL || less == NULL || more == NULL) {
        return INVALID_ARGS;
    }
    heap_node **less_link = less;
    heap_node **more_link = more;
    while (t != NULL) {
        int better = 0;
        more_important(key, t->item, &better);
        if (better) {
            /* t и его левое поддерево менее важны - в less; дальше режем правое поддерево */
            *less_link = t;
            less_link = &t->right;
            t = t->right;
        } else {
            *more_link = t;
            more_link = &t->left;
            t = t->left;
        }
    }
    *less_link = NULL;
    *more_link = NULL;
    return OK;
}

/* Вставить узел (вес уже задан): спуститься, пока вес узлов больше; на этом месте разрезать поддерево */
status_code treap_insert_node(node_heap *h, heap_node *node) {
    if (h == NULL || node == NULL) {
        return INVALID_ARGS;
    }
    heap_node **link = &h->root;
    while (*link != NULL && (*link)->weight > node->weight) {
        int better = 0;
        more_important(node->item, (*link)->item, &better);
        link = better ? &(*link)->right : &(*link)->left;
    }
    treap_split(*link, node->item, &node->left, &node->right);
    *link = node;
    h->size++;
    return OK;
}

status_code treap_insert(void *heap, request *item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *node = NULL;
    status_code sc = new_node(item, &node);
    if (sc != OK) {
        return sc;
    }
    next_random(&h->random_state, &node->weight);
    return treap_insert_node(h, node);
}

status_code treap_top(const void *heap, request **item) {
    const node_heap *h = (const node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    const heap_node *x = h->root;
    while (x != NULL && x->right != NULL) {
        x = x->right;
    }
    *item = x != NULL ? x->item : NULL;
    return OK;
}

/* Извлечь самый правый узел: у него нет правого ребёнка, на его место встаёт левое поддерево */
status_code treap_extract(void *heap, request **item) {
    node_heap *h = (node_heap *)heap;
    if (h == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    if (h->root == NULL) {
        *item = NULL;
        return OK;
    }
    heap_node **link = &h->root;
    while ((*link)->right != NULL) {
        link = &(*link)->right;
    }
    heap_node *top = *link;
    *link = top->left;
    *item = top->item;
    free(top);
    h->size--;
    return OK;
}

/*
 * Слияние с разрушением: ключи двух деревьев перемешаны, поэтому узлы меньшего дерева по одному вставляются
 * в большее (со своими весами) - O(m log(n + m)). Если b больше a, сначала деревья меняются местами.
 */
status_code treap_meld(void *heap_a, void *heap_b) {
    node_heap *a = (node_heap *)heap_a;
    node_heap *b = (node_heap *)heap_b;
    if (a == NULL || b == NULL || a == b) {
        return INVALID_ARGS;
    }
    if (b->size > a->size) {
        heap_node *root = a->root;
        size_t size = a->size;
        a->root = b->root;
        a->size = b->size;
        b->root = root;
        b->size = size;
    }
    heap_node **nodes = NULL;
    status_code sc = nodes_walk(b, &nodes);
    if (sc != OK) {
        return sc;
    }
    for (size_t i = 0; i < b->size; i++) {
        nodes[i]->left = NULL;
        nodes[i]->right = NULL;
        treap_insert_node(a, nodes[i]);
    }
    free(nodes);
    b->root = NULL;
    b->size = 0;
    return OK;
}

/* ===================== интерфейс приоритетной очереди ===================== */

/*
 * Единый интерфейс: таблица функций. Каждая структура заполняет свою таблицу; очередь отделения хранит
 * указатель на таблицу и на данные - и все вызовы идут через таблицу.
 *   insert  - добавить заявку (ключ - приоритет и номер заявки);
 *   top     - найти самую важную (*item = NULL - очередь пуста);
 *   extract - извлечь самую важную;
 *   meld    - слияние с разрушением: всё из b переходит в a, b остаётся пустой;
 *   collect - все заявки в массив (для слияния без разрушения, pq_meld_copy).
 */
typedef struct {
    const char *name;
    status_code (*create)(void **heap);
    status_code (*insert)(void *heap, request *item);
    status_code (*top)(const void *heap, request **item);
    status_code (*extract)(void *heap, request **item);
    status_code (*meld)(void *heap_a, void *heap_b);
    status_code (*size)(const void *heap, size_t *size);
    status_code (*collect)(const void *heap, request **out);
    status_code (*destroy)(void *heap);
} heap_ops;

const heap_ops HEAPS[] = {
    {"BinaryHeap", binary_create, binary_insert, binary_top, binary_extract, binary_meld, binary_size,
     binary_collect, binary_destroy},
    {"BinomialHeap", node_heap_create, binomial_insert, binomial_top, binomial_extract, binomial_meld,
     node_heap_size, node_heap_collect, node_heap_destroy},
    {"FibonacciHeap", node_heap_create, fibonacci_insert, fibonacci_top, fibonacci_extract, fibonacci_meld,
     node_heap_size, node_heap_collect, node_heap_destroy},
    {"SkewHeap", node_heap_create, skew_insert, root_top, skew_extract, skew_meld, node_heap_size,
     node_heap_collect, node_heap_destroy},
    {"LeftistHeap", node_heap_create, leftist_insert, root_top, leftist_extract, leftist_meld, node_heap_size,
     node_heap_collect, node_heap_destroy},
    {"Treap", node_heap_create, treap_insert, treap_top, treap_extract, treap_meld, node_heap_size,
     node_heap_collect, node_heap_destroy}
};
#define HEAP_KINDS (sizeof(HEAPS) / sizeof(HEAPS[0]))

/* Очередь отделения: таблица функций, данные структуры и номер отделения, которому очередь принадлежит */
typedef struct {
    const heap_ops *ops;
    void *data;
    size_t department;
} priority_queue;

status_code pq_create(const heap_ops *ops, const size_t department, priority_queue **queue) {
    if (ops == NULL || queue == NULL) {
        return INVALID_ARGS;
    }
    priority_queue *q = (priority_queue *)malloc(sizeof(priority_queue));
    if (q == NULL) {
        return ALLOC_ERROR;
    }
    q->ops = ops;
    q->department = department;
    status_code sc = ops->create(&q->data);
    if (sc != OK) {
        free(q);
        return sc;
    }
    *queue = q;
    return OK;
}

status_code pq_destroy(priority_queue *queue) {
    if (queue == NULL) {
        return OK;
    }
    status_code sc = queue->ops->destroy(queue->data);
    free(queue);
    return sc;
}

status_code pq_size(const priority_queue *queue, size_t *size) {
    if (queue == NULL || size == NULL) {
        return INVALID_ARGS;
    }
    return queue->ops->size(queue->data, size);
}

/*
 * Слияние без разрушения исходных: новая очередь того же вида, в неё добавляются все заявки a и b
 * (заявки общие - очереди хранят только указатели на них). a и b не меняются.
 */
status_code pq_meld_copy(const priority_queue *a, const priority_queue *b, priority_queue **result) {
    if (a == NULL || b == NULL || result == NULL || a->ops != b->ops) {
        return INVALID_ARGS;
    }
    size_t size_a = 0;
    size_t size_b = 0;
    pq_size(a, &size_a);
    pq_size(b, &size_b);
    request **items = (request **)malloc((size_a + size_b + 1) * sizeof(request *));
    if (items == NULL) {
        return ALLOC_ERROR;
    }
    priority_queue *q = NULL;
    status_code sc = a->ops->collect(a->data, items);
    if (sc == OK) {
        sc = b->ops->collect(b->data, items + size_a);
    }
    if (sc == OK) {
        sc = pq_create(a->ops, a->department, &q);
    }
    for (size_t i = 0; sc == OK && i < size_a + size_b; i++) {
        sc = q->ops->insert(q->data, items[i]);
    }
    free(items);
    if (sc != OK) {
        pq_destroy(q);
        return sc;
    }
    *result = q;
    return OK;
}

/* ===================== словари: строка -> очередь ===================== */

/* Копия строки в новой памяти */
status_code copy_string(const char *s, char **result) {
    if (s == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    char *copy = (char *)malloc(strlen(s) + 1);
    if (copy == NULL) {
        return ALLOC_ERROR;
    }
    strcpy(copy, s);
    *result = copy;
    return OK;
}

/* --------- хеш-таблица с цепочками (HashSet) --------- */

typedef struct hash_entry {
    char *key;
    priority_queue *value;
    struct hash_entry *next;   /* следующий элемент той же корзины (цепочка) */
} hash_entry;

typedef struct {
    hash_entry **buckets;
    size_t bucket_count;
    size_t count;
} hash_set;

#define HASH_START_BUCKETS 16

status_code hash_create(void **map) {
    if (map == NULL) {
        return INVALID_ARGS;
    }
    hash_set *h = (hash_set *)malloc(sizeof(hash_set));
    if (h == NULL) {
        return ALLOC_ERROR;
    }
    h->buckets = (hash_entry **)calloc(HASH_START_BUCKETS, sizeof(hash_entry *));
    if (h->buckets == NULL) {
        free(h);
        return ALLOC_ERROR;
    }
    h->bucket_count = HASH_START_BUCKETS;
    h->count = 0;
    *map = h;
    return OK;
}

/* Номер корзины: строка как число по основанию 31 (по модулю 2^64), затем остаток от деления на число корзин */
status_code hash_index(const char *key, const size_t bucket_count, size_t *index) {
    if (key == NULL || index == NULL || bucket_count == 0) {
        return INVALID_ARGS;
    }
    unsigned long long h = 0;
    for (size_t i = 0; key[i] != '\0'; i++) {
        h = h * 31 + (unsigned char)key[i];
    }
    *index = (size_t)(h % bucket_count);
    return OK;
}

/* Вдвое больше корзин: элементы переносятся в новые корзины (сами элементы не копируются) */
status_code hash_grow(hash_set *h) {
    if (h == NULL) {
        return INVALID_ARGS;
    }
    size_t count = h->bucket_count * 2;
    hash_entry **buckets = (hash_entry **)calloc(count, sizeof(hash_entry *));
    if (buckets == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < h->bucket_count; i++) {
        hash_entry *e = h->buckets[i];
        while (e != NULL) {
            hash_entry *next = e->next;
            size_t index = 0;
            hash_index(e->key, count, &index);
            e->next = buckets[index];
            buckets[index] = e;
            e = next;
        }
    }
    free(h->buckets);
    h->buckets = buckets;
    h->bucket_count = count;
    return OK;
}

status_code hash_find(const void *map, const char *key, priority_queue **value) {
    const hash_set *h = (const hash_set *)map;
    if (h == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t index = 0;
    hash_index(key, h->bucket_count, &index);
    *value = NULL;
    for (const hash_entry *e = h->buckets[index]; e != NULL; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            *value = e->value;
            break;
        }
    }
    return OK;
}

status_code hash_insert(void *map, const char *key, priority_queue *value) {
    hash_set *h = (hash_set *)map;
    if (h == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    priority_queue *old = NULL;
    hash_find(h, key, &old);
    if (old != NULL) {
        return KEY_EXISTS;
    }
    /* в среднем больше двух элементов на корзину - таблица растёт */
    if (h->count + 1 > 2 * h->bucket_count) {
        status_code sc = hash_grow(h);
        if (sc != OK) {
            return sc;
        }
    }
    hash_entry *e = (hash_entry *)malloc(sizeof(hash_entry));
    if (e == NULL) {
        return ALLOC_ERROR;
    }
    if (copy_string(key, &e->key) != OK) {
        free(e);
        return ALLOC_ERROR;
    }
    e->value = value;
    size_t index = 0;
    hash_index(key, h->bucket_count, &index);
    e->next = h->buckets[index];
    h->buckets[index] = e;
    h->count++;
    return OK;
}

status_code hash_destroy(void *map) {
    hash_set *h = (hash_set *)map;
    if (h == NULL) {
        return OK;
    }
    for (size_t i = 0; i < h->bucket_count; i++) {
        hash_entry *e = h->buckets[i];
        while (e != NULL) {
            hash_entry *next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(h->buckets);
    free(h);
    return OK;
}

/* --------- отсортированный динамический массив (DynamicArray) --------- */

typedef struct {
    char *key;
    priority_queue *value;
} array_entry;

typedef struct {
    array_entry *items;   /* по возрастанию ключа (strcmp) */
    size_t count;
    size_t capacity;
} dynamic_array;

status_code array_create(void **map) {
    if (map == NULL) {
        return INVALID_ARGS;
    }
    dynamic_array *a = (dynamic_array *)calloc(1, sizeof(dynamic_array));
    if (a == NULL) {
        return ALLOC_ERROR;
    }
    *map = a;
    return OK;
}

/* Дихотомический поиск: *found - есть ли ключ; *position - где он или куда его вставить */
status_code array_search(const dynamic_array *a, const char *key, size_t *position, int *found) {
    if (a == NULL || key == NULL || position == NULL || found == NULL) {
        return INVALID_ARGS;
    }
    size_t low = 0;
    size_t high = a->count;   /* ищем в [low, high) */
    *found = 0;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int cmp = strcmp(a->items[middle].key, key);
        if (cmp == 0) {
            *found = 1;
            low = middle;
            break;
        }
        if (cmp < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *position = low;
    return OK;
}

status_code array_find(const void *map, const char *key, priority_queue **value) {
    const dynamic_array *a = (const dynamic_array *)map;
    if (a == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t position = 0;
    int found = 0;
    array_search(a, key, &position, &found);
    *value = found ? a->items[position].value : NULL;
    return OK;
}

/* Вставка с сохранением порядка: элементы правее места сдвигаются на одну позицию (memmove) */
status_code array_insert(void *map, const char *key, priority_queue *value) {
    dynamic_array *a = (dynamic_array *)map;
    if (a == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t position = 0;
    int found = 0;
    array_search(a, key, &position, &found);
    if (found) {
        return KEY_EXISTS;
    }
    if (a->count == a->capacity) {
        size_t capacity = a->capacity == 0 ? 8 : a->capacity * 2;
        array_entry *tmp = (array_entry *)realloc(a->items, capacity * sizeof(array_entry));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        a->items = tmp;
        a->capacity = capacity;
    }
    char *copy = NULL;
    if (copy_string(key, &copy) != OK) {
        return ALLOC_ERROR;
    }
    memmove(a->items + position + 1, a->items + position, (a->count - position) * sizeof(array_entry));
    a->items[position].key = copy;
    a->items[position].value = value;
    a->count++;
    return OK;
}

status_code array_destroy(void *map) {
    dynamic_array *a = (dynamic_array *)map;
    if (a == NULL) {
        return OK;
    }
    for (size_t i = 0; i < a->count; i++) {
        free(a->items[i].key);
    }
    free(a->items);
    free(a);
    return OK;
}

/* --------- двоичное дерево поиска (BinarySearchTree) --------- */

typedef struct tree_node {
    char *key;
    priority_queue *value;
    struct tree_node *left;    /* ключи меньше */
    struct tree_node *right;   /* ключи больше */
} tree_node;

typedef struct {
    tree_node *root;
    size_t count;
} search_tree;

status_code tree_create(void **map) {
    if (map == NULL) {
        return INVALID_ARGS;
    }
    search_tree *t = (search_tree *)calloc(1, sizeof(search_tree));
    if (t == NULL) {
        return ALLOC_ERROR;
    }
    *map = t;
    return OK;
}

/* Поиск без рекурсии: от корня влево (ключ меньше) или вправо (больше) */
status_code tree_find(const void *map, const char *key, priority_queue **value) {
    const search_tree *t = (const search_tree *)map;
    if (t == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    *value = NULL;
    const tree_node *x = t->root;
    while (x != NULL) {
        int cmp = strcmp(key, x->key);
        if (cmp == 0) {
            *value = x->value;
            break;
        }
        x = cmp < 0 ? x->left : x->right;
    }
    return OK;
}

/* Вставка: спуск как при поиске до пустого места (link - указатель на это место) */
status_code tree_insert(void *map, const char *key, priority_queue *value) {
    search_tree *t = (search_tree *)map;
    if (t == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    tree_node **link = &t->root;
    while (*link != NULL) {
        int cmp = strcmp(key, (*link)->key);
        if (cmp == 0) {
            return KEY_EXISTS;
        }
        link = cmp < 0 ? &(*link)->left : &(*link)->right;
    }
    tree_node *node = (tree_node *)calloc(1, sizeof(tree_node));
    if (node == NULL) {
        return ALLOC_ERROR;
    }
    if (copy_string(key, &node->key) != OK) {
        free(node);
        return ALLOC_ERROR;
    }
    node->value = value;
    *link = node;
    t->count++;
    return OK;
}

/* Освободить без рекурсии: узлы - в стек (в нём не больше count узлов) */
status_code tree_destroy(void *map) {
    search_tree *t = (search_tree *)map;
    if (t == NULL) {
        return OK;
    }
    if (t->root != NULL) {
        tree_node **stack = (tree_node **)malloc(t->count * sizeof(tree_node *));
        if (stack == NULL) {
            return ALLOC_ERROR;
        }
        size_t top = 0;
        stack[top++] = t->root;
        while (top > 0) {
            tree_node *x = stack[--top];
            if (x->left != NULL) {
                stack[top++] = x->left;
            }
            if (x->right != NULL) {
                stack[top++] = x->right;
            }
            free(x->key);
            free(x);
        }
        free(stack);
    }
    free(t);
    return OK;
}

/* --------- бор (Trie) --------- */

/* Узел бора: символ, первый ребёнок, следующий брат; value - очередь, если здесь кончается ключ */
typedef struct trie_node {
    char symbol;
    struct trie_node *child;
    struct trie_node *sibling;
    priority_queue *value;
} trie_node;

typedef struct {
    trie_node *root;    /* корень - пустой символ */
    size_t nodes;       /* сколько узлов (для стека при освобождении) */
} trie;

status_code trie_create(void **map) {
    if (map == NULL) {
        return INVALID_ARGS;
    }
    trie *t = (trie *)malloc(sizeof(trie));
    if (t == NULL) {
        return ALLOC_ERROR;
    }
    t->root = (trie_node *)calloc(1, sizeof(trie_node));
    if (t->root == NULL) {
        free(t);
        return ALLOC_ERROR;
    }
    t->nodes = 1;
    *map = t;
    return OK;
}

/* Ребёнок узла с символом symbol (*result = NULL - нет такого) */
status_code trie_child(const trie_node *node, const char symbol, trie_node **result) {
    if (node == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    trie_node *x = node->child;
    while (x != NULL && x->symbol != symbol) {
        x = x->sibling;
    }
    *result = x;
    return OK;
}

/* Поиск: по символам ключа вниз по бору */
status_code trie_find(const void *map, const char *key, priority_queue **value) {
    const trie *t = (const trie *)map;
    if (t == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    *value = NULL;
    trie_node *node = t->root;
    for (size_t i = 0; key[i] != '\0' && node != NULL; i++) {
        trie_child(node, key[i], &node);
    }
    if (node != NULL) {
        *value = node->value;
    }
    return OK;
}

/* Вставка: недостающие узлы пути создаются; в последнем узле - значение */
status_code trie_insert(void *map, const char *key, priority_queue *value) {
    trie *t = (trie *)map;
    if (t == NULL || key == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    trie_node *node = t->root;
    for (size_t i = 0; key[i] != '\0'; i++) {
        trie_node *next = NULL;
        trie_child(node, key[i], &next);
        if (next == NULL) {
            next = (trie_node *)calloc(1, sizeof(trie_node));
            if (next == NULL) {
                return ALLOC_ERROR;
            }
            next->symbol = key[i];
            next->sibling = node->child;
            node->child = next;
            t->nodes++;
        }
        node = next;
    }
    if (node->value != NULL) {
        return KEY_EXISTS;
    }
    node->value = value;
    return OK;
}

/* Освободить без рекурсии: дети и братья узла - в стек (в нём не больше nodes узлов) */
status_code trie_destroy(void *map) {
    trie *t = (trie *)map;
    if (t == NULL) {
        return OK;
    }
    trie_node **stack = (trie_node **)malloc(t->nodes * sizeof(trie_node *));
    if (stack == NULL) {
        return ALLOC_ERROR;
    }
    size_t top = 0;
    stack[top++] = t->root;
    while (top > 0) {
        trie_node *x = stack[--top];
        if (x->child != NULL) {
            stack[top++] = x->child;
        }
        if (x->sibling != NULL) {
            stack[top++] = x->sibling;
        }
        free(x);
    }
    free(stack);
    free(t);
    return OK;
}

/* --------- интерфейс словаря --------- */

/* insert - добавить ключ (KEY_EXISTS - уже есть); find - найти (*value = NULL - нет ключа) */
typedef struct {
    const char *name;
    status_code (*create)(void **map);
    status_code (*insert)(void *map, const char *key, priority_queue *value);
    status_code (*find)(const void *map, const char *key, priority_queue **value);
    status_code (*destroy)(void *map);
} map_ops;

const map_ops MAPS[] = {
    {"HashSet", hash_create, hash_insert, hash_find, hash_destroy},
    {"DynamicArray", array_create, array_insert, array_find, array_destroy},
    {"BinarySearchTree", tree_create, tree_insert, tree_find, tree_destroy},
    {"Trie", trie_create, trie_insert, trie_find, trie_destroy}
};
#define MAP_KINDS (sizeof(MAPS) / sizeof(MAPS[0]))

/* ===================== время ISO 8601 ===================== */

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

/* Неотрицательное число -> текст (для идентификаторов отделений "1", "2", ...) */
status_code number_text(unsigned long long value, char *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    char digits[24];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);
    size_t k = 0;
    while (n > 0) {
        out[k++] = digits[--n];
    }
    out[k] = '\0';
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

/* ===================== файл параметров ===================== */

typedef struct {
    const heap_ops *heap;          /* структура для заявок отделения */
    const map_ops *map;            /* структура "идентификатор отделения -> очередь" */
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
            for (size_t i = 0; i < HEAP_KINDS; i++) {
                if (strcmp(text, HEAPS[i].name) == 0) {
                    cfg->heap = &HEAPS[i];
                }
            }
            return cfg->heap != NULL ? OK : SYNTAX_ERROR;
        case 1:   /* структура для отделений */
            *reason = "wrong data structure for departments (HashSet, DynamicArray, BinarySearchTree or Trie)";
            for (size_t i = 0; i < MAP_KINDS; i++) {
                if (strcmp(text, MAPS[i].name) == 0) {
                    cfg->map = &MAPS[i];
                }
            }
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
 * SYNTAX_ERROR - ошибка, *error - строка и причина; FILE_ERROR - файл не открылся.
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

/* ===================== центр поддержки ===================== */

/* Оператор отделения */
typedef struct {
    char *name;            /* случайное имя из латинских букв, разное внутри отделения */
    request *current;      /* заявка в обработке; NULL - оператор свободен */
    long long started;     /* когда начал обработку (секунды) */
    long long finish;      /* когда закончит */
} operator_info;

/* Отделение */
typedef struct {
    char id[24];               /* идентификатор: "1", "2", ... */
    priority_queue *queue;     /* очередь заявок (та же, что в словаре) */
    operator_info *operators;
    size_t operator_count;
    size_t busy;               /* сколько операторов заняты */
} department;

/* Вся модель */
typedef struct {
    model_config cfg;
    long long max_priority;
    department *departments;
    void *map;                   /* словарь "идентификатор отделения -> очередь" (cfg.map - его функции) */
    request *requests;           /* все заявки из файлов; после чтения - по времени поступления */
    size_t request_count;
    size_t request_capacity;
    FILE *log;
    unsigned long long random_state;
    long long now;               /* текущий шаг модели (секунды) */
    long long received;          /* сколько заявок поступило (номер последней) */
    long long handled;           /* сколько обработано */
    size_t errors;               /* сколько было ошибок во входных данных */
} support_center;

/* Начало строки журнала: "[время] [КОД]: " (дальше описание пишет вызывающая функция) */
status_code log_prefix(support_center *c, const long long when, const char *code) {
    if (c == NULL || code == NULL) {
        return INVALID_ARGS;
    }
    char text[24];
    time_text(when, text);
    fprintf(c->log, "[%s] [%s]: ", text, code);
    return OK;
}

/* Ошибка во входных данных: в stderr и в журнал (со временем начала модели) */
status_code report_error(support_center *c, const char *path, const size_t line, const char *reason) {
    if (c == NULL || path == NULL || reason == NULL) {
        return INVALID_ARGS;
    }
    c->errors++;
    log_prefix(c, c->cfg.start, "ERROR");
    if (line > 0) {
        fprintf(stderr, "Error: file '%s' line %zu: %s\n", path, line, reason);
        fprintf(c->log, "file '%s' line %zu: %s\n", path, line, reason);
    } else {
        fprintf(stderr, "Error: file '%s': %s\n", path, reason);
        fprintf(c->log, "file '%s': %s\n", path, reason);
    }
    return OK;
}

/* Случайное имя оператора k отделения d: 4..8 латинских букв, не такое, как у операторов 0..k-1 */
status_code random_name(support_center *c, const size_t d, const size_t k) {
    if (c == NULL || d >= c->cfg.department_count) {
        return INVALID_ARGS;
    }
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    department *dep = &c->departments[d];
    char *name = (char *)malloc(9);
    if (name == NULL) {
        return ALLOC_ERROR;
    }
    int unique = 0;
    while (!unique) {
        unsigned long long r = 0;
        next_random(&c->random_state, &r);
        size_t length = 4 + (size_t)(r % 5);
        for (size_t i = 0; i < length; i++) {
            next_random(&c->random_state, &r);
            name[i] = alphabet[r % 52];
        }
        name[length] = '\0';
        /* такое имя уже есть в отделении - придумываем новое */
        unique = 1;
        for (size_t i = 0; i < k; i++) {
            if (strcmp(dep->operators[i].name, name) == 0) {
                unique = 0;
            }
        }
    }
    dep->operators[k].name = name;
    return OK;
}

/* Создать отделения: идентификатор, очередь выбранного вида, операторы с именами; очередь - в словарь */
status_code create_departments(support_center *c) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = c->cfg.map->create(&c->map);
    if (sc != OK) {
        return sc;
    }
    c->departments = (department *)calloc(c->cfg.department_count, sizeof(department));
    if (c->departments == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t d = 0; d < c->cfg.department_count; d++) {
        department *dep = &c->departments[d];
        number_text(d + 1, dep->id);
        sc = pq_create(c->cfg.heap, d, &dep->queue);
        if (sc != OK) {
            return sc;
        }
        sc = c->cfg.map->insert(c->map, dep->id, dep->queue);
        if (sc != OK) {
            return sc;
        }
        dep->operators = (operator_info *)calloc((size_t)c->cfg.operators[d], sizeof(operator_info));
        if (dep->operators == NULL) {
            return ALLOC_ERROR;
        }
        dep->operator_count = (size_t)c->cfg.operators[d];
        for (size_t k = 0; k < dep->operator_count; k++) {
            sc = random_name(c, d, k);
            if (sc != OK) {
                return sc;
            }
        }
    }
    return OK;
}

/*
 * Разобрать строку заявки:  <время> <приоритет> <идентификатор отделения> "<текст>".
 * SYNTAX_ERROR - ошибка, *reason - причина. Текст - между первой и последней кавычкой (внутри могут быть кавычки).
 */
status_code parse_request(support_center *c, char *line, request *r, const char **reason) {
    if (c == NULL || line == NULL || r == NULL || reason == NULL) {
        return INVALID_ARGS;
    }
    char *word = NULL;
    char *rest = NULL;
    /* время */
    cut_word(line, &word, &rest);
    if (parse_time(word, &r->arrival) != OK) {
        *reason = "wrong time (need YYYY-MM-DDTHH:MM:SS)";
        return SYNTAX_ERROR;
    }
    /* приоритет */
    cut_word(rest, &word, &rest);
    long long priority = 0;
    if (parse_number(word, 0, c->max_priority, &priority) != OK) {
        *reason = "wrong priority (need a number from 0 to max_priority)";
        return SYNTAX_ERROR;
    }
    r->priority = (int)priority;
    /* отделение: ищем его очередь в словаре */
    cut_word(rest, &word, &rest);
    priority_queue *queue = NULL;
    if (word[0] != '\0') {
        c->cfg.map->find(c->map, word, &queue);
    }
    if (queue == NULL) {
        *reason = "unknown department";
        return SYNTAX_ERROR;
    }
    r->department = queue->department;
    /* текст в кавычках */
    char *text = NULL;
    trim(rest, &text);
    size_t length = strlen(text);
    if (length < 2 || text[0] != '"' || text[length - 1] != '"') {
        *reason = "the text must be in double quotes";
        return SYNTAX_ERROR;
    }
    if (r->arrival < c->cfg.start) {
        *reason = "the request comes before the start of the model";
        return SYNTAX_ERROR;
    }
    text[length - 1] = '\0';
    status_code sc = copy_string(text + 1, &r->text);
    if (sc != OK) {
        return sc;
    }
    /* шаг поступления: время округляется вверх до целой минуты от начала модели */
    r->step = c->cfg.start + (r->arrival - c->cfg.start + 59) / 60 * 60;
    r->id = 0;
    return OK;
}

/* Добавить разобранную заявку в массив (ёмкость растёт вдвое) */
status_code add_request(support_center *c, const request *r) {
    if (c == NULL || r == NULL) {
        return INVALID_ARGS;
    }
    if (c->request_count == c->request_capacity) {
        size_t capacity = c->request_capacity == 0 ? 64 : c->request_capacity * 2;
        request *tmp = (request *)realloc(c->requests, capacity * sizeof(request));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        c->requests = tmp;
        c->request_capacity = capacity;
    }
    c->requests[c->request_count] = *r;
    c->requests[c->request_count].order = c->request_count;
    c->request_count++;
    return OK;
}

/* Прочитать файл заявок; ошибочные строки - в stderr и журнал, остальные строки читаются дальше */
status_code load_requests(support_center *c, const char *path) {
    if (c == NULL || path == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return report_error(c, path, 0, "can not read the file");
    }
    size_t line_number = 0;
    status_code sc = OK;
    for (;;) {
        char *line = NULL;
        int end = 0;
        sc = read_line(file, &line, &end);
        if (sc != OK || end) {
            free(line);
            break;
        }
        line_number++;
        char *text = NULL;
        trim(line, &text);
        if (text[0] != '\0') {
            request r = {0, 0, 0, 0, 0, 0, NULL};
            const char *reason = NULL;
            sc = parse_request(c, text, &r, &reason);
            if (sc == SYNTAX_ERROR) {
                sc = report_error(c, path, line_number, reason);
            } else if (sc == OK) {
                sc = add_request(c, &r);
                if (sc != OK) {
                    free(r.text);
                }
            }
        }
        free(line);
        if (sc != OK) {
            break;
        }
    }
    if (sc == OK && ferror(file)) {
        sc = report_error(c, path, 0, "read error");
    }
    fclose(file);
    return sc;
}

/* Компаратор qsort (его прототип задан qsort): по времени, при равном - по порядку в файлах */
int compare_requests(const void *a, const void *b) {
    const request *x = (const request *)a;
    const request *y = (const request *)b;
    if (x->arrival != y->arrival) {
        return x->arrival < y->arrival ? -1 : 1;
    }
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

/* ===================== моделирование ===================== */

/* Свободные операторы отделения d (по порядку) берут самые важные заявки из очереди */
status_code take_requests(support_center *c, const size_t d) {
    if (c == NULL || d >= c->cfg.department_count) {
        return INVALID_ARGS;
    }
    department *dep = &c->departments[d];
    for (size_t k = 0; k < dep->operator_count; k++) {
        operator_info *op = &dep->operators[k];
        if (op->current != NULL) {
            continue;
        }
        request *r = NULL;
        status_code sc = dep->queue->ops->extract(dep->queue->data, &r);
        if (sc != OK) {
            return sc;
        }
        if (r == NULL) {
            break;   /* очередь пуста */
        }
        /* время обработки - случайное от min_time до max_time минут */
        unsigned long long random = 0;
        next_random(&c->random_state, &random);
        long long minutes = c->cfg.min_time +
                            (long long)(random % (unsigned long long)(c->cfg.max_time - c->cfg.min_time + 1));
        op->current = r;
        op->started = c->now;
        op->finish = c->now + minutes * 60;
        dep->busy++;
        log_prefix(c, c->now, "REQUEST_HANDLING_STARTED");
        fprintf(c->log, "request %lld taken by operator %s (department %s)\n", r->id, op->name, dep->id);
    }
    return OK;
}

/* Операторы, у которых время обработки кончилось сейчас, освобождаются */
status_code finish_requests(support_center *c) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    for (size_t d = 0; d < c->cfg.department_count; d++) {
        department *dep = &c->departments[d];
        for (size_t k = 0; k < dep->operator_count; k++) {
            operator_info *op = &dep->operators[k];
            if (op->current == NULL || op->finish != c->now) {
                continue;
            }
            log_prefix(c, c->now, "REQUEST_HANDLING_FINISHED");
            fprintf(c->log, "request %lld handled in %lld min by operator %s (department %s)\n", op->current->id,
                    (op->finish - op->started) / 60, op->name, dep->id);
            op->current = NULL;
            dep->busy--;
            c->handled++;
        }
    }
    return OK;
}

/* Перегружено ли отделение: queue >= operators * coeff, то есть queue * denominator >= operators * numerator */
status_code is_overloaded(const support_center *c, const size_t queue, const size_t operators, int *result) {
    if (c == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    *result = (long long)queue * c->cfg.coeff_denominator >= (long long)operators * c->cfg.coeff_numerator;
    return OK;
}

/*
 * Куда передать очередь перегруженного отделения d: в отделение, которое после передачи (и после того, как
 * его свободные операторы возьмут заявки) само не будет перегружено; из таких - в наименее загруженное:
 * (занятые операторы + очередь) / операторы - меньше всех (дроби сравниваются умножением крест-накрест);
 * при равной загрузке - с меньшим номером. *target = число отделений - такого нет.
 */
status_code choose_target(const support_center *c, const size_t d, size_t *target) {
    if (c == NULL || target == NULL || d >= c->cfg.department_count) {
        return INVALID_ARGS;
    }
    size_t moved = 0;
    pq_size(c->departments[d].queue, &moved);
    *target = c->cfg.department_count;
    long long best_work = 0;
    long long best_operators = 1;
    for (size_t j = 0; j < c->cfg.department_count; j++) {
        if (j == d) {
            continue;
        }
        const department *dep = &c->departments[j];
        size_t queue = 0;
        pq_size(dep->queue, &queue);
        /* сколько останется в очереди, когда свободные операторы возьмут заявки */
        size_t free_operators = dep->operator_count - dep->busy;
        size_t after = queue + moved > free_operators ? queue + moved - free_operators : 0;
        int overloaded = 0;
        is_overloaded(c, after, dep->operator_count, &overloaded);
        if (overloaded) {
            continue;
        }
        long long work = (long long)(dep->busy + queue);
        long long operators = (long long)dep->operator_count;
        if (*target == c->cfg.department_count || work * best_operators < best_work * operators) {
            *target = j;
            best_work = work;
            best_operators = operators;
        }
    }
    return OK;
}

/* Отделение d перегружено после поступления заявки last: вся очередь - другому отделению (meld) */
status_code handle_overload(support_center *c, const size_t d, const request *last) {
    if (c == NULL || last == NULL || d >= c->cfg.department_count) {
        return INVALID_ARGS;
    }
    department *dep = &c->departments[d];
    size_t queue = 0;
    pq_size(dep->queue, &queue);
    size_t target = 0;
    choose_target(c, d, &target);
    log_prefix(c, c->now, "DEPARTMENT_OVERLOADED");
    fprintf(c->log, "request %lld overloaded department %s (%zu requests in the queue, %zu operators); ", last->id,
            dep->id, queue, dep->operator_count);
    if (target == c->cfg.department_count) {
        fprintf(c->log, "no department can take them\n");
        return OK;
    }
    department *other = &c->departments[target];
    fprintf(c->log, "all of them moved to department %s\n", other->id);
    status_code sc = other->queue->ops->meld(other->queue->data, dep->queue->data);
    if (sc != OK) {
        return sc;
    }
    return take_requests(c, target);
}

/* Поступление заявки: номер, очередь отделения, свободные операторы, проверка перегрузки */
status_code arrive(support_center *c, request *r) {
    if (c == NULL || r == NULL) {
        return INVALID_ARGS;
    }
    department *dep = &c->departments[r->department];
    r->id = ++c->received;
    log_prefix(c, c->now, "NEW_REQUEST");
    fprintf(c->log, "request %lld to department %s\n", r->id, dep->id);
    status_code sc = dep->queue->ops->insert(dep->queue->data, r);
    if (sc == OK) {
        sc = take_requests(c, r->department);
    }
    if (sc != OK) {
        return sc;
    }
    size_t queue = 0;
    pq_size(dep->queue, &queue);
    int overloaded = 0;
    is_overloaded(c, queue, dep->operator_count, &overloaded);
    return overloaded ? handle_overload(c, r->department, r) : OK;
}

/*
 * Моделирование от start до end. На шаге: окончания обработки, свободные операторы берут заявки, поступления.
 * Следующий шаг - ближайшее событие (поступление или окончание обработки): пустые минуты не перебираются.
 */
status_code run_model(support_center *c) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    log_prefix(c, c->cfg.start, "MODELING_STARTED");
    fprintf(c->log, "departments: %zu; requests are kept in %s, departments in %s\n",
            c->cfg.department_count, c->cfg.heap->name, c->cfg.map->name);
    size_t next_request = 0;
    c->now = c->cfg.start;
    status_code sc = OK;
    while (sc == OK && c->now <= c->cfg.end) {
        /* 1) окончания, 2) свободные операторы берут заявки из очередей */
        sc = finish_requests(c);
        for (size_t d = 0; sc == OK && d < c->cfg.department_count; d++) {
            sc = take_requests(c, d);
        }
        /* 3) поступления этого шага - по одной */
        while (sc == OK && next_request < c->request_count && c->requests[next_request].step == c->now) {
            sc = arrive(c, &c->requests[next_request]);
            next_request++;
        }
        /* ближайшее следующее событие */
        long long next = LLONG_MAX;
        if (next_request < c->request_count) {
            next = c->requests[next_request].step;
        }
        for (size_t d = 0; d < c->cfg.department_count; d++) {
            for (size_t k = 0; k < c->departments[d].operator_count; k++) {
                const operator_info *op = &c->departments[d].operators[k];
                if (op->current != NULL && op->finish < next) {
                    next = op->finish;
                }
            }
        }
        if (next == LLONG_MAX) {
            break;   /* событий больше нет */
        }
        c->now = next;
    }
    if (sc != OK) {
        return sc;
    }
    /* итоги на конец моделирования */
    size_t in_progress = 0;
    size_t waiting = 0;
    for (size_t d = 0; d < c->cfg.department_count; d++) {
        size_t queue = 0;
        pq_size(c->departments[d].queue, &queue);
        waiting += queue;
        in_progress += c->departments[d].busy;
    }
    log_prefix(c, c->cfg.end, "MODELING_FINISHED");
    fprintf(c->log, "%lld requests received, %lld handled, %zu in progress, %zu waiting in queues, "
            "%zu after the end of the model\n", c->received, c->handled, in_progress, waiting,
            c->request_count - next_request);
    return OK;
}

/* Освободить всё: отделения (очереди, имена операторов), словарь, заявки */
status_code center_free(support_center *c) {
    if (c == NULL) {
        return OK;
    }
    if (c->departments != NULL) {
        for (size_t d = 0; d < c->cfg.department_count; d++) {
            department *dep = &c->departments[d];
            pq_destroy(dep->queue);
            if (dep->operators != NULL) {
                for (size_t k = 0; k < dep->operator_count; k++) {
                    free(dep->operators[k].name);
                }
                free(dep->operators);
            }
        }
        free(c->departments);
    }
    if (c->map != NULL) {
        c->cfg.map->destroy(c->map);
    }
    for (size_t i = 0; i < c->request_count; i++) {
        free(c->requests[i].text);
    }
    free(c->requests);
    free(c->cfg.operators);
    return OK;
}

/* ===================== самопроверка интерфейсов (--self-test) ===================== */

/*
 * В модели нужны не все операции интерфейса (top, collect и слияние без разрушения в ней не вызываются),
 * поэтому все операции всех структур проверяются отдельно - режимом --self-test.
 */

/* Компаратор qsort (его прототип задан qsort): указатели на заявки, сначала более важные */
int compare_importance(const void *a, const void *b) {
    const request *x = *(request *const *)a;
    const request *y = *(request *const *)b;
    int better = 0;
    more_important(x, y, &better);
    if (better) {
        return -1;
    }
    more_important(y, x, &better);
    return better ? 1 : 0;
}

/*
 * Извлечь всё из очереди и сравнить с expected (count заявок по убыванию важности); перед каждым
 * извлечением top должен вернуть ту же заявку. В конце очередь пуста: top и extract дают NULL.
 */
status_code drain_check(priority_queue *q, request **expected, const size_t count, int *good) {
    if (q == NULL || good == NULL || (expected == NULL && count > 0)) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < count; i++) {
        request *top = NULL;
        request *r = NULL;
        q->ops->top(q->data, &top);
        status_code sc = q->ops->extract(q->data, &r);
        if (sc != OK) {
            return sc;
        }
        if (r != expected[i] || top != r) {
            *good = 0;
        }
    }
    size_t size = 1;
    request dummy;
    request *top = &dummy;   /* не NULL: проверяем, что top заменит его на NULL */
    request *r = &dummy;
    pq_size(q, &size);
    q->ops->top(q->data, &top);
    status_code sc = q->ops->extract(q->data, &r);
    if (size != 0 || top != NULL || r != NULL) {
        *good = 0;
    }
    return sc;
}

/*
 * Проверка одной кучи: 120 заявок в a и 80 в b (приоритеты 0..9 - много равных), извлечение части и снова
 * вставка; слияние без разрушения (a и b не меняются), потом с разрушением (b пустая); порядок извлечения
 * сравнивается с отсортированным qsort массивом.
 */
status_code check_heap(const heap_ops *ops, unsigned long long *state, int *good) {
    if (ops == NULL || state == NULL || good == NULL) {
        return INVALID_ARGS;
    }
    const size_t total = 200;
    const size_t in_a = 120;
    *good = 1;
    const size_t back = 30;   /* сколько заявок извлечь из a и вернуть обратно */
    request *items = (request *)calloc(total, sizeof(request));
    request **sorted = (request **)malloc(total * sizeof(request *));
    request **taken = (request **)malloc(back * sizeof(request *));
    priority_queue *a = NULL;
    priority_queue *b = NULL;
    priority_queue *copy = NULL;
    status_code sc = items != NULL && sorted != NULL && taken != NULL ? OK : ALLOC_ERROR;
    if (sc == OK) {
        sc = pq_create(ops, 0, &a);
    }
    if (sc == OK) {
        sc = pq_create(ops, 1, &b);
    }
    /* заявки: номер по порядку, приоритет случайный */
    for (size_t i = 0; sc == OK && i < total; i++) {
        unsigned long long r = 0;
        next_random(state, &r);
        items[i].id = (long long)i + 1;
        items[i].priority = (int)(r % 10);
        sorted[i] = &items[i];
        priority_queue *q = i < in_a ? a : b;
        sc = q->ops->insert(q->data, &items[i]);
    }
    /* извлечь из a 30 самых важных и вернуть их обратно - куча должна остаться правильной */
    for (size_t i = 0; sc == OK && i < back; i++) {
        sc = a->ops->extract(a->data, &taken[i]);
    }
    for (size_t i = 0; sc == OK && i < back; i++) {
        sc = a->ops->insert(a->data, taken[i]);
    }
    /* слияние без разрушения: копия - все 200, a и b - по-прежнему 120 и 80 */
    if (sc == OK) {
        sc = pq_meld_copy(a, b, &copy);
    }
    if (sc == OK) {
        qsort(sorted, total, sizeof(request *), compare_importance);
        size_t size_a = 0;
        size_t size_b = 0;
        pq_size(a, &size_a);
        pq_size(b, &size_b);
        if (size_a != in_a || size_b != total - in_a) {
            *good = 0;
        }
        sc = drain_check(copy, sorted, total, good);
    }
    /* слияние с разрушением: всё в a, b пустая */
    if (sc == OK) {
        sc = ops->meld(a->data, b->data);
    }
    if (sc == OK) {
        sc = drain_check(b, NULL, 0, good);
    }
    if (sc == OK) {
        sc = drain_check(a, sorted, total, good);
    }
    pq_destroy(a);
    pq_destroy(b);
    pq_destroy(copy);
    free(items);
    free(sorted);
    free(taken);
    return sc;
}

/* Проверка одного словаря: ключи "1".."100" в случайном порядке, поиск всех, отсутствующих и повтор ключа */
status_code check_map(const map_ops *ops, unsigned long long *state, int *good) {
    if (ops == NULL || state == NULL || good == NULL) {
        return INVALID_ARGS;
    }
    const size_t count = 100;
    *good = 1;
    void *map = NULL;
    priority_queue *values = (priority_queue *)calloc(count, sizeof(priority_queue));
    size_t *order = (size_t *)malloc(count * sizeof(size_t));
    status_code sc = values != NULL && order != NULL ? ops->create(&map) : ALLOC_ERROR;
    if (sc == OK) {
        /* случайный порядок ключей: перемешивание Фишера - Йетса */
        for (size_t i = 0; i < count; i++) {
            order[i] = i;
        }
        for (size_t i = count - 1; i > 0; i--) {
            unsigned long long r = 0;
            next_random(state, &r);
            size_t j = (size_t)(r % (i + 1));
            size_t tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
    }
    char key[24];
    for (size_t i = 0; sc == OK && i < count; i++) {
        number_text(order[i] + 1, key);
        sc = ops->insert(map, key, &values[order[i]]);
        if (sc == KEY_EXISTS) {
            *good = 0;   /* ключи все разные - словарь ошибся */
            sc = OK;
        }
    }
    /* каждый ключ находится и даёт своё значение; повторная вставка - KEY_EXISTS */
    for (size_t i = 0; sc == OK && i < count; i++) {
        priority_queue *found = NULL;
        number_text(i + 1, key);
        ops->find(map, key, &found);
        if (found != &values[i] || ops->insert(map, key, &values[0]) != KEY_EXISTS) {
            *good = 0;
        }
    }
    /* ключей, которых нет, не находит: "0", "101", "01", "1x", пустая строка */
    const char *missing[] = {"0", "101", "01", "1x", ""};
    for (size_t i = 0; sc == OK && i < 5; i++) {
        priority_queue *found = &values[0];
        ops->find(map, missing[i], &found);
        if (found != NULL) {
            *good = 0;
        }
    }
    if (map != NULL) {
        ops->destroy(map);
    }
    free(values);
    free(order);
    return sc;
}

/* Самопроверка всех структур: по строке "<название>: OK" или "<название>: FAILED"; код - есть ли FAILED */
status_code self_test(unsigned long long *state, int *all_good) {
    if (state == NULL || all_good == NULL) {
        return INVALID_ARGS;
    }
    *all_good = 1;
    for (size_t i = 0; i < HEAP_KINDS; i++) {
        int good = 0;
        status_code sc = check_heap(&HEAPS[i], state, &good);
        if (sc != OK) {
            return sc;
        }
        printf("%s: %s\n", HEAPS[i].name, good ? "OK" : "FAILED");
        *all_good &= good;
    }
    for (size_t i = 0; i < MAP_KINDS; i++) {
        int good = 0;
        status_code sc = check_map(&MAPS[i], state, &good);
        if (sc != OK) {
            return sc;
        }
        printf("%s: %s\n", MAPS[i].name, good ? "OK" : "FAILED");
        *all_good &= good;
    }
    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    const char *usage = "Usage: lab4_task9 <max_priority> <config file> <requests file> ... "
                        "[--log <file>] [--seed N]\n"
                        "       lab4_task9 --self-test [--seed N]";
    /* разбор аргументов: --log, --seed и --self-test где угодно, остальное - по порядку */
    const char *log_path = NULL;
    const char *seed_text = NULL;
    int self_check = 0;
    const char **files = (const char **)malloc((size_t)argc * sizeof(char *));
    if (files == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    size_t count = 0;
    int wrong = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--log") == 0 || strcmp(argv[i], "--seed") == 0) {
            const char **place = argv[i][2] == 'l' ? &log_path : &seed_text;
            if (i + 1 == argc || *place != NULL) {
                wrong = 1;   /* нет значения или ключ повторяется */
            } else {
                *place = argv[++i];
            }
        } else if (strcmp(argv[i], "--self-test") == 0) {
            wrong |= self_check;
            self_check = 1;
        } else {
            files[count++] = argv[i];
        }
    }
    /* самопроверке нужен только --seed; модели - хотя бы три аргумента */
    if (wrong || (self_check && (count > 0 || log_path != NULL)) || (!self_check && count < 3)) {
        fprintf(stderr, "%s\n", usage);
        free(files);
        return 1;
    }

    /* начальное число генератора */
    support_center c;
    memset(&c, 0, sizeof(c));
    long long seed = (long long)time(NULL);
    if (seed_text != NULL && parse_number(seed_text, 0, LLONG_MAX, &seed) != OK) {
        fprintf(stderr, "Error: the seed must be a number\n");
        free(files);
        return 1;
    }
    /* у xorshift состояние 0 "залипает" (всегда 0), поэтому подмешиваем постоянную */
    c.random_state = (unsigned long long)seed ^ 0x9E3779B97F4A7C15ULL;
    if (c.random_state == 0) {
        c.random_state = 0x9E3779B97F4A7C15ULL;
    }

    /* --self-test: проверка всех операций всех куч и словарей, модель не запускается */
    if (self_check) {
        int all_good = 0;
        status_code sc = self_test(&c.random_state, &all_good);
        free(files);
        if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
            return 1;
        }
        if (sc != OK) {
            /* INVALID_ARGS не бывает ни при каком вводе - только при ошибке в самой программе */
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        return all_good ? 0 : 1;
    }

    /* max_priority */
    if (parse_number(files[0], 1, INT_MAX, &c.max_priority) != OK) {
        fprintf(stderr, "Error: max_priority must be a natural number (up to 2147483647)\n");
        free(files);
        return 1;
    }

    /* файл параметров */
    config_error error = {0, NULL};
    status_code sc = read_config(files[1], &c.cfg, &error);
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
        free(c.cfg.operators);
        free(files);
        return 1;
    }

    /* журнал: не один из входных файлов (сравниваются файлы, а не строки путей) */
    if (log_path != NULL) {
        for (size_t i = 1; i < count; i++) {
            int equal = 0;
            same_file(log_path, files[i], &equal);
            if (equal) {
                fprintf(stderr, "Error: the log file must not be one of the input files\n");
                free(c.cfg.operators);
                free(files);
                return 1;
            }
        }
        c.log = fopen(log_path, "w");
        if (c.log == NULL) {
            fprintf(stderr, "Error: can not open log file '%s'\n", log_path);
            free(c.cfg.operators);
            free(files);
            return 1;
        }
    } else {
        c.log = stdout;
    }

    /* отделения, заявки, моделирование */
    sc = create_departments(&c);
    for (size_t i = 2; sc == OK && i < count; i++) {
        sc = load_requests(&c, files[i]);
    }
    if (sc == OK) {
        if (c.request_count > 1) {
            qsort(c.requests, c.request_count, sizeof(request), compare_requests);
        }
        sc = run_model(&c);
    }
    /* ошибка записи журнала (например, диск полон) */
    if (sc == OK && ferror(c.log)) {
        sc = FILE_ERROR;
    }
    if (c.log != stdout && fclose(c.log) != 0 && sc == OK) {
        sc = FILE_ERROR;
    }
    size_t errors = c.errors;
    center_free(&c);
    free(files);
    if (sc == FILE_ERROR) {
        fprintf(stderr, "Error: can not write the log\n");
        return 1;
    }
    if (sc == ALLOC_ERROR) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    if (sc != OK) {
        /* ошибки во входных данных обрабатываются на месте (report_error), сюда приходят только статусы,
           которых не бывает ни при каком вводе (INVALID_ARGS) - ошибка в самой программе */
        fprintf(stderr, "Error: internal error\n");
        return 1;
    }
    /* ошибки во входных данных: моделирование выполнено, но код возврата 1 */
    return errors > 0 ? 1 : 0;
}
