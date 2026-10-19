/*
 * Лабораторная 2, задание 7. Самые длинные векторы по разным нормам.
 *
 * Дано: размерность пространства n, несколько векторов из n-мерного пространства
 * и несколько норм (способов измерить длину вектора). Для каждой нормы нужно
 * найти самый длинный вектор; если таких несколько (одинаковая длина) - все.
 *
 * Нормы (x = (x1, ..., xn)):
 *   ||x||_inf = max |x_j|                              - самая большая координата по модулю;
 *   ||x||_p   = (|x1|^p + ... + |xn|^p)^(1/p), p >= 1  - p = 2 - обычная длина, p = 1 - сумма модулей;
 *   ||x||_A   = sqrt((Ax, x))                          - A - положительно определенная матрица n x n.
 * Пример: x = (3, -4):  inf -> 4,  p = 1 -> 7,  p = 2 -> 5.
 *
 * Запуск:  lab2_task7                                        - демонстрация (набор готовых примеров)
 *          lab2_task7 <eps> <n> <вектор>... <норма>...      - свои векторы и нормы
 *            вектор - n чисел через запятую:        3,-4
 *            норма  - inf | p=<число> | A=<n*n чисел через запятую, по строкам>
 *          например: lab2_task7 1e-9 2 3,4 -4,3 5,0 inf p=1 p=2 A=2,1,1,2
 *
 * Главная функция задания - find_longest с переменным числом аргументов (...):
 *   find_longest(&result, eps, n, vector_count, norm_count,
 *                v1, v2, ..., f1, param1, f2, param2, ...)
 * Векторы передаются как экземпляры структуры vector (по значению), нормы - парами:
 * указатель на функцию нормы и указатель на ее параметр (p или матрицу A; NULL для inf).
 * Все функции норм имеют один и тот же тип norm_func - это и есть "универсальный тип
 * указателя на функцию": функция find_longest вызывает любую норму одинаково, а то,
 * чем нормы отличаются (p, матрица), спрятано за указателем const void *param.
 *
 * Что такое "самый длинный" при сравнении через eps: вектор самый длинный, если ни один
 * другой вектор не длиннее его больше чем на eps. Так находятся сразу все "равные"
 * самые длинные, и не нужно сравнивать вещественные числа без eps.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free, strtod */
#include <stdarg.h>   /* va_list, va_start, va_arg, va_end - работа с переменным числом аргументов */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */
#include <limits.h>   /* INT_MAX - самое большое int */
#include <ctype.h>    /* isspace */
#include <math.h>     /* fabs, fmax, pow, sqrt, isfinite */

/* Точность для демонстрации: длины, которые отличаются меньше чем на EPS_DEMO,
   считаем равными. Это не глобальная переменная, а просто имя для числа:
   в функции eps всегда передается параметром */
#define EPS_DEMO 1e-9

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,           /* передали NULL (указатель на ответ или на функцию нормы) */
    INVALID_EPS,            /* eps <= 0 (или не число) */
    INVALID_DIMENSION,      /* размерность n меньше 1 */
    INVALID_COUNT,          /* векторов или норм меньше 1 */
    INVALID_VECTOR,         /* у вектора не та размерность, нет координат или координата inf/nan */
    INVALID_NUMBER,         /* строка из командной строки - не число (или не то количество чисел) */
    INVALID_PARAM,          /* параметр нормы плохой: p < 1, p - не число, нет p или матрицы */
    BAD_MATRIX_SIZE,        /* матрица не n x n */
    NOT_SYMMETRIC,          /* матрица несимметричная: a[i][j] != a[j][i] */
    NOT_POSITIVE_DEFINITE,  /* матрица не положительно определенная */
    OVERFLOW_ERROR,         /* длина получилась слишком большой (бесконечность) */
    ALLOC_ERROR             /* malloc не смог выделить память */
} status_code;

/* Вектор: размерность и адрес массива координат (координаты лежат у вызывающего,
   структура их только "показывает"). Такую структуру можно передать в ... по значению */
typedef struct {
    int n;
    const double *coords;
} vector;

/* Матрица n x n для нормы ||x||_A: элементы по строкам, a[i * n + j] - строка i, столбец j */
typedef struct {
    int n;
    const double *a;
} matrix;

/* Универсальный тип указателя на функцию нормы. Все нормы устроены одинаково:
     v     - вектор;
     param - параметр нормы: адрес числа p (norm_p), адрес матрицы (norm_a), NULL (norm_inf);
             тип const void * - "указатель на что угодно", каждая норма сама знает, что там;
     eps   - точность (нужна только norm_a для проверки матрицы, остальные ее не используют,
             но параметр есть у всех: тип функции у всех норм должен быть один);
     result   - сюда кладем длину вектора.
   Возвращает статус: OK или ошибку параметра нормы */
typedef status_code (*norm_func)(const vector *v, const void *param, const double eps, double *result);

/* Норма вместе со своим параметром (так нормы хранятся внутри, после чтения из ...) */
typedef struct {
    norm_func func;
    const void *param;
} norm;

/* Один найденный самый длинный вектор */
typedef struct {
    int index;       /* номер вектора в том порядке, как их передали (с 0) */
    vector v;        /* сам вектор (копия структуры; координаты - те же, что передали) */
    double length;    /* его длина в этой норме */
} longest_vector;

/* Результат для одной нормы */
typedef struct {
    status_code status;       /* OK или ошибка этой нормы (p < 1, плохая матрица, ...) */
    int count;                /* сколько векторов самые длинные (0 при ошибке) */
    longest_vector *list;   /* они сами, в порядке передачи (NULL при ошибке) */
} norm_result;

/* ===================== нормы ===================== */

/* Проверка вектора: координаты есть, их ровно n (n >= 1) */
status_code check_vector(const vector *v) {
    if (v == NULL) {
        return INVALID_ARGS;
    }
    if (v->coords == NULL || v->n < 1) {
        return INVALID_VECTOR;
    }
    return OK;
}

/* Самая большая по модулю координата: max |x_j|.
   Максимум считаем функцией fmax из math.h (C99): fmax(a, b) - большее из двух чисел.
   Так в коде нет сравнения вещественных чисел - это просто вычисление значения */
status_code max_abs(const vector *v, double *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_vector(v);
    if (sc != OK) {
        return sc;
    }
    double max_value = 0.0;
    for (int j = 0; j < v->n; j++) {
        max_value = fmax(max_value, fabs(v->coords[j]));
    }
    *result = max_value;
    return OK;
}

