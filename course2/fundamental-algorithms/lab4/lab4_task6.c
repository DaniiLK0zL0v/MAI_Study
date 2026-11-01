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
    o->links[o->link_count++] = other;
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
status_code connect(post *p, const int a, const int b) {
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

/* Связи отделения - по возрастанию id (для одинакового поиска при каждом запуске) */
int compare_ints(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
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

/* Куда отправить письмо из отделения o: следующий шаг пути или ближайший сосед со свободным местом */
status_code choose_target(post *p, const office *o, const letter *l, office **target) {
    if (p == NULL || o == NULL || l == NULL || target == NULL) {
        return INVALID_ARGS;
    }
    *target = NULL;
    int next = -1;
    status_code sc = next_hop(p, o->id, l->to, &next);
    if (sc != OK) {
        return sc;
    }
    if (next != -1) {
        office *hop = NULL;
        find_office(p, next, &hop);
        size_t load = 0;
        office_load(hop, &load);
        if (load < (size_t)hop->capacity) {
            *target = hop;
            return OK;
        }
    }
    /* путь закрыт - ближайшее доступное: сосед со свободным местом (с меньшим id) */
    for (size_t k = 0; k < o->link_count; k++) {
        office *neighbour = NULL;
        if (find_office(p, o->links[k], &neighbour) != OK || neighbour->id == next) {
            continue;
        }
        size_t load = 0;
        office_load(neighbour, &load);
        if (load < (size_t)neighbour->capacity) {
            *target = neighbour;
            return OK;
        }
    }
    return OK;   /* некуда - письмо ждёт */
}

/*
 * Один такт: из каждого отделения (по возрастанию id) самое важное письмо в пути уходит дальше.
 * Сначала решаем, куда идёт каждое письмо, и только потом переносим - чтобы письмо за такт сделало один шаг.
 */
status_code tick(post *p) {
    if (p == NULL) {
        return INVALID_ARGS;
    }
    p->tick++;
    size_t n = p->office_count;
    letter **moving = (letter **)calloc(n + 1, sizeof(letter *));
    if (moving == NULL) {
        return ALLOC_ERROR;
    }
    status_code sc = OK;
    for (size_t i = 0; i < n && sc == OK; i++) {
        if (p->offices[i].queue != NULL) {
            sc = heap_pop(&p->offices[i].queue, &p->offices[i].queue_size, &moving[i]);
        }
    }
    for (size_t i = 0; i < n && sc == OK; i++) {
        letter *l = moving[i];
        if (l == NULL) {
            continue;
        }
        office *from = &p->offices[i];
        office *target = NULL;
        sc = choose_target(p, from, l, &target);
        if (sc == OK && target == NULL) {
            sc = heap_push(&from->queue, &from->queue_size, l);   /* остаётся на месте */
        } else if (sc == OK) {
            sc = log_event(p, "letter ", l->id, " moved from office ", from->id, "");
            if (sc == OK) {
                char text[96];
                char number[24];
                strcpy(text, "letter ");
                int_text(l->id, number);
                strcat(text, number);
                strcat(text, " arrived at office ");
                int_text(target->id, number);
                strcat(text, number);
                sc = log_line(p, text);
            }
            if (sc == OK) {
                sc = put_letter(p, target, l);
            }
        }
    }
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
            l->office = -1;
            l->state = IN_TRANSIT;   /* чтобы mark_undelivered приняла; из кучи письмо уже снято */
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
                x->links[k] = x->links[--x->link_count];
                break;
            }
        }
        qsort(x->links, x->link_count, sizeof(int), compare_ints);
    }
    find_office(p, id, &o);
    free(o->links);
    free(o->waiting);
    size_t index = (size_t)(o - p->offices);
    memmove(o, o + 1, (p->office_count - index - 1) * sizeof(office));
    p->office_count--;
    return log_event(p, "office ", id, " removed", 0, NULL);
}
