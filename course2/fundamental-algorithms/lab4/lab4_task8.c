/*
 * Лабораторная 4, задание 8. Планировщик процессов с приоритетами (биномиальная куча, бор для строк).
 *
 * Запуск:  lab4_task8 [--input <файл>] [--output <файл>] [--fast]
 *   команды - со стандартного ввода или из файла --input; события - в стандартный вывод или в файл --output.
 *   --fast: время идёт только командой !step и после конца ввода - сразу, без ожидания (для проверки).
 *   Без --fast: один такт - 0.2 секунды настоящего времени; перед каждой командой программа догоняет
 *   такты по прошедшему времени, после конца ввода работает, пока все процессы не завершатся.
 * Команды:
 *   !np <name> <pid> <priority> <time>                - новый процесс
 *   !pp <parent pid> <name> <pid> <priority> <time>   - новый дочерний процесс
 *   !wait <pid> <time>   - приостановить исполняемый сейчас процесс на time тактов
 *   !list / !all         - активные процессы / все процессы
 *   !exec <count>        - сколько процессов исполняется одновременно (вначале 1)
 *   !edit <pid> <priority> - новый приоритет
 *   !step <count>        - (дополнительно) пропустить count тактов сейчас
 *
 * Каждый такт исполняются count самых приоритетных готовых процессов (при равном приоритете - добавленный
 * раньше); каждый за такт получает одну единицу времени. Набор исполняемых пересчитывается каждый такт,
 * поэтому новый процесс с большим приоритетом (или !edit) вытесняет текущие. Родитель, отработавший своё
 * время, ждёт завершения всех дочерних и только тогда завершается.
 *
 * Готовые процессы - в биномиальной куче. Смена приоритета и ожидание: старая запись в куче становится
 * недействительной (номер версии процесса увеличивается), процесс кладётся заново - "ленивое удаление":
 * при извлечении недействительные записи пропускаются. Имена процессов хранятся в боре: каждое разное имя -
 * один раз, процессы ссылаются на него.
 */
#define _POSIX_C_SOURCE 200809L   /* clock_gettime, nanosleep */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* strcmp, strlen, memcpy */
#include <limits.h>   /* INT_MAX */
#include <time.h>     /* clock_gettime, nanosleep (Linux) */
#ifdef _WIN32
#include <windows.h>  /* GetTickCount64, Sleep */
#endif

#define TICK_MS 200
#define MAX_STEP 1000000
#define MAX_RUN_TICKS 10000000   /* после конца ввода - не дольше этого (защита от бесконечного ожидания) */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,       /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,       /* команда записана неправильно */
    PID_EXISTS,         /* процесс с таким pid уже есть */
    NO_SUCH_PROCESS,    /* нет процесса с таким pid */
    NOT_RUNNING,        /* !wait: процесс сейчас не исполняется */
    PARENT_FINISHED,    /* !pp: родитель уже завершён */
    FILE_ERROR,         /* файл не открылся / ошибка чтения или записи */
    ALLOC_ERROR         /* malloc / realloc не смогли выделить память */
} status_code;

typedef enum { READY, RUNNING, WAITING, CHILDREN_WAIT, FINISHED } process_state;

/* Процесс */
typedef struct process {
    int pid;
    const char *name;        /* строка из бора (общая для одинаковых имён) */
    int priority;
    long long time;          /* сколько нужно всего */
    long long left;          /* сколько осталось */
    long long order;         /* номер добавления - при равном приоритете раньше добавленный */
    long long version;       /* номер версии записи в куче (ленивое удаление) */
    long long wake;          /* WAITING: такт, на котором процесс снова готов */
    process_state state;
    int was_running;         /* исполнялся на прошлом такте */
    struct process *parent;
    size_t children_active;  /* сколько дочерних ещё не завершено */
} process;

/* Запись в биномиальной куче: процесс и его приоритет/версия на момент вставки */
typedef struct heap_node {
    process *item;
    int priority;
    long long order;
    long long version;
    size_t degree;               /* у дерева этого корня 2^degree узлов */
    struct heap_node *child;     /* старший ребёнок */
    struct heap_node *sibling;   /* следующий корень (в списке корней) или следующий брат */
} heap_node;

