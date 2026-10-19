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
 *   find_longest(&rez, eps, n, kolvo_vekt, kolvo_norm,
 *                v1, v2, ..., f1, param1, f2, param2, ...)
 * Векторы передаются как экземпляры структуры vektor (по значению), нормы - парами:
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
    const double *koord;
} vektor;

/* Матрица n x n для нормы ||x||_A: элементы по строкам, a[i * n + j] - строка i, столбец j */
typedef struct {
    int n;
    const double *a;
} matrica;

/* Универсальный тип указателя на функцию нормы. Все нормы устроены одинаково:
     v     - вектор;
     param - параметр нормы: адрес числа p (norm_p), адрес матрицы (norm_a), NULL (norm_inf);
             тип const void * - "указатель на что угодно", каждая норма сама знает, что там;
     eps   - точность (нужна только norm_a для проверки матрицы, остальные ее не используют,
             но параметр есть у всех: тип функции у всех норм должен быть один);
     rez   - сюда кладем длину вектора.
   Возвращает статус: OK или ошибку параметра нормы */
typedef status_code (*norm_func)(const vektor *v, const void *param, const double eps, double *rez);

/* Норма вместе со своим параметром (так нормы хранятся внутри, после чтения из ...) */
typedef struct {
    norm_func func;
    const void *param;
} norma;

/* Один найденный самый длинный вектор */
typedef struct {
    int nomer;       /* номер вектора в том порядке, как их передали (с 0) */
    vektor v;        /* сам вектор (копия структуры; координаты - те же, что передали) */
    double dlina;    /* его длина в этой норме */
} longest_vektor;

/* Результат для одной нормы */
typedef struct {
    status_code status;       /* OK или ошибка этой нормы (p < 1, плохая матрица, ...) */
    int kolvo;                /* сколько векторов самые длинные (0 при ошибке) */
    longest_vektor *spisok;   /* они сами, в порядке передачи (NULL при ошибке) */
} norm_result;

/* ===================== нормы ===================== */

/* Проверка вектора: координаты есть, их ровно n (n >= 1) */
status_code check_vektor(const vektor *v) {
    if (v == NULL) {
        return INVALID_ARGS;
    }
    if (v->koord == NULL || v->n < 1) {
        return INVALID_VECTOR;
    }
    return OK;
}

/* Самая большая по модулю координата: max |x_j|.
   Максимум считаем функцией fmax из math.h (C99): fmax(a, b) - большее из двух чисел.
   Так в коде нет сравнения вещественных чисел - это просто вычисление значения */
