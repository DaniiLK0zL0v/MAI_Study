/*
 * Лабораторная 4, задание 6. Моделирование почтового сервиса: отделения и письма.
 *
 * Запуск:  lab4_task6 <файл связей> [<файл журнала>] [--manual]
 *   Файл связей: пары "id1 id2" - между отделениями id1 и id2 есть связь. Отделения из этого файла
 *   создаются с вместимостью DEFAULT_CAPACITY; другие можно добавить командой add_office.
 *   Файл журнала: туда пишутся все перемещения писем и изменения состояний писем и отделений;
 *   без него имя журнала - "post_" и случайные буквы и цифры.
 *   --manual: время идёт только командами step и wait (для проверки); без флага - настоящее время.
 *
 * Время: каждые 0.2 секунды (один "такт") самое приоритетное письмо каждого отделения уходит в соседнее
 * отделение - на шаг ближе к получателю. Ввод с клавиатуры ждёт, поэтому такты считаются "задним числом":
 * перед каждой командой программа смотрит, сколько настоящего времени прошло, и выполняет столько тактов.
 *
 * Письма в отделении - косая приоритетная очередь (skew heap): сначала больший приоритет, при равном -
 * письмо, созданное раньше. Доставленные письма ждут получателя в отделении и тоже занимают место:
 * в отделении не больше capacity писем любого состояния.
 * Маршрут - кратчайший путь по связям (поиск в ширину). Следующее отделение пути переполнено или до
 * получателя нет пути - письмо уходит в ближайшее (соседнее) отделение со свободным местом и пробует снова
 * ("петля" перенаправления), пока доставка не станет возможной.
 *
 * Команды (help - список): add_office, remove_office, add_letter, undelivered, take, list, offices,
 * letters, step, wait, exit.
 */
#define _POSIX_C_SOURCE 200809L   /* clock_gettime, nanosleep, isatty на Linux */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort */
#include <string.h>   /* strcmp, strlen, memcpy */
#include <limits.h>   /* INT_MAX */
#include <time.h>     /* time; clock_gettime, nanosleep (Linux) */
#ifdef _WIN32
#include <windows.h>  /* GetTickCount64, Sleep */
#include <io.h>       /* _isatty, _fileno */
#else
#include <unistd.h>   /* isatty, fileno */
#endif

#define DEFAULT_CAPACITY 10   /* вместимость отделений из файла связей */
#define TICK_MS 200           /* один такт - 0.2 секунды */
#define MAX_TICKS_AT_ONCE 100000

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,        /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,        /* команда или строка файла записана неправильно */
    NO_SUCH_OFFICE,      /* нет отделения с таким id */
    OFFICE_EXISTS,       /* отделение с таким id уже есть */
    NO_SUCH_LETTER,      /* нет письма с таким id (или оно уже не в системе) */
    OFFICE_FULL,         /* в отделении нет места */
    NOT_DELIVERED_YET,   /* take: письмо ещё не в отделении получателя */
    FILE_ERROR,          /* файл не открылся / ошибка чтения или записи */
    ALLOC_ERROR          /* malloc / realloc не смогли выделить память */
} status_code;

typedef enum { IN_TRANSIT, DELIVERED, UNDELIVERED, TAKEN } letter_state;

/* Письмо */
typedef struct {
    int id;
    char type[8];          /* "normal" или "urgent" */
    int priority;
    int from;              /* id отделений отправителя и получателя */
    int to;
    char *data;            /* технические данные - любая строка */
    letter_state state;
    int office;            /* где сейчас (для IN_TRANSIT и DELIVERED) */
} letter;

/* Узел косой кучи */
typedef struct heap_node {
    letter *item;
    struct heap_node *left;
    struct heap_node *right;
} heap_node;

/* Отделение */
typedef struct {
    int id;
    int capacity;
    int *links;            /* id связанных отделений */
    size_t link_count;
    heap_node *queue;      /* письма в пути (косая куча) */
    size_t queue_size;
    letter **waiting;      /* доставленные, ждут получателя */
    size_t waiting_count;
} office;

/* Вся почта */
typedef struct {
    office *offices;       /* по возрастанию id */
    size_t office_count;
    letter **letters;      /* все письма когда-либо созданные (по id) - для списка и take */
    size_t letter_count;
    long long tick;        /* номер такта */
    FILE *log;
} post;

/* ===================== id писем (static-счётчик по условию) ===================== */

/* Следующий id письма: static-переменная внутри функции живёт всё время работы программы,
   но видна только здесь (это не глобальная переменная) */
status_code next_letter_id(int *id) {
    static int counter = 0;
    if (id == NULL) {
        return INVALID_ARGS;
    }
    if (counter == INT_MAX) {
        return ALLOC_ERROR;
    }
    *id = ++counter;
    return OK;
}

/* ===================== косая куча ===================== */

/* a важнее b: больше приоритет, при равном - меньше id (письмо создано раньше) */
status_code more_important(const letter *a, const letter *b, int *result) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    *result = a->priority > b->priority || (a->priority == b->priority && a->id < b->id);
    return OK;
}

/*
 * Слияние двух косых куч (без рекурсии). Корень - важнейший из двух корней; его правое поддерево
 * сливается с другой кучей, после чего левое и правое меняются местами (в этом "косость": так правые
 * ветви не вырастают надолго, и операции в среднем O(log n)).
 */
