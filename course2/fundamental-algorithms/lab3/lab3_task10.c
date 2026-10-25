/*
 * Лабораторная 3, задание 10. Деревья скобочных выражений.
 *
 * Запуск:  lab3_task10 <входной файл> <выходной файл>
 * Каждая непустая строка входного файла - скобочное выражение, например
 *     A (B (E (G, T, R (W, Z)), F (L, M)), C)
 * узел := имя [ ( узел , узел , ... ) ]; имя - любые символы, кроме пробелов, скобок и запятой.
 * Пробелы между частями допускаются. Дерево не обязательно двоичное: детей сколько угодно.
 * В выходной файл для каждого выражения пишется дерево:
 *     Expression 1: A (B (E (G, T, R (W, Z)), F (L, M)), C)
 *     A
 *     +-- B
 *     |   +-- E
 *     |   |   +-- G
 *     ...
 *     `-- C
 * "+--" - ребенок, после которого есть еще братья; "`--" - последний ребенок; "|" - линия к следующим
 * братьям предка. Деревья разделены пустой строкой.
 *
 * Узел хранит первого ребенка и следующего брата (так можно хранить любое число детей).
 * Разбор и печать - без рекурсии (свой стек): вложенность скобок может быть очень большой.
 * Неправильная строка - сообщение с номером строки и код 1 (деревья предыдущих строк уже записаны).
 * Входной и выходной файл не могут быть одним файлом: сравниваются сами файлы, а не строки путей.
 */
#define _POSIX_C_SOURCE 200809L   /* stat - функция POSIX */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, _fullpath */
#include <string.h>   /* memcpy */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace */
#ifndef _WIN32
#include <sys/stat.h>   /* stat - для same_file */
#endif

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    SYNTAX_ERROR,      /* строка - не правильное скобочное выражение */
    END_OF_INPUT,      /* строки кончились */
    BAD_PATH,          /* путь неправильный (Windows, same_file) */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Узел дерева: имя, первый ребенок, следующий брат; last_child - чтобы добавлять детей в конец */
typedef struct tree_node {
    char *name;
    struct tree_node *first_child;
    struct tree_node *last_child;
    struct tree_node *next_sibling;
} tree_node;

/* Стек указателей в динамической памяти */
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

/* ===================== дерево ===================== */

/*
 * Освободить дерево без рекурсии: стек узлов; у каждого узла в стек кладем первого ребенка
 * и следующего брата, сам узел освобождаем.
 */
status_code tree_free(tree_node *root) {
    if (root == NULL) {
        return OK;
    }
    pointer_stack stack = {NULL, 0, 0};
    status_code sc = stack_push(&stack, root);
    while (sc == OK && stack.count > 0) {
        stack.count--;
        tree_node *node = (tree_node *)stack.items[stack.count];
        if (node->first_child != NULL) {
            sc = stack_push(&stack, node->first_child);
        }
        if (sc == OK && node->next_sibling != NULL) {
            sc = stack_push(&stack, node->next_sibling);
        }
        free(node->name);
        free(node);
    }
    /* нехватка памяти для стека - редкость; тогда часть узлов не освободится, но программа
       все равно сразу завершится с ошибкой */
    stack_free(&stack);
    return sc;
}

/* Новый узел с именем text[from..to) */
status_code node_create(const char *text, const size_t from, const size_t to, tree_node **node) {
    if (text == NULL || node == NULL || to <= from) {
        return INVALID_ARGS;
    }
    tree_node *n = (tree_node *)malloc(sizeof(tree_node));
    if (n == NULL) {
        return ALLOC_ERROR;
    }
    n->name = (char *)malloc(to - from + 1);
    if (n->name == NULL) {
        free(n);
        return ALLOC_ERROR;
    }
    memcpy(n->name, text + from, to - from);
    n->name[to - from] = '\0';
    n->first_child = NULL;
    n->last_child = NULL;
    n->next_sibling = NULL;
    *node = n;
    return OK;
}

/* Символ имени: не пробельный, не скобка и не запятая */
status_code is_name_char(const char c, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = c != '\0' && c != '(' && c != ')' && c != ',' && !isspace((unsigned char)c);
    return OK;
}

/*
 * Разобрать выражение в дерево (без рекурсии). Стек - узлы, чьи скобки сейчас открыты:
 * новый узел становится ребенком верхнего узла стека.
 *   имя     - новый узел; дальше может быть '(' (у него будут дети), ',' или ')' или конец
 *   '('     - узел, только что прочитанный, кладем в стек; дальше обязательно имя
 *   ','     - только внутри скобок; дальше обязательно имя
 *   ')'     - закрываем скобки верхнего узла (снимаем его со стека)
 * Конец строки: все скобки закрыты и корень один. *problem - описание ошибки.
 */
