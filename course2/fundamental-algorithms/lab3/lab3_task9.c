/*
 * Лабораторная 3, задание 9. Статистика слов в тексте: двоичное дерево поиска,
 * его глубина, сохранение дерева в файл и восстановление той же формы.
 *
 * Запуск:  lab3_task9 <файл с текстом> <сепаратор> [<сепаратор> ...]
 * Каждый сепаратор - один символ; можно записать \t (табуляция), \s (пробел), \\ (обратная черта).
 * Перевод строки всегда разделяет слова (файл текстовый, слово не переходит на новую строку).
 * Слово - непустая последовательность символов между сепараторами; слова сравниваются побайтно
 * (с учетом регистра). Для каждого слова дерево хранит, сколько раз оно встретилось.
 *
 * Потом диалог - команды со стандартного ввода:
 *     count <слово>      сколько раз слово встретилось
 *     top <n>            n самых частых слов (при равных - по алфавиту)
 *     longest, shortest  самое длинное / самое короткое слово
 *     depth              глубина дерева (число уровней; пустое - 0)
 *     save <файл>        сохранить дерево; load <файл> - восстановить дерево той же формы
 *     tree               форма дерева (корень, левые L и правые R поддеревья с отступами)
 *     list               все слова по алфавиту
 *     help, exit
 *
 * Все обходы дерева - без рекурсии (свой стек или очередь в динамической памяти): если слова
 * в тексте идут по алфавиту, дерево вырождается в цепочку, и рекурсия глубиной в десятки
 * тысяч вызовов могла бы переполнить стек программы.
 */
#define _POSIX_C_SOURCE 200809L   /* stat, isatty, fileno - функции POSIX */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort */
#include <string.h>   /* strcmp, strlen, memcpy */
#include <limits.h>   /* LLONG_MAX */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace */
#ifdef _WIN32
#include <io.h>         /* _isatty, _fileno */
#else
#include <sys/stat.h>   /* stat - для same_file */
#include <unistd.h>     /* isatty, fileno */
#endif

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    BAD_SEPARATOR,     /* аргумент-сепаратор не один символ и не \t, \s, \\ */
    NO_WORDS,          /* в дереве нет слов */
    FILE_ERROR,        /* ошибка чтения или записи файла */
    BAD_FORMAT,        /* файл дерева испорчен */
    NOT_SEARCH_TREE,   /* в файле дерево, но не дерево поиска */
    BAD_PATH,          /* путь неправильный (Windows, same_file) */
    END_OF_INPUT,      /* ввод кончился */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

typedef struct tree_node {
    char *word;
    size_t length;
    long long count;
    struct tree_node *left;    /* слова меньше */
    struct tree_node *right;   /* слова больше */
} tree_node;

/* Стек указателей в динамической памяти (для обходов без рекурсии) */
typedef struct {
    void **items;
    size_t count;
    size_t capacity;
} pointer_stack;

/* ===================== стек ===================== */

status_code stack_push(pointer_stack *s, void *item) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    if (s->count == s->capacity) {
        size_t new_capacity = s->capacity == 0 ? 16 : s->capacity * 2;
        if (new_capacity > SIZE_MAX / sizeof(void *)) {
            return ALLOC_ERROR;
        }
        void **tmp = (void **)realloc(s->items, new_capacity * sizeof(void *));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        s->items = tmp;
        s->capacity = new_capacity;
    }
    s->items[s->count] = item;
    s->count++;
    return OK;
}

status_code stack_pop(pointer_stack *s, void **item) {
    if (s == NULL || item == NULL || s->count == 0) {
        return INVALID_ARGS;
    }
    s->count--;
    *item = s->items[s->count];
    return OK;
}

status_code stack_free(pointer_stack *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    free(s->items);
    s->items = NULL;
    s->count = 0;
    s->capacity = 0;
    return OK;
}

/* ===================== дерево: вставка, поиск, освобождение ===================== */

