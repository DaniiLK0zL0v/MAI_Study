/*
 * Лаб 2, задание 4.
 * Задача о рюкзаке: выбор файлов для резервного копирования.
 * Ввод со stdin, вывод на stdout.
 * DP-решение задачи 0/1 knapsack.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OK,
    INVALID_ARGS,
    ALLOC_ERROR,
    INVALID_DATA
} status_code;

typedef struct {
    char *name;
    int size;
    int importance;
} FileInfo;

typedef struct {
    FileInfo *files;    /* выбранные файлы (копия указателей на имена) */
    int count;
    int total_size;
    int total_importance;
} BackupResult;

/* Освобождение массива FileInfo */
static void free_files(FileInfo *files, int n) {
    if (files == NULL) return;
    for (int i = 0; i < n; i++) {
        free(files[i].name);
    }
    free(files);
}

/* Освобождение результата */
static void free_result(BackupResult *res) {
    if (res == NULL) return;
    free(res->files); /* имена не дублировались, не освобождаем */
}

/* Решение задачи о рюкзаке методом DP.
 * files - массив файлов, n - количество, capacity - вместимость.
 * Результат записывается в result. */
status_code knapsack(const FileInfo *files, int n, int capacity, BackupResult *result) {
    if (files == NULL || result == NULL || n < 0 || capacity < 0) {
        return INVALID_ARGS;
    }

    result->files = NULL;
    result->count = 0;
    result->total_size = 0;
    result->total_importance = 0;

    if (n == 0 || capacity == 0) {
        return OK;
    }

    /* dp[i][w] = макс. важность из первых i файлов при вместимости w */
    /* Используем одномерный массив для экономии памяти */
    int *dp = (int *)calloc((size_t)(capacity + 1), sizeof(int));
    if (dp == NULL) return ALLOC_ERROR;

    /* Для восстановления ответа: keep[i][w] = 1, если i-й файл взят */
    /* Двумерный массив keep[n][capacity+1] */
    int *keep = (int *)calloc((size_t)n * (size_t)(capacity + 1), sizeof(int));
    if (keep == NULL) {
        free(dp);
        return ALLOC_ERROR;
    }

    /* DP: для каждого файла i обрабатываем в обратном порядке по весу,
     * но нам нужен keep, поэтому используем полный 2D подход через dp_prev */
    int *dp_prev = (int *)calloc((size_t)(capacity + 1), sizeof(int));
    if (dp_prev == NULL) {
        free(dp);
        free(keep);
        return ALLOC_ERROR;
    }

    for (int i = 0; i < n; i++) {
        for (int w = 0; w <= capacity; w++) {
            dp[w] = dp_prev[w]; /* не берём i-й файл */
            keep[i * (capacity + 1) + w] = 0;
        }
        for (int w = files[i].size; w <= capacity; w++) {
            int val = dp_prev[w - files[i].size] + files[i].importance;
            if (val > dp[w]) {
                dp[w] = val;
                keep[i * (capacity + 1) + w] = 1;
            }
        }
        /* dp_prev = dp для следующей итерации */
        for (int w = 0; w <= capacity; w++) {
            dp_prev[w] = dp[w];
        }
    }

    result->total_importance = dp[capacity];

    /* Восстановление ответа */
    int *selected = (int *)malloc((size_t)n * sizeof(int));
    if (selected == NULL) {
        free(dp);
        free(dp_prev);
        free(keep);
        return ALLOC_ERROR;
    }
    int sel_count = 0;
    int w = capacity;
    for (int i = n - 1; i >= 0; i--) {
        if (keep[i * (capacity + 1) + w]) {
            selected[sel_count++] = i;
            w -= files[i].size;
        }
    }

    free(dp);
    free(dp_prev);
    free(keep);

    /* Заполняем результат (в порядке перечисления во входных данных) */
    result->files = (FileInfo *)malloc((size_t)(sel_count > 0 ? sel_count : 1) * sizeof(FileInfo));
    if (result->files == NULL) {
        free(selected);
        return ALLOC_ERROR;
    }
    result->count = sel_count;
    result->total_size = 0;
    /* selected содержит индексы в обратном порядке, развернём */
    for (int i = sel_count - 1; i >= 0; i--) {
        int idx = selected[i];
        result->files[sel_count - 1 - i] = files[idx]; /* поверхностная копия */
        result->total_size += files[idx].size;
    }

    free(selected);
    return OK;
}

int main(void) {
    int n, capacity;
    if (scanf("%d %d", &n, &capacity) != 2 || n < 0 || capacity < 0) {
        fprintf(stderr, "Error: invalid input\n");
        return 1;
    }

    FileInfo *files = NULL;
    if (n > 0) {
        files = (FileInfo *)malloc((size_t)n * sizeof(FileInfo));
        if (files == NULL) {
            fprintf(stderr, "Error: memory allocation failed\n");
            return 1;
        }
        for (int i = 0; i < n; i++) {
            files[i].name = NULL;
        }
    }

    for (int i = 0; i < n; i++) {
        char name_buf[256];
        int sz, imp;
        if (scanf("%255s %d %d", name_buf, &sz, &imp) != 3 || sz <= 0 || imp < 0) {
            fprintf(stderr, "Error: invalid file data at line %d\n", i + 2);
            free_files(files, n);
            return 1;
        }
        files[i].name = (char *)malloc(strlen(name_buf) + 1);
        if (files[i].name == NULL) {
            fprintf(stderr, "Error: memory allocation failed\n");
            free_files(files, n);
            return 1;
        }
        strcpy(files[i].name, name_buf);
        files[i].size = sz;
        files[i].importance = imp;
    }

    BackupResult result;
    status_code sc = knapsack(files, n, capacity, &result);
    if (sc != OK) {
        fprintf(stderr, "Error: knapsack failed (%d)\n", sc);
        free_files(files, n);
        return 1;
    }

    printf("TOTAL_SIZE %d\n", result.total_size);
    printf("TOTAL_IMPORTANCE %d\n", result.total_importance);
    printf("FILES %d\n", result.count);
    for (int i = 0; i < result.count; i++) {
        printf("%s\n", result.files[i].name);
    }

    free(result.files);
    free_files(files, n);
    return 0;
}