status_code max_abs(const vektor *v, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_vektor(v);
    if (sc != OK) {
        return sc;
    }
    double maks = 0.0;
    for (int j = 0; j < v->n; j++) {
        maks = fmax(maks, fabs(v->koord[j]));
    }
    *rez = maks;
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
status_code get_masshtab(const vektor *v, const double eps, double *masshtab) {
    if (masshtab == NULL) {
        return INVALID_ARGS;
    }
    double m = 0.0;
    status_code sc = max_abs(v, &m);
    if (sc != OK) {
        return sc;
    }
    *masshtab = (m < eps) ? 1.0 : m;
    return OK;
}

/* ||x||_inf = max |x_j| - самая большая по модулю координата.
   param и eps этой норме не нужны: (void)param говорит компилятору "не используется
   специально", чтобы не было предупреждения. Параметры все равно есть: тип функции
   у всех норм должен быть одинаковым (norm_func) */
status_code norm_inf(const vektor *v, const void *param, const double eps, double *rez) {
    (void)param;
    (void)eps;
    return max_abs(v, rez);
}

/* ||x||_p = (|x1|^p + ... + |xn|^p)^(1/p),  p >= 1.  param - адрес числа p (const double *).
   p < 1 не норма: например при p = 0.5 не выполняется неравенство треугольника.
   pow(a, b) - a в степени b (math.h).
   Координаты сначала делим на масштаб (get_masshtab), ответ умножаем обратно.
   OVERFLOW_ERROR - только если сама длина больше максимального double */
status_code norm_p(const vektor *v, const void *param, const double eps, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_vektor(v);
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
    double masshtab = 1.0;
    sc = get_masshtab(v, eps, &masshtab);
    if (sc != OK) {
        return sc;
    }
    double summa = 0.0;
    for (int j = 0; j < v->n; j++) {
        summa = summa + pow(fabs(v->koord[j]) / masshtab, p);
    }
    double dlina = masshtab * pow(summa, 1.0 / p);
    /* isfinite: не бесконечность и не NaN */
    if (!isfinite(dlina)) {
        return OVERFLOW_ERROR;
    }
    *rez = dlina;
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
status_code cholesky(const matrica *m, const double eps, double **rez) {
    if (m == NULL || rez == NULL) {
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
    *rez = L;
    return OK;
}

/* ||x||_A = sqrt((Ax, x)),  param - адрес матрицы (const matrica *).
   (Ax, x) - скалярное произведение вектора Ax на x: сумма по i, j  a[i][j] * x_i * x_j.
   Считаем через разложение Холецкого A = L * L^T:
       (Ax, x) = (L L^T x, x) = (L^T x, L^T x) = |y|^2,  где y = L^T x,
   то есть ||x||_A = обычная длина вектора y. Плюсы: cholesky заодно проверяет, что A
   положительно определенная (иначе это не норма), а сумма квадратов |y|^2 никогда не
   бывает отрицательной из-за погрешностей - корень из нее извлекается всегда.
   y_j = (L^T x)_j = сумма по i >= j:  L[i][j] * x_i  (столбец j матрицы L на x).
   Как и в norm_p, координаты делим на масштаб, а ответ умножаем на него обратно */
status_code norm_a(const vektor *v, const void *param, const double eps, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_vektor(v);
    if (sc != OK) {
        return sc;
    }
    if (param == NULL) {
        return INVALID_PARAM;
    }
    /* const void * -> const matrica *: для этой нормы там лежит матрица */
    const matrica *m = (const matrica *)param;
    if (m->n != v->n) {
        return BAD_MATRIX_SIZE;
    }
    double masshtab = 1.0;
    sc = get_masshtab(v, eps, &masshtab);
    if (sc != OK) {
        return sc;
    }
    double *L = NULL;
    sc = cholesky(m, eps, &L);
    if (sc != OK) {
        return sc;
    }
    const int n = v->n;
    double summa = 0.0;
    for (int j = 0; j < n; j++) {
        double y = 0.0;
        for (int i = j; i < n; i++) {
            y = y + L[i * n + j] * (v->koord[i] / masshtab);
        }
        summa = summa + y * y;
    }
    free(L);
    double dlina = masshtab * sqrt(summa);
    if (!isfinite(dlina)) {
        return OVERFLOW_ERROR;
    }
    *rez = dlina;
    return OK;
}

/* ===================== поиск самых длинных ===================== */

/* Освободить результаты: сначала список в каждой норме, потом сам массив.
   free(NULL) ничего не делает, поэтому NULL (ошибка нормы, rez == NULL) - не ошибка */
status_code free_results(norm_result *rez, const int kolvo_norm) {
    if (rez == NULL) {
        return OK;
    }
    for (int i = 0; i < kolvo_norm; i++) {
        free(rez[i].spisok);
    }
    free(rez);
    return OK;
}

/* Самые длинные векторы для одной нормы: заполняет *nr.
   1) считаем длину каждого вектора (массив dliny);
   2) вектор i - самый длинный, если нет вектора j, который длиннее его больше чем
      на eps:  dliny[j] - dliny[i] > eps. Сравниваем каждый с каждым (kolvo * kolvo
      сравнений) - зато только через eps и без "точного максимума". Порядок векторов
      на ответ не влияет.
   Пример, eps = 1e-9: длины 5, 5, 3  -> самые длинные первый и второй (никто не длиннее 5
   больше чем на eps), третий - нет (5 - 3 > eps).
   Ошибка нормы (плохое p, матрица) записывается в nr->status, функция возвращает OK:
   это ошибка одной нормы, а не всей задачи. ALLOC_ERROR - возвращается */
status_code longest_for_norm(const vektor *vekt, const int kolvo_vekt, const norma *nm,
                             const double eps, norm_result *nr) {
    if (vekt == NULL || nm == NULL || nr == NULL) {
        return INVALID_ARGS;
    }
    nr->status = OK;
    nr->kolvo = 0;
    nr->spisok = NULL;

    double *dliny = (double *)malloc((size_t)kolvo_vekt * sizeof(double));
    if (dliny == NULL) {
        return ALLOC_ERROR;
    }
    /* 1) длины всех векторов. Вызов через указатель на функцию: nm->func(...) вызывает
       ту норму, адрес которой нам передали (norm_inf, norm_p или norm_a) */
    for (int i = 0; i < kolvo_vekt; i++) {
        status_code sc = nm->func(&vekt[i], nm->param, eps, &dliny[i]);
        if (sc == ALLOC_ERROR) {
            free(dliny);
            return ALLOC_ERROR;
        }
        if (sc != OK) {
            /* ошибка нормы одинакова для всех векторов (плохое p или матрица) -
               запоминаем и дальше не считаем */
            nr->status = sc;
            free(dliny);
            return OK;
        }
    }

    /* 2) какие самые длинные: samiy[i] = 1, если никто не длиннее i больше чем на eps */
    int *samiy = (int *)malloc((size_t)kolvo_vekt * sizeof(int));
    if (samiy == NULL) {
        free(dliny);
        return ALLOC_ERROR;
    }
    int kolvo = 0;
    for (int i = 0; i < kolvo_vekt; i++) {
        samiy[i] = 1;
        for (int j = 0; j < kolvo_vekt; j++) {
            if (dliny[j] - dliny[i] > eps) {
                samiy[i] = 0;
            }
        }
        if (samiy[i]) {
            kolvo++;
        }
    }
    /* хотя бы один самый длинный всегда есть: у вектора с наибольшей длиной
       никто не длиннее, поэтому kolvo >= 1 */

    /* 3) переписываем самые длинные в список ответа */
    longest_vektor *spisok = (longest_vektor *)malloc((size_t)kolvo * sizeof(longest_vektor));
    if (spisok == NULL) {
        free(samiy);
        free(dliny);
        return ALLOC_ERROR;
    }
    int k = 0;
    for (int i = 0; i < kolvo_vekt; i++) {
        if (samiy[i]) {
            spisok[k].nomer = i;
            spisok[k].v = vekt[i];
            spisok[k].dlina = dliny[i];
            k++;
        }
    }
    free(samiy);
    free(dliny);
    nr->kolvo = kolvo;
    nr->spisok = spisok;
    return OK;
}

/* Общая часть: векторы и нормы уже в массивах. Используется и функцией с ...
   (find_longest), и режимом командной строки (там количество векторов и норм
   известно только во время работы, а список ... пишется в коде заранее).
   Проверяет все параметры, потом для каждой нормы вызывает longest_for_norm.
   *rez - массив из kolvo_norm результатов (освобождать free_results) */
status_code find_longest_array(norm_result **rez, const double eps, const int n,
                               const vektor *vekt, const int kolvo_vekt,
                               const norma *normy, const int kolvo_norm) {
    if (rez == NULL || vekt == NULL || normy == NULL) {
        return INVALID_ARGS;
    }
    if (!isfinite(eps) || eps <= 0) {
        return INVALID_EPS;
    }
    if (n < 1) {
        return INVALID_DIMENSION;
    }
    if (kolvo_vekt < 1 || kolvo_norm < 1) {
        return INVALID_COUNT;
    }
    /* каждый вектор: размерность ровно n, координаты есть и все - обычные числа */
    for (int i = 0; i < kolvo_vekt; i++) {
        if (vekt[i].n != n || vekt[i].koord == NULL) {
            return INVALID_VECTOR;
        }
        for (int j = 0; j < n; j++) {
            if (!isfinite(vekt[i].koord[j])) {
                return INVALID_VECTOR;
            }
        }
    }
    /* у каждой нормы есть функция (вызов функции по адресу NULL - аварийное завершение) */
    for (int i = 0; i < kolvo_norm; i++) {
        if (normy[i].func == NULL) {
            return INVALID_ARGS;
        }
    }
    /* Размер памяти kolvo * sizeof(...) не должен переполнить size_t. Проверка через
       деление - то же, что "kolvo * sizeof > SIZE_MAX", но без умножения, которое само
       могло бы переполниться (на 64-битной системе int для этого слишком мал) */
    if ((size_t)kolvo_norm > SIZE_MAX / sizeof(norm_result) ||
        (size_t)kolvo_vekt > SIZE_MAX / sizeof(longest_vektor)) {
        return ALLOC_ERROR;
    }

    norm_result *mas = (norm_result *)malloc((size_t)kolvo_norm * sizeof(norm_result));
    if (mas == NULL) {
        return ALLOC_ERROR;
    }
    /* сразу spisok = NULL во всех: если память кончится на середине, free_results
       освободит готовые списки, а для остальных сделает free(NULL) */
    for (int i = 0; i < kolvo_norm; i++) {
        mas[i].spisok = NULL;
    }
    for (int i = 0; i < kolvo_norm; i++) {
        status_code sc = longest_for_norm(vekt, kolvo_vekt, &normy[i], eps, &mas[i]);
        if (sc != OK) {
            /* сюда попадает только ALLOC_ERROR: ошибки норм записаны в mas[i].status */
            free_results(mas, kolvo_norm);
            return sc;
        }
    }
    *rez = mas;
    return OK;
}

/* ===================== функция с переменным числом аргументов ===================== */

/* Для каждой нормы найти самые длинные векторы:
     find_longest(&rez, 1e-9, 2, 3, 2,  v1, v2, v3,  norm_inf, (const void *)NULL,
                                                      norm_p, (const void *)&p)
   rez        - сюда кладем адрес массива из kolvo_norm результатов в динамической памяти
                (освобождать функцией free_results(rez, kolvo_norm));
   eps        - точность: длины, отличающиеся меньше чем на eps, считаются равными, eps > 0;
   n          - размерность пространства, n >= 1;
   kolvo_vekt - сколько векторов дальше, >= 1;
   kolvo_norm - сколько норм после векторов, >= 1;
   ...        - сначала kolvo_vekt структур vektor (по значению), потом kolvo_norm пар
                "функция нормы (norm_func), ее параметр (const void *)".
   Порядок как в условии: размерность, векторы, нормы. Оба количества стоят перед ...:
   функция только из них знает, сколько аргументов и какого типа читать.

   ВАЖНО про типы в ...: функция не может узнать тип аргумента, va_arg просто читает
   байты как указанный тип. Поэтому:
   - векторы - именно структуры vektor;
   - параметр нормы - именно const void *: писать (const void *)&p и (const void *)NULL.
     Просто NULL нельзя: NULL может быть определен как 0 (int), а int и указатель
     могут иметь разный размер (4 и 8 байт) - прочитается мусор. А double * и
     void * стандарт не обязывает передавать в ... одинаково, поэтому тоже приводим.
   Ошибка одной нормы (p < 1, несимметричная матрица, ...) не останавливает остальные:
   она записывается в rez[i].status */
status_code find_longest(norm_result **rez, const double eps, const int n, const int kolvo_vekt,
                         const int kolvo_norm, ...) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (kolvo_vekt < 1 || kolvo_norm < 1) {
        return INVALID_COUNT;
    }
    if ((size_t)kolvo_vekt > SIZE_MAX / sizeof(vektor) ||
        (size_t)kolvo_norm > SIZE_MAX / sizeof(norma)) {
        return ALLOC_ERROR;
    }
    /* Аргументы из ... можно прочитать только один раз и только по порядку, а векторы
       нужны для каждой нормы заново - поэтому сначала переписываем все в массивы */
    vektor *vekt = (vektor *)malloc((size_t)kolvo_vekt * sizeof(vektor));
    if (vekt == NULL) {
        return ALLOC_ERROR;
    }
    norma *normy = (norma *)malloc((size_t)kolvo_norm * sizeof(norma));
    if (normy == NULL) {
        free(vekt);
        return ALLOC_ERROR;
    }

    /* va_list - "указатель" на следующий безымянный аргумент.
       va_start(args, kolvo_norm) ставит его сразу после параметра kolvo_norm */
    va_list args;
    va_start(args, kolvo_norm);
    for (int i = 0; i < kolvo_vekt; i++) {
        /* va_arg достает следующий аргумент как структуру vektor (копия по значению) */
        vekt[i] = va_arg(args, vektor);
    }
    for (int i = 0; i < kolvo_norm; i++) {
        /* пара: адрес функции нормы, потом адрес ее параметра */
        normy[i].func = va_arg(args, norm_func);
        normy[i].param = va_arg(args, const void *);
    }
    /* va_end обязателен после va_start; дальше аргументы не нужны */
    va_end(args);

    /* сама работа - общая с режимом командной строки. В ответе копии структур vektor,
       массивы vekt и normy больше не нужны */
    status_code sc = find_longest_array(rez, eps, n, vekt, kolvo_vekt, normy, kolvo_norm);
    free(normy);
    free(vekt);
    return sc;
}

/* ===================== ввод (только для main) ===================== */

/* Строка -> double. Разрешены обычные записи: 3, -2.5, 1e-9, .5
   strtod(str, &end) читает число с начала строки, в end - адрес первого непрочитанного
   символа; *end != '\0' - после числа мусор. Пробел в начале запрещаем сами
   (strtod его пропустил бы). inf, nan, 1e999 - не обычные числа -> INVALID_NUMBER */
status_code parse_double(const char *str, double *chislo) {
    if (str == NULL || chislo == NULL) {
        return INVALID_ARGS;
    }
    /* isspace принимает unsigned char: русская буква в char отрицательна */
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }
    char *end = NULL;
    double znach = strtod(str, &end);
    if (end == str || *end != '\0' || !isfinite(znach)) {
        return INVALID_NUMBER;
    }
    *chislo = znach;
    return OK;
}

/* Размерность n из командной строки: только цифры, от 1 до INT_MAX.
   Число собираем вручную; перед "znach * 10 + cifra" проверяем, что не выйдем
   за INT_MAX:  znach * 10 + cifra > INT_MAX  <=>  znach > (INT_MAX - cifra) / 10 */
status_code parse_dimension(const char *str, int *n) {
    if (str == NULL || n == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_DIMENSION;
    }
    int znach = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_DIMENSION;
        }
        int cifra = str[i] - '0';
        if (znach > (INT_MAX - cifra) / 10) {
            return INVALID_DIMENSION;
        }
        znach = znach * 10 + cifra;
    }
    if (znach < 1) {
        return INVALID_DIMENSION;
    }
    *n = znach;
    return OK;
}