/* Освободить дерево: узлы по очереди со стеком (без рекурсии) */
status_code tree_free(tree_node **root) {
    if (root == NULL) {
        return INVALID_ARGS;
    }
    /* "расплетаем": если у корня есть левый ребенок, поворачиваем дерево вправо,
       иначе удаляем корень и переходим к правому - без дополнительной памяти */
    tree_node *node = *root;
    while (node != NULL) {
        if (node->left != NULL) {
            tree_node *left = node->left;
            node->left = left->right;
            left->right = node;
            node = left;
        } else {
            tree_node *right = node->right;
            free(node->word);
            free(node);
            node = right;
        }
    }
    *root = NULL;
    return OK;
}

/* Новый узел со словом (копия length байт) */
status_code node_create(const char *word, const size_t length, const long long count, tree_node **node) {
    if (word == NULL || node == NULL) {
        return INVALID_ARGS;
    }
    tree_node *n = (tree_node *)malloc(sizeof(tree_node));
    if (n == NULL) {
        return ALLOC_ERROR;
    }
    n->word = (char *)malloc(length + 1);
    if (n->word == NULL) {
        free(n);
        return ALLOC_ERROR;
    }
    memcpy(n->word, word, length);
    n->word[length] = '\0';
    n->length = length;
    n->count = count;
    n->left = NULL;
    n->right = NULL;
    *node = n;
    return OK;
}

/* Добавить одно вхождение слова: есть в дереве - счетчик + 1, нет - новый лист на своем месте */
status_code tree_add_word(tree_node **root, const char *word, const size_t length) {
    if (root == NULL || word == NULL) {
        return INVALID_ARGS;
    }
    tree_node **link = root;   /* поле, куда повесить новый узел */
    while (*link != NULL) {
        int order = strcmp(word, (*link)->word);
        if (order == 0) {
            if ((*link)->count < LLONG_MAX) {
                (*link)->count++;
            }
            return OK;
        }
        link = order < 0 ? &(*link)->left : &(*link)->right;
    }
    return node_create(word, length, 1, link);
}