status_code heap_merge(heap_node *a, heap_node *b, heap_node **result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    heap_node *root = NULL;
    heap_node **link = &root;   /* куда подвесить следующий выбранный узел */
    while (a != NULL && b != NULL) {
        int better = 0;
        more_important(b->item, a->item, &better);
        if (better) {
            heap_node *tmp = a;
            a = b;
            b = tmp;
        }
        /* a - важнейший: он становится на место link; его старое правое поддерево сливаем дальше с b */
        *link = a;
        heap_node *rest = a->right;
        a->right = a->left;     /* обмен детей: слитое пойдёт в левое поддерево */
        link = &a->left;
        a = rest;
    }
    *link = a != NULL ? a : b;
    *result = root;
    return OK;
}

status_code heap_push(heap_node **heap, size_t *size, letter *item) {
    if (heap == NULL || size == NULL || item == NULL) {
        return INVALID_ARGS;
    }
    heap_node *node = (heap_node *)malloc(sizeof(heap_node));
    if (node == NULL) {
        return ALLOC_ERROR;
    }
    node->item = item;
    node->left = NULL;
    node->right = NULL;
    (*size)++;
    return heap_merge(*heap, node, heap);
}

/* Снять важнейшее письмо: корень, его дети сливаются */
status_code heap_pop(heap_node **heap, size_t *size, letter **item) {
    if (heap == NULL || size == NULL || item == NULL || *heap == NULL) {
        return INVALID_ARGS;
    }
    heap_node *root = *heap;
    *item = root->item;
    heap_merge(root->left, root->right, heap);
    free(root);
    (*size)--;
    return OK;
}

/* Убрать из кучи конкретное письмо: снимаем всё, кроме него, и кладём обратно */
status_code heap_remove(heap_node **heap, size_t *size, const letter *target, int *found) {
    if (heap == NULL || size == NULL || target == NULL || found == NULL) {
        return INVALID_ARGS;
    }
    heap_node *rest = NULL;
    size_t rest_size = 0;
    *found = 0;
    while (*heap != NULL) {
        letter *item = NULL;
        heap_pop(heap, size, &item);
        if (item == target && !*found) {
            *found = 1;
            continue;
        }
        status_code sc = heap_push(&rest, &rest_size, item);
        if (sc != OK) {
            return sc;
        }
    }
    *heap = rest;
    *size = rest_size;
    return OK;
}

/* ===================== отделения ===================== */

/* Найти отделение по id (двоичный поиск по упорядоченному массиву) */
status_code find_office(const post *p, const int id, office **result) {
    if (p == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t low = 0;
    size_t high = p->office_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (p->offices[middle].id < id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *result = (low < p->office_count && p->offices[low].id == id) ? &p->offices[low] : NULL;
    return *result == NULL ? NO_SUCH_OFFICE : OK;
}

/* Сколько писем в отделении (любого состояния) */
status_code office_load(const office *o, size_t *load) {
    if (o == NULL || load == NULL) {
        return INVALID_ARGS;
    }
    *load = o->queue_size + o->waiting_count;
    return OK;
}

status_code add_link(office *o, const int other) {
    if (o == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < o->link_count; i++) {
        if (o->links[i] == other) {
            return OK;   /* связь уже есть */
        }
    }
    int *tmp = (int *)realloc(o->links, (o->link_count + 1) * sizeof(int));
    if (tmp == NULL) {
        return ALLOC_ERROR;
    }
    o->links = tmp;
    /* связи храним по возрастанию id: поиск пути и выбор соседа одинаковы при каждом запуске */
    size_t pos = o->link_count;
    while (pos > 0 && o->links[pos - 1] > other) {
        o->links[pos] = o->links[pos - 1];
        pos--;
    }
    o->links[pos] = other;
    o->link_count++;
    return OK;
}

/* Новое отделение (без связей) - на своё место в упорядоченном массиве */
status_code create_office(post *p, const int id, const int capacity) {
    if (p == NULL || capacity <= 0) {
        return INVALID_ARGS;
    }
    office *existing = NULL;
    if (find_office(p, id, &existing) == OK) {
        return OFFICE_EXISTS;
    }
    office *tmp = (office *)realloc(p->offices, (p->office_count + 1) * sizeof(office));
    if (tmp == NULL) {
        return ALLOC_ERROR;
    }
    p->offices = tmp;
    size_t pos = p->office_count;
    while (pos > 0 && p->offices[pos - 1].id > id) {
        p->offices[pos] = p->offices[pos - 1];
        pos--;
    }
    office *o = &p->offices[pos];
    o->id = id;
    o->capacity = capacity;
    o->links = NULL;
    o->link_count = 0;
    o->queue = NULL;
    o->queue_size = 0;
    o->waiting = NULL;
    o->waiting_count = 0;
    p->office_count++;
    return OK;
}

/* Связать два существующих отделения */
status_code link_offices(post *p, const int a, const int b) {
    if (p == NULL || a == b) {
        return INVALID_ARGS;
    }
    office *x = NULL;
    office *y = NULL;
    status_code sc = find_office(p, a, &x);
    if (sc == OK) {
        sc = find_office(p, b, &y);
    }
    if (sc == OK) {
        sc = add_link(x, b);
    }
    if (sc == OK) {
        sc = add_link(y, a);
    }
    return sc;
}

/*
 * Следующее отделение по кратчайшему пути от from к to (поиск в ширину по связям).
 * *next = -1 - пути нет.
 */
status_code next_hop(const post *p, const int from, const int to, int *next) {
    if (p == NULL || next == NULL) {
        return INVALID_ARGS;
    }
    *next = -1;
    size_t n = p->office_count;
    int *queue = (int *)malloc((n + 1) * sizeof(int));        /* номера отделений в массиве */
    int *first_step = (int *)malloc((n + 1) * sizeof(int));   /* первый шаг пути до отделения (id) */
    int *seen = (int *)calloc(n + 1, sizeof(int));
    if (queue == NULL || first_step == NULL || seen == NULL) {
        free(queue);
        free(first_step);
        free(seen);
        return ALLOC_ERROR;
    }
    office *start = NULL;
    find_office(p, from, &start);
    size_t head = 0;
    size_t tail = 0;
    size_t start_index = (size_t)(start - p->offices);
    queue[tail++] = (int)start_index;
    seen[start_index] = 1;
    first_step[start_index] = -1;
    while (head < tail && *next == -1) {
        const office *o = &p->offices[queue[head++]];
        /* соседи - по возрастанию id, чтобы путь был одинаковым при каждом запуске */
        for (size_t k = 0; k < o->link_count; k++) {
            office *neighbour = NULL;
            if (find_office(p, o->links[k], &neighbour) != OK) {
                continue;
            }
            size_t index = (size_t)(neighbour - p->offices);
            if (seen[index]) {
                continue;
            }
            seen[index] = 1;
            first_step[index] = first_step[o - p->offices] == -1 ? neighbour->id : first_step[o - p->offices];
            if (neighbour->id == to) {
                *next = first_step[index];
                break;
            }
            queue[tail++] = (int)index;
        }
    }
    free(queue);
    free(first_step);
    free(seen);
    return OK;
}

/* ===================== журнал ===================== */

status_code log_line(post *p, const char *text) {
    if (p == NULL || text == NULL) {
        return INVALID_ARGS;
    }
    fprintf(p->log, "[tick %lld] %s\n", p->tick, text);
    fflush(p->log);
    return OK;
}

/* Текст целого числа без printf (см. задание 4 про ложные сообщения Dr. Memory) */
status_code int_text(const long long value, char *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long m = value < 0 ? 0ULL - (unsigned long long)value : (unsigned long long)value;
    char digits[24];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + m % 10);
        m /= 10;
    } while (m > 0);
    size_t k = 0;
    if (value < 0) {
        out[k++] = '-';
    }
    while (n > 0) {
        out[k++] = digits[--n];
    }
    out[k] = '\0';
    return OK;
}