/* Масштаб для норм p и A. Все нормы "однородные": ||c * x|| = |c| * ||x||. Поэтому
   можно поделить координаты на m = max |x_j|, посчитать норму маленького вектора
   (все его координаты от -1 до 1) и умножить ответ обратно на m:
       ||x|| = m * ||x / m||.
   Зачем: при x = (1e200, 1e200) и p = 2 квадрат 1e400 не помещается в double
   (максимум ~1.8e308) и получилась бы бесконечность, хотя сама длина 1.41e200 нормальная.
   А при большом p (например 1000) числа меньше 1 в степени p превращаются в 0.
   После деления на m самая большая координата равна 1, и этих бед нет.
   Если m < eps (все координаты почти 0), делить на m нельзя (может быть ровно 0) -
   тогда масштаб 1, то есть считаем как есть: числа маленькие, переполнения не будет,
   а длина такого вектора все равно меньше n * eps */
status_code get_scale(const vector *v, const double eps, double *scale) {
    if (scale == NULL) {
        return INVALID_ARGS;
    }
    double m = 0.0;
    status_code sc = max_abs(v, &m);
    if (sc != OK) {
        return sc;
    }
    *scale = (m < eps) ? 1.0 : m;
    return OK;
}

/* ||x||_inf = max |x_j| - самая большая по модулю координата.
   param и eps этой норме не нужны: (void)param говорит компилятору "не используется
   специально", чтобы не было предупреждения. Параметры все равно есть: тип функции
   у всех норм должен быть одинаковым (norm_func) */
status_code norm_inf(const vector *v, const void *param, const double eps, double *result) {
    (void)param;
    (void)eps;
    return max_abs(v, result);
}

/* ||x||_p = (|x1|^p + ... + |xn|^p)^(1/p),  p >= 1.  param - адрес числа p (const double *).
   p < 1 не норма: например при p = 0.5 не выполняется неравенство треугольника.
   pow(a, b) - a в степени b (math.h).
   Координаты сначала делим на масштаб (get_scale), ответ умножаем обратно.
   OVERFLOW_ERROR - только если сама длина больше максимального double */
status_code norm_p(const vector *v, const void *param, const double eps, double *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_vector(v);
    if (sc != OK) {
        return sc;
    }
    if (param == NULL) {
        return INVALID_PARAM;
    }
    /* const void * -> const double *: мы знаем, что для этой нормы там лежит число p.
       Сравнение p < 1 - с точной границей из определения нормы, eps тут не нужен */
    const double p = *(const double *)param;
    if (!isfinite(p) || p < 1) {
        return INVALID_PARAM;
    }
    double scale = 1.0;
    sc = get_scale(v, eps, &scale);
    if (sc != OK) {
        return sc;
    }
    double sum = 0.0;
    for (int j = 0; j < v->n; j++) {
        sum = sum + pow(fabs(v->coords[j]) / scale, p);
    }
    double length = scale * pow(sum, 1.0 / p);
    /* isfinite: не бесконечность и не NaN */
    if (!isfinite(length)) {
        return OVERFLOW_ERROR;
    }
    *result = length;
    return OK;
}

/* Разложение Холецкого: симметричная положительно определенная матрица A = L * L^T,
   где L - нижняя треугольная (над диагональю нули), на диагонали L - положительные числа.
   Формулы (идем по столбцам j = 0, 1, ..., n - 1):
       s = a[j][j] - (L[j][0]^2 + ... + L[j][j-1]^2),   L[j][j] = sqrt(s);
       L[i][j] = (a[i][j] - (L[i][0]*L[j][0] + ... + L[i][j-1]*L[j][j-1])) / L[j][j],  i > j.
   Главное свойство: такое разложение существует ТОГДА И ТОЛЬКО ТОГДА, когда матрица
   положительно определенная. Если на каком-то шаге s <= 0 (у нас: s < eps), из s нельзя
   извлечь корень - значит A не положительно определенная.
   Пример: A = [[2, 1], [1, 2]]:  L[0][0] = sqrt(2),  L[1][0] = 1 / sqrt(2),
           s = 2 - 1/2 = 1.5 > 0,  L[1][1] = sqrt(1.5)  -> положительно определенная.
           A = [[1, 2], [2, 1]]:  L[0][0] = 1,  L[1][0] = 2,  s = 1 - 4 = -3 < 0 -> нет.
   L - массив n * n в динамической памяти (освобождает вызывающий). Сначала проверяем
   симметричность: |a[i][j] - a[j][i]| < eps */
status_code cholesky(const matrix *m, const double eps, double **result) {
    if (m == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    if (m->a == NULL || m->n < 1) {
        return INVALID_PARAM;
    }
    if (!isfinite(eps) || eps <= 0) {
        return INVALID_EPS;
    }
    const int n = m->n;
    const double *a = m->a;
    /* все элементы - обычные числа (не inf и не nan) */
    for (int i = 0; i < n * n; i++) {
        if (!isfinite(a[i])) {
            return INVALID_PARAM;
        }
    }
    /* симметричность: элемент строки i столбца j равен элементу строки j столбца i */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (fabs(a[i * n + j] - a[j * n + i]) >= eps) {
                return NOT_SYMMETRIC;
            }
        }
    }
    /* calloc обнуляет память: элементы над диагональю L так и останутся нулями */
    double *L = (double *)calloc((size_t)n * (size_t)n, sizeof(double));
    if (L == NULL) {
        return ALLOC_ERROR;
    }
    for (int j = 0; j < n; j++) {
        /* диагональный элемент: s = a[j][j] - сумма квадратов уже найденных L[j][k] */
        double s = a[j * n + j];
        for (int k = 0; k < j; k++) {
            s = s - L[j * n + k] * L[j * n + k];
        }
        if (s < eps) {
            free(L);
            return NOT_POSITIVE_DEFINITE;
        }
        L[j * n + j] = sqrt(s);
        /* элементы столбца j ниже диагонали */
        for (int i = j + 1; i < n; i++) {
            double t = a[i * n + j];
            for (int k = 0; k < j; k++) {
                t = t - L[i * n + k] * L[j * n + k];
            }
            L[i * n + j] = t / L[j * n + j];
        }
    }
    *result = L;
    return OK;
}