/* Биномиальная куча: список корней по возрастанию степени */
typedef struct {
    heap_node *head;
} binomial_heap;

/* Узел бора строк: символ, первый ребёнок, следующий брат; str - строка, если здесь кончается имя */
typedef struct trie_node {
    char symbol;
    struct trie_node *child;
    struct trie_node *sibling;
    char *str;
} trie_node;

/* Вся система */
typedef struct {
    process **processes;     /* по возрастанию pid (двоичный поиск) */
    size_t count;
    long long next_order;
    binomial_heap ready;
    trie_node *names;        /* корень бора (пустой символ) */
    int slots;               /* сколько процессов исполняется одновременно (!exec) */
    long long tick;
    FILE *events;
} scheduler;

/* ===================== бор строк ===================== */

/*
 * Найти строку в боре, а если её нет - добавить. *result - копия строки из бора (одна на все одинаковые).
 * Узел - символ; дети узла - список (первый ребёнок + братья).
 */
status_code trie_intern(trie_node *root, const char *s, const char **result) {
    if (root == NULL || s == NULL || result == NULL || s[0] == '\0') {
        return INVALID_ARGS;
    }
    trie_node *node = root;
    for (size_t i = 0; s[i] != '\0'; i++) {
        trie_node *next = node->child;
        while (next != NULL && next->symbol != s[i]) {
            next = next->sibling;
        }
        if (next == NULL) {
            next = (trie_node *)calloc(1, sizeof(trie_node));
            if (next == NULL) {
                return ALLOC_ERROR;
            }
            next->symbol = s[i];
            next->sibling = node->child;
            node->child = next;
        }
        node = next;
    }
    if (node->str == NULL) {
        node->str = (char *)malloc(strlen(s) + 1);
        if (node->str == NULL) {
            return ALLOC_ERROR;
        }
        strcpy(node->str, s);
    }
    *result = node->str;
    return OK;
}

/* Освободить бор без рекурсии: узел за узлом, детей и братьев - в стек */
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
        trie_node *links[2] = {node->child, node->sibling};
        for (int k = 0; k < 2; k++) {
            if (links[k] == NULL) {
                continue;
            }
            if (size == capacity) {
                trie_node **tmp = (trie_node **)realloc(stack, capacity * 2 * sizeof(trie_node *));
                if (tmp == NULL) {
                    free(stack);
                    return ALLOC_ERROR;
                }
                stack = tmp;
                capacity *= 2;
            }
            stack[size++] = links[k];
        }
        free(node->str);
        free(node);
    }
    free(stack);
    return OK;
}

/* ===================== биномиальная куча ===================== */

/* a важнее b: больше приоритет, при равном - раньше добавлен */
status_code more_important(const heap_node *a, const heap_node *b, int *result) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    *result = a->priority > b->priority || (a->priority == b->priority && a->order < b->order);
    return OK;
}

/* Связать два дерева одной степени: менее важный корень становится старшим ребёнком более важного */
status_code link_trees(heap_node *child, heap_node *parent) {
    if (child == NULL || parent == NULL) {
        return INVALID_ARGS;
    }
    child->sibling = parent->child;
    parent->child = child;
    parent->degree++;
    return OK;
}

/*
 * Объединение двух куч: 1) слить списки корней по возрастанию степени; 2) пройти по списку и связать
 * соседние деревья одинаковой степени (как сложение двоичных чисел с переносом).
 */