/* Строка журнала из частей: текст, число, текст, число, ... (до 4 чисел) */
status_code log_event(post *p, const char *a, const long long x, const char *b, const long long y, const char *c) {
    if (p == NULL || a == NULL) {
        return INVALID_ARGS;
    }
    char text[256];
    char number[24];
    strcpy(text, a);
    int_text(x, number);
    strcat(text, number);
    if (b != NULL) {
        strcat(text, b);
        if (c != NULL) {
            int_text(y, number);
            strcat(text, number);
            strcat(text, c);
        }
    }
    return log_line(p, text);
}

/* ===================== письма ===================== */

const char *state_name(const letter_state s) {
    switch (s) {
        case IN_TRANSIT:  return "in transit";
        case DELIVERED:   return "delivered (waiting for the receiver)";
        case UNDELIVERED: return "undelivered";
        default:          return "taken by the receiver";
    }
}

/* Положить письмо в отделение: в путь (куча) или, если это отделение получателя, - ждать получателя */
status_code put_letter(post *p, office *o, letter *l) {
    if (p == NULL || o == NULL || l == NULL) {
        return INVALID_ARGS;
    }
    l->office = o->id;
    if (o->id == l->to) {
        letter **tmp = (letter **)realloc(o->waiting, (o->waiting_count + 1) * sizeof(letter *));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        o->waiting = tmp;
        o->waiting[o->waiting_count++] = l;
        l->state = DELIVERED;
        return log_event(p, "letter ", l->id, " delivered to office ", o->id, "");
    }
    l->state = IN_TRANSIT;
    return heap_push(&o->queue, &o->queue_size, l);
}

/* Убрать письмо из отделения, где оно лежит */
status_code take_out(post *p, letter *l) {
    if (p == NULL || l == NULL) {
        return INVALID_ARGS;
    }
    office *o = NULL;
    if (find_office(p, l->office, &o) != OK) {
        return OK;   /* отделение уже удалено */
    }
    if (l->state == DELIVERED) {
        for (size_t i = 0; i < o->waiting_count; i++) {
            if (o->waiting[i] == l) {
                o->waiting[i] = o->waiting[--o->waiting_count];
                break;
            }
        }
        return OK;
    }
    int found = 0;
    return heap_remove(&o->queue, &o->queue_size, l, &found);
}