/* ||x||_A = sqrt((Ax, x)),  param - адрес матрицы (const matrix *).
   (Ax, x) - скалярное произведение вектора Ax на x: сумма по i, j  a[i][j] * x_i * x_j.
   Считаем через разложение Холецкого A = L * L^T:
       (Ax, x) = (L L^T x, x) = (L^T x, L^T x) = |y|^2,  где y = L^T x,
   то есть ||x||_A = обычная длина вектора y. Плюсы: cholesky заодно проверяет, что A
   положительно определенная (иначе это не норма), а сумма квадратов |y|^2 никогда не
   бывает отрицательной из-за погрешностей - корень из нее извлекается всегда.
   y_j = (L^T x)_j = сумма по i >= j:  L[i][j] * x_i  (столбец j матрицы L на x).
   Как и в norm_p, координаты делим на масштаб, а ответ умножаем на него обратно */
status_code norm_a(const vector *v, const void *param, const double eps, double *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_vector(v);
    if (sc != OK) {
        return sc;
    }
    if (param == NULL) {
        return INVALID_PARAM;
    }
    /* const void * -> const matrix *: для этой нормы там лежит матрица */
    const matrix *m = (const matrix *)param;
    if (m->n != v->n) {
        return BAD_MATRIX_SIZE;
    }
    double scale = 1.0;
    sc = get_scale(v, eps, &scale);
    if (sc != OK) {
        return sc;
    }
    double *L = NULL;
    sc = cholesky(m, eps, &L);
    if (sc != OK) {
        return sc;
    }
    const int n = v->n;
    double sum = 0.0;
    for (int j = 0; j < n; j++) {
        double y = 0.0;
        for (int i = j; i < n; i++) {
            y = y + L[i * n + j] * (v->coords[i] / scale);
        }
        sum = sum + y * y;
    }
    free(L);
    double length = scale * sqrt(sum);
    if (!isfinite(length)) {
        return OVERFLOW_ERROR;
    }
    *result = length;
    return OK;
}

/* ===================== поиск самых длинных ===================== */

/* Освободить результаты: сначала список в каждой норме, потом сам массив.
   free(NULL) ничего не делает, поэтому NULL (ошибка нормы, result == NULL) - не ошибка */
status_code free_results(norm_result *result, const int norm_count) {
    if (result == NULL) {
        return OK;
    }
    for (int i = 0; i < norm_count; i++) {
        free(result[i].list);
    }
    free(result);
    return OK;
}

/* Самые длинные векторы для одной нормы: заполняет *nr.
   1) считаем длину каждого вектора (массив lengths);
   2) вектор i - самый длинный, если нет вектора j, который длиннее его больше чем
      на eps:  lengths[j] - lengths[i] > eps. Сравниваем каждый с каждым (count * count
      сравнений) - зато только через eps и без "точного максимума". Порядок векторов
      на ответ не влияет.
   Пример, eps = 1e-9: длины 5, 5, 3  -> самые длинные первый и второй (никто не длиннее 5
   больше чем на eps), третий - нет (5 - 3 > eps).
   Ошибка нормы (плохое p, матрица) записывается в nr->status, функция возвращает OK:
   это ошибка одной нормы, а не всей задачи. ALLOC_ERROR - возвращается */
status_code longest_for_norm(const vector *vectors, const int vector_count, const norm *nm,
                             const double eps, norm_result *nr) {
    if (vectors == NULL || nm == NULL || nr == NULL) {
        return INVALID_ARGS;
    }
    nr->status = OK;
    nr->count = 0;
    nr->list = NULL;

    double *lengths = (double *)malloc((size_t)vector_count * sizeof(double));
    if (lengths == NULL) {
        return ALLOC_ERROR;
    }
    /* 1) длины всех векторов. Вызов через указатель на функцию: nm->func(...) вызывает
       ту норму, адрес которой нам передали (norm_inf, norm_p или norm_a) */
    for (int i = 0; i < vector_count; i++) {
        status_code sc = nm->func(&vectors[i], nm->param, eps, &lengths[i]);
        if (sc == ALLOC_ERROR) {
            free(lengths);
            return ALLOC_ERROR;
        }
        if (sc != OK) {
            /* ошибка нормы одинакова для всех векторов (плохое p или матрица) -
               запоминаем и дальше не считаем */
            nr->status = sc;
            free(lengths);
            return OK;
        }
    }

    /* 2) какие самые длинные: is_longest[i] = 1, если никто не длиннее i больше чем на eps */
    int *is_longest = (int *)malloc((size_t)vector_count * sizeof(int));
    if (is_longest == NULL) {
        free(lengths);
        return ALLOC_ERROR;
    }
    int count = 0;
    for (int i = 0; i < vector_count; i++) {
        is_longest[i] = 1;
        for (int j = 0; j < vector_count; j++) {
            if (lengths[j] - lengths[i] > eps) {
                is_longest[i] = 0;
            }
        }
        if (is_longest[i]) {
            count++;
        }
    }
    /* хотя бы один самый длинный всегда есть: у вектора с наибольшей длиной
       никто не длиннее, поэтому count >= 1 */

    /* 3) переписываем самые длинные в список ответа */
    longest_vector *list = (longest_vector *)malloc((size_t)count * sizeof(longest_vector));
    if (list == NULL) {
        free(is_longest);
        free(lengths);
        return ALLOC_ERROR;
    }
    int k = 0;
    for (int i = 0; i < vector_count; i++) {
        if (is_longest[i]) {
            list[k].index = i;
            list[k].v = vectors[i];
            list[k].length = lengths[i];
            k++;
        }
    }
    free(is_longest);
    free(lengths);
    nr->count = count;
    nr->list = list;
    return OK;
}

/* Общая часть: векторы и нормы уже в массивах. Используется и функцией с ...
   (find_longest), и режимом командной строки (там количество векторов и норм
   известно только во время работы, а список ... пишется в коде заранее).
   Проверяет все параметры, потом для каждой нормы вызывает longest_for_norm.
   *result - массив из norm_count результатов (освобождать free_results) */