status_code parse_expression(const char *text, tree_node **root, const char **problem) {
    if (text == NULL || root == NULL || problem == NULL) {
        return INVALID_ARGS;
    }
    *root = NULL;
    pointer_stack open = {NULL, 0, 0};
    tree_node *last = NULL;     /* последний прочитанный узел */
    int expect_name = 1;        /* сейчас должно быть имя */
    size_t pos = 0;
    status_code sc = OK;
    while (sc == OK) {
        while (isspace((unsigned char)text[pos])) {
            pos++;
        }
        char c = text[pos];
        if (c == '\0') {
            break;
        }
        int name_char = 0;
        is_name_char(c, &name_char);
        if (expect_name) {
            if (!name_char) {
                *problem = c == ')' ? "a name expected before ')' (empty brackets or ',)')" : "a name expected";
                sc = SYNTAX_ERROR;
                break;
            }
            size_t from = pos;
            while (name_char) {
                pos++;
                is_name_char(text[pos], &name_char);
            }
            tree_node *node = NULL;
            sc = node_create(text, from, pos, &node);
            if (sc != OK) {
                break;
            }
            if (open.count == 0) {
                if (*root != NULL) {
                    /* сюда не попадаем: после корня имя без запятой не ожидается */
                    tree_free(node);
                    *problem = "only one root is allowed";
                    sc = SYNTAX_ERROR;
                    break;
                }
                *root = node;
            } else {
                /* ребенок верхнего открытого узла - в конец списка детей */
                tree_node *parent = (tree_node *)open.items[open.count - 1];
                if (parent->last_child == NULL) {
                    parent->first_child = node;
                } else {
                    parent->last_child->next_sibling = node;
                }
                parent->last_child = node;
            }
            last = node;
            expect_name = 0;
            continue;
        }
        /* после имени или ')' */
        if (c == '(') {
            if (last == NULL || last->first_child != NULL) {
                *problem = "'(' must follow a name";   /* "A (B) (C)" */
                sc = SYNTAX_ERROR;
                break;
            }
            sc = stack_push(&open, last);
            expect_name = 1;
        } else if (c == ',') {
            if (open.count == 0) {
                *problem = "',' outside brackets: only one root is allowed";
                sc = SYNTAX_ERROR;
                break;
            }
            expect_name = 1;
        } else if (c == ')') {
            if (open.count == 0) {
                *problem = "')' without '('";
                sc = SYNTAX_ERROR;
                break;
            }
            open.count--;
            last = (tree_node *)open.items[open.count];   /* после ')' может быть только , ) или конец */
        } else {
            *problem = open.count == 0 ? "only one root is allowed (',' or brackets expected)"
                                       : "',' or ')' expected between names";
            sc = SYNTAX_ERROR;
            break;
        }
        pos++;
    }
    if (sc == OK && *root == NULL) {
        *problem = "the expression is empty";
        sc = SYNTAX_ERROR;
    } else if (sc == OK && expect_name) {
        *problem = "a name expected at the end";
        sc = SYNTAX_ERROR;
    } else if (sc == OK && open.count > 0) {
        *problem = "')' expected: a bracket is not closed";
        sc = SYNTAX_ERROR;
    }
    stack_free(&open);
    if (sc != OK) {
        tree_free(*root);
        *root = NULL;
    }
    return sc;
}

/*
 * Напечатать дерево (без рекурсии). Стек - узлы и их глубины; дети кладутся в обратном порядке,
 * чтобы печататься по порядку. is_last[d] - последний ли ребенок узел на уровне d: от этого зависит,
 * рисовать ли "|" ниже. Перед узлом глубины d: для уровней 1..d-1 - "|   " или "    ", потом "+-- " или "`-- ".
 */