/* Новое письмо в отделении отправителя */
status_code add_letter(post *p, const char *type, const int priority, const int from, const int to, const char *data,
                       int *id) {
    if (p == NULL || type == NULL || data == NULL || id == NULL) {
        return INVALID_ARGS;
    }
    office *source = NULL;
    office *target = NULL;
    status_code sc = find_office(p, from, &source);
    if (sc == OK) {
        sc = find_office(p, to, &target);
    }
    if (sc != OK) {
        return sc;
    }
    size_t load = 0;
    office_load(source, &load);
    if (load >= (size_t)source->capacity) {
        return OFFICE_FULL;
    }
    letter *l = (letter *)malloc(sizeof(letter));
    char *copy = (char *)malloc(strlen(data) + 1);
    letter **tmp = (letter **)realloc(p->letters, (p->letter_count + 1) * sizeof(letter *));
    if (tmp != NULL) {
        p->letters = tmp;
    }
    if (l == NULL || copy == NULL || tmp == NULL) {
        free(l);
        free(copy);
        return ALLOC_ERROR;
    }
    sc = next_letter_id(&l->id);
    if (sc != OK) {
        free(l);
        free(copy);
        return sc;
    }
    strcpy(l->type, type);
    strcpy(copy, data);
    l->data = copy;
    l->priority = priority;
    l->from = from;
    l->to = to;
    p->letters[p->letter_count++] = l;
    *id = l->id;
    sc = log_event(p, "letter ", l->id, " created in office ", from, "");
    if (sc == OK) {
        sc = put_letter(p, source, l);
    }
    return sc;
}

status_code find_letter(const post *p, const int id, letter **result) {
    if (p == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < p->letter_count; i++) {
        if (p->letters[i]->id == id) {
            *result = p->letters[i];
            return OK;
        }
    }
    return NO_SUCH_LETTER;
}

/* Пометить письмо недоставленным и убрать из системы (из отделения) */
status_code mark_undelivered(post *p, letter *l, const char *reason) {
    if (p == NULL || l == NULL || reason == NULL) {
        return INVALID_ARGS;
    }
    if (l->state != IN_TRANSIT && l->state != DELIVERED) {
        return NO_SUCH_LETTER;   /* уже не в системе */
    }
    status_code sc = take_out(p, l);
    if (sc == OK) {
        l->state = UNDELIVERED;
        char text[200];
        char number[24];
        strcpy(text, "letter ");
        int_text(l->id, number);
        strcat(text, number);
        strcat(text, " marked undelivered: ");
        strncat(text, reason, 100);
        sc = log_line(p, text);
    }
    return sc;
}

/* Забрать письмо в отделении получателя: только доставленное */
status_code take_letter(post *p, letter *l) {
    if (p == NULL || l == NULL) {
        return INVALID_ARGS;
    }
    if (l->state == IN_TRANSIT) {
        return NOT_DELIVERED_YET;
    }
    if (l->state != DELIVERED) {
        return NO_SUCH_LETTER;
    }
    status_code sc = take_out(p, l);
    if (sc == OK) {
        l->state = TAKEN;
        sc = log_event(p, "letter ", l->id, " taken by the receiver in office ", l->office, "");
    }
    return sc;
}

/* ===================== такт ===================== */

/* Есть ли в отделении место с учётом писем, которые уже идут в него на этом такте (reserved) */
status_code has_room(const post *p, const office *o, const size_t *reserved, int *room) {
    if (p == NULL || o == NULL || reserved == NULL || room == NULL) {
        return INVALID_ARGS;
    }
    size_t load = 0;
    office_load(o, &load);
    *room = load + reserved[o - p->offices] < (size_t)o->capacity;
    return OK;
}

/*
 * Куда отправить письмо из отделения o: следующий шаг кратчайшего пути, а если там нет места или пути
 * нет - ближайшее доступное отделение (сосед со свободным местом, с меньшим id). *target = NULL - некуда.
 */
status_code choose_target(post *p, const office *o, const letter *l, const size_t *reserved, office **target) {
    if (p == NULL || o == NULL || l == NULL || reserved == NULL || target == NULL) {
        return INVALID_ARGS;
    }
    *target = NULL;
    int next = -1;
    status_code sc = next_hop(p, o->id, l->to, &next);
    if (sc != OK) {
        return sc;
    }
    int room = 0;
    if (next != -1) {
        office *hop = NULL;
        find_office(p, next, &hop);
        has_room(p, hop, reserved, &room);
        if (room) {
            *target = hop;
            return OK;
        }
    }
    for (size_t k = 0; k < o->link_count; k++) {
        office *neighbour = NULL;
        if (find_office(p, o->links[k], &neighbour) != OK || neighbour->id == next) {
            continue;
        }
        has_room(p, neighbour, reserved, &room);
        if (room) {
            *target = neighbour;
            return OK;
        }
    }
    return OK;
}

/*
 * Один такт: из каждого отделения (по возрастанию id) самое важное письмо в пути уходит дальше.
 * 1) решаем, куда идёт верхнее письмо каждого отделения (письма ещё на местах, а уже назначенные
 *    приходы "бронируют" место в отделении - так вместимость не превысится);
 * 2) снимаем выбранные письма со своих куч; 3) кладём в новые отделения.
 * Так каждое письмо за такт делает ровно один шаг.
 */