status_code find_longest_array(norm_result **result, const double eps, const int n,
                               const vector *vectors, const int vector_count,
                               const norm *norms, const int norm_count) {
    if (result == NULL || vectors == NULL || norms == NULL) {
        return INVALID_ARGS;
    }
    if (!isfinite(eps) || eps <= 0) {
        return INVALID_EPS;
    }
    if (n < 1) {
        return INVALID_DIMENSION;
    }
    if (vector_count < 1 || norm_count < 1) {
        return INVALID_COUNT;
    }
    /* каждый вектор: размерность ровно n, координаты есть и все - обычные числа */
    for (int i = 0; i < vector_count; i++) {
        if (vectors[i].n != n || vectors[i].coords == NULL) {
            return INVALID_VECTOR;
        }
        for (int j = 0; j < n; j++) {
            if (!isfinite(vectors[i].coords[j])) {
                return INVALID_VECTOR;
            }
        }
    }
    /* у каждой нормы есть функция (вызов функции по адресу NULL - аварийное завершение) */
    for (int i = 0; i < norm_count; i++) {
        if (norms[i].func == NULL) {
            return INVALID_ARGS;
        }
    }
    /* Размер памяти count * sizeof(...) не должен переполнить size_t. Проверка через
       деление - то же, что "count * sizeof > SIZE_MAX", но без умножения, которое само
       могло бы переполниться (на 64-битной системе int для этого слишком мал) */
    if ((size_t)norm_count > SIZE_MAX / sizeof(norm_result) ||
        (size_t)vector_count > SIZE_MAX / sizeof(longest_vector)) {
        return ALLOC_ERROR;
    }

    norm_result *arr = (norm_result *)malloc((size_t)norm_count * sizeof(norm_result));
    if (arr == NULL) {
        return ALLOC_ERROR;
    }
    /* сразу list = NULL во всех: если память кончится на середине, free_results
       освободит готовые списки, а для остальных сделает free(NULL) */
    for (int i = 0; i < norm_count; i++) {
        arr[i].list = NULL;
    }
    for (int i = 0; i < norm_count; i++) {
        status_code sc = longest_for_norm(vectors, vector_count, &norms[i], eps, &arr[i]);
        if (sc != OK) {
            /* сюда попадает только ALLOC_ERROR: ошибки норм записаны в arr[i].status */
            free_results(arr, norm_count);
            return sc;
        }
    }
    *result = arr;
    return OK;
}

/* ===================== функция с переменным числом аргументов ===================== */

/* Для каждой нормы найти самые длинные векторы:
     find_longest(&result, 1e-9, 2, 3, 2,  v1, v2, v3,  norm_inf, (const void *)NULL,
                                                      norm_p, (const void *)&p)
   result        - сюда кладем адрес массива из norm_count результатов в динамической памяти
                (освобождать функцией free_results(result, norm_count));
   eps        - точность: длины, отличающиеся меньше чем на eps, считаются равными, eps > 0;
   n          - размерность пространства, n >= 1;
   vector_count - сколько векторов дальше, >= 1;
   norm_count - сколько норм после векторов, >= 1;
   ...        - сначала vector_count структур vector (по значению), потом norm_count пар
                "функция нормы (norm_func), ее параметр (const void *)".
   Порядок как в условии: размерность, векторы, нормы. Оба количества стоят перед ...:
   функция только из них знает, сколько аргументов и какого типа читать.

   ВАЖНО про типы в ...: функция не может узнать тип аргумента, va_arg просто читает
   байты как указанный тип. Поэтому:
   - векторы - именно структуры vector;
   - параметр нормы - именно const void *: писать (const void *)&p и (const void *)NULL.
     Просто NULL нельзя: NULL может быть определен как 0 (int), а int и указатель
     могут иметь разный размер (4 и 8 байт) - прочитается мусор. А double * и
     void * стандарт не обязывает передавать в ... одинаково, поэтому тоже приводим.
   Ошибка одной нормы (p < 1, несимметричная матрица, ...) не останавливает остальные:
   она записывается в result[i].status */
status_code find_longest(norm_result **result, const double eps, const int n, const int vector_count,
                         const int norm_count, ...) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    if (vector_count < 1 || norm_count < 1) {
        return INVALID_COUNT;
    }
    if ((size_t)vector_count > SIZE_MAX / sizeof(vector) ||
        (size_t)norm_count > SIZE_MAX / sizeof(norm)) {
        return ALLOC_ERROR;
    }
    /* Аргументы из ... можно прочитать только один раз и только по порядку, а векторы
       нужны для каждой нормы заново - поэтому сначала переписываем все в массивы */
    vector *vectors = (vector *)malloc((size_t)vector_count * sizeof(vector));
    if (vectors == NULL) {
        return ALLOC_ERROR;
    }
    norm *norms = (norm *)malloc((size_t)norm_count * sizeof(norm));
    if (norms == NULL) {
        free(vectors);
        return ALLOC_ERROR;
    }

    /* va_list - "указатель" на следующий безымянный аргумент.
       va_start(args, norm_count) ставит его сразу после параметра norm_count */
    va_list args;
    va_start(args, norm_count);
    for (int i = 0; i < vector_count; i++) {
        /* va_arg достает следующий аргумент как структуру vector (копия по значению) */
        vectors[i] = va_arg(args, vector);
    }
    for (int i = 0; i < norm_count; i++) {
        /* пара: адрес функции нормы, потом адрес ее параметра */
        norms[i].func = va_arg(args, norm_func);
        norms[i].param = va_arg(args, const void *);
    }
    /* va_end обязателен после va_start; дальше аргументы не нужны */
    va_end(args);

    /* сама работа - общая с режимом командной строки. В ответе копии структур vector,
       массивы vectors и norms больше не нужны */
    status_code sc = find_longest_array(result, eps, n, vectors, vector_count, norms, norm_count);
    free(norms);
    free(vectors);
    return sc;
}

/* ===================== ввод (только для main) ===================== */

/* Строка -> double. Разрешены обычные записи: 3, -2.5, 1e-9, .5
   strtod(str, &end) читает число с начала строки, в end - адрес первого непрочитанного
   символа; *end != '\0' - после числа мусор. Пробел в начале запрещаем сами
   (strtod его пропустил бы). inf, nan, 1e999 - не обычные числа -> INVALID_NUMBER */
status_code parse_double(const char *str, double *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    /* isspace принимает unsigned char: русская буква в char отрицательна */
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }
    char *end = NULL;
    double value = strtod(str, &end);
    if (end == str || *end != '\0' || !isfinite(value)) {
        return INVALID_NUMBER;
    }
    *number = value;
    return OK;
}

/* Размерность n из командной строки: только цифры, от 1 до INT_MAX.
   Число собираем вручную; перед "value * 10 + digit" проверяем, что не выйдем
   за INT_MAX:  value * 10 + digit > INT_MAX  <=>  value > (INT_MAX - digit) / 10 */