/* Сколько раз встретилось слово (0 - ни разу) */
status_code tree_count(const tree_node *root, const char *word, long long *count) {
    if (word == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    *count = 0;
    while (root != NULL) {
        int order = strcmp(word, root->word);
        if (order == 0) {
            *count = root->count;
            break;
        }
        root = order < 0 ? root->left : root->right;
    }
    return OK;
}

/*
 * Все узлы по алфавиту (обход "левое поддерево - узел - правое" со стеком).
 * Результат - массив указателей (освобождает вызывающий).
 */
status_code tree_in_order(const tree_node *root, const tree_node ***nodes, size_t *count) {
    if (nodes == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    *nodes = NULL;
    *count = 0;
    pointer_stack stack = {NULL, 0, 0};
    pointer_stack result = {NULL, 0, 0};
    const tree_node *current = root;
    status_code sc = OK;
    while (sc == OK && (current != NULL || stack.count > 0)) {
        /* спускаемся влево до конца, запоминая путь */
        while (sc == OK && current != NULL) {
            sc = stack_push(&stack, (void *)current);
            current = current->left;
        }
        if (sc != OK) {
            break;
        }
        void *top = NULL;
        stack_pop(&stack, &top);
        const tree_node *node = (const tree_node *)top;
        sc = stack_push(&result, (void *)node);
        current = node->right;
    }
    stack_free(&stack);
    if (sc != OK) {
        stack_free(&result);
        return sc;
    }
    *nodes = (const tree_node **)result.items;
    *count = result.count;
    return OK;
}

/* Глубина - число уровней: обход по уровням (очередь); пустое дерево - 0 */
status_code tree_depth(const tree_node *root, size_t *depth) {
    if (depth == NULL) {
        return INVALID_ARGS;
    }
    *depth = 0;
    if (root == NULL) {
        return OK;
    }
    /* очередь - массив; level - узлы текущего уровня, next - следующего */
    pointer_stack level = {NULL, 0, 0};
    pointer_stack next = {NULL, 0, 0};
    status_code sc = stack_push(&level, (void *)root);
    while (sc == OK && level.count > 0) {
        (*depth)++;
        next.count = 0;
        for (size_t i = 0; i < level.count && sc == OK; i++) {
            const tree_node *n = (const tree_node *)level.items[i];
            if (n->left != NULL) {
                sc = stack_push(&next, (void *)n->left);
            }
            if (sc == OK && n->right != NULL) {
                sc = stack_push(&next, (void *)n->right);
            }
        }
        /* следующий уровень становится текущим (массивы меняются местами) */
        pointer_stack tmp = level;
        level = next;
        next = tmp;
    }
    stack_free(&level);
    stack_free(&next);
    return sc;
}

/* Самое длинное и самое короткое слово; при равной длине - первое по алфавиту */
status_code tree_longest_shortest(const tree_node *root, const tree_node **longest, const tree_node **shortest) {
    if (longest == NULL || shortest == NULL) {
        return INVALID_ARGS;
    }
    const tree_node **nodes = NULL;
    size_t count = 0;
    status_code sc = tree_in_order(root, &nodes, &count);
    if (sc != OK) {
        return sc;
    }
    if (count == 0) {
        free(nodes);
        return NO_WORDS;
    }
    *longest = nodes[0];
    *shortest = nodes[0];
    for (size_t i = 1; i < count; i++) {
        if (nodes[i]->length > (*longest)->length) {
            *longest = nodes[i];
        }
        if (nodes[i]->length < (*shortest)->length) {
            *shortest = nodes[i];
        }
    }
    free(nodes);
    return OK;
}

/* Компаратор для top: больше вхождений - раньше; при равенстве - по алфавиту */
int compare_by_count(const void *a, const void *b) {
    const tree_node *x = *(const tree_node *const *)a;
    const tree_node *y = *(const tree_node *const *)b;
    if (x->count != y->count) {
        return x->count > y->count ? -1 : 1;
    }
    return strcmp(x->word, y->word);
}

/* n самых частых: массив указателей (освобождает вызывающий), *count - сколько в нем (<= n) */
status_code tree_top(const tree_node *root, const size_t n, const tree_node ***top, size_t *count) {
    if (top == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    const tree_node **nodes = NULL;
    size_t total = 0;
    status_code sc = tree_in_order(root, &nodes, &total);
    if (sc != OK) {
        return sc;
    }
    if (total > 1) {
        qsort(nodes, total, sizeof(const tree_node *), compare_by_count);
    }
    *top = nodes;
    *count = total < n ? total : n;
    return OK;
}

/* ===================== сохранение и восстановление ===================== */

/*
 * Сохранить дерево: прямой обход (узел, левое, правое) со стеком; пустое место - строка "-",
 * узел - строка "+ <count> <length> <слово>". Длина записана явно, поэтому в слове может быть
 * любой символ (кроме перевода строки - он всегда сепаратор). По такой записи форма восстанавливается точно.
 */
status_code tree_save(FILE *out, const tree_node *root, size_t *nodes) {
    if (out == NULL || nodes == NULL) {
        return INVALID_ARGS;
    }
    *nodes = 0;
    pointer_stack stack = {NULL, 0, 0};
    /* в стек кладем и пустые места (NULL) - их тоже надо записать */
    status_code sc = stack_push(&stack, (void *)root);
    while (sc == OK && stack.count > 0) {
        void *top = NULL;
        stack_pop(&stack, &top);
        const tree_node *n = (const tree_node *)top;
        if (n == NULL) {
            fprintf(out, "-\n");
            continue;
        }
        fprintf(out, "+ %lld %zu ", n->count, n->length);
        fwrite(n->word, 1, n->length, out);
        fprintf(out, "\n");
        (*nodes)++;
        /* правое кладем первым, чтобы левое вышло раньше (стек - последний пришел, первый ушел) */
        sc = stack_push(&stack, (void *)n->right);
        if (sc == OK) {
            sc = stack_push(&stack, (void *)n->left);
        }
    }
    stack_free(&stack);
    if (sc == OK && ferror(out)) {
        sc = FILE_ERROR;
    }
    return sc;
}

/* Прочитать число из файла дерева (цифры до пробела) */
status_code read_number(FILE *in, unsigned long long *value) {
    if (in == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long v = 0;
    int digits = 0;
    int c = 0;
    while ((c = fgetc(in)) >= '0' && c <= '9') {
        if (v > (ULLONG_MAX - (unsigned)(c - '0')) / 10) {
            return BAD_FORMAT;
        }
        v = v * 10 + (unsigned)(c - '0');
        digits++;
    }
    if (digits == 0 || c != ' ') {
        return BAD_FORMAT;
    }
    *value = v;
    return OK;
}

/*
 * Восстановить дерево. Стек хранит "места" (указатели на поля left/right), которые надо заполнить:
 * вначале одно место - корень. Строка "-" - место пустое; узел - создаем, вешаем на место,
 * кладем в стек его правое и левое места (левое заполнится первым, как при сохранении).
 */
status_code tree_load(FILE *in, tree_node **root) {
    if (in == NULL || root == NULL) {
        return INVALID_ARGS;
    }
    *root = NULL;
    pointer_stack slots = {NULL, 0, 0};
    status_code sc = stack_push(&slots, (void *)root);
    while (sc == OK && slots.count > 0) {
        void *top = NULL;
        stack_pop(&slots, &top);
        tree_node **slot = (tree_node **)top;
        int c = fgetc(in);
        if (c == '-') {
            sc = fgetc(in) == '\n' ? OK : BAD_FORMAT;
            *slot = NULL;
            continue;
        }
        if (c != '+' || fgetc(in) != ' ') {
            sc = BAD_FORMAT;
            break;
        }
        unsigned long long count = 0;
        unsigned long long length = 0;
        sc = read_number(in, &count);
        if (sc == OK) {
            sc = read_number(in, &length);
        }
        if (sc == OK && (count == 0 || count > (unsigned long long)LLONG_MAX || length == 0 || length > SIZE_MAX - 1)) {
            sc = BAD_FORMAT;
        }
        char *word = NULL;
        if (sc == OK) {
            word = (char *)malloc((size_t)length + 1);
            if (word == NULL) {
                sc = ALLOC_ERROR;
            }
        }
        if (sc == OK && (fread(word, 1, (size_t)length, in) != (size_t)length || fgetc(in) != '\n')) {
            sc = BAD_FORMAT;
        }
        if (sc == OK) {
            sc = node_create(word, (size_t)length, (long long)count, slot);
        }
        free(word);
        if (sc == OK) {
            sc = stack_push(&slots, (void *)&(*slot)->right);
        }
        if (sc == OK) {
            sc = stack_push(&slots, (void *)&(*slot)->left);
        }
    }
    stack_free(&slots);
    /* после дерева в файле ничего не должно быть */
    if (sc == OK && fgetc(in) != EOF) {
        sc = BAD_FORMAT;
    }
    /* это должно быть дерево поиска: по алфавиту слова строго возрастают */
    if (sc == OK) {
        const tree_node **nodes = NULL;
        size_t count = 0;
        sc = tree_in_order(*root, &nodes, &count);
        for (size_t i = 1; sc == OK && i < count; i++) {
            if (strcmp(nodes[i - 1]->word, nodes[i]->word) >= 0) {
                sc = NOT_SEARCH_TREE;
            }
        }
        free(nodes);
    }
    if (sc != OK) {
        tree_free(root);
    }
    return sc;
}

/* Печать формы дерева: прямой обход, отступ 2 пробела на уровень, L/R - левый/правый ребенок */
status_code tree_print_shape(FILE *out, const tree_node *root) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    if (root == NULL) {
        fprintf(out, "(empty tree)\n");
        return OK;
    }
    /* в стеке - узел, его глубина и метка; храним их в небольшой структуре в куче */
    typedef struct {
        const tree_node *node;
        size_t depth;
        char side;
    } item;
    pointer_stack stack = {NULL, 0, 0};
    item *first = (item *)malloc(sizeof(item));
    if (first == NULL) {
        return ALLOC_ERROR;
    }
    first->node = root;
    first->depth = 0;
    first->side = ' ';
    status_code sc = stack_push(&stack, first);
    if (sc != OK) {
        free(first);
    }
    while (sc == OK && stack.count > 0) {
        void *top = NULL;
        stack_pop(&stack, &top);
        item *it = (item *)top;
        for (size_t i = 0; i < it->depth; i++) {
            fprintf(out, "  ");
        }
        if (it->depth > 0) {
            fprintf(out, "%c: ", it->side);
        }
        fprintf(out, "'%s' %lld\n", it->node->word, it->node->count);
        /* дети: правый в стек первым - левый напечатается раньше */
        const tree_node *children[2] = {it->node->right, it->node->left};
        const char sides[2] = {'R', 'L'};
        for (int k = 0; k < 2 && sc == OK; k++) {
            if (children[k] == NULL) {
                continue;
            }
            item *child = (item *)malloc(sizeof(item));
            if (child == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            child->node = children[k];
            child->depth = it->depth + 1;
            child->side = sides[k];
            sc = stack_push(&stack, child);
            if (sc != OK) {
                free(child);
            }
        }
        free(it);
    }
    /* при ошибке в стеке могли остаться элементы */
    for (size_t i = 0; i < stack.count; i++) {
        free(stack.items[i]);
    }
    stack_free(&stack);
    return sc;
}

/* ===================== текст ===================== */

/* Аргумент -> символ-сепаратор: один символ или \t, \s, \\ */
status_code parse_separator(const char *arg, unsigned char *separator) {
    if (arg == NULL || separator == NULL) {
        return INVALID_ARGS;
    }
    if (arg[0] != '\0' && arg[1] == '\0') {
        *separator = (unsigned char)arg[0];
        return OK;
    }
    if (arg[0] == '\\' && arg[1] != '\0' && arg[2] == '\0') {
        if (arg[1] == 't') {
            *separator = '\t';
            return OK;
        }
        if (arg[1] == 's') {
            *separator = ' ';
            return OK;
        }
        if (arg[1] == '\\') {
            *separator = '\\';
            return OK;
        }
    }
    return BAD_SEPARATOR;
}

/* Прочитать текст и собрать дерево. is_separator[c] = 1 - символ c разделяет слова */
status_code build_tree(FILE *in, const unsigned char *is_separator, tree_node **root) {
    if (in == NULL || is_separator == NULL || root == NULL) {
        return INVALID_ARGS;
    }
    *root = NULL;
    size_t size = 16;
    size_t length = 0;
    char *word = (char *)malloc(size);
    if (word == NULL) {
        return ALLOC_ERROR;
    }
    status_code sc = OK;
    int c = 0;
    while (sc == OK) {
        c = fgetc(in);
        int ends_word = c == EOF || c == '\n' || c == '\r' || is_separator[(unsigned char)c];
        if (ends_word) {
            if (length > 0) {
                word[length] = '\0';
                sc = tree_add_word(root, word, length);
                length = 0;
            }
            if (c == EOF) {
                break;
            }
            continue;
        }
        if (length + 1 >= size) {
            if (size > SIZE_MAX / 2) {
                sc = ALLOC_ERROR;
                break;
            }
            char *tmp = (char *)realloc(word, size * 2);
            if (tmp == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            word = tmp;
            size *= 2;
        }
        word[length] = (char)c;
        length++;
    }
    free(word);
    if (sc == OK && ferror(in)) {
        sc = FILE_ERROR;
    }
    if (sc != OK) {
        tree_free(root);
    }
    return sc;
}

/* ===================== файлы ===================== */

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
    return BAD_PATH;
}
#endif

/* Ведут ли два пути к одному файлу (сравниваются файлы, а не строки путей) */
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
    if (stat(path1, &info1) == 0 && stat(path2, &info2) == 0 &&
        info1.st_dev == info2.st_dev && info1.st_ino == info2.st_ino) {
        *equal = 1;
    }
#endif
    return OK;
}

/* ===================== диалог ===================== */

status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t buf_size = 16;
    size_t length = 0;
    char *buf = (char *)malloc(buf_size);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    int c = fgetc(in);
    if (c == EOF) {
        free(buf);
        return END_OF_INPUT;
    }
    while (c != EOF && c != '\n') {
        if (length + 1 >= buf_size) {
            if (buf_size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            char *new_ptr = (char *)realloc(buf, buf_size * 2);
            if (new_ptr == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = new_ptr;
            buf_size *= 2;
        }
        buf[length] = (char)c;
        length++;
        c = fgetc(in);
    }
    /* '\r' в конце (ввод из файла Windows) - не часть команды */
    if (length > 0 && buf[length - 1] == '\r') {
        length--;
    }
    buf[length] = '\0';
    *line = buf;
    return OK;
}

/* Команда - первое слово до пробела, аргумент - все после одного пробела (слово может содержать
   пробелы, если пробел не сепаратор) */
status_code split_command(char *line, char **command, char **argument) {
    if (line == NULL || command == NULL || argument == NULL) {
        return INVALID_ARGS;
    }
    *command = line;
    *argument = NULL;
    size_t i = 0;
    while (line[i] != '\0' && line[i] != ' ') {
        i++;
    }
    if (line[i] == ' ') {
        line[i] = '\0';
        *argument = line + i + 1;
    }
    return OK;
}

status_code print_help(FILE *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Commands: count <word>, top <n>, longest, shortest, depth, save <file>, load <file>,\n");
    fprintf(out, "          tree, list, help, exit\n");
    return OK;
}

/* Выполнить команду. text_path - файл с текстом (save не должен его затереть) */
status_code run_command(tree_node **root, const char *text_path, char *line, int *finish) {
    if (root == NULL || text_path == NULL || line == NULL || finish == NULL) {
        return INVALID_ARGS;
    }
    *finish = 0;
    char *command = NULL;
    char *argument = NULL;
    split_command(line, &command, &argument);
    int has_argument = argument != NULL && argument[0] != '\0';
    if (command[0] == '\0' && !has_argument) {
        return OK;   /* пустая строка */
    }
    int needs_argument = strcmp(command, "count") == 0 || strcmp(command, "top") == 0
                         || strcmp(command, "save") == 0 || strcmp(command, "load") == 0;
    int no_argument = strcmp(command, "longest") == 0 || strcmp(command, "shortest") == 0
                      || strcmp(command, "depth") == 0 || strcmp(command, "tree") == 0 || strcmp(command, "list") == 0
                      || strcmp(command, "help") == 0 || strcmp(command, "exit") == 0;
    if (!needs_argument && !no_argument) {
        fprintf(stderr, "Error: unknown command '%s' (type help)\n", command);
        return OK;
    }
    if (needs_argument && !has_argument) {
        fprintf(stderr, "Error: command '%s' needs an argument\n", command);
        return OK;
    }
    if (no_argument && argument != NULL) {
        fprintf(stderr, "Error: command '%s' takes no argument\n", command);
        return OK;
    }

    if (strcmp(command, "exit") == 0) {
        *finish = 1;
        return OK;
    }
    if (strcmp(command, "help") == 0) {
        return print_help(stdout);
    }
    if (strcmp(command, "count") == 0) {
        long long count = 0;
        tree_count(*root, argument, &count);
        printf("'%s': %lld\n", argument, count);
        return OK;
    }
    if (strcmp(command, "depth") == 0) {
        size_t depth = 0;
        status_code sc = tree_depth(*root, &depth);
        if (sc == OK) {
            printf("Depth: %zu\n", depth);
        }
        return sc;
    }
    if (strcmp(command, "tree") == 0) {
        return tree_print_shape(stdout, *root);
    }
    if (strcmp(command, "longest") == 0 || strcmp(command, "shortest") == 0) {
        const tree_node *longest = NULL;
        const tree_node *shortest = NULL;
        status_code sc = tree_longest_shortest(*root, &longest, &shortest);
        if (sc == NO_WORDS) {
            fprintf(stderr, "Error: there are no words\n");
            return OK;
        }
        if (sc != OK) {
            return sc;
        }
        /* функция только нашла слова - печатает вызывающий код */
        const tree_node *n = strcmp(command, "longest") == 0 ? longest : shortest;
        printf("%s: '%s' (%zu chars)\n", strcmp(command, "longest") == 0 ? "Longest" : "Shortest", n->word, n->length);
        return OK;
    }
    if (strcmp(command, "list") == 0 || strcmp(command, "top") == 0) {
        int is_top = strcmp(command, "top") == 0;
        size_t n = 0;
        if (is_top) {
            /* n - целое >= 1 */
            for (size_t i = 0; argument[i] != '\0'; i++) {
                if (argument[i] < '0' || argument[i] > '9' || n > (SIZE_MAX - 9) / 10) {
                    n = 0;
                    break;
                }
                n = n * 10 + (size_t)(argument[i] - '0');
            }
            if (n == 0) {
                fprintf(stderr, "Error: n must be a whole number 1 or more\n");
                return OK;
            }
        }
        const tree_node **nodes = NULL;
        size_t count = 0;
        status_code sc = is_top ? tree_top(*root, n, &nodes, &count) : tree_in_order(*root, &nodes, &count);
        if (sc != OK) {
            return sc;
        }
        printf(is_top ? "Top %zu:\n" : "Words: %zu\n", count);
        for (size_t i = 0; i < count; i++) {
            if (is_top) {
                printf("%zu. '%s' %lld\n", i + 1, nodes[i]->word, nodes[i]->count);
            } else {
                printf("'%s' %lld\n", nodes[i]->word, nodes[i]->count);
            }
        }
        free(nodes);
        return OK;
    }
    if (strcmp(command, "save") == 0) {
        int equal = 0;
        status_code sc = same_file(argument, text_path, &equal);
        if (sc == ALLOC_ERROR) {
            return sc;
        }
        if (equal) {
            fprintf(stderr, "Error: can not save the tree over the text file\n");
            return OK;
        }
        /* "wb": на Windows иначе '\n' записались бы как "\r\n" и длины слов не совпали бы */
        FILE *out = fopen(argument, "wb");
        if (out == NULL) {
            fprintf(stderr, "Error: can not create file '%s'\n", argument);
            return OK;
        }
        size_t nodes = 0;
        sc = tree_save(out, *root, &nodes);
        if (fclose(out) != 0 && sc == OK) {
            sc = FILE_ERROR;
        }
        if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not write file '%s'\n", argument);
            return OK;
        }
        if (sc == OK) {
            printf("Saved %zu nodes to '%s'\n", nodes, argument);
        }
        return sc;
    }
    /* load: при ошибке старое дерево остается */
    FILE *in = fopen(argument, "rb");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argument);
        return OK;
    }
    tree_node *loaded = NULL;
    status_code sc = tree_load(in, &loaded);
    fclose(in);
    if (sc == BAD_FORMAT || sc == NOT_SEARCH_TREE) {
        fprintf(stderr, sc == BAD_FORMAT ? "Error: file '%s' is not a saved tree\n"
                                         : "Error: the tree in file '%s' is not a search tree\n", argument);
        return OK;
    }
    if (sc != OK) {
        return sc;
    }
    tree_free(root);
    *root = loaded;
    const tree_node **nodes = NULL;
    size_t count = 0;
    size_t depth = 0;
    sc = tree_in_order(*root, &nodes, &count);
    free(nodes);
    if (sc == OK) {
        sc = tree_depth(*root, &depth);
    }
    if (sc == OK) {
        printf("Loaded %zu words, depth %zu\n", count, depth);
    }
    return sc;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: lab3_task9 <text file> <separator> [<separator> ...]   (\\t - tab, \\s - space)\n");
        return 1;
    }
    /* таблица сепараторов: is_separator[код символа] = 1 */
    unsigned char *is_separator = (unsigned char *)calloc(256, 1);
    if (is_separator == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        unsigned char c = 0;
        if (parse_separator(argv[i], &c) != OK) {
            fprintf(stderr, "Error: separator '%s' must be one character (or \\t, \\s, \\\\)\n", argv[i]);
            free(is_separator);
            return 1;
        }
        is_separator[c] = 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        free(is_separator);
        return 1;
    }
    tree_node *root = NULL;
    status_code sc = build_tree(in, is_separator, &root);
    fclose(in);
    free(is_separator);
    if (sc != OK) {
        fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: can not read the text file\n");
        return 1;
    }

#ifdef _WIN32
    int interactive = _isatty(_fileno(stdin));
#else
    int interactive = isatty(fileno(stdin));
#endif
    if (interactive) {
        print_help(stdout);
    }
    int result = 0;
    int finish = 0;
    while (!finish) {
        if (interactive) {
            printf("> ");
            fflush(stdout);
        }
        char *line = NULL;
        sc = read_line(stdin, &line);
        if (sc == END_OF_INPUT) {
            break;
        }
        if (sc == OK) {
            sc = run_command(&root, argv[1], line, &finish);
            free(line);
        }
        if (sc != OK) {
            fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: internal error\n");
            result = 1;
            break;
        }
    }
    tree_free(&root);
    return result;
}