status_code tick(post *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    p->tick++;
    size_t n = p->office_count;
    office **targets = (office **)calloc(n + 1, sizeof(office *));
    size_t *reserved = (size_t *)calloc(n + 1, sizeof(size_t));
    letter **moving = (letter **)calloc(n + 1, sizeof(letter *));
    status_code sc = (targets == NULL || reserved == NULL || moving == NULL) ? ALLOC_ERROR : OK;
    for (size_t i = 0; i < n && sc == OK; i++) {
        if (p->offices[i].queue == NULL) {
            continue;
        }
        const letter *top = p->offices[i].queue->item;   /* верх кучи - самое важное письмо */
        sc = choose_target(p, &p->offices[i], top, reserved, &targets[i]);
        if (sc == OK && targets[i] != NULL) {
            reserved[targets[i] - p->offices]++;
        }
    }
    for (size_t i = 0; i < n && sc == OK; i++) {
        if (targets[i] != NULL) {
            sc = heap_pop(&p->offices[i].queue, &p->offices[i].queue_size, &moving[i]);
        }
    }
    for (size_t i = 0; i < n && sc == OK; i++) {
        letter *l = moving[i];
        if (l == NULL) {
            continue;
        }
        char text[96];
        char number[24];
        strcpy(text, "letter ");
        int_text(l->id, number);
        strcat(text, number);
        strcat(text, " moved from office ");
        int_text(p->offices[i].id, number);
        strcat(text, number);
        strcat(text, " to office ");
        int_text(targets[i]->id, number);
        strcat(text, number);
        sc = log_line(p, text);
        if (sc == OK) {
            sc = put_letter(p, targets[i], l);
        }
    }
    free(targets);
    free(reserved);
    free(moving);
    return sc;
}

