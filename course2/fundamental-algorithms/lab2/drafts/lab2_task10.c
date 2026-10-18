/*
 * Лаб 2, задание 10.
 * Переразложение многочлена по степеням (x - a).
 * f(x) = f0 + f1*x + f2*x^2 + ... + fn*x^n
 *       = g0 + g1*(x-a) + g2*(x-a)^2 + ... + gn*(x-a)^n
 *
 * Метод: синтетическое деление (схема Горнера), многократно.
 * g[k] = f(a) после k шагов деления на (x - a).
 *
 * Аргументы функции: eps, a, указатель на результат, степень n, коэффициенты f0..fn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

typedef enum {
    OK,
    INVALID_ARGS,
    ALLOC_ERROR
} status_code;

/*
 * Переразложение многочлена.
 *   eps    — точность (для сравнения вещественных чисел)
 *   a      — значение a в (x - a)
 *   result — указатель на массив коэффициентов g0, g1, ..., gn
 *            (динамически выделяется внутри функции)
 *   n      — степень многочлена
 *   ...    — коэффициенты f0, f1, ..., fn (double), всего n+1 штук
 */
status_code polynomial_reexpand(double eps, double a, double **result, int n, ...) {
    if (result == NULL || n < 0 || eps < 0) {
        return INVALID_ARGS;
    }
    (void)eps; /* используется для округления к нулю мелких коэффициентов */

    int size = n + 1;
    /* Считываем коэффициенты f0..fn */
    double *f = (double *)malloc((size_t)size * sizeof(double));
    if (f == NULL) return ALLOC_ERROR;

    va_list args;
    va_start(args, n);
    for (int i = 0; i < size; i++) {
        f[i] = va_arg(args, double);
    }
    va_end(args);

    /* Выделяем массив для результата g0..gn */
    *result = (double *)malloc((size_t)size * sizeof(double));
    if (*result == NULL) {
        free(f);
        return ALLOC_ERROR;
    }

    /*
     * Алгоритм: многократное синтетическое деление.
     *
     * f(x) записан как f[0] + f[1]*x + ... + f[n]*x^n.
     * f(a) = g[0], а после деления f(x)/(x-a) получаем многочлен степени n-1,
     * коэффициенты которого — новые f[1]..f[n-1].
     * Повторяем n+1 раз.
     *
     * Конкретно: для получения g[k] вычисляем значение текущего многочлена
     * при x = a (схема Горнера), и «сжимаем» массив коэффициентов.
     */

    /* Рабочая копия коэффициентов. Храним в порядке f[n], f[n-1], ..., f[0]
     * для удобства схемы Горнера, но можно и в прямом порядке. */
    /* Будем работать в порядке f[0], f[1], ..., f[n]. */
    /* Деление многочлена P(x) = c0 + c1*x + ... + cm*x^m на (x - a):
     * d[m-1] = c[m]
     * d[k] = c[k+1] + a * d[k+1]   (k = m-2, ..., 0)    -- нет, это для обратного порядка
     * P(a) = c[0] + a * d[0]
     *
     * Правильнее, для полинома в порядке c0 + c1*x + ... + cn*x^n:
     *   b[n] = c[n]
     *   b[k] = c[k] + a * b[k+1]   (k = n-1, ..., 0)
     *   Тогда P(a) = b[0], а частное Q(x) = b[1] + b[2]*x + ... + b[n]*x^(n-1)
     */

    int cur_degree = n;
    for (int k = 0; k <= n; k++) {
        /* Вычисляем f(a) схемой Горнера и одновременно делим */
        /* b[cur_degree] = f[cur_degree] */
        /* b[i] = f[i] + a * b[i+1], i = cur_degree-1, ..., 0 */
        /* g[k] = b[0] */
        /* новые коэфф. = b[1], b[2], ..., b[cur_degree] */

        double *b = (double *)malloc((size_t)(cur_degree + 1) * sizeof(double));
        if (b == NULL) {
            free(f);
            free(*result);
            *result = NULL;
            return ALLOC_ERROR;
        }

        b[cur_degree] = f[cur_degree];
        for (int i = cur_degree - 1; i >= 0; i--) {
            b[i] = f[i] + a * b[i + 1];
        }

        (*result)[k] = b[0];
        if (fabs((*result)[k]) < eps) {
            (*result)[k] = 0.0;
        }

        /* Обновляем f: f[i] = b[i+1] для i = 0..cur_degree-1 */
        for (int i = 0; i < cur_degree; i++) {
            f[i] = b[i + 1];
        }
        cur_degree--;

        free(b);
    }

    free(f);
    return OK;
}