status_code heap_union(heap_node *a, heap_node *b, heap_node **result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    /* 1) слияние упорядоченных списков */
    heap_node *head = NULL;
    heap_node **tail = &head;
    while (a != NULL && b != NULL) {
        if (a->degree <= b->degree) {
            *tail = a;
            a = a->sibling;
        } else {
            *tail = b;
            b = b->sibling;
        }
        tail = &(*tail)->sibling;
    }
    *tail = a != NULL ? a : b;
    if (head == NULL) {
        *result = NULL;
        return OK;
    }
    /* 2) связывание одинаковых степеней */
    heap_node *prev = NULL;
    heap_node *x = head;
    heap_node *next = x->sibling;
    while (next != NULL) {
        int same_three = next->sibling != NULL && next->sibling->degree == x->degree;
        if (x->degree != next->degree || same_three) {
            prev = x;           /* степени разные (или впереди три одинаковых) - идём дальше */
            x = next;
        } else {
            int better = 0;
            more_important(x, next, &better);
            if (better) {
                x->sibling = next->sibling;
                link_trees(next, x);
            } else {
                if (prev == NULL) {
                    head = next;
                } else {
                    prev->sibling = next;
                }
                link_trees(x, next);
                x = next;
            }
        }
        next = x->sibling;
    }
    *result = head;
    return OK;
}