status_code run_ticks(post *p, const long long count) {
    if (p == NULL || count < 0) {
        return INVALID_ARGS;
    }
    for (long long i = 0; i < count; i++) {
        status_code sc = tick(p);
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

/* Удалить отделение (см. условие): письма к нему или из него - недоставлены, остальные - к соседям */
status_code remove_office(post *p, const int id) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    office *o = NULL;
    status_code sc = find_office(p, id, &o);
    if (sc != OK) {
        return sc;
    }
    /* 1) все письма в системе к этому отделению или из него - недоставлены */
    for (size_t i = 0; i < p->letter_count && sc == OK; i++) {
        letter *l = p->letters[i];
        if ((l->state == IN_TRANSIT || l->state == DELIVERED) && (l->to == id || l->from == id)) {
            sc = mark_undelivered(p, l, "its office is removed");
        }
    }
    /* 2) остальные письма удаляемого отделения - соседям со свободным местом */
    find_office(p, id, &o);
    while (sc == OK && o->queue != NULL) {
        letter *l = NULL;
        heap_pop(&o->queue, &o->queue_size, &l);
        office *target = NULL;
        for (size_t k = 0; k < o->link_count && target == NULL; k++) {
            office *neighbour = NULL;
            size_t load = 0;
            if (find_office(p, o->links[k], &neighbour) == OK && office_load(neighbour, &load) == OK &&
                load < (size_t)neighbour->capacity) {
                target = neighbour;
            }
        }
        if (target == NULL) {
            /* из кучи письмо уже снято - остаётся отметить состояние */
            l->office = -1;
            l->state = UNDELIVERED;
            sc = log_event(p, "letter ", l->id, " marked undelivered: no room in the neighbours of office ", id,
                           "");
        } else {
            sc = log_event(p, "letter ", l->id, " sent away from the removed office ", id, "");
            if (sc == OK) {
                sc = put_letter(p, target, l);
            }
        }
    }
    if (sc != OK) {
        return sc;
    }
    /* 3) связи с ним у соседей и само отделение */
    for (size_t i = 0; i < p->office_count; i++) {
        office *x = &p->offices[i];
        for (size_t k = 0; k < x->link_count; k++) {
            if (x->links[k] == id) {
                /* сдвиг хвоста влево - порядок по возрастанию сохраняется */
                memmove(x->links + k, x->links + k + 1, (x->link_count - k - 1) * sizeof(int));
                x->link_count--;
                break;
            }
        }
    }
    find_office(p, id, &o);
    free(o->links);
    free(o->waiting);
    size_t index = (size_t)(o - p->offices);
    memmove(o, o + 1, (p->office_count - index - 1) * sizeof(office));
    p->office_count--;
    return log_event(p, "office ", id, " removed", 0, NULL);
}

/* ===================== время ===================== */

/* Сколько миллисекунд прошло с какого-то момента (только для разностей). В C99 таких часов нет -
   берём у системы: Linux - clock_gettime(CLOCK_MONOTONIC), Windows - GetTickCount64 */
status_code now_ms(long long *ms) {
    if (ms == NULL) {
        return INVALID_ARGS;
    }
#ifdef _WIN32
    *ms = (long long)GetTickCount64();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    *ms = (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
    return OK;
}

status_code sleep_ms(const long long ms) {
    if (ms < 0) {
        return INVALID_ARGS;
    }
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec t;
    t.tv_sec = (time_t)(ms / 1000);
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#endif
    return OK;
}

/* ===================== ввод ===================== */

/* Строка -> int: необязательный минус и цифры, без лишних символов */
status_code parse_int(const char *s, int *value) {
    if (s == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t i = s[0] == '-' ? 1 : 0;
    if (s[i] == '\0') {
        return SYNTAX_ERROR;
    }
    long long result = 0;
    for (; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return SYNTAX_ERROR;
        }
        result = result * 10 + (s[i] - '0');
        if (result > INT_MAX) {
            return SYNTAX_ERROR;
        }
    }
    *value = s[0] == '-' ? (int)-result : (int)result;
    return OK;
}

/* Прочитать строку любой длины (без '\n' и '\r'); *end = 1 - ввод кончился */
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

/* Разрезать строку на слова (по пробелам). Слова - указатели в line (line портится). max слов */
status_code split_words(char *line, char **words, const size_t max, size_t *count) {
    if (line == NULL || words == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    *count = 0;
    char *p = line;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*count == max) {
            return SYNTAX_ERROR;
        }
        words[(*count)++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return OK;
}

/* Файл связей: в каждой строке "id1 id2"; отделения создаются с вместимостью DEFAULT_CAPACITY */
status_code load_mappings(post *p, const char *path, size_t *bad_line) {
    if (p == NULL || path == NULL || bad_line == NULL) {
        return INVALID_ARGS;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return FILE_ERROR;
    }
    status_code sc = OK;
    size_t number = 0;
    *bad_line = 0;
    while (sc == OK) {
        char *line = NULL;
        int end = 0;
        sc = read_line(file, &line, &end);
        if (sc != OK || end) {
            free(line);
            break;
        }
        number++;
        char *words[3];
        size_t count = 0;
        int a = 0;
        int b = 0;
        sc = split_words(line, words, 3, &count);
        if (sc == OK && count == 0) {
            free(line);
            continue;   /* пустая строка */
        }
        if (sc == OK && (count != 2 || parse_int(words[0], &a) != OK || parse_int(words[1], &b) != OK || a < 0 ||
                         b < 0 || a == b)) {
            sc = SYNTAX_ERROR;
        }
        free(line);
        office *o = NULL;
        if (sc == OK && find_office(p, a, &o) != OK) {
            sc = create_office(p, a, DEFAULT_CAPACITY);
        }
        if (sc == OK && find_office(p, b, &o) != OK) {
            sc = create_office(p, b, DEFAULT_CAPACITY);
        }
        if (sc == OK) {
            sc = link_offices(p, a, b);
        }
        if (sc != OK) {
            *bad_line = number;
        }
    }
    fclose(file);
    return sc;
}

/* ===================== вывод ===================== */

status_code print_letter(FILE *out, const letter *l) {
    if (out == NULL || l == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "letter %d: %s, priority %d, from office %d to office %d, %s", l->id, l->type, l->priority, l->from,
            l->to, state_name(l->state));
    if (l->state == IN_TRANSIT) {
        fprintf(out, ", now in office %d", l->office);
    }
    fprintf(out, ", data: \"%s\"\n", l->data);
    return OK;
}

status_code print_letters(FILE *out, const post *p) {
    if (out == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Letters: %zu\n", p->letter_count);
    for (size_t i = 0; i < p->letter_count; i++) {
        print_letter(out, p->letters[i]);
    }
    return OK;
}

status_code print_offices(FILE *out, const post *p) {
    if (out == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Offices: %zu\n", p->office_count);
    for (size_t i = 0; i < p->office_count; i++) {
        const office *o = &p->offices[i];
        fprintf(out, "office %d: %zu of %d places used (%zu in transit, %zu waiting), links:", o->id,
                o->queue_size + o->waiting_count, o->capacity, o->queue_size, o->waiting_count);
        for (size_t k = 0; k < o->link_count; k++) {
            fprintf(out, " %d", o->links[k]);
        }
        fprintf(out, "%s\n", o->link_count == 0 ? " none" : "");
    }
    return OK;
}

status_code print_help(FILE *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Commands:\n");
    fprintf(out, "  add_office ID CAPACITY [LINKED_ID ...]   add a post office\n");
    fprintf(out, "  remove_office ID                         remove a post office\n");
    fprintf(out, "  add_letter TYPE PRIORITY FROM TO [DATA]  send a letter (TYPE: normal or urgent)\n");
    fprintf(out, "  undelivered LETTER_ID                    mark a letter as undelivered\n");
    fprintf(out, "  take LETTER_ID                           take a delivered letter\n");
    fprintf(out, "  letters                                  show all letters\n");
    fprintf(out, "  list FILE                                write all letters to a file\n");
    fprintf(out, "  offices                                  show all post offices\n");
    fprintf(out, "  step [N]                                 move letters N times now (default 1)\n");
    fprintf(out, "  wait SECONDS                             wait (letters move every 0.2 s)\n");
    fprintf(out, "  help                                     this list\n");
    fprintf(out, "  exit                                     quit\n");
    return OK;
}

status_code error_text(const status_code sc, const char **text) {
    if (text == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case SYNTAX_ERROR:      *text = "wrong command, see help"; break;
        case NO_SUCH_OFFICE:    *text = "there is no such post office"; break;
        case OFFICE_EXISTS:     *text = "a post office with this id already exists"; break;
        case NO_SUCH_LETTER:    *text = "there is no such letter in the system"; break;
        case OFFICE_FULL:       *text = "the post office is full"; break;
        case NOT_DELIVERED_YET: *text = "the letter has not reached its post office yet"; break;
        case FILE_ERROR:        *text = "can not write the file"; break;
        case INVALID_ARGS:      *text = "wrong values"; break;
        default:                *text = "internal error"; break;   /* другие статусы сюда не попадают */
    }
    return OK;
}

/* ===================== команды ===================== */

/*
 * Выполнить одну команду (слова уже разделены). *quit = 1 - команда exit.
 * data - остаток строки после 5-го слова (технические данные письма), может быть пустым.
 */
status_code execute(post *p, char **w, const size_t n, const char *data, const int manual, int *quit) {
    if (p == NULL || w == NULL || data == NULL || quit == NULL || n == 0) {
        return INVALID_ARGS;
    }
    int x = 0;
    int y = 0;
    if (strcmp(w[0], "help") == 0 && n == 1) {
        return print_help(stdout);
    }
    if (strcmp(w[0], "exit") == 0 && n == 1) {
        *quit = 1;
        return OK;
    }
    if (strcmp(w[0], "offices") == 0 && n == 1) {
        return print_offices(stdout, p);
    }
    if (strcmp(w[0], "letters") == 0 && n == 1) {
        return print_letters(stdout, p);
    }
    if (strcmp(w[0], "list") == 0 && n == 2) {
        FILE *file = fopen(w[1], "w");
        if (file == NULL) {
            return FILE_ERROR;
        }
        print_letters(file, p);
        if (fclose(file) != 0) {
            return FILE_ERROR;
        }
        printf("All letters are written to %s\n", w[1]);
        return OK;
    }
    if (strcmp(w[0], "add_office") == 0 && n >= 3) {
        if (parse_int(w[1], &x) != OK || parse_int(w[2], &y) != OK || x < 0 || y <= 0) {
            return SYNTAX_ERROR;
        }
        /* сначала проверяем связи: отделения должны существовать */
        for (size_t i = 3; i < n; i++) {
            int other = 0;
            office *o = NULL;
            if (parse_int(w[i], &other) != OK) {
                return SYNTAX_ERROR;
            }
            if (find_office(p, other, &o) != OK) {
                return NO_SUCH_OFFICE;
            }
        }
        status_code sc = create_office(p, x, y);
        for (size_t i = 3; i < n && sc == OK; i++) {
            int other = 0;
            parse_int(w[i], &other);
            if (other != x) {
                sc = link_offices(p, x, other);
            }
        }
        if (sc == OK) {
            sc = log_event(p, "office ", x, " added, capacity ", y, "");
            printf("Office %d added\n", x);
        }
        return sc;
    }
    if (strcmp(w[0], "remove_office") == 0 && n == 2) {
        if (parse_int(w[1], &x) != OK) {
            return SYNTAX_ERROR;
        }
        status_code sc = remove_office(p, x);
        if (sc == OK) {
            printf("Office %d removed\n", x);
        }
        return sc;
    }
    if (strcmp(w[0], "add_letter") == 0 && n >= 5) {
        int priority = 0;
        if ((strcmp(w[1], "normal") != 0 && strcmp(w[1], "urgent") != 0) || parse_int(w[2], &priority) != OK ||
            parse_int(w[3], &x) != OK || parse_int(w[4], &y) != OK) {
            return SYNTAX_ERROR;
        }
        int id = 0;
        status_code sc = add_letter(p, w[1], priority, x, y, data, &id);
        if (sc == OK) {
            printf("Letter %d accepted in office %d\n", id, x);
        }
        return sc;
    }
    if ((strcmp(w[0], "undelivered") == 0 || strcmp(w[0], "take") == 0) && n == 2) {
        letter *l = NULL;
        if (parse_int(w[1], &x) != OK) {
            return SYNTAX_ERROR;
        }
        status_code sc = find_letter(p, x, &l);
        if (sc == OK && w[0][0] == 'u') {
            sc = mark_undelivered(p, l, "by the operator");
            if (sc == OK) {
                printf("Letter %d is marked as undelivered\n", x);
            }
        } else if (sc == OK) {
            sc = take_letter(p, l);
            if (sc == OK) {
                printf("Letter %d is taken in office %d\n", x, l->office);
            }
        }
        return sc;
    }
    if (strcmp(w[0], "step") == 0 && n <= 2) {
        x = 1;
        if (n == 2 && (parse_int(w[1], &x) != OK || x < 0 || x > MAX_TICKS_AT_ONCE)) {
            return SYNTAX_ERROR;
        }
        return run_ticks(p, x);
    }
    if (strcmp(w[0], "wait") == 0 && n == 2) {
        if (parse_int(w[1], &x) != OK || x < 0 || x > 3600) {
            return SYNTAX_ERROR;
        }
        if (manual) {
            return run_ticks(p, (long long)x * 1000 / TICK_MS);   /* время "понарошку" */
        }
        return sleep_ms((long long)x * 1000);   /* такты догонит цикл команд */
    }
    return SYNTAX_ERROR;
}

/* Цикл команд: строка -> (догнать такты) -> команда -> ответ */
status_code command_loop(post *p, const int manual, const int interactive) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    long long last = 0;
    now_ms(&last);
    int quit = 0;
    status_code sc = OK;
    if (interactive) {
        printf("Post service. Type help for the list of commands.\n");
    }
    while (!quit && sc == OK) {
        if (interactive) {
            printf("> ");
            fflush(stdout);
        }
        char *line = NULL;
        int end = 0;
        sc = read_line(stdin, &line, &end);
        if (sc != OK || end) {
            free(line);
            break;
        }
        /* настоящее время: сколько тактов прошло, пока ждали команду */
        if (!manual) {
            long long now = 0;
            now_ms(&now);
            long long ticks = (now - last) / TICK_MS;
            last += ticks * TICK_MS;
            sc = run_ticks(p, ticks > MAX_TICKS_AT_ONCE ? MAX_TICKS_AT_ONCE : ticks);
        }
        /* данные письма - всё после пятого слова (с пробелами внутри) */
        const char *data = "";
        size_t skip = 0;
        size_t words_seen = 0;
        while (line[skip] != '\0' && words_seen < 5) {
            while (line[skip] == ' ' || line[skip] == '\t') {
                skip++;
            }
            if (line[skip] == '\0') {
                break;
            }
            while (line[skip] != '\0' && line[skip] != ' ' && line[skip] != '\t') {
                skip++;
            }
            words_seen++;
        }
        while (line[skip] == ' ' || line[skip] == '\t') {
            skip++;
        }
        char *data_copy = (char *)malloc(strlen(line + skip) + 1);
        if (data_copy == NULL) {
            free(line);
            sc = ALLOC_ERROR;
            break;
        }
        strcpy(data_copy, line + skip);
        data = data_copy;

        char *words[64];
        size_t n = 0;
        status_code result = split_words(line, words, 64, &n);
        if (result == OK && n > 0) {
            /* add_letter: слова после пятого - это данные, их не проверяем как аргументы */
            if (strcmp(words[0], "add_letter") == 0 && n > 5) {
                n = 5;
            }
            result = execute(p, words, n, data, manual, &quit);
        }
        if (result == ALLOC_ERROR) {
            sc = result;
        } else if (result != OK) {
            const char *text = NULL;
            error_text(result, &text);
            printf("Error: %s\n", text);
        }
        if (!manual) {
            /* wait спал - догоняем такты сразу */
            long long now = 0;
            now_ms(&now);
            long long ticks = (now - last) / TICK_MS;
            last += ticks * TICK_MS;
            if (sc == OK) {
                sc = run_ticks(p, ticks > MAX_TICKS_AT_ONCE ? MAX_TICKS_AT_ONCE : ticks);
            }
        }
        fflush(stdout);
        free(data_copy);
        free(line);
    }
    return sc;
}

/* Освободить всё */
status_code post_free(post *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < p->office_count; i++) {
        letter *l = NULL;
        while (p->offices[i].queue != NULL) {
            heap_pop(&p->offices[i].queue, &p->offices[i].queue_size, &l);
        }
        free(p->offices[i].links);
        free(p->offices[i].waiting);
    }
    free(p->offices);
    for (size_t i = 0; i < p->letter_count; i++) {
        free(p->letters[i]->data);
        free(p->letters[i]);
    }
    free(p->letters);
    return OK;
}

/* Случайное имя журнала: "post_" и 8 букв и цифр (свой генератор от времени) */
status_code random_log_name(char *out, const size_t size) {
    if (out == NULL || size < 20) {
        return INVALID_ARGS;
    }
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    unsigned long long state = (unsigned long long)time(NULL) ^ 0x9E3779B97F4A7C15ULL;
    strcpy(out, "post_");
    for (int i = 0; i < 8; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out[5 + i] = alphabet[state % 62];
    }
    strcpy(out + 13, ".log");
    return OK;
}

int main(int argc, char *argv[]) {
    /* файл связей, необязательно файл журнала и --manual */
    const char *mappings = NULL;
    const char *log_path = NULL;
    int manual = 0;
    int wrong = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--manual") == 0) {
            wrong |= manual;
            manual = 1;
        } else if (mappings == NULL) {
            mappings = argv[i];
        } else if (log_path == NULL) {
            log_path = argv[i];
        } else {
            wrong = 1;
        }
    }
    if (wrong || mappings == NULL) {
        fprintf(stderr, "Usage: lab4_task6 <mappings file> [<log file>] [--manual]\n");
        return 1;
    }
    char random_name[32];
    if (log_path == NULL) {
        random_log_name(random_name, sizeof(random_name));
        log_path = random_name;
    }

    post p = {NULL, 0, NULL, 0, 0, NULL};
    size_t bad_line = 0;
    status_code sc = load_mappings(&p, mappings, &bad_line);
    if (sc != OK) {
        if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not read file '%s'\n", mappings);
        } else if (sc == SYNTAX_ERROR) {
            fprintf(stderr, "Error: wrong line %zu in '%s' (need two different ids)\n", bad_line, mappings);
        } else {
            fprintf(stderr, "Error: not enough memory\n");
        }
        post_free(&p);
        return 1;
    }
    p.log = fopen(log_path, "w");
    if (p.log == NULL) {
        fprintf(stderr, "Error: can not open log file '%s'\n", log_path);
        post_free(&p);
        return 1;
    }
    printf("Log file: %s\n", log_path);
    log_event(&p, "post service started with ", (long long)p.office_count, " offices", 0, NULL);

#ifdef _WIN32
    int interactive = _isatty(_fileno(stdin));
#else
    int interactive = isatty(fileno(stdin));
#endif
    sc = command_loop(&p, manual, interactive);
    log_line(&p, "post service stopped");
    fclose(p.log);
    post_free(&p);
    if (sc != OK) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    return 0;
}