/* ========================= main (демонстрация) ========================= */

int main(void) {
    double *g = NULL;
    status_code sc;

    printf("=== Polynomial re-expansion ===\n\n");

    /* Пример 1: f(x) = 1 + 2x + 3x^2, a = 1
     * f(x) = 1 + 2x + 3x^2
     * f(1) = 6, f'(1) = 8, f''(1)/2! = 3
     * g(x-1) = 6 + 8(x-1) + 3(x-1)^2 */
    printf("f(x) = 1 + 2x + 3x^2, a = 1\n");
    sc = polynomial_reexpand(1e-9, 1.0, &g, 2, 1.0, 2.0, 3.0);
    if (sc == OK) {
        printf("g(x-a): ");
        for (int i = 0; i <= 2; i++) {
            printf("g[%d] = %.4f  ", i, g[i]);
        }
        printf("\n");
        /* Проверка: g0=6, g1=8, g2=3 */
        free(g);
    }

    /* Пример 2: f(x) = x^3, a = 2
     * f(x) = 0 + 0*x + 0*x^2 + 1*x^3
     * (x)^3 = ((x-2)+2)^3 = (x-2)^3 + 3*2*(x-2)^2 + 3*4*(x-2) + 8
     * g0=8, g1=12, g2=6, g3=1 */
    printf("\nf(x) = x^3, a = 2\n");
    sc = polynomial_reexpand(1e-9, 2.0, &g, 3, 0.0, 0.0, 0.0, 1.0);
    if (sc == OK) {
        printf("g(x-a): ");
        for (int i = 0; i <= 3; i++) {
            printf("g[%d] = %.4f  ", i, g[i]);
        }
        printf("\n");
        free(g);
    }

    /* Пример 3: f(x) = 5, a = 3 (константа) */
    printf("\nf(x) = 5, a = 3\n");
    sc = polynomial_reexpand(1e-9, 3.0, &g, 0, 5.0);
    if (sc == OK) {
        printf("g(x-a): g[0] = %.4f\n", g[0]);
        free(g);
    }

    /* Пример 4: f(x) = 1 + x + x^2 + x^3 + x^4, a = 1 */
    printf("\nf(x) = 1 + x + x^2 + x^3 + x^4, a = 1\n");
    sc = polynomial_reexpand(1e-9, 1.0, &g, 4, 1.0, 1.0, 1.0, 1.0, 1.0);
    if (sc == OK) {
        printf("g(x-a): ");
        for (int i = 0; i <= 4; i++) {
            printf("g[%d] = %.4f  ", i, g[i]);
        }
        printf("\n");
        /* f(1)=5, f'(1)=1+2+3+4=10, f''(1)/2=1+3+6=10, f'''(1)/6=1+4=5... нет, проверим */
        /* f'(x) = 1+2x+3x^2+4x^3, f'(1)=10
         * f''(x) = 2+6x+12x^2, f''(1)=20, /2=10
         * f'''(x) = 6+24x, f'''(1)=30, /6=5
         * f''''(x) = 24, /24=1
         * g = 5, 10, 10, 5, 1 */
        free(g);
    }

    return 0;
}