/* Сколько чисел в списке через запятую ("3,-4,5" -> 3): запятых + 1.
   Правильные ли сами числа, проверит parse_list */
status_code count_list(const char *str, size_t *kolvo) {
    if (str == NULL || kolvo == NULL) {
        return INVALID_ARGS;
    }
    size_t k = 1;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == ',') {
            k++;
        }
    }
    *kolvo = k;
    return OK;
}

/* Список ровно из kolvo чисел через запятую -> массив rez (место выделил вызывающий).
   После каждого числа должна быть запятая (если число не последнее) или конец строки.
   Пустое место (",,", запятая в конце), пробелы, мусор, inf/nan - INVALID_NUMBER */
status_code parse_list(const char *str, const size_t kolvo, double *rez) {
    if (str == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    const char *tek = str;   /* где начинается очередное число */
    for (size_t i = 0; i < kolvo; i++) {
        if (*tek == '\0' || isspace((unsigned char)*tek)) {
            return INVALID_NUMBER;
        }
        char *end = NULL;
        double znach = strtod(tek, &end);
        if (end == tek || !isfinite(znach)) {
            return INVALID_NUMBER;
        }
        /* после последнего числа - конец строки, после остальных - запятая */
        if (i + 1 == kolvo) {
            if (*end != '\0') {
                return INVALID_NUMBER;
            }
        } else {
            if (*end != ',') {
                return INVALID_NUMBER;
            }
            end++;   /* перешагиваем запятую */
        }
        rez[i] = znach;
        tek = end;
    }
    return OK;
}

/* Вид аргумента командной строки */
typedef enum {
    ARG_VEKTOR,   /* "3,-4" */
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
        *kind = ARG_VEKTOR;
    }
    return OK;
}