status_code heap_insert(binomial_heap *h, process *p) {
    if (h == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    heap_node *node = (heap_node *)calloc(1, sizeof(heap_node));
    if (node == NULL) {
        return ALLOC_ERROR;
    }
    node->item = p;
    node->priority = p->priority;
    node->order = p->order;
    node->version = p->version;
    return heap_union(h->head, node, &h->head);
}

/* Извлечь важнейшую запись: найти корень-максимум, убрать его, его детей (в обратном порядке) слить с кучей */
status_code heap_extract(binomial_heap *h, heap_node **result) {
    if (h == NULL || result == NULL || h->head == NULL) {
        return INVALID_ARGS;
    }
    heap_node *best = h->head;
    heap_node *best_prev = NULL;
    heap_node *prev = h->head;
    for (heap_node *x = h->head->sibling; x != NULL; prev = x, x = x->sibling) {
        int better = 0;
        more_important(x, best, &better);
        if (better) {
            best = x;
            best_prev = prev;
        }
    }
    if (best_prev == NULL) {
        h->head = best->sibling;
    } else {
        best_prev->sibling = best->sibling;
    }
    /* дети корня идут от старшего к младшему - разворачиваем, чтобы степени возрастали */
    heap_node *reversed = NULL;
    heap_node *c = best->child;
    while (c != NULL) {
        heap_node *next = c->sibling;
        c->sibling = reversed;
        reversed = c;
        c = next;
    }
    best->child = NULL;
    best->sibling = NULL;
    *result = best;
    return heap_union(h->head, reversed, &h->head);
}

/*
 * Извлечь важнейший процесс, который действительно готов: записи с устаревшей версией или о процессах
 * не в состоянии READY/RUNNING пропускаются (ленивое удаление). *p = NULL - готовых нет.
 */
status_code heap_extract_valid(binomial_heap *h, process **p) {
    if (h == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    *p = NULL;
    while (h->head != NULL) {
        heap_node *node = NULL;
        status_code sc = heap_extract(h, &node);
        if (sc != OK) {
            return sc;
        }
        process *candidate = node->item;
        int valid = node->version == candidate->version &&
                    (candidate->state == READY || candidate->state == RUNNING);
        free(node);
        if (valid) {
            *p = candidate;
            return OK;
        }
    }
    return OK;
}

/* Освободить все записи кучи без рекурсии (стек узлов) */
status_code heap_free(binomial_heap *h) {
    if (h == NULL) {
        return INVALID_ARGS;
    }
    heap_node *dummy = NULL;
    while (h->head != NULL) {
        heap_extract(h, &dummy);
        free(dummy);
    }
    return OK;
}

/* ===================== процессы ===================== */

status_code find_process(const scheduler *s, const int pid, size_t *index, process **result) {
    if (s == NULL || index == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t low = 0;
    size_t high = s->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (s->processes[middle]->pid < pid) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *index = low;
    *result = (low < s->count && s->processes[low]->pid == pid) ? s->processes[low] : NULL;
    return *result == NULL ? NO_SUCH_PROCESS : OK;
}

/* Текст числа без printf (в сборке mingw printf даёт ложные сообщения Dr. Memory - см. задание 4) */
status_code number_text(const long long value, char *out) {
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

status_code print_number(FILE *out, const long long value) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    char text[24];
    number_text(value, text);
    fputs(text, out);
    return OK;
}

/* Событие: "[tick N] process <pid> (<name>) <что случилось>" */
status_code event(scheduler *s, const process *p, const char *what) {
    if (s == NULL || p == NULL || what == NULL) {
        return INVALID_ARGS;
    }
    fputs("[tick ", s->events);
    print_number(s->events, s->tick);
    fputs("] process ", s->events);
    print_number(s->events, p->pid);
    fputs(" (", s->events);
    fputs(p->name, s->events);
    fputs(") ", s->events);
    fputs(what, s->events);
    fputc('\n', s->events);
    return OK;
}

/* Новый процесс (parent - NULL или родитель) */
status_code add_process(scheduler *s, const char *name, const int pid, const int priority, const long long time,
                        process *parent) {
    if (s == NULL || name == NULL || time <= 0) {
        return INVALID_ARGS;
    }
    size_t index = 0;
    process *existing = NULL;
    if (find_process(s, pid, &index, &existing) == OK) {
        return PID_EXISTS;
    }
    process *p = (process *)calloc(1, sizeof(process));
    if (p == NULL) {
        return ALLOC_ERROR;
    }
    status_code sc = trie_intern(s->names, name, &p->name);
    process **tmp = NULL;
    if (sc == OK) {
        tmp = (process **)realloc(s->processes, (s->count + 1) * sizeof(process *));
        if (tmp == NULL) {
            sc = ALLOC_ERROR;
        } else {
            s->processes = tmp;
        }
    }
    if (sc != OK) {
        free(p);
        return sc;
    }
    p->pid = pid;
    p->priority = priority;
    p->time = time;
    p->left = time;
    p->order = s->next_order++;
    p->state = READY;
    p->parent = parent;
    memmove(s->processes + index + 1, s->processes + index, (s->count - index) * sizeof(process *));
    s->processes[index] = p;
    s->count++;
    if (parent != NULL) {
        parent->children_active++;
    }
    sc = heap_insert(&s->ready, p);
    if (sc == OK) {
        sc = event(s, p, parent != NULL ? "added to the queue (child)" : "added to the queue");
    }
    return sc;
}

/*
 * Процесс отработал своё время: если дочерние ещё не завершены - ждёт их; иначе завершается, и это может
 * завершить родителя, который ждал только его (и так вверх по цепочке - циклом, без рекурсии).
 */
status_code work_done(scheduler *s, process *p) {
    if (s == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    if (p->children_active > 0) {
        p->state = CHILDREN_WAIT;
        return event(s, p, "has done its work and waits for its children");
    }
    process *current = p;
    while (current != NULL) {
        current->state = FINISHED;
        status_code sc = event(s, current, "finished");
        if (sc != OK) {
            return sc;
        }
        process *parent = current->parent;
        if (parent == NULL) {
            break;
        }
        parent->children_active--;
        current = (parent->state == CHILDREN_WAIT && parent->children_active == 0) ? parent : NULL;
    }
    return OK;
}

/* ===================== такт ===================== */

/*
 * Один такт:
 *   1) ожидавшие процессы, чьё время ожидания вышло, снова готовы;
 *   2) из кучи берутся slots самых важных готовых - они исполняются в этом такте;
 *   3) события: исполнялся раньше, а теперь нет - "stopped"; не исполнялся, а теперь да - "started";
 *   4) исполняемые получают по единице времени; закончившие - завершаются (или ждут детей),
 *      остальные возвращаются в кучу.
 */
status_code tick(scheduler *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = OK;
    s->tick++;   /* номер выполняемого такта: все события этого такта - с ним */
    /* исполнявшиеся на прошлом такте снова просто готовы: кто исполняется сейчас - решит выбор */
    for (size_t i = 0; i < s->count; i++) {
        if (s->processes[i]->state == RUNNING) {
            s->processes[i]->state = READY;
        }
    }
    /* 1) пробуждение (по возрастанию pid) */
    for (size_t i = 0; i < s->count && sc == OK; i++) {
        process *p = s->processes[i];
        if (p->state == WAITING && p->wake <= s->tick) {
            p->state = READY;
            p->version++;
            sc = heap_insert(&s->ready, p);
            if (sc == OK) {
                sc = event(s, p, "has finished waiting");
            }
        }
    }
    /* 2) выбор исполняемых */
    process **chosen = (process **)malloc(((size_t)s->slots + 1) * sizeof(process *));
    if (chosen == NULL) {
        return ALLOC_ERROR;
    }
    size_t taken = 0;
    while (sc == OK && taken < (size_t)s->slots) {
        process *p = NULL;
        sc = heap_extract_valid(&s->ready, &p);
        if (sc != OK || p == NULL) {
            break;
        }
        chosen[taken++] = p;
    }
    /* 3) события остановки и начала: отмечаем выбранных, потом проходим по всем по pid */
    for (size_t k = 0; k < taken; k++) {
        chosen[k]->state = RUNNING;
    }
    for (size_t i = 0; i < s->count && sc == OK; i++) {
        process *p = s->processes[i];
        if (p->was_running && p->state != RUNNING) {
            sc = event(s, p, "stopped (preempted)");
            p->was_running = 0;
        }
    }
    for (size_t i = 0; i < s->count && sc == OK; i++) {
        process *p = s->processes[i];
        if (p->state == RUNNING && !p->was_running) {
            sc = event(s, p, "started");
        }
    }
    /* 4) работа */
    for (size_t i = 0; i < s->count && sc == OK; i++) {
        process *p = s->processes[i];
        if (p->state != RUNNING) {
            continue;
        }
        p->left--;
        if (p->left == 0) {
            p->was_running = 0;
            sc = work_done(s, p);
        } else {
            p->was_running = 1;
            p->version++;
            sc = heap_insert(&s->ready, p);   /* вернуть в кучу - на следующем такте выбор заново */
        }
    }
    /* у исполняемых, оставшихся в куче, состояние RUNNING (так видно в !list); остальные готовые - READY */
    free(chosen);
    return sc;
}

status_code run_ticks(scheduler *s, const long long count) {
    if (s == NULL || count < 0) {
        return INVALID_ARGS;
    }
    for (long long i = 0; i < count; i++) {
        status_code sc = tick(s);
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

/* Есть ли незавершённые процессы */
status_code any_active(const scheduler *s, int *active) {
    if (s == NULL || active == NULL) {
        return INVALID_ARGS;
    }
    *active = 0;
    for (size_t i = 0; i < s->count; i++) {
        if (s->processes[i]->state != FINISHED) {
            *active = 1;
            break;
        }
    }
    return OK;
}

/* ===================== команды ===================== */

const char *state_name(const process_state state) {
    switch (state) {
        case READY:         return "ready";
        case RUNNING:       return "running";
        case WAITING:       return "waiting";
        case CHILDREN_WAIT: return "waiting for its children";
        default:            return "finished";
    }
}

/* Строка о процессе для !list и !all */
status_code print_process(FILE *out, const process *p) {
    if (out == NULL || p == NULL) {
        return INVALID_ARGS;
    }
    fputs("pid ", out);
    print_number(out, p->pid);
    fputs(" (", out);
    fputs(p->name, out);
    fputs("): priority ", out);
    print_number(out, p->priority);
    fputs(", time left ", out);
    print_number(out, p->left);
    fputs(" of ", out);
    print_number(out, p->time);
    fputs(", ", out);
    fputs(state_name(p->state), out);
    if (p->state == WAITING) {
        fputs(" until tick ", out);
        print_number(out, p->wake);
    }
    if (p->parent != NULL) {
        fputs(", parent ", out);
        print_number(out, p->parent->pid);
    }
    fputc('\n', out);
    return OK;
}

status_code print_processes(FILE *out, const scheduler *s, const int only_active) {
    if (out == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < s->count; i++) {
        if (!only_active || s->processes[i]->state != FINISHED) {
            print_process(out, s->processes[i]);
        }
    }
    return OK;
}

/* Строка -> целое (необязательный минус и цифры) в пределах int */
status_code parse_int(const char *str, int *value) {
    if (str == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    size_t i = str[0] == '-' ? 1 : 0;
    if (str[i] == '\0') {
        return SYNTAX_ERROR;
    }
    long long result = 0;
    for (; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return SYNTAX_ERROR;
        }
        result = result * 10 + (str[i] - '0');
        if (result > INT_MAX) {
            return SYNTAX_ERROR;
        }
    }
    *value = str[0] == '-' ? (int)-result : (int)result;
    return OK;
}

/* Выполнить команду (слова уже разделены) */
status_code execute(scheduler *s, char **w, const size_t n) {
    if (s == NULL || w == NULL || n == 0) {
        return INVALID_ARGS;
    }
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    if (strcmp(w[0], "!np") == 0 && n == 5) {
        if (parse_int(w[2], &a) != OK || parse_int(w[3], &b) != OK || parse_int(w[4], &c) != OK || a < 0 || c <= 0) {
            return SYNTAX_ERROR;
        }
        return add_process(s, w[1], a, b, c, NULL);
    }
    if (strcmp(w[0], "!pp") == 0 && n == 6) {
        if (parse_int(w[1], &d) != OK || parse_int(w[3], &a) != OK || parse_int(w[4], &b) != OK ||
            parse_int(w[5], &c) != OK || a < 0 || c <= 0) {
            return SYNTAX_ERROR;
        }
        size_t index = 0;
        process *parent = NULL;
        status_code sc = find_process(s, d, &index, &parent);
        if (sc != OK) {
            return sc;
        }
        if (parent->state == FINISHED) {
            return PARENT_FINISHED;
        }
        return add_process(s, w[2], a, b, c, parent);
    }
    if (strcmp(w[0], "!wait") == 0 && n == 3) {
        if (parse_int(w[1], &a) != OK || parse_int(w[2], &b) != OK || b <= 0) {
            return SYNTAX_ERROR;
        }
        size_t index = 0;
        process *p = NULL;
        status_code sc = find_process(s, a, &index, &p);
        if (sc != OK) {
            return sc;
        }
        if (p->state != RUNNING) {
            return NOT_RUNNING;
        }
        p->state = WAITING;
        p->was_running = 0;
        p->version++;             /* запись в куче больше недействительна */
        p->wake = s->tick + b + 1;   /* пропускает ровно b тактов, на такте wake снова готов */
        return event(s, p, "stopped (waiting)");
    }
    if (strcmp(w[0], "!list") == 0 && n == 1) {
        return print_processes(stdout, s, 1);
    }
    if (strcmp(w[0], "!all") == 0 && n == 1) {
        return print_processes(stdout, s, 0);
    }
    if (strcmp(w[0], "!exec") == 0 && n == 2) {
        if (parse_int(w[1], &a) != OK || a <= 0 || a > 1000) {
            return SYNTAX_ERROR;
        }
        s->slots = a;
        return OK;
    }
    if (strcmp(w[0], "!edit") == 0 && n == 3) {
        if (parse_int(w[1], &a) != OK || parse_int(w[2], &b) != OK) {
            return SYNTAX_ERROR;
        }
        size_t index = 0;
        process *p = NULL;
        status_code sc = find_process(s, a, &index, &p);
        if (sc != OK) {
            return sc;
        }
        p->priority = b;
        if (p->state == READY || p->state == RUNNING) {
            p->version++;
            sc = heap_insert(&s->ready, p);   /* старая запись станет недействительной */
        }
        return sc;
    }
    if (strcmp(w[0], "!step") == 0 && n == 2) {
        if (parse_int(w[1], &a) != OK || a < 0 || a > MAX_STEP) {
            return SYNTAX_ERROR;
        }
        return run_ticks(s, a);
    }
    return SYNTAX_ERROR;
}

status_code error_text(const status_code sc, const char **text) {
    if (text == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case SYNTAX_ERROR:    *text = "wrong command"; break;
        case PID_EXISTS:      *text = "a process with this pid already exists"; break;
        case NO_SUCH_PROCESS: *text = "there is no process with this pid"; break;
        case NOT_RUNNING:     *text = "the process is not running now"; break;
        case PARENT_FINISHED: *text = "the parent process has already finished"; break;
        default:              *text = "internal error"; break;   /* другие статусы сюда не попадают */
    }
    return OK;
}

/* ===================== время и ввод ===================== */

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

/* Догнать такты по прошедшему настоящему времени */
status_code catch_up(scheduler *s, long long *last) {
    if (s == NULL || last == NULL) {
        return INVALID_ARGS;
    }
    long long now = 0;
    now_ms(&now);
    long long ticks = (now - *last) / TICK_MS;
    *last += ticks * TICK_MS;
    return run_ticks(s, ticks > MAX_STEP ? MAX_STEP : ticks);
}

/* Все команды, потом - работа до завершения всех процессов */
status_code run(scheduler *s, FILE *input, const int fast) {
    if (s == NULL || input == NULL) {
        return INVALID_ARGS;
    }
    long long last = 0;
    now_ms(&last);
    status_code sc = OK;
    size_t number = 0;
    while (sc == OK) {
        char *line = NULL;
        int end = 0;
        sc = read_line(input, &line, &end);
        if (sc != OK || end) {
            free(line);
            break;
        }
        number++;
        if (!fast) {
            sc = catch_up(s, &last);
        }
        char *words[8];
        size_t n = 0;
        status_code result = sc == OK ? split_words(line, words, 8, &n) : sc;
        if (result == OK && n > 0) {
            result = execute(s, words, n);
        }
        if (result == ALLOC_ERROR) {
            sc = result;
        } else if (result != OK) {
            const char *text = NULL;
            error_text(result, &text);
            fprintf(stderr, "Error in command %zu: %s\n", number, text);
        }
        free(line);
        fflush(stdout);
        fflush(s->events);
    }
    /* ввод кончился - работаем, пока есть незавершённые процессы */
    int active = 1;
    for (long long k = 0; sc == OK && k < MAX_RUN_TICKS; k++) {
        any_active(s, &active);
        if (!active) {
            break;
        }
        if (!fast) {
            sleep_ms(TICK_MS);
        }
        sc = tick(s);
    }
    return sc;
}

status_code scheduler_free(scheduler *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    heap_free(&s->ready);
    for (size_t i = 0; i < s->count; i++) {
        free(s->processes[i]);
    }
    free(s->processes);
    trie_free(s->names);
    return OK;
}

int main(int argc, char *argv[]) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    int fast = 0;
    int wrong = 0;
    for (int i = 1; i < argc && !wrong; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc && input_path == NULL) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc && output_path == NULL) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--fast") == 0 && !fast) {
            fast = 1;
        } else {
            wrong = 1;
        }
    }
    if (wrong) {
        fprintf(stderr, "Usage: lab4_task8 [--input <file>] [--output <file>] [--fast]\n");
        return 1;
    }
    FILE *input = stdin;
    if (input_path != NULL) {
        input = fopen(input_path, "r");
        if (input == NULL) {
            fprintf(stderr, "Error: can not read file '%s'\n", input_path);
            return 1;
        }
    }
    scheduler s;
    memset(&s, 0, sizeof(s));
    s.slots = 1;
    s.events = stdout;
    s.names = (trie_node *)calloc(1, sizeof(trie_node));
    if (s.names == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        if (input != stdin) {
            fclose(input);
        }
        return 1;
    }
    if (output_path != NULL) {
        s.events = fopen(output_path, "w");
        if (s.events == NULL) {
            fprintf(stderr, "Error: can not open file '%s'\n", output_path);
            trie_free(s.names);
            if (input != stdin) {
                fclose(input);
            }
            return 1;
        }
    }
    status_code sc = run(&s, input, fast);
    if (input != stdin) {
        fclose(input);
    }
    if (s.events != stdout && fclose(s.events) != 0 && sc == OK) {
        sc = FILE_ERROR;
    }
    scheduler_free(&s);
    if (sc != OK) {
        fprintf(stderr, sc == FILE_ERROR ? "Error: can not write the events file\n" : "Error: not enough memory\n");
        return 1;
    }
    return 0;
}
