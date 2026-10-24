/*
 * Лабораторная 3, задание 3. Код Хаффмана для байтов файла и оценка выигрыша от сжатия.
 *
 * Файл читается в двоичном режиме; единица кодирования - байт (значения 0..255).
 * Для каждого значения байта считается частота, по частотам строится дерево Хаффмана
 * (своей минимальной кучей), из дерева - коды из 0 и 1. Печатается таблица кодов,
 * размер файла, размер закодированных данных в битах и байтах и экономия в процентах.
 *
 * Запуск:  lab3_task3 <путь к файлу>
 * Пример (файл "abracadabra", 11 байт):
 *     byte  frequency  code
 *       97          5  0
 *       98          2  110
 *       99          1  100
 *      100          1  101
 *      114          2  111
 *     Original size: 11 bytes
 *     Encoded size: 23 bits = 3 bytes
 *     Savings: 72.73%
 *
 * Дерево Хаффмана: все встречающиеся байты - листья с весом = частота. Пока узлов больше одного,
 * из кучи берутся два самых легких, объединяются в новый узел (вес = сумма) и он кладется обратно.
 * Оставшийся узел - корень. Код байта - путь от корня до листа: налево 0, направо 1.
 * Частые байты оказываются ближе к корню и получают короткие коды.
 *
 * При равных весах условие разрешает любой порядок. Чтобы ответ был одинаковым всегда
 * (и его можно было проверить), порядок задан: у каждого узла есть номер - листья нумеруются
 * по возрастанию значения байта, новые узлы получают следующие номера; из двух узлов
 * с равным весом первым берется узел с меньшим номером. Первый взятый узел - левый (0).
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free */
#include <limits.h>   /* ULLONG_MAX */

#define BYTE_VALUES 256         /* значений у байта: 0..255 */
#define MAX_NODES 511           /* листьев не больше 256, внутренних на один меньше */
#define READ_BUFFER_SIZE 65536  /* файл читаем кусками по 64 КБ */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* в функцию передали NULL или неправильные данные */
    FILE_ERROR,       /* ошибка чтения файла */
    OVERFLOW_ERROR,   /* число не помещается в unsigned long long */
    HEAP_EMPTY,       /* из пустой кучи нечего брать */
    HEAP_FULL,        /* в куче нет места */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* Узел дерева Хаффмана. Узлы лежат в одном массиве, дети - номера в этом массиве (-1 - нет ребенка) */
typedef struct {
    unsigned long long weight;   /* суммарная частота байтов в поддереве */
    int left;                    /* левый ребенок (код 0) или -1 у листа */
    int right;                   /* правый ребенок (код 1) или -1 у листа */
    int symbol;                  /* значение байта у листа, -1 у внутреннего узла */
} tree_node;

/* Дерево: массив узлов и номер корня (-1 - дерево пустое) */
typedef struct {
    tree_node *nodes;
    int count;
    int root;
} huffman_tree;

/* Минимальная куча номеров узлов. "Меньше" - меньший вес, при равенстве - меньший номер */
typedef struct {
    int *items;
    int size;
    int capacity;
    const tree_node *nodes;   /* по номеру узла узнаем его вес */
} min_heap;

/* Результат оценки сжатия */
typedef struct {
    unsigned long long original_bytes;   /* N - размер файла */
    unsigned long long encoded_bits;     /* сумма частота * длина кода */
    unsigned long long encoded_bytes;    /* M - биты, округленные вверх до байтов */
    double savings_percent;              /* 100 * (1 - M / N), для пустого файла 0 */
} compression_info;

/* ===================== частоты ===================== */

/* Прочитать весь файл (уже открытый в двоичном режиме) и посчитать частоты байтов */
status_code count_frequencies(FILE *file, unsigned long long frequencies[BYTE_VALUES], unsigned long long *total) {
    if (file == NULL || frequencies == NULL || total == NULL) {
        return INVALID_ARGS;
    }
    for (int i = 0; i < BYTE_VALUES; i++) {
        frequencies[i] = 0;
    }
    *total = 0;
    unsigned char *buffer = (unsigned char *)malloc(READ_BUFFER_SIZE);
    if (buffer == NULL) {
        return ALLOC_ERROR;
    }
    size_t got = 0;
    /* fread возвращает, сколько байт прочитал; 0 - конец файла или ошибка */
    while ((got = fread(buffer, 1, READ_BUFFER_SIZE, file)) > 0) {
        for (size_t i = 0; i < got; i++) {
            frequencies[buffer[i]]++;
        }
        *total += got;
    }
    free(buffer);
    /* ferror отличает ошибку чтения от обычного конца файла */
    if (ferror(file)) {
        return FILE_ERROR;
    }
    return OK;
}