/* Все, что main прочитал из командной строки (вся память - здесь, освобождает free_cli) */
typedef struct {
    double eps;
    int n;
    vektor *vekt;         /* векторы; их координаты - в koord */
    int kolvo_vekt;
    double *koord;        /* координаты всех векторов подряд: kolvo_vekt * n чисел */
    norma *normy;         /* нормы; их параметры - в p_znach и matricy */
    int kolvo_norm;
    double *p_znach;      /* числа p (по одному месту на каждую норму, нужны только для p=) */
    matrica *matricy;     /* матрицы (тоже по месту на каждую норму) */
    double *koef;         /* элементы всех матриц подряд: kolvo_matr * n * n чисел */
} cli_data;

/* Освободить память cli_data. free(NULL) ничего не делает - можно звать в любой момент */
status_code free_cli(cli_data *d) {
    if (d == NULL) {
        return INVALID_ARGS;
    }
    free(d->vekt);
    free(d->koord);
    free(d->normy);
    free(d->p_znach);
    free(d->matricy);
    free(d->koef);
    d->vekt = NULL;
    d->koord = NULL;
    d->normy = NULL;
    d->p_znach = NULL;
    d->matricy = NULL;
    d->koef = NULL;
    return OK;
}

/* Разбор векторов и норм из argv[3 .. argc-1] в *d (eps и n уже разобраны).
   Два прохода: первый только считает векторы, нормы и матрицы и проверяет, что в каждом
   векторе n чисел, а в матрице n * n (тогда память выделяется ровно по размеру и только
   после проверки - огромное n из командной строки не вызовет огромный malloc);
   второй выделяет память и читает числа.
   *plohoy - номер аргумента с ошибкой (для сообщения в main) */
