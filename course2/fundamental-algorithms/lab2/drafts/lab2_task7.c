/*
 * Лаб 2, задание 7.
 * Функция с переменным числом аргументов:
 * размерность n, вектора, указатели на функции-нормы.
 * Для каждой нормы — возвращает самый длинный вектор (или несколько).
 *
 * Нормы: ||x||_inf, ||x||_p (p>=1), ||x||_A (A — полож. определённая матрица).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <float.h>

typedef enum {
    OK,
    INVALID_ARGS,
    ALLOC_ERROR
} status_code;

/* Вектор: координаты в динамическом массиве */
typedef struct {
    double *coords;
    int n;
} Vector;

/* Результат для одной нормы: массив индексов самых длинных векторов */
typedef struct {
    int *indices;
    int count;
    double max_norm;
} NormResult;

/* Тип указателя на функцию нормы.
 * Принимает вектор и дополнительные параметры (void *).
 * Возвращает значение нормы. */
typedef double (*NormFunc)(const Vector *v, const void *params);

/* ========================= Реализации норм ========================= */

/* Норма бесконечности: max |x_j| */
double norm_inf(const Vector *v, const void *params) {
    (void)params;
    double max_val = 0.0;
    for (int j = 0; j < v->n; j++) {
        double absv = fabs(v->coords[j]);
        if (absv > max_val) max_val = absv;
    }
    return max_val;
}

/* p-норма: (sum |x_j|^p)^(1/p).  params — указатель на double p */
double norm_p(const Vector *v, const void *params) {
    const double *p_ptr = (const double *)params;
    double p = *p_ptr;
    if (p < 1.0) p = 1.0;

    double sum = 0.0;
    for (int j = 0; j < v->n; j++) {
        sum += pow(fabs(v->coords[j]), p);
    }
    return pow(sum, 1.0 / p);
}

/* Матричная норма: sqrt((Ax, x)).  params — указатель на double[n*n] (матрица A) */
double norm_A(const Vector *v, const void *params) {
    const double *A = (const double *)params;
    int n = v->n;

    /* Ax */
    double *Ax = (double *)malloc((size_t)n * sizeof(double));
    if (Ax == NULL) return -1.0;

    for (int i = 0; i < n; i++) {
        Ax[i] = 0.0;
        for (int j = 0; j < n; j++) {
            Ax[i] += A[i * n + j] * v->coords[j];
        }
    }

    /* (Ax, x) */
    double dot = 0.0;
    for (int i = 0; i < n; i++) {
        dot += Ax[i] * v->coords[i];
    }

    free(Ax);
    return sqrt(fabs(dot));
}

/* ========================= Основная функция ========================= */

/*
 * find_longest_vectors:
 *   n           — размерность пространства
 *   vectors     — массив векторов
 *   vec_count   — количество векторов
 *   results     — массив результатов (по одному для каждой нормы)
 *   norm_count  — количество норм
 *   ...         — пары (NormFunc func, void *params)
 *
 * Возвращает OK / ошибку. results должен быть выделен вызывающим кодом.
 */
status_code find_longest_vectors(int n, const Vector *vectors, int vec_count,
                                 NormResult *results, int norm_count, ...) {
    if (vectors == NULL || results == NULL || n <= 0 || vec_count <= 0 || norm_count <= 0) {
        return INVALID_ARGS;
    }

    va_list args;
    va_start(args, norm_count);

    for (int ni = 0; ni < norm_count; ni++) {
        NormFunc func = va_arg(args, NormFunc);
        void *params = va_arg(args, void *);

        results[ni].indices = NULL;
        results[ni].count = 0;
        results[ni].max_norm = -1.0;

        /* Вычисляем нормы всех векторов */
        double *norms = (double *)malloc((size_t)vec_count * sizeof(double));
        if (norms == NULL) {
            va_end(args);
            return ALLOC_ERROR;
        }

        double max_norm = -1.0;
        for (int vi = 0; vi < vec_count; vi++) {
            norms[vi] = func(&vectors[vi], params);
            if (norms[vi] > max_norm) {
                max_norm = norms[vi];
            }
        }

        /* Считаем, сколько векторов имеют максимальную норму */
        int count = 0;
        for (int vi = 0; vi < vec_count; vi++) {
            if (fabs(norms[vi] - max_norm) < 1e-9) {
                count++;
            }
        }

        results[ni].indices = (int *)malloc((size_t)count * sizeof(int));
        if (results[ni].indices == NULL) {
            free(norms);
            va_end(args);
            return ALLOC_ERROR;
        }

        int idx = 0;
        for (int vi = 0; vi < vec_count; vi++) {
            if (fabs(norms[vi] - max_norm) < 1e-9) {
                results[ni].indices[idx++] = vi;
            }
        }
        results[ni].count = count;
        results[ni].max_norm = max_norm;

        free(norms);
    }

    va_end(args);
    return OK;
}

/* ========================= main (демонстрация) ========================= */

static void free_results(NormResult *results, int count) {
    for (int i = 0; i < count; i++) {
        free(results[i].indices);
    }
}

int main(void) {
    int n = 3;
    int vec_count = 4;

    /* Создаём 4 вектора в R^3 */
    double c0[] = {1.0, 2.0, 3.0};
    double c1[] = {4.0, 5.0, 6.0};
    double c2[] = {7.0, 8.0, 9.0};
    double c3[] = {1.0, 1.0, 1.0};

    Vector vectors[4];
    vectors[0].coords = c0; vectors[0].n = n;
    vectors[1].coords = c1; vectors[1].n = n;
    vectors[2].coords = c2; vectors[2].n = n;
    vectors[3].coords = c3; vectors[3].n = n;

    /* Параметры норм */
    double p_val = 2.0;  /* для p-нормы */
    /* Матрица A = I (единичная) для матричной нормы */
    double A[9] = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };

    int norm_count = 3;
    NormResult results[3];

    status_code sc = find_longest_vectors(n, vectors, vec_count, results, norm_count,
                                          (NormFunc)norm_inf, NULL,
                                          (NormFunc)norm_p, &p_val,
                                          (NormFunc)norm_A, A);

    if (sc != OK) {
        fprintf(stderr, "Error: %d\n", sc);
        return 1;
    }

    const char *norm_names[] = {"infinity", "p=2 (Euclidean)", "matrix A=I"};

    for (int i = 0; i < norm_count; i++) {
        printf("Norm %s: max_norm = %.4f\n", norm_names[i], results[i].max_norm);
        printf("  Longest vectors (%d):", results[i].count);
        for (int j = 0; j < results[i].count; j++) {
            int vi = results[i].indices[j];
            printf(" v%d(", vi);
            for (int k = 0; k < n; k++) {
                if (k > 0) printf(",");
                printf("%.1f", vectors[vi].coords[k]);
            }
            printf(")");
        }
        printf("\n");
    }

    free_results(results, norm_count);
    return 0;
}