/* ===================== минимальная куча ===================== */

/* Узел a "меньше" узла b: меньше вес, при равных весах - меньше номер */
status_code node_less(const tree_node *nodes, const int a, const int b, int *less) {
    if (nodes == NULL || less == NULL) {
        return INVALID_ARGS;
    }
    if (nodes[a].weight != nodes[b].weight) {
        *less = nodes[a].weight < nodes[b].weight;
    } else {
        *less = a < b;
    }
    return OK;
}

/* Создать пустую кучу на capacity элементов */
status_code heap_create(min_heap *heap, const int capacity, const tree_node *nodes) {
    if (heap == NULL || nodes == NULL || capacity < 1) {
        return INVALID_ARGS;
    }
    heap->items = (int *)malloc((size_t)capacity * sizeof(int));
    if (heap->items == NULL) {
        return ALLOC_ERROR;
    }
    heap->size = 0;
    heap->capacity = capacity;
    heap->nodes = nodes;
    return OK;
}

status_code heap_destroy(min_heap *heap) {
    if (heap == NULL) {
        return INVALID_ARGS;
    }
    free(heap->items);
    heap->items = NULL;
    heap->size = 0;
    heap->capacity = 0;
    return OK;
}

/*
 * Добавить узел. Куча хранится в массиве: у элемента i дети - 2i+1 и 2i+2, родитель - (i-1)/2.
 * Правило кучи: родитель не больше детей, поэтому самый маленький - в items[0].
 * Новый элемент кладем в конец и "поднимаем", пока он меньше родителя.
 */
status_code heap_push(min_heap *heap, const int node) {
    if (heap == NULL || heap->items == NULL) {
        return INVALID_ARGS;
    }
    if (heap->size == heap->capacity) {
        return HEAP_FULL;
    }
    int i = heap->size;
    heap->items[i] = node;
    heap->size++;
    while (i > 0) {
        int parent = (i - 1) / 2;
        int less = 0;
        node_less(heap->nodes, heap->items[i], heap->items[parent], &less);
        if (!less) {
            break;   /* родитель уже меньше - место найдено */
        }
        int tmp = heap->items[i];
        heap->items[i] = heap->items[parent];
        heap->items[parent] = tmp;
        i = parent;
    }
    return OK;
}

/*
 * Взять самый маленький узел (items[0]). На его место ставим последний элемент
 * и "опускаем": меняем с меньшим из детей, пока он больше этого ребенка.
 */
status_code heap_pop(min_heap *heap, int *node) {
    if (heap == NULL || heap->items == NULL || node == NULL) {
        return INVALID_ARGS;
    }
    if (heap->size == 0) {
        return HEAP_EMPTY;
    }
    *node = heap->items[0];
    heap->size--;
    heap->items[0] = heap->items[heap->size];
    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;
        int less = 0;
        if (left < heap->size) {
            node_less(heap->nodes, heap->items[left], heap->items[smallest], &less);
            if (less) {
                smallest = left;
            }
        }
        if (right < heap->size) {
            node_less(heap->nodes, heap->items[right], heap->items[smallest], &less);
            if (less) {
                smallest = right;
            }
        }
        if (smallest == i) {
            break;   /* оба ребенка не меньше - место найдено */
        }
        int tmp = heap->items[i];
        heap->items[i] = heap->items[smallest];
        heap->items[smallest] = tmp;
        i = smallest;
    }
    return OK;
}

/* ===================== дерево Хаффмана ===================== */

status_code tree_destroy(huffman_tree *tree) {
    if (tree == NULL) {
        return INVALID_ARGS;
    }
    free(tree->nodes);
    tree->nodes = NULL;
    tree->count = 0;
    tree->root = -1;
    return OK;
}

/*
 * Построить дерево по частотам. Листья - только байты с частотой > 0, по возрастанию значения.
 * Для пустого файла дерево пустое (root = -1).
 */