status_code parse_cli(const int argc, char *argv[], cli_data *d, int *plohoy) {
    if (argv == NULL || d == NULL || plohoy == NULL) {
        return INVALID_ARGS;
    }
    const int n = d->n;
    int kolvo_vekt = 0;
    int kolvo_norm = 0;
    int kolvo_matr = 0;

    /* проход 1: считаем и проверяем количество чисел */
    for (int i = 3; i < argc; i++) {
        arg_kind kind;
        kind_of_arg(argv[i], &kind);
        *plohoy = i;
        if (kind == ARG_VEKTOR) {
            size_t k = 0;
            count_list(argv[i], &k);
            if (k != (size_t)n) {
                return INVALID_VECTOR;
            }
            kolvo_vekt++;
        } else {
            if (kind == ARG_A) {
                /* матрица n x n: n * n чисел. (size_t)n * n не переполнится:
                   n <= INT_MAX, а size_t на 64-битной системе намного больше */
                size_t k = 0;
                count_list(argv[i] + 2, &k);
                if (k != (size_t)n * (size_t)n) {
                    return BAD_MATRIX_SIZE;
                }
                kolvo_matr++;
            }
            kolvo_norm++;
        }
    }
    if (kolvo_vekt < 1 || kolvo_norm < 1) {
        *plohoy = 0;
        return INVALID_COUNT;
    }

    /* выделяем память. Размеры - не больше длины командной строки (каждое число -
       хотя бы один символ), поэтому переполнения size_t тут нет */
    d->vekt = (vektor *)malloc((size_t)kolvo_vekt * sizeof(vektor));
    d->koord = (double *)malloc((size_t)kolvo_vekt * (size_t)n * sizeof(double));
    d->normy = (norma *)malloc((size_t)kolvo_norm * sizeof(norma));
    d->p_znach = (double *)malloc((size_t)kolvo_norm * sizeof(double));
    d->matricy = (matrica *)malloc((size_t)kolvo_norm * sizeof(matrica));
    /* матриц может не быть совсем - тогда хотя бы 1 элемент, чтобы malloc(0)
       не вернул NULL (это не ошибка памяти, а так malloc(0) имеет право делать) */
    size_t razmer_koef = (size_t)kolvo_matr * (size_t)n * (size_t)n;
    d->koef = (double *)malloc((razmer_koef > 0 ? razmer_koef : 1) * sizeof(double));
    if (d->vekt == NULL || d->koord == NULL || d->normy == NULL || d->p_znach == NULL ||
        d->matricy == NULL || d->koef == NULL) {
        *plohoy = 0;
        return ALLOC_ERROR;
    }

    /* проход 2: читаем числа. iv, inorm, imatr - сколько уже прочитали */
    int iv = 0;
    int inorm = 0;
    int imatr = 0;
    for (int i = 3; i < argc; i++) {
        arg_kind kind;
        kind_of_arg(argv[i], &kind);
        *plohoy = i;
        if (kind == ARG_VEKTOR) {
            /* координаты вектора iv - в koord с места iv * n */
            double *mesto = d->koord + (size_t)iv * (size_t)n;
            if (parse_list(argv[i], (size_t)n, mesto) != OK) {
                return INVALID_VECTOR;
            }
            d->vekt[iv].n = n;
            d->vekt[iv].koord = mesto;
            iv++;
        } else if (kind == ARG_INF) {
            d->normy[inorm].func = norm_inf;
            d->normy[inorm].param = NULL;
            inorm++;
        } else if (kind == ARG_P) {
            /* число после "p=". p < 1 тут не ошибка разбора: это проверит сама norm_p */
            if (parse_double(argv[i] + 2, &d->p_znach[inorm]) != OK) {
                return INVALID_PARAM;
            }
            d->normy[inorm].func = norm_p;
            d->normy[inorm].param = &d->p_znach[inorm];
            inorm++;
        } else {
            /* элементы матрицы imatr - в koef с места imatr * n * n */
            double *mesto = d->koef + (size_t)imatr * (size_t)n * (size_t)n;
            if (parse_list(argv[i] + 2, (size_t)n * (size_t)n, mesto) != OK) {
                return BAD_MATRIX_SIZE;
            }
            d->matricy[inorm].n = n;
            d->matricy[inorm].a = mesto;
            d->normy[inorm].func = norm_a;
            d->normy[inorm].param = &d->matricy[inorm];
            inorm++;
            imatr++;
        }
    }
    d->kolvo_vekt = kolvo_vekt;
    d->kolvo_norm = kolvo_norm;
    return OK;
}