status_code tree_print(FILE *out, const tree_node *root) {
    if (out == NULL || root == NULL) {
        return INVALID_ARGS;
    }
    pointer_stack nodes = {NULL, 0, 0};
    /* глубины узлов стека - отдельный массив той же длины: depths[i] - глубина nodes.items[i] */
    size_t *depths = NULL;
    size_t depths_size = 0;
    unsigned char *is_last = NULL;
    size_t last_size = 0;
    status_code sc = stack_push(&nodes, (void *)root);
    while (sc == OK && nodes.count > 0) {
        /* массив глубин - не короче стека узлов */
        if (depths_size < nodes.capacity) {
            size_t *tmp = (size_t *)realloc(depths, nodes.capacity * sizeof(size_t));
            if (tmp == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            if (depths_size == 0) {
                tmp[0] = 0;   /* корень - глубина 0 */
            }
            depths = tmp;
            depths_size = nodes.capacity;
        }
        nodes.count--;
        const tree_node *node = (const tree_node *)nodes.items[nodes.count];
        size_t depth = depths[nodes.count];
        /* запомнить, последний ли узел на своем уровне */
        if (depth >= last_size) {
            size_t new_size = last_size == 0 ? 16 : last_size * 2;
            while (new_size <= depth) {
                new_size *= 2;
            }
            unsigned char *tmp = (unsigned char *)realloc(is_last, new_size);
            if (tmp == NULL) {
                sc = ALLOC_ERROR;
                break;
            }
            is_last = tmp;
            last_size = new_size;
        }
        is_last[depth] = node->next_sibling == NULL;
        for (size_t level = 1; level < depth; level++) {
            fputs(is_last[level] ? "    " : "|   ", out);
        }
        if (depth > 0) {
            fputs(is_last[depth] ? "`-- " : "+-- ", out);
        }
        fprintf(out, "%s\n", node->name);
        /* дети в стек: сначала собрать их, потом положить с последнего - первый выйдет первым */
        size_t first_index = nodes.count;
        for (const tree_node *child = node->first_child; child != NULL && sc == OK; child = child->next_sibling) {
            sc = stack_push(&nodes, (void *)child);
            /* стек мог вырасти - массив глубин тоже */
            if (sc == OK && depths_size < nodes.capacity) {
                size_t *tmp = (size_t *)realloc(depths, nodes.capacity * sizeof(size_t));
                if (tmp == NULL) {
                    sc = ALLOC_ERROR;
                } else {
                    depths = tmp;
                    depths_size = nodes.capacity;
                }
            }
            if (sc == OK) {
                depths[nodes.count - 1] = depth + 1;
            }
        }
        /* развернуть добавленный кусок стека (детей) - чтобы первый ребенок оказался сверху */
        for (size_t i = first_index, j = nodes.count; sc == OK && i + 1 < j; i++, j--) {
            void *tmp = nodes.items[i];
            nodes.items[i] = nodes.items[j - 1];
            nodes.items[j - 1] = tmp;
            /* глубины у всех детей одинаковые - их менять не нужно */
        }
    }
    stack_free(&nodes);
    free(depths);
    free(is_last);
    return sc;
}

/* ===================== файлы ===================== */

/* Прочитать строку любой длины (без '\n' и '\r' в конце); END_OF_INPUT - строк больше нет */
status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t buf_size = 64;
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
            char *tmp = (char *)realloc(buf, buf_size * 2);
            if (tmp == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = tmp;
            buf_size *= 2;
        }
        buf[length] = (char)c;
        length++;
        c = fgetc(in);
    }
    if (length > 0 && buf[length - 1] == '\r') {
        length--;
    }
    buf[length] = '\0';
    *line = buf;
    return OK;
}

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

/* Ведут ли два пути к одному файлу (Linux - номера устройства и файла, Windows - полные пути) */
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

/* Строка из одних пробелов? */
status_code is_blank(const char *line, int *blank) {
    if (line == NULL || blank == NULL) {
        return INVALID_ARGS;
    }
    *blank = 1;
    for (size_t i = 0; line[i] != '\0'; i++) {
        if (!isspace((unsigned char)line[i])) {
            *blank = 0;
            break;
        }
    }
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: lab3_task10 <input file> <output file>\n");
        return 1;
    }
    /* выходной файл не должен быть входным: "w" стер бы выражения раньше, чем мы их прочитаем */
    int equal = 0;
    if (same_file(argv[1], argv[2], &equal) == ALLOC_ERROR) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    if (equal) {
        fprintf(stderr, "Error: input and output must be different files\n");
        return 1;
    }
    FILE *in = fopen(argv[1], "r");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        return 1;
    }
    FILE *out = fopen(argv[2], "w");
    if (out == NULL) {
        fprintf(stderr, "Error: can not create file '%s'\n", argv[2]);
        fclose(in);
        return 1;
    }

    int result = 0;
    size_t line_number = 0;
    size_t written = 0;
    while (result == 0) {
        char *line = NULL;
        status_code sc = read_line(in, &line);
        if (sc == END_OF_INPUT) {
            break;
        }
        if (sc != OK) {
            fprintf(stderr, "Error: not enough memory\n");
            result = 1;
            break;
        }
        line_number++;
        /* метка BOM в начале файла из Блокнота - пропускаем */
        char *text = line;
        if (line_number == 1 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
            && (unsigned char)text[2] == 0xBF) {
            text += 3;
        }
        int blank = 0;
        is_blank(text, &blank);
        if (!blank) {
            tree_node *root = NULL;
            const char *problem = NULL;
            sc = parse_expression(text, &root, &problem);
            if (sc == SYNTAX_ERROR) {
                fprintf(stderr, "Error: line %zu: %s\n", line_number, problem);
                result = 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: not enough memory\n");
                result = 1;
            } else {
                /* деревья разделены пустой строкой */
                fprintf(out, "%sExpression %zu: %s\n", written > 0 ? "\n" : "", written + 1, text);
                sc = tree_print(out, root);
                written++;
                if (sc != OK) {
                    fprintf(stderr, "Error: not enough memory\n");
                    result = 1;
                }
                tree_free(root);
            }
        }
        free(line);
    }
    fclose(in);
    if (fclose(out) != 0 && result == 0) {
        fprintf(stderr, "Error: can not write file '%s'\n", argv[2]);
        result = 1;
    }
    if (result == 0) {
        printf("Written %zu trees to '%s'\n", written, argv[2]);
    }
    return result;
}