status_code build_tree(const unsigned long long frequencies[BYTE_VALUES], huffman_tree *tree) {
    if (frequencies == NULL || tree == NULL) {
        return INVALID_ARGS;
    }
    tree->nodes = (tree_node *)malloc(MAX_NODES * sizeof(tree_node));
    if (tree->nodes == NULL) {
        return ALLOC_ERROR;
    }
    tree->count = 0;
    tree->root = -1;

    /* листья: номера 0..m-1 по возрастанию значения байта */
    for (int b = 0; b < BYTE_VALUES; b++) {
        if (frequencies[b] > 0) {
            tree_node leaf = {frequencies[b], -1, -1, b};
            tree->nodes[tree->count] = leaf;
            tree->count++;
        }
    }
    if (tree->count == 0) {
        return OK;   /* пустой файл */
    }

    min_heap heap;
    status_code sc = heap_create(&heap, MAX_NODES, tree->nodes);
    if (sc != OK) {
        tree_destroy(tree);
        return sc;
    }
    for (int i = 0; i < tree->count; i++) {
        heap_push(&heap, i);   /* места хватает: capacity = MAX_NODES */
    }
    /* пока в куче больше одного узла: два самых легких -> новый узел */
    while (heap.size > 1) {
        int first = 0;
        int second = 0;
        heap_pop(&heap, &first);
        heap_pop(&heap, &second);
        /* сумма весов не больше размера файла, а он помещается в unsigned long long */
        if (tree->nodes[first].weight > ULLONG_MAX - tree->nodes[second].weight) {
            heap_destroy(&heap);
            tree_destroy(tree);
            return OVERFLOW_ERROR;
        }
        tree_node parent = {tree->nodes[first].weight + tree->nodes[second].weight, first, second, -1};
        tree->nodes[tree->count] = parent;
        heap_push(&heap, tree->count);
        tree->count++;
    }
    heap_pop(&heap, &tree->root);   /* последний узел - корень */
    heap_destroy(&heap);
    return OK;
}

/*
 * Записать коды всех листьев поддерева node. path - путь от корня (символы '0'/'1'),
 * depth - его длина. Код листа копируется в codes[байт] (строка в динамической памяти).
 * Глубина рекурсии не больше 255: у дерева из 256 листьев путь не длиннее 255.
 */
status_code collect_codes(const huffman_tree *tree, const int node, char *path, const int depth,
                          char *codes[BYTE_VALUES]) {
    if (tree == NULL || path == NULL || codes == NULL || node < 0) {
        return INVALID_ARGS;
    }
    const tree_node *current = &tree->nodes[node];
    if (current->symbol >= 0) {
        /* лист: код = путь. depth == 0 только если весь файл из одного значения байта -
           тогда по условию код "0" */
        int length = depth == 0 ? 1 : depth;
        char *code = (char *)malloc((size_t)length + 1);
        if (code == NULL) {
            return ALLOC_ERROR;
        }
        if (depth == 0) {
            code[0] = '0';
        } else {
            for (int i = 0; i < depth; i++) {
                code[i] = path[i];
            }
        }
        code[length] = '\0';
        codes[current->symbol] = code;
        return OK;
    }
    /* внутренний узел: налево - '0', направо - '1' */
    path[depth] = '0';
    status_code sc = collect_codes(tree, current->left, path, depth + 1, codes);
    if (sc != OK) {
        return sc;
    }
    path[depth] = '1';
    return collect_codes(tree, current->right, path, depth + 1, codes);
}

/* Освободить коды (у байтов, которых нет в файле, код NULL - free(NULL) ничего не делает) */
status_code free_codes(char *codes[BYTE_VALUES]) {
    if (codes == NULL) {
        return INVALID_ARGS;
    }
    for (int b = 0; b < BYTE_VALUES; b++) {
        free(codes[b]);
        codes[b] = NULL;
    }
    return OK;
}

/* Построить коды по дереву. codes[b] - строка из 0 и 1 или NULL, если байта b нет в файле */
status_code make_codes(const huffman_tree *tree, char *codes[BYTE_VALUES]) {
    if (tree == NULL || codes == NULL) {
        return INVALID_ARGS;
    }
    for (int b = 0; b < BYTE_VALUES; b++) {
        codes[b] = NULL;
    }
    if (tree->root < 0) {
        return OK;   /* пустой файл - кодов нет */
    }
    /* путь от корня не длиннее 255 символов */
    char *path = (char *)malloc(BYTE_VALUES);
    if (path == NULL) {
        return ALLOC_ERROR;
    }
    status_code sc = collect_codes(tree, tree->root, path, 0, codes);
    free(path);
    if (sc != OK) {
        free_codes(codes);
    }
    return sc;
}

/* ===================== оценка сжатия ===================== */