/* ===================== вывод (только для main) ===================== */

/* Текст ошибки для статуса (строковые литералы живут всю программу - отдавать их адрес можно) */
status_code error_text(const status_code sc, const char **soobshenie) {
    if (soobshenie == NULL) {
        return INVALID_ARGS;
    }
    if (sc == INVALID_ARGS) {
        /* в демонстрации это пример "NULL вместо функции нормы" */
        *soobshenie = "a pointer is NULL (no norm function)";
    } else if (sc == INVALID_EPS) {
        *soobshenie = "eps must be greater than 0";
    } else if (sc == INVALID_DIMENSION) {
        *soobshenie = "dimension n must be 1 or more";
    } else if (sc == INVALID_COUNT) {
        *soobshenie = "give at least one vector and one norm";
    } else if (sc == INVALID_VECTOR) {
        *soobshenie = "every vector must have n usual numbers";
    } else if (sc == INVALID_PARAM) {
        *soobshenie = "p must be a usual number 1 or more";
    } else if (sc == BAD_MATRIX_SIZE) {
        *soobshenie = "matrix A must be n x n";
    } else if (sc == NOT_SYMMETRIC) {
        *soobshenie = "matrix A must be symmetric";
    } else if (sc == NOT_POSITIVE_DEFINITE) {
        *soobshenie = "matrix A must be positive definite";
    } else if (sc == OVERFLOW_ERROR) {
        *soobshenie = "the length is too big";
    } else if (sc == ALLOC_ERROR) {
        *soobshenie = "not enough memory";
    } else {
        /* INVALID_NUMBER сюда из main и демонстрации прийти не может: его main
           обрабатывает сам. Если сообщение появилось - ошибка в самой программе */
        *soobshenie = "internal error";
    }
    return OK;
}

/* Печать вектора: "(3, -4)". Числа через %.10g: до 10 значащих цифр, без лишних нулей */
status_code print_vektor(const vektor *v) {
    if (v == NULL || v->koord == NULL) {
        return INVALID_ARGS;
    }
    printf("(");
    for (int j = 0; j < v->n; j++) {
        if (j > 0) {
            printf(", ");
        }
        printf("%.10g", v->koord[j]);
    }
    printf(")");
    return OK;
}

/* Название нормы: "inf", "p = 2", "A = [[2, 1], [1, 2]]".
   Какая это норма, узнаем сравнением адресов функций (адреса функций сравнивать
   через == можно - это не вещественные числа) */