status_code parse_dimension(const char *str, int *n) {
    if (str == NULL || n == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_DIMENSION;
    }
    int value = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_DIMENSION;
        }
        int digit = str[i] - '0';
        if (value > (INT_MAX - digit) / 10) {
            return INVALID_DIMENSION;
        }
        value = value * 10 + digit;
    }
    if (value < 1) {
        return INVALID_DIMENSION;
    }
    *n = value;
    return OK;
}

/* Сколько чисел в списке через запятую ("3,-4,5" -> 3): запятых + 1.
   Правильные ли сами числа, проверит parse_list */
status_code count_list(const char *str, size_t *count) {
    if (str == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    size_t k = 1;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == ',') {
            k++;
        }
    }
    *count = k;
    return OK;
}

/* Список ровно из count чисел через запятую -> массив result (место выделил вызывающий).
   После каждого числа должна быть запятая (если число не последнее) или конец строки.
   Пустое место (",,", запятая в конце), пробелы, мусор, inf/nan - INVALID_NUMBER */
status_code parse_list(const char *str, const size_t count, double *result) {
    if (str == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    const char *cur = str;   /* где начинается очередное число */
    for (size_t i = 0; i < count; i++) {
        if (*cur == '\0' || isspace((unsigned char)*cur)) {
            return INVALID_NUMBER;
        }
        char *end = NULL;
        double value = strtod(cur, &end);
        if (end == cur || !isfinite(value)) {
            return INVALID_NUMBER;
        }
        /* после последнего числа - конец строки, после остальных - запятая */
        if (i + 1 == count) {
            if (*end != '\0') {
                return INVALID_NUMBER;
            }
        } else {
            if (*end != ',') {
                return INVALID_NUMBER;
            }
            end++;   /* перешагиваем запятую */
        }
        result[i] = value;
        cur = end;
    }
    return OK;
}

/* Вид аргумента командной строки */
typedef enum {
    ARG_VECTOR,   /* "3,-4" */
    ARG_INF,      /* "inf" */
    ARG_P,        /* "p=2" */
    ARG_A         /* "A=2,1,1,2" */
} arg_kind;

/* Определить вид аргумента по началу строки: "inf", "p=...", "A=..." - нормы,
   все остальное - вектор (ошибки в нем найдет parse_list) */
status_code kind_of_arg(const char *str, arg_kind *kind) {
    if (str == NULL || kind == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == 'i' && str[1] == 'n' && str[2] == 'f' && str[3] == '\0') {
        *kind = ARG_INF;
    } else if (str[0] == 'p' && str[1] == '=') {
        *kind = ARG_P;
    } else if (str[0] == 'A' && str[1] == '=') {
        *kind = ARG_A;
    } else {
        *kind = ARG_VECTOR;
    }
    return OK;
}

/* Все, что main прочитал из командной строки (вся память - здесь, освобождает free_cli) */
typedef struct {
    double eps;
    int n;
    vector *vectors;         /* векторы; их координаты - в coords */
    int vector_count;
    double *coords;        /* координаты всех векторов подряд: vector_count * n чисел */
    norm *norms;         /* нормы; их параметры - в p_values и matrices */
    int norm_count;
    double *p_values;      /* числа p (по одному месту на каждую норму, нужны только для p=) */
    matrix *matrices;     /* матрицы (тоже по месту на каждую норму) */
    double *coefs;         /* элементы всех матриц подряд: matrix_count * n * n чисел */
} cli_data;

/* Освободить память cli_data. free(NULL) ничего не делает - можно звать в любой момент */
status_code free_cli(cli_data *d) {
    if (d == NULL) {
        return INVALID_ARGS;
    }
    free(d->vectors);
    free(d->coords);
    free(d->norms);
    free(d->p_values);
    free(d->matrices);
    free(d->coefs);
    d->vectors = NULL;
    d->coords = NULL;
    d->norms = NULL;
    d->p_values = NULL;
    d->matrices = NULL;
    d->coefs = NULL;
    return OK;
}

/* Разбор векторов и норм из argv[3 .. argc-1] в *d (eps и n уже разобраны).
   Два прохода: первый только считает векторы, нормы и матрицы и проверяет, что в каждом
   векторе n чисел, а в матрице n * n (тогда память выделяется ровно по размеру и только
   после проверки - огромное n из командной строки не вызовет огромный malloc);
   второй выделяет память и читает числа.
   *bad_arg - номер аргумента с ошибкой (для сообщения в main) */
status_code parse_cli(const int argc, char *argv[], cli_data *d, int *bad_arg) {
    if (argv == NULL || d == NULL || bad_arg == NULL) {
        return INVALID_ARGS;
    }
    const int n = d->n;
    int vector_count = 0;
    int norm_count = 0;
    int matrix_count = 0;

    /* проход 1: считаем и проверяем количество чисел */
    for (int i = 3; i < argc; i++) {
        arg_kind kind;
        kind_of_arg(argv[i], &kind);
        *bad_arg = i;
        if (kind == ARG_VECTOR) {
            size_t k = 0;
            count_list(argv[i], &k);
            if (k != (size_t)n) {
                return INVALID_VECTOR;
            }
            vector_count++;
        } else {
            if (kind == ARG_A) {
                /* матрица n x n: n * n чисел. (size_t)n * n не переполнится:
                   n <= INT_MAX, а size_t на 64-битной системе намного больше */
                size_t k = 0;
                count_list(argv[i] + 2, &k);
                if (k != (size_t)n * (size_t)n) {
                    return BAD_MATRIX_SIZE;
                }
                matrix_count++;
            }
            norm_count++;
        }
    }
    if (vector_count < 1 || norm_count < 1) {
        *bad_arg = 0;
        return INVALID_COUNT;
    }

    /* выделяем память. Размеры - не больше длины командной строки (каждое число -
       хотя бы один символ), поэтому переполнения size_t тут нет */
    d->vectors = (vector *)malloc((size_t)vector_count * sizeof(vector));
    d->coords = (double *)malloc((size_t)vector_count * (size_t)n * sizeof(double));
    d->norms = (norm *)malloc((size_t)norm_count * sizeof(norm));
    d->p_values = (double *)malloc((size_t)norm_count * sizeof(double));
    d->matrices = (matrix *)malloc((size_t)norm_count * sizeof(matrix));
    /* матриц может не быть совсем - тогда хотя бы 1 элемент, чтобы malloc(0)
       не вернул NULL (это не ошибка памяти, а так malloc(0) имеет право делать) */
    size_t coef_size = (size_t)matrix_count * (size_t)n * (size_t)n;
    d->coefs = (double *)malloc((coef_size > 0 ? coef_size : 1) * sizeof(double));
    if (d->vectors == NULL || d->coords == NULL || d->norms == NULL || d->p_values == NULL ||
        d->matrices == NULL || d->coefs == NULL) {
        *bad_arg = 0;
        return ALLOC_ERROR;
    }

    /* проход 2: читаем числа. i_vector, i_norm, i_matrix - сколько уже прочитали */
    int i_vector = 0;
    int i_norm = 0;
    int i_matrix = 0;
    for (int i = 3; i < argc; i++) {
        arg_kind kind;
        kind_of_arg(argv[i], &kind);
        *bad_arg = i;
        if (kind == ARG_VECTOR) {
            /* координаты вектора i_vector - в coords с места i_vector * n */
            double *place = d->coords + (size_t)i_vector * (size_t)n;
            if (parse_list(argv[i], (size_t)n, place) != OK) {
                return INVALID_VECTOR;
            }
            d->vectors[i_vector].n = n;
            d->vectors[i_vector].coords = place;
            i_vector++;
        } else if (kind == ARG_INF) {
            d->norms[i_norm].func = norm_inf;
            d->norms[i_norm].param = NULL;
            i_norm++;
        } else if (kind == ARG_P) {
            /* число после "p=". p < 1 тут не ошибка разбора: это проверит сама norm_p */
            if (parse_double(argv[i] + 2, &d->p_values[i_norm]) != OK) {
                return INVALID_PARAM;
            }
            d->norms[i_norm].func = norm_p;
            d->norms[i_norm].param = &d->p_values[i_norm];
            i_norm++;
        } else {
            /* элементы матрицы i_matrix - в coefs с места i_matrix * n * n */
            double *place = d->coefs + (size_t)i_matrix * (size_t)n * (size_t)n;
            if (parse_list(argv[i] + 2, (size_t)n * (size_t)n, place) != OK) {
                return BAD_MATRIX_SIZE;
            }
            d->matrices[i_norm].n = n;
            d->matrices[i_norm].a = place;
            d->norms[i_norm].func = norm_a;
            d->norms[i_norm].param = &d->matrices[i_norm];
            i_norm++;
            i_matrix++;
        }
    }
    d->vector_count = vector_count;
    d->norm_count = norm_count;
    return OK;
}

/* ===================== вывод (только для main) ===================== */

/* Текст ошибки для статуса (строковые литералы живут всю программу - отдавать их адрес можно) */
status_code error_text(const status_code sc, const char **message) {
    if (message == NULL) {
        return INVALID_ARGS;
    }
    if (sc == INVALID_ARGS) {
        /* в демонстрации это пример "NULL вместо функции нормы" */
        *message = "a pointer is NULL (no norm function)";
    } else if (sc == INVALID_EPS) {
        *message = "eps must be greater than 0";
    } else if (sc == INVALID_DIMENSION) {
        *message = "dimension n must be 1 or more";
    } else if (sc == INVALID_COUNT) {
        *message = "give at least one vector and one norm";
    } else if (sc == INVALID_VECTOR) {
        *message = "every vector must have n usual numbers";
    } else if (sc == INVALID_PARAM) {
        *message = "p must be a usual number 1 or more";
    } else if (sc == BAD_MATRIX_SIZE) {
        *message = "matrix A must be n x n";
    } else if (sc == NOT_SYMMETRIC) {
        *message = "matrix A must be symmetric";
    } else if (sc == NOT_POSITIVE_DEFINITE) {
        *message = "matrix A must be positive definite";
    } else if (sc == OVERFLOW_ERROR) {
        *message = "the length is too big";
    } else if (sc == ALLOC_ERROR) {
        *message = "not enough memory";
    } else {
        /* INVALID_NUMBER сюда из main и демонстрации прийти не может: его main
           обрабатывает сам. Если сообщение появилось - ошибка в самой программе */
        *message = "internal error";
    }
    return OK;
}

/* Печать вектора: "(3, -4)". Числа через %.10g: до 10 значащих цифр, без лишних нулей */
status_code print_vector(const vector *v) {
    if (v == NULL || v->coords == NULL) {
        return INVALID_ARGS;
    }
    printf("(");
    for (int j = 0; j < v->n; j++) {
        if (j > 0) {
            printf(", ");
        }
        printf("%.10g", v->coords[j]);
    }
    printf(")");
    return OK;
}

/* Название нормы: "inf", "p = 2", "A = [[2, 1], [1, 2]]".
   Какая это норма, узнаем сравнением адресов функций (адреса функций сравнивать
   через == можно - это не вещественные числа) */
status_code print_norm_name(const norm *nm) {
    if (nm == NULL) {
        return INVALID_ARGS;
    }
    if (nm->func == norm_inf) {
        printf("norm inf");
    } else if (nm->func == norm_p && nm->param != NULL) {
        printf("norm p = %.10g", *(const double *)nm->param);
    } else if (nm->func == norm_a && nm->param != NULL) {
        const matrix *m = (const matrix *)nm->param;
        printf("norm A = [");
        for (int i = 0; i < m->n; i++) {
            printf(i > 0 ? ", [" : "[");
            for (int j = 0; j < m->n; j++) {
                printf(j > 0 ? ", %.10g" : "%.10g", m->a[i * m->n + j]);
            }
            printf("]");
        }
        printf("]");
    } else {
        printf("unknown norm");
    }
    return OK;
}

/* Печать векторов задачи: "v1 = (3, 4)" - номера с 1 */
status_code print_vectors(const vector *vectors, const int vector_count) {
    if (vectors == NULL) {
        return INVALID_ARGS;
    }
    for (int i = 0; i < vector_count; i++) {
        printf("  v%d = ", i + 1);
        print_vector(&vectors[i]);
        printf("\n");
    }
    return OK;
}

/* Печать результатов всех норм:
     norm p = 2: longest 2 of 3
       v1 = (3, 4), length 5
       v2 = (-4, 3), length 5
   Ошибка нормы - строка "norm p = 0.5: Error: p must be a usual number 1 or more".
   *error_count - сколько норм с ошибкой (для кода возврата) */
status_code print_results(const norm *norms, const norm_result *result, const int norm_count,
                          const int vector_count, int *error_count) {
    if (norms == NULL || result == NULL || error_count == NULL) {
        return INVALID_ARGS;
    }
    *error_count = 0;
    for (int i = 0; i < norm_count; i++) {
        print_norm_name(&norms[i]);
        if (result[i].status != OK) {
            const char *message = NULL;
            error_text(result[i].status, &message);
            printf(": Error: %s\n", message);
            (*error_count)++;
            continue;
        }
        printf(": longest %d of %d\n", result[i].count, vector_count);
        for (int k = 0; k < result[i].count; k++) {
            printf("  v%d = ", result[i].list[k].index + 1);
            print_vector(&result[i].list[k].v);
            printf(", length %.10g\n", result[i].list[k].length);
        }
    }
    return OK;
}

/* Один пример демонстрации: заголовок, векторы, результат (или ошибка всей функции).
   vectors и norms - те же векторы и нормы, что передали в find_longest (для печати);
   sc и result - то, что она вернула */
status_code print_demo(const char *title, const double eps, const vector *vectors,
                       const int vector_count, const norm *norms, const int norm_count,
                       const status_code sc, const norm_result *result) {
    if (title == NULL || vectors == NULL || norms == NULL) {
        return INVALID_ARGS;
    }
    printf("%s, eps = %g\n", title, eps);
    print_vectors(vectors, vector_count);
    if (sc == OK) {
        if (result == NULL) {
            return INVALID_ARGS;
        }
        int error_count = 0;
        print_results(norms, result, norm_count, vector_count, &error_count);
    } else {
        const char *message = NULL;
        error_text(sc, &message);
        printf("Error: %s\n", message);
    }
    printf("\n");
    return OK;
}

/* Демонстрация: готовые вызовы find_longest. Векторы и нормы записаны прямо в вызове
   (для этого и нужна функция с ...). Массивы vN и nN рядом - только для печати условия.
   Каждый пример: вызов, печать, free_results. После ошибки всей функции result == NULL,
   free_results(NULL, ...) ничего не делает - поэтому освобождаем всегда.
   Массивы координат здесь - не буферы, а готовые данные примеров */
status_code run_demo(void) {
    norm_result *result = NULL;
    status_code sc = OK;

    printf("The longest vectors for every norm.\n");
    printf("A vector is the longest if no other vector is longer by more than eps.\n\n");

    /* Параметры норм: числа p и матрицы. Их адреса передаются как const void * */
    const double p1 = 1.0;
    const double p2 = 2.0;
    const double p3 = 3.0;
    const double a2[] = {2.0, 1.0,
                         1.0, 2.0};
    const matrix m2 = {2, a2};
    const double a3[] = {1.0, 0.0, 0.0,
                         0.0, 2.0, 0.0,
                         0.0, 0.0, 3.0};
    const matrix m3 = {3, a3};

    /* 1) плоскость. inf: 4, 4, 5, 1 -> v3;  p = 1: 7, 7, 5, 2 -> v1, v2;
          p = 2: 5, 5, 5, 1.41 -> v1, v2, v3 (три одинаковых);
          A: (Ax, x) = 2x^2 + 2xy + 2y^2 = 74, 26, 50, 6 -> v1 */
    const double k11[] = {3.0, 4.0};
    const double k12[] = {-4.0, 3.0};
    const double k13[] = {5.0, 0.0};
    const double k14[] = {1.0, 1.0};
    const vector v1[] = {{2, k11}, {2, k12}, {2, k13}, {2, k14}};
    const norm n1[] = {{norm_inf, NULL}, {norm_p, &p1}, {norm_p, &p2}, {norm_a, &m2}};
    sc = find_longest(&result, EPS_DEMO, 2, 4, 4, v1[0], v1[1], v1[2], v1[3],
                      norm_inf, (const void *)NULL, norm_p, (const void *)&p1,
                      norm_p, (const void *)&p2, norm_a, (const void *)&m2);
    print_demo("Plane (n = 2)", EPS_DEMO, v1, 4, n1, 4, sc, result);
    free_results(result, 4);
    result = NULL;   /* чтобы в следующем примере при ошибке не напечатать и не освободить старое */

    /* 2) трехмерное пространство. inf: 3, 3, 2, 3.5 -> v4;  p = 1: 6, 6, 6, 3.5 -> v1, v2, v3;
          p = 2: 3.74, 3.74, 3.46, 3.5 -> v1, v2;  p = 3: 3.30, 3.30, 2.88, 3.5 -> v4;
          A = diag(1, 2, 3): x^2 + 2y^2 + 3z^2 = 36, 20, 24, 36.75 -> v4 */
    const double k21[] = {1.0, 2.0, 3.0};
    const double k22[] = {3.0, 2.0, 1.0};
    const double k23[] = {2.0, 2.0, 2.0};
    const double k24[] = {0.0, 0.0, -3.5};
    const vector v2[] = {{3, k21}, {3, k22}, {3, k23}, {3, k24}};
    const norm n2[] = {{norm_inf, NULL}, {norm_p, &p1}, {norm_p, &p2}, {norm_p, &p3},
                        {norm_a, &m3}};
    sc = find_longest(&result, EPS_DEMO, 3, 4, 5, v2[0], v2[1], v2[2], v2[3],
                      norm_inf, (const void *)NULL, norm_p, (const void *)&p1,
                      norm_p, (const void *)&p2, norm_p, (const void *)&p3,
                      norm_a, (const void *)&m3);
    print_demo("Space (n = 3)", EPS_DEMO, v2, 4, n2, 5, sc, result);
    free_results(result, 5);
    result = NULL;

    /* 3) роль eps: длины 1 и 1.0000001 отличаются на 1e-7. С eps = 1e-9 второй вектор
          длиннее, с eps = 1e-6 - они "равны", самые длинные оба */
    const double k31[] = {1.0, 0.0};
    const double k32[] = {0.0, 1.0000001};
    const vector v3[] = {{2, k31}, {2, k32}};
    const norm n3[] = {{norm_p, &p2}};
    sc = find_longest(&result, EPS_DEMO, 2, 2, 1, v3[0], v3[1], norm_p, (const void *)&p2);
    print_demo("Role of eps", EPS_DEMO, v3, 2, n3, 1, sc, result);
    free_results(result, 1);
    result = NULL;
    sc = find_longest(&result, 1e-6, 2, 2, 1, v3[0], v3[1], norm_p, (const void *)&p2);
    print_demo("Role of eps", 1e-6, v3, 2, n3, 1, sc, result);
    free_results(result, 1);
    result = NULL;

    /* 4) один вектор (он и самый длинный) и нулевой вектор (длина 0 в любой норме) */
    const double k41[] = {0.0, 0.0};
    const vector v4[] = {{2, k41}};
    const norm n4[] = {{norm_inf, NULL}, {norm_a, &m2}};
    sc = find_longest(&result, EPS_DEMO, 2, 1, 2, v4[0],
                      norm_inf, (const void *)NULL, norm_a, (const void *)&m2);
    print_demo("One zero vector", EPS_DEMO, v4, 1, n4, 2, sc, result);
    free_results(result, 2);
    result = NULL;

    printf("Errors:\n\n");

    /* 5) ошибки отдельных норм - остальные нормы все равно посчитаны:
          p = 0.5 (не норма), несимметричная матрица, не положительно определенная
          (A = [[1, 2], [2, 1]]: (Ax, x) для x = (1, -1) равно -2), матрица 3 x 3 при n = 2 */
    const double p_bad = 0.5;
    const double a_not_sym[] = {1.0, 2.0,
                               0.0, 1.0};
    const matrix m_not_sym = {2, a_not_sym};
    const double a_not_pd[] = {1.0, 2.0,
                               2.0, 1.0};
    const matrix m_not_pd = {2, a_not_pd};
    const norm n5[] = {{norm_inf, NULL}, {norm_p, &p_bad}, {norm_a, &m_not_sym},
                        {norm_a, &m_not_pd}, {norm_a, &m3}};
    sc = find_longest(&result, EPS_DEMO, 2, 4, 5, v1[0], v1[1], v1[2], v1[3],
                      norm_inf, (const void *)NULL, norm_p, (const void *)&p_bad,
                      norm_a, (const void *)&m_not_sym, norm_a, (const void *)&m_not_pd,
                      norm_a, (const void *)&m3);
    print_demo("Bad norms", EPS_DEMO, v1, 4, n5, 5, sc, result);
    free_results(result, 5);
    result = NULL;

    /* 6) ошибки всей функции: результата нет вообще */
    sc = find_longest(&result, 0.0, 2, 1, 1, v1[0], norm_inf, (const void *)NULL);
    print_demo("Zero eps", 0.0, v1, 1, n1, 1, sc, result);
    free_results(result, 1);
    result = NULL;

    /* вектор из трехмерного пространства среди двумерных */
    const vector v6[] = {v1[0], v2[0]};
    sc = find_longest(&result, EPS_DEMO, 2, 2, 1, v6[0], v6[1], norm_inf, (const void *)NULL);
    print_demo("Vector of other dimension", EPS_DEMO, v6, 2, n1, 1, sc, result);
    free_results(result, 1);
    result = NULL;

    /* NULL вместо функции нормы. Условие напечатать нельзя (нормы нет) - только ошибку */
    sc = find_longest(&result, EPS_DEMO, 2, 1, 1, v1[0], (norm_func)NULL, (const void *)NULL);
    if (sc == OK) {
        printf("NULL norm function: no error\n\n");
    } else {
        const char *message = NULL;
        error_text(sc, &message);
        printf("NULL norm function: Error: %s\n\n", message);
    }
    free_results(result, 1);
    result = NULL;

    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    /* 1) Без аргументов - демонстрация на готовых примерах */
    if (argc == 1) {
        if (run_demo() != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        return 0;
    }

    /* 2) С аргументами: eps, n, хотя бы один вектор и одна норма */
    if (argc < 5) {
        fprintf(stderr, "Usage: lab2_task7                                  (demo)\n");
        fprintf(stderr, "       lab2_task7 <eps> <n> <vector>... <norm>...\n");
        fprintf(stderr, "vector: n numbers with ',' like 3,-4\n");
        fprintf(stderr, "norm:   inf | p=<number> | A=<n*n numbers with ',' row by row>\n");
        return 1;
    }

    /* eps и размерность - первые два аргумента */
    cli_data d = {0.0, 0, NULL, 0, NULL, NULL, 0, NULL, NULL, NULL};
    if (parse_double(argv[1], &d.eps) != OK || d.eps <= 0) {
        fprintf(stderr, "Error: eps '%s' must be a number greater than 0\n", argv[1]);
        return 1;
    }
    if (parse_dimension(argv[2], &d.n) != OK) {
        fprintf(stderr, "Error: n '%s' must be a whole number 1 or more\n", argv[2]);
        return 1;
    }

    /* 3) Векторы и нормы. При ошибке bad_arg - номер неправильного аргумента */
    int bad_arg = 0;
    status_code sc = parse_cli(argc, argv, &d, &bad_arg);
    if (sc != OK) {
        if (sc == INVALID_VECTOR) {
            fprintf(stderr, "Error: vector '%s' must be %d usual numbers with ','\n",
                    argv[bad_arg], d.n);
        } else if (sc == BAD_MATRIX_SIZE) {
            fprintf(stderr, "Error: '%s': matrix must be n*n usual numbers with ','\n",
                    argv[bad_arg]);
        } else if (sc == INVALID_PARAM) {
            fprintf(stderr, "Error: '%s': p must be a usual number\n", argv[bad_arg]);
        } else if (sc == INVALID_COUNT) {
            fprintf(stderr, "Error: give at least one vector and one norm\n");
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
        } else {
            /* INVALID_ARGS прийти не может - argv и d не NULL */
            fprintf(stderr, "Error: internal error\n");
        }
        free_cli(&d);
        return 1;
    }

    /* 4) Сама задача. Здесь вариант с массивами: сколько векторов и норм, известно только
       сейчас, а список аргументов ... пишется в коде заранее */
    norm_result *result = NULL;
    sc = find_longest_array(&result, d.eps, d.n, d.vectors, d.vector_count, d.norms, d.norm_count);
    if (sc != OK) {
        /* eps, n и векторы уже проверены при разборе - остается ALLOC_ERROR */
        const char *message = NULL;
        error_text(sc, &message);
        fprintf(stderr, "Error: %s\n", message);
        free_cli(&d);
        return 1;
    }

    /* 5) Печать: векторы, потом самые длинные для каждой нормы.
       Код возврата 1, если хотя бы одна норма с ошибкой (p < 1, плохая матрица) */
    printf("Vectors (n = %d), eps = %g:\n", d.n, d.eps);
    print_vectors(d.vectors, d.vector_count);
    int error_count = 0;
    print_results(d.norms, result, d.norm_count, d.vector_count, &error_count);

    free_results(result, d.norm_count);
    free_cli(&d);
    return error_count > 0 ? 1 : 0;
}