/* Длина строки кода (своя, чтобы не зависеть от string.h ради одной функции) */
status_code code_length(const char *code, unsigned long long *length) {
    if (code == NULL || length == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long n = 0;
    while (code[n] != '\0') {
        n++;
    }
    *length = n;
    return OK;
}

/* Размер закодированных данных и экономия по формуле из условия */
status_code estimate_compression(const unsigned long long frequencies[BYTE_VALUES], char *const codes[BYTE_VALUES],
                                 const unsigned long long total, compression_info *info) {
    if (frequencies == NULL || codes == NULL || info == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long bits = 0;
    for (int b = 0; b < BYTE_VALUES; b++) {
        if (frequencies[b] == 0) {
            continue;
        }
        if (codes[b] == NULL) {
            return INVALID_ARGS;   /* у встречающегося байта обязан быть код */
        }
        unsigned long long length = 0;
        code_length(codes[b], &length);
        /* bits += frequency * length без переполнения: проверяем до умножения и сложения */
        if (frequencies[b] > (ULLONG_MAX - bits) / length) {
            return OVERFLOW_ERROR;
        }
        bits += frequencies[b] * length;
    }
    info->original_bytes = total;
    info->encoded_bits = bits;
    /* округление вверх: (bits + 7) / 8, записанное так, чтобы bits + 7 не переполнилось */
    info->encoded_bytes = bits / 8 + (bits % 8 != 0 ? 1 : 0);
    if (total == 0) {
        info->savings_percent = 0.0;
    } else {
        info->savings_percent = 100.0 * (1.0 - (double)info->encoded_bytes / (double)total);
    }
    return OK;
}

/* ===================== вывод ===================== */

/* Таблица кодов (байты по возрастанию) и итоги */
status_code print_result(FILE *out, const unsigned long long frequencies[BYTE_VALUES], char *const codes[BYTE_VALUES],
                         const compression_info *info) {
    if (out == NULL || frequencies == NULL || codes == NULL || info == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "byte  frequency  code\n");
    for (int b = 0; b < BYTE_VALUES; b++) {
        if (frequencies[b] > 0) {
            fprintf(out, "%4d  %9llu  %s\n", b, frequencies[b], codes[b]);
        }
    }
    fprintf(out, "Original size: %llu bytes\n", info->original_bytes);
    fprintf(out, "Encoded size: %llu bits = %llu bytes\n", info->encoded_bits, info->encoded_bytes);
    fprintf(out, "Savings: %.2f%%\n", info->savings_percent);
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: lab3_task3 <file>\n");
        return 1;
    }

    /* массивы на 256 элементов - в динамической памяти, а не на стеке */
    unsigned long long *frequencies = (unsigned long long *)malloc(BYTE_VALUES * sizeof(unsigned long long));
    char **codes = (char **)malloc(BYTE_VALUES * sizeof(char *));
    if (frequencies == NULL || codes == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        free(frequencies);
        free(codes);
        return 1;
    }
    for (int b = 0; b < BYTE_VALUES; b++) {
        codes[b] = NULL;   /* free_codes в конце можно вызвать в любом случае */
    }

    /* result: 0 - всё хорошо, 1 - была ошибка (сообщение уже напечатано); дальше шаги идут,
       пока result == 0, а память освобождается один раз в конце */
    int result = 0;

    /* 1) частоты байтов; "rb" - двоичный режим: на Windows иначе \r\n превратилось бы в \n,
       а байт 26 (Ctrl+Z) оборвал бы чтение */
    unsigned long long total = 0;
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        result = 1;
    } else {
        status_code sc = count_frequencies(file, frequencies, &total);
        fclose(file);
        if (sc == FILE_ERROR) {
            fprintf(stderr, "Error: can not read file '%s'\n", argv[1]);
            result = 1;
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
            result = 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: internal error\n");   /* аргументы не NULL - сюда не попадаем */
            result = 1;
        }
    }

    /* 2) дерево и коды */
    if (result == 0) {
        huffman_tree tree;
        status_code sc = build_tree(frequencies, &tree);
        if (sc == OK) {
            sc = make_codes(&tree, codes);
            tree_destroy(&tree);
        }
        if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
            result = 1;
        } else if (sc != OK) {
            /* OVERFLOW_ERROR невозможен: сумма весов = размер файла, он уже сосчитан */
            fprintf(stderr, "Error: internal error\n");
            result = 1;
        }
    }

    /* 3) оценка и вывод */
    if (result == 0) {
        compression_info info;
        status_code sc = estimate_compression(frequencies, codes, total, &info);
        if (sc == OK) {
            print_result(stdout, frequencies, codes, &info);
        } else if (sc == OVERFLOW_ERROR) {
            fprintf(stderr, "Error: the file is too big to count its encoded size\n");
            result = 1;
        } else {
            fprintf(stderr, "Error: internal error\n");
            result = 1;
        }
    }

    free_codes(codes);
    free(codes);
    free(frequencies);
    return result;
}