status_code print_norm_name(const norma *nm) {
    if (nm == NULL) {
        return INVALID_ARGS;
    }
    if (nm->func == norm_inf) {
        printf("norm inf");
    } else if (nm->func == norm_p && nm->param != NULL) {
        printf("norm p = %.10g", *(const double *)nm->param);
    } else if (nm->func == norm_a && nm->param != NULL) {
        const matrica *m = (const matrica *)nm->param;
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
status_code print_vektory(const vektor *vekt, const int kolvo_vekt) {
    if (vekt == NULL) {
        return INVALID_ARGS;
    }
    for (int i = 0; i < kolvo_vekt; i++) {
        printf("  v%d = ", i + 1);
        print_vektor(&vekt[i]);
        printf("\n");
    }
    return OK;
}

/* Печать результатов всех норм:
     norm p = 2: longest 2 of 3
       v1 = (3, 4), length 5
       v2 = (-4, 3), length 5
   Ошибка нормы - строка "norm p = 0.5: Error: p must be a usual number 1 or more".
   *kolvo_oshibok - сколько норм с ошибкой (для кода возврата) */
status_code print_results(const norma *normy, const norm_result *rez, const int kolvo_norm,
                          const int kolvo_vekt, int *kolvo_oshibok) {
    if (normy == NULL || rez == NULL || kolvo_oshibok == NULL) {
        return INVALID_ARGS;
    }
    *kolvo_oshibok = 0;
    for (int i = 0; i < kolvo_norm; i++) {
        print_norm_name(&normy[i]);
        if (rez[i].status != OK) {
            const char *soobshenie = NULL;
            error_text(rez[i].status, &soobshenie);
            printf(": Error: %s\n", soobshenie);
            (*kolvo_oshibok)++;
            continue;
        }
        printf(": longest %d of %d\n", rez[i].kolvo, kolvo_vekt);
        for (int k = 0; k < rez[i].kolvo; k++) {
            printf("  v%d = ", rez[i].spisok[k].nomer + 1);
            print_vektor(&rez[i].spisok[k].v);
            printf(", length %.10g\n", rez[i].spisok[k].dlina);
        }
    }
    return OK;
}

/* Один пример демонстрации: заголовок, векторы, результат (или ошибка всей функции).
   vekt и normy - те же векторы и нормы, что передали в find_longest (для печати);
   sc и rez - то, что она вернула */
status_code print_demo(const char *zagolovok, const double eps, const vektor *vekt,
                       const int kolvo_vekt, const norma *normy, const int kolvo_norm,
                       const status_code sc, const norm_result *rez) {
    if (zagolovok == NULL || vekt == NULL || normy == NULL) {
        return INVALID_ARGS;
    }
    printf("%s, eps = %g\n", zagolovok, eps);
    print_vektory(vekt, kolvo_vekt);
    if (sc == OK) {
        if (rez == NULL) {
            return INVALID_ARGS;
        }
        int kolvo_oshibok = 0;
        print_results(normy, rez, kolvo_norm, kolvo_vekt, &kolvo_oshibok);
    } else {
        const char *soobshenie = NULL;
        error_text(sc, &soobshenie);
        printf("Error: %s\n", soobshenie);
    }
    printf("\n");
    return OK;
}

/* Демонстрация: готовые вызовы find_longest. Векторы и нормы записаны прямо в вызове
   (для этого и нужна функция с ...). Массивы vN и nN рядом - только для печати условия.
   Каждый пример: вызов, печать, free_results. После ошибки всей функции rez == NULL,
   free_results(NULL, ...) ничего не делает - поэтому освобождаем всегда.
   Массивы координат здесь - не буферы, а готовые данные примеров */
status_code run_demo(void) {
    norm_result *rez = NULL;
    status_code sc = OK;

    printf("The longest vectors for every norm.\n");
    printf("A vector is the longest if no other vector is longer by more than eps.\n\n");

    /* Параметры норм: числа p и матрицы. Их адреса передаются как const void * */
    const double p1 = 1.0;
    const double p2 = 2.0;
    const double p3 = 3.0;
    const double a2[] = {2.0, 1.0,
                         1.0, 2.0};
    const matrica m2 = {2, a2};
    const double a3[] = {1.0, 0.0, 0.0,
                         0.0, 2.0, 0.0,
                         0.0, 0.0, 3.0};
    const matrica m3 = {3, a3};

    /* 1) плоскость. inf: 4, 4, 5, 1 -> v3;  p = 1: 7, 7, 5, 2 -> v1, v2;
          p = 2: 5, 5, 5, 1.41 -> v1, v2, v3 (три одинаковых);
          A: (Ax, x) = 2x^2 + 2xy + 2y^2 = 74, 26, 50, 6 -> v1 */
    const double k11[] = {3.0, 4.0};
    const double k12[] = {-4.0, 3.0};
    const double k13[] = {5.0, 0.0};
    const double k14[] = {1.0, 1.0};
    const vektor v1[] = {{2, k11}, {2, k12}, {2, k13}, {2, k14}};
    const norma n1[] = {{norm_inf, NULL}, {norm_p, &p1}, {norm_p, &p2}, {norm_a, &m2}};
    sc = find_longest(&rez, EPS_DEMO, 2, 4, 4, v1[0], v1[1], v1[2], v1[3],
                      norm_inf, (const void *)NULL, norm_p, (const void *)&p1,
                      norm_p, (const void *)&p2, norm_a, (const void *)&m2);
    print_demo("Plane (n = 2)", EPS_DEMO, v1, 4, n1, 4, sc, rez);
    free_results(rez, 4);
    rez = NULL;   /* чтобы в следующем примере при ошибке не напечатать и не освободить старое */

    /* 2) трехмерное пространство. inf: 3, 3, 2, 3.5 -> v4;  p = 1: 6, 6, 6, 3.5 -> v1, v2, v3;
          p = 2: 3.74, 3.74, 3.46, 3.5 -> v1, v2;  p = 3: 3.30, 3.30, 2.88, 3.5 -> v4;
          A = diag(1, 2, 3): x^2 + 2y^2 + 3z^2 = 36, 20, 24, 36.75 -> v4 */
    const double k21[] = {1.0, 2.0, 3.0};
    const double k22[] = {3.0, 2.0, 1.0};
    const double k23[] = {2.0, 2.0, 2.0};
    const double k24[] = {0.0, 0.0, -3.5};
    const vektor v2[] = {{3, k21}, {3, k22}, {3, k23}, {3, k24}};
    const norma n2[] = {{norm_inf, NULL}, {norm_p, &p1}, {norm_p, &p2}, {norm_p, &p3},
                        {norm_a, &m3}};
    sc = find_longest(&rez, EPS_DEMO, 3, 4, 5, v2[0], v2[1], v2[2], v2[3],
                      norm_inf, (const void *)NULL, norm_p, (const void *)&p1,
                      norm_p, (const void *)&p2, norm_p, (const void *)&p3,
                      norm_a, (const void *)&m3);
    print_demo("Space (n = 3)", EPS_DEMO, v2, 4, n2, 5, sc, rez);
    free_results(rez, 5);
    rez = NULL;

    /* 3) роль eps: длины 1 и 1.0000001 отличаются на 1e-7. С eps = 1e-9 второй вектор
          длиннее, с eps = 1e-6 - они "равны", самые длинные оба */
    const double k31[] = {1.0, 0.0};
    const double k32[] = {0.0, 1.0000001};
    const vektor v3[] = {{2, k31}, {2, k32}};
    const norma n3[] = {{norm_p, &p2}};
    sc = find_longest(&rez, EPS_DEMO, 2, 2, 1, v3[0], v3[1], norm_p, (const void *)&p2);
    print_demo("Role of eps", EPS_DEMO, v3, 2, n3, 1, sc, rez);
    free_results(rez, 1);
    rez = NULL;
    sc = find_longest(&rez, 1e-6, 2, 2, 1, v3[0], v3[1], norm_p, (const void *)&p2);
    print_demo("Role of eps", 1e-6, v3, 2, n3, 1, sc, rez);
    free_results(rez, 1);
    rez = NULL;

    /* 4) один вектор (он и самый длинный) и нулевой вектор (длина 0 в любой норме) */
    const double k41[] = {0.0, 0.0};
    const vektor v4[] = {{2, k41}};
    const norma n4[] = {{norm_inf, NULL}, {norm_a, &m2}};
    sc = find_longest(&rez, EPS_DEMO, 2, 1, 2, v4[0],
                      norm_inf, (const void *)NULL, norm_a, (const void *)&m2);
    print_demo("One zero vector", EPS_DEMO, v4, 1, n4, 2, sc, rez);
    free_results(rez, 2);
    rez = NULL;

    printf("Errors:\n\n");

    /* 5) ошибки отдельных норм - остальные нормы все равно посчитаны:
          p = 0.5 (не норма), несимметричная матрица, не положительно определенная
          (A = [[1, 2], [2, 1]]: (Ax, x) для x = (1, -1) равно -2), матрица 3 x 3 при n = 2 */
    const double p_plohoe = 0.5;
    const double a_nesimm[] = {1.0, 2.0,
                               0.0, 1.0};
    const matrica m_nesimm = {2, a_nesimm};
    const double a_neplus[] = {1.0, 2.0,
                               2.0, 1.0};
    const matrica m_neplus = {2, a_neplus};
    const norma n5[] = {{norm_inf, NULL}, {norm_p, &p_plohoe}, {norm_a, &m_nesimm},
                        {norm_a, &m_neplus}, {norm_a, &m3}};
    sc = find_longest(&rez, EPS_DEMO, 2, 4, 5, v1[0], v1[1], v1[2], v1[3],
                      norm_inf, (const void *)NULL, norm_p, (const void *)&p_plohoe,
                      norm_a, (const void *)&m_nesimm, norm_a, (const void *)&m_neplus,
                      norm_a, (const void *)&m3);
    print_demo("Bad norms", EPS_DEMO, v1, 4, n5, 5, sc, rez);
    free_results(rez, 5);
    rez = NULL;

    /* 6) ошибки всей функции: результата нет вообще */
    sc = find_longest(&rez, 0.0, 2, 1, 1, v1[0], norm_inf, (const void *)NULL);
    print_demo("Zero eps", 0.0, v1, 1, n1, 1, sc, rez);
    free_results(rez, 1);
    rez = NULL;

    /* вектор из трехмерного пространства среди двумерных */
    const vektor v6[] = {v1[0], v2[0]};
    sc = find_longest(&rez, EPS_DEMO, 2, 2, 1, v6[0], v6[1], norm_inf, (const void *)NULL);
    print_demo("Vector of other dimension", EPS_DEMO, v6, 2, n1, 1, sc, rez);
    free_results(rez, 1);
    rez = NULL;

    /* NULL вместо функции нормы. Условие напечатать нельзя (нормы нет) - только ошибку */
    sc = find_longest(&rez, EPS_DEMO, 2, 1, 1, v1[0], (norm_func)NULL, (const void *)NULL);
    if (sc == OK) {
        printf("NULL norm function: no error\n\n");
    } else {
        const char *soobshenie = NULL;
        error_text(sc, &soobshenie);
        printf("NULL norm function: Error: %s\n\n", soobshenie);
    }
    free_results(rez, 1);
    rez = NULL;

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

    /* 3) Векторы и нормы. При ошибке plohoy - номер неправильного аргумента */
    int plohoy = 0;
    status_code sc = parse_cli(argc, argv, &d, &plohoy);
    if (sc != OK) {
        if (sc == INVALID_VECTOR) {
            fprintf(stderr, "Error: vector '%s' must be %d usual numbers with ','\n",
                    argv[plohoy], d.n);
        } else if (sc == BAD_MATRIX_SIZE) {
            fprintf(stderr, "Error: '%s': matrix must be n*n usual numbers with ','\n",
                    argv[plohoy]);
        } else if (sc == INVALID_PARAM) {
            fprintf(stderr, "Error: '%s': p must be a usual number\n", argv[plohoy]);
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
    norm_result *rez = NULL;
    sc = find_longest_array(&rez, d.eps, d.n, d.vekt, d.kolvo_vekt, d.normy, d.kolvo_norm);
    if (sc != OK) {
        /* eps, n и векторы уже проверены при разборе - остается ALLOC_ERROR */
        const char *soobshenie = NULL;
        error_text(sc, &soobshenie);
        fprintf(stderr, "Error: %s\n", soobshenie);
        free_cli(&d);
        return 1;
    }

    /* 5) Печать: векторы, потом самые длинные для каждой нормы.
       Код возврата 1, если хотя бы одна норма с ошибкой (p < 1, плохая матрица) */
    printf("Vectors (n = %d), eps = %g:\n", d.n, d.eps);
    print_vektory(d.vekt, d.kolvo_vekt);
    int kolvo_oshibok = 0;
    print_results(d.normy, rez, d.kolvo_norm, d.kolvo_vekt, &kolvo_oshibok);

    free_results(rez, d.kolvo_norm);
    free_cli(&d);
    return kolvo_oshibok > 0 ? 1 : 0;
}
