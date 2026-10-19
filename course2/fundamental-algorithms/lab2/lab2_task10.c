/*
 * Лабораторная 2, задание 10. Переразложение многочлена по степеням (x - a).
 *
 * Дан многочлен  f(x) = f0 + f1*x + f2*x^2 + ... + fn*x^n  и число a.
 * Нужно найти g0, g1, ..., gn такие, что
 *     f(x) = g0 + g1*(x - a) + g2*(x - a)^2 + ... + gn*(x - a)^n.
 * Пример: f(x) = x^2, a = 1:  x^2 = ((x - 1) + 1)^2 = 1 + 2*(x - 1) + 1*(x - 1)^2,
 *         то есть g = 1, 2, 1.
 *
 * Запуск:  lab2_task10                               - демонстрация (набор готовых примеров)
 *          lab2_task10 <eps> <a> <f0> [f1 ... fn]     - свой многочлен
 *          например: lab2_task10 1e-9 1 1 2 3  ->  f(x) = 1 + 2*x + 3*x^2 по степеням (x - 1)
 *
 * Главная функция задания - polynomial_reexpand с переменным числом аргументов (...):
 *   polynomial_reexpand(eps, a, &g, n, f0, f1, ..., fn)
 * Она выделяет память под коэффициенты g (освобождает вызывающий) и считает их
 * схемой Горнера (функция horner_shift).
 *
 * Как считаем (схема Горнера, "многократное деление на (x - a)"):
 *   g0 = f(a) - остаток от деления f(x) на (x - a);
 *   g1 = остаток от деления частного еще раз на (x - a), и так далее.
 * Почему так: f(x) = g0 + (x - a) * [g1 + g2*(x - a) + ...]. Подставим x = a -
 * скобка умножится на 0, останется g0 = f(a). А то, что в квадратных скобках, -
 * это частное от деления f(x) на (x - a); с ним повторяем то же самое.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free, strtod */
#include <stdarg.h>   /* va_list, va_start, va_arg, va_end - работа с переменным числом аргументов */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */
#include <ctype.h>    /* isspace */
#include <math.h>     /* fabs, isfinite, INFINITY */

/* Точность для демонстрации: числа, которые по модулю меньше EPS_DEMO, считаем нулем.
   Это не глобальная переменная, а просто имя для числа: в функции eps всегда
   передается параметром */
#define EPS_DEMO 1e-9

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* в функцию передали NULL */
    INVALID_EPS,      /* eps <= 0 (или не число): с такой точностью сравнивать нельзя */
    INVALID_DEGREE,   /* степень многочлена меньше 0 */
    INVALID_NUMBER,   /* строка не число / коэффициент или a - бесконечность или NaN */
    OVERFLOW_ERROR,   /* при вычислении получилось слишком большое число (бесконечность) */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* ===================== проверки ===================== */

/* Общая проверка параметров для обеих функций переразложения:
   eps > 0, указатель для ответа не NULL, степень >= 0, a - обычное конечное число,
   и массив из n + 1 чисел double помещается в size_t (иначе malloc попросит
   неправильный размер) */
status_code check_params(const double eps, const double a, double **result, const int n) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    /* isfinite(x) - макрос из math.h (C99): "x - обычное число, не бесконечность
       и не NaN". Для NaN любое сравнение ложно (и eps <= 0 тоже), поэтому
       без isfinite eps = NaN прошел бы проверку */
    if (!isfinite(eps) || eps <= 0) {
        return INVALID_EPS;
    }
    if (n < 0) {
        return INVALID_DEGREE;
    }
    if (!isfinite(a)) {
        return INVALID_NUMBER;
    }
    /* Коэффициентов n + 1 (от f0 до fn). Размер памяти = (n + 1) * sizeof(double).
       Проверка "(n + 1) > SIZE_MAX / sizeof(double)" - то же самое, что
       "(n + 1) * sizeof(double) > SIZE_MAX", но без умножения, которое само
       могло бы переполниться. На 64-битной системе этого не бывает никогда
       (int слишком мал), а на 32-битной - возможно */
    if ((size_t)n + 1 > SIZE_MAX / sizeof(double)) {
        return OVERFLOW_ERROR;
    }
    return OK;
}

/* ===================== алгоритм ===================== */

/* Переразложение "на месте": в массиве c лежат f0, f1, ..., fn,
   после работы там лежат g0, g1, ..., gn.
   Новой памяти не нужно: все шаги пишут в тот же массив.

   Один шаг деления на (x - a) (схема Горнера) для многочлена c[k] + ... + c[n]*x^(n-k):
       идем от старшего коэффициента к младшему:  c[i] = c[i] + a * c[i + 1].
   После шага в c[k] стоит значение многочлена в точке a, то есть остаток
   от деления на (x - a), - это и есть g_k. А в c[k+1..n] стоят коэффициенты
   частного - с ними делаем следующий шаг (k = k + 1).
   Пример: f(x) = x^2, a = 1, c = {0, 0, 1}:
       k = 0:  c[1] = 0 + 1*1 = 1,  c[0] = 0 + 1*1 = 1   -> c = {1, 1, 1}, g0 = 1
       k = 1:  c[1] = 1 + 1*1 = 2                        -> c = {1, 2, 1}, g1 = 2
       c[2] = 1 не меняется (старший коэффициент при сдвиге x на a остается тем же), g2 = 1
       ответ: x^2 = 1 + 2*(x - 1) + 1*(x - 1)^2
   Всего примерно n*n/2 сложений и умножений.

   eps: после вычисления числа, которые по модулю меньше eps, заменяем на 0.
   Деление на (x - a) в дробных числах дает погрешность: там, где должен быть
   ровно 0, может получиться 1.7e-18. Это шум округления, а не настоящий коэффициент */
status_code horner_shift(double *c, const int n, const double a, const double eps) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    if (!isfinite(eps) || eps <= 0) {
        return INVALID_EPS;
    }
    if (n < 0) {
        return INVALID_DEGREE;
    }

    /* k - номер шага, он же номер коэффициента g_k, который этот шаг находит.
       Шагов n: после шага n - 1 последний коэффициент c[n] уже готов (это gn = fn) */
    for (int k = 0; k < n; k++) {
        /* один шаг деления на (x - a): от старшего коэффициента к c[k] */
        for (int i = n - 1; i >= k; i--) {
            c[i] = c[i] + a * c[i + 1];
            /* Огромные a или коэффициенты могут дать число больше максимального
               double (~1.8e308) - тогда получится бесконечность. Дальше считать
               бессмысленно: сообщаем о переполнении */
            if (!isfinite(c[i])) {
                return OVERFLOW_ERROR;
            }
        }
    }

    /* Убираем шум округления: |g| < eps -> ровно 0.
       Заодно уходит "-0" (отрицательный ноль), который иначе напечатался бы как "-0" */
    for (int i = 0; i <= n; i++) {
        if (fabs(c[i]) < eps) {
            c[i] = 0.0;
        }
    }
    return OK;
}

/* ===================== функция с переменным числом аргументов ===================== */

/* Переразложение многочлена по степеням (x - a):
     polynomial_reexpand(1e-9, 1.0, &g, 2, 1.0, 2.0, 3.0)
       -> g = {6, 8, 3}:  1 + 2x + 3x^2 = 6 + 8(x - 1) + 3(x - 1)^2
   eps - точность (числа меньше eps по модулю считаются нулем), eps > 0;
   a   - число a из (x - a);
   result - сюда кладем адрес массива g0..gn в динамической памяти (освобождает вызывающий);
   n   - степень многочлена, n >= 0;
   ... - коэффициенты f0, f1, ..., fn, ровно n + 1 штук, тип double.
   Порядок параметров - как в условии. n стоит последним перед ...: va_start
   отсчитывает список с последнего именованного параметра.

   ВАЖНО: коэффициенты передавать именно как double: 1.0, а не 1. Тип аргумента
   функция узнать не может - va_arg(args, double) просто читает 8 байт как double.
   Если передать int 1, прочитается мусор (неопределенное поведение).
   (float передавать можно: в ... он сам превращается в double.) */
status_code polynomial_reexpand(const double eps, const double a, double **result, const int n, ...) {
    status_code sc = check_params(eps, a, result, n);
    if (sc != OK) {
        return sc;
    }

    /* Один массив на все: сначала читаем в него f0..fn, потом horner_shift
       превращает их в g0..gn прямо в нем же */
    double *c = (double *)malloc(((size_t)n + 1) * sizeof(double));
    if (c == NULL) {
        return ALLOC_ERROR;
    }

    /* va_list - "указатель" на следующий безымянный аргумент.
       va_start(args, n) ставит его сразу после параметра n */
    va_list args;
    va_start(args, n);
    for (int i = 0; i <= n; i++) {
        /* va_arg достает следующий аргумент и считает, что он типа double */
        c[i] = va_arg(args, double);
    }
    /* va_end обязателен после va_start; дальше аргументы нам не нужны */
    va_end(args);

    /* бесконечность или NaN среди коэффициентов - считать с ними нельзя */
    for (int i = 0; i <= n; i++) {
        if (!isfinite(c[i])) {
            free(c);
            return INVALID_NUMBER;
        }
    }

    sc = horner_shift(c, n, a, eps);
    if (sc != OK) {
        /* выходим с ошибкой - память за собой освобождаем, *result не трогаем */
        free(c);
        return sc;
    }

    *result = c;
    return OK;
}

/* То же переразложение, но коэффициенты f0..fn приходят массивом.
   Нужна для режима с аргументами командной строки: функцию с ... нельзя
   вызвать с количеством аргументов, которое станет известно только во время
   работы (сколько чисел ввел пользователь). Список аргументов ... пишется в коде
   заранее. Сам алгоритм у обеих функций общий - horner_shift.
   Массив f не меняется (const): ответ - новый массив в динамической памяти */
status_code reexpand_array(const double eps, const double a, double **result, const int n,
                           const double *f) {
    if (f == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_params(eps, a, result, n);
    if (sc != OK) {
        return sc;
    }

    double *c = (double *)malloc(((size_t)n + 1) * sizeof(double));
    if (c == NULL) {
        return ALLOC_ERROR;
    }
    /* копируем f в c и сразу проверяем каждое число */
    for (int i = 0; i <= n; i++) {
        if (!isfinite(f[i])) {
            free(c);
            return INVALID_NUMBER;
        }
        c[i] = f[i];
    }

    sc = horner_shift(c, n, a, eps);
    if (sc != OK) {
        free(c);
        return sc;
    }

    *result = c;
    return OK;
}

/* ===================== проверка ответа ===================== */

/* Значение многочлена c0 + c1*t + ... + cn*t^n в точке t (схема Горнера):
     c0 + t*(c1 + t*(c2 + ... + t*cn))
   считаем изнутри наружу: result = cn, потом result = result*t + c[i] для i = n-1, ..., 0 */
status_code eval_poly(const double *c, const int n, const double t, double *result) {
    if (c == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    if (n < 0) {
        return INVALID_DEGREE;
    }
    double value = c[n];
    for (int i = n - 1; i >= 0; i--) {
        value = value * t + c[i];
    }
    /* огромные коэффициенты в степени могут дать бесконечность */
    if (!isfinite(value)) {
        return OVERFLOW_ERROR;
    }
    *result = value;
    return OK;
}

/* Проверка ответа: в точках x = -2, -1, 0, 1, 2 считаем f(x) и g(x - a);
   если переразложение верное, они должны совпасть.
   *matched = 1 - во всех точках совпало, 0 - где-то не совпало.
   Сравнение с относительной точностью: |f - g| <= eps * (1 + |f|).
   Почему не просто |f - g| < eps: при f около 1e6 погрешность округления
   сама около 1e-10, и "абсолютный" eps = 1e-9 был бы слишком строгим.
   Слагаемое 1 нужно, чтобы около f = 0 сравнение оставалось обычным, абсолютным.
   Если значения слишком большие (бесконечность), вернем OVERFLOW_ERROR -
   тогда проверку просто не печатаем как "ок" */
status_code check_reexpand(const double *f, const double *g, const int n, const double a,
                           const double eps, int *matched) {
    if (f == NULL || g == NULL || matched == NULL) {
        return INVALID_ARGS;
    }
    if (!isfinite(eps) || eps <= 0) {
        return INVALID_EPS;
    }
    *matched = 1;
    for (int x = -2; x <= 2; x++) {
        double value_f = 0.0;
        double value_g = 0.0;
        status_code sc = eval_poly(f, n, (double)x, &value_f);
        if (sc != OK) {
            return sc;
        }
        /* g - многочлен от (x - a), поэтому подставляем t = x - a */
        sc = eval_poly(g, n, (double)x - a, &value_g);
        if (sc != OK) {
            return sc;
        }
        if (fabs(value_f - value_g) > eps * (1.0 + fabs(value_f))) {
            *matched = 0;
        }
    }
    return OK;
}

/* ===================== ввод (только для main) ===================== */

/* Строка из командной строки -> double. Разрешены обычные записи: 3, -2.5, 1e-9, .5
   strtod(str, &end) читает число с начала строки и в end кладет адрес первого
   непрочитанного символа. Если *end != '\0' - после числа есть мусор ("12abc").
   strtod сам пропускает пробелы в начале, а мы их не разрешаем: " 5" - ошибка.
   "inf", "nan" и слишком большие числа (1e999 -> strtod дает бесконечность)
   strtod читает, но считать с ними нельзя -> OVERFLOW_ERROR */
status_code parse_double(const char *str, double *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    /* isspace принимает unsigned char: русская буква в char отрицательна,
       и без приведения было бы неопределенное поведение */
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }
    char *end = NULL;
    double value = strtod(str, &end);
    /* end == str - ни одной цифры не прочитано ("abc"), *end != '\0' - мусор после числа */
    if (end == str || *end != '\0') {
        return INVALID_NUMBER;
    }
    if (!isfinite(value)) {
        return OVERFLOW_ERROR;
    }
    *number = value;
    return OK;
}

/* ===================== вывод (только для main) ===================== */

/* Печать "x" или "(x - a)": основание степеней многочлена.
   При a, близком к 0 (|a| < eps), пишем просто x - иначе было бы "(x - 0)".
   При отрицательном a пишем "(x + 2)" вместо "(x - -2)".
   Числа печатаем через %.10g: до 10 значащих цифр, без лишних нулей (0.5, а не 0.500000) */
status_code print_base(const double a, const double eps) {
    if (fabs(a) < eps) {
        printf("x");
    } else if (a > 0) {
        printf("(x - %.10g)", a);
    } else {
        printf("(x + %.10g)", -a);
    }
    return OK;
}

/* Печать многочлена c0 + c1*b + c2*b^2 + ... + cn*b^n, где b - это x или (x - a)
   (печатает print_base). Нулевые слагаемые (|c| < eps) пропускаем, знак минус
   пишем красиво: "6 - 4*x", а не "6 + -4*x". Если все коэффициенты нулевые - "0" */
status_code print_poly(const double *c, const int n, const double a, const double eps) {
    if (c == NULL) {
        return INVALID_ARGS;
    }
    /* first = 1, пока не напечатали ни одного слагаемого: у первого слагаемого
       знак пишется прямо с числом ("-3*x"), у следующих - через " + " / " - " */
    int first = 1;
    for (int i = 0; i <= n; i++) {
        /* нулевой коэффициент - слагаемого нет */
        if (fabs(c[i]) < eps) {
            continue;
        }
        if (first) {
            printf("%.10g", c[i]);
            first = 0;
        } else if (c[i] < 0) {
            printf(" - %.10g", -c[i]);
        } else {
            printf(" + %.10g", c[i]);
        }
        /* множитель: i = 0 - без него, i = 1 - "*x", i >= 2 - "*x^i" */
        if (i >= 1) {
            printf("*");
            print_base(a, eps);
        }
        if (i >= 2) {
            printf("^%d", i);
        }
    }
    if (first) {
        printf("0");
    }
    return OK;
}

/* Печать условия: "f(x) = 1 + 2*x + 3*x^2, a = 1" */
status_code print_task(const double *f, const int n, const double a, const double eps) {
    if (f == NULL) {
        return INVALID_ARGS;
    }
    printf("f(x) = ");
    /* f записан по степеням самого x, поэтому основание печатаем с a = 0 */
    print_poly(f, n, 0.0, eps);
    printf(", a = %.10g\n", a);
    return OK;
}

/* Печать ответа (g - массив g0..gn) и проверки:
     g = 6, 8, 3
     f(x) = 6 + 8*(x - 1) + 3*(x - 1)^2
     check at x = -2..2: ok */
status_code print_answer(const double *f, const double *g, const int n, const double a,
                         const double eps) {
    if (f == NULL || g == NULL) {
        return INVALID_ARGS;
    }
    /* сначала все коэффициенты списком (тут видны и нулевые) */
    printf("g = ");
    for (int i = 0; i <= n; i++) {
        if (i > 0) {
            printf(", ");
        }
        printf("%.10g", g[i]);
    }
    printf("\n");

    /* потом тот же многочлен по степеням (x - a) */
    printf("f(x) = ");
    print_poly(g, n, a, eps);
    printf("\n");

    /* и проверка подстановкой: f(x) и g(x - a) в нескольких точках */
    int matched = 0;
    status_code sc = check_reexpand(f, g, n, a, eps, &matched);
    if (sc == OVERFLOW_ERROR) {
        printf("check at x = -2..2: skipped, the values are too big\n");
    } else if (sc != OK) {
        return sc;
    } else if (matched) {
        printf("check at x = -2..2: ok\n");
    } else {
        printf("check at x = -2..2: values differ by more than eps\n");
    }
    return OK;
}

/* Текст ошибки для статуса, который вернула функция переразложения.
   Строка сообщения кладется в *message (строковые литералы живут всю программу,
   поэтому возвращать их адрес можно) */
status_code error_text(const status_code sc, const char **message) {
    if (message == NULL) {
        return INVALID_ARGS;
    }
    if (sc == INVALID_EPS) {
        *message = "eps must be greater than 0";
    } else if (sc == INVALID_DEGREE) {
        *message = "degree must be 0 or more";
    } else if (sc == INVALID_NUMBER) {
        *message = "a and coefficients must be usual numbers (not inf or nan)";
    } else if (sc == OVERFLOW_ERROR) {
        *message = "the result is too big";
    } else if (sc == ALLOC_ERROR) {
        *message = "not enough memory";
    } else {
        /* INVALID_ARGS (NULL) из main и демонстрации прийти не может.
           Если сообщение появилось - ошибка в самой программе */
        *message = "internal error";
    }
    return OK;
}

/* Один пример демонстрации: условие, потом ответ или ошибка.
   f - те же коэффициенты, что передали в polynomial_reexpand (для печати и проверки);
   sc и g - то, что она вернула */
status_code print_demo(const double *f, const int n, const double a, const status_code sc,
                       const double *g) {
    if (f == NULL) {
        return INVALID_ARGS;
    }
    print_task(f, n, a, EPS_DEMO);
    if (sc == OK) {
        if (g == NULL) {
            return INVALID_ARGS;
        }
        print_answer(f, g, n, a, EPS_DEMO);
    } else {
        const char *message = NULL;
        error_text(sc, &message);
        printf("-> error: %s\n", message);
    }
    printf("\n");
    return OK;
}

/* Демонстрация: готовые вызовы polynomial_reexpand с разным количеством аргументов.
   Каждый пример: 1) массив f - коэффициенты (только для печати и проверки);
   2) вызов polynomial_reexpand, коэффициенты передаются через ... по одному;
   3) печать; 4) free(g). free(NULL) ничего не делает, поэтому после ошибки
   (g остался NULL) free тоже безопасен.
   Массивы f здесь - не буферы, а готовые данные примера (как строки в задании 8) */
status_code run_demo(void) {
    double *g = NULL;
    status_code sc = OK;

    printf("Re-expansion f(x) = f0 + f1*x + ... + fn*x^n\n");
    printf("into f(x) = g0 + g1*(x - a) + ... + gn*(x - a)^n, eps = %g\n\n", EPS_DEMO);

    /* 1 + 2x + 3x^2 в точке 1: g0 = f(1) = 6, g1 = f'(1) = 8, g2 = 3 */
    const double f1[] = {1.0, 2.0, 3.0};
    sc = polynomial_reexpand(EPS_DEMO, 1.0, &g, 2, f1[0], f1[1], f1[2]);
    print_demo(f1, 2, 1.0, sc, g);
    free(g);
    g = NULL;   /* чтобы в следующем примере при ошибке не напечатать и не освободить старое */

    /* x^3 = ((x - 2) + 2)^3 = 8 + 12(x - 2) + 6(x - 2)^2 + (x - 2)^3 */
    const double f2[] = {0.0, 0.0, 0.0, 1.0};
    sc = polynomial_reexpand(EPS_DEMO, 2.0, &g, 3, f2[0], f2[1], f2[2], f2[3]);
    print_demo(f2, 3, 2.0, sc, g);
    free(g);
    g = NULL;

    /* степень 0: константа не зависит от x, g0 = f0 */
    const double f3[] = {5.0};
    sc = polynomial_reexpand(EPS_DEMO, 3.0, &g, 0, f3[0]);
    print_demo(f3, 0, 3.0, sc, g);
    free(g);
    g = NULL;

    /* 1 + x + x^2 + x^3 + x^4 в точке 1: биномиальные коэффициенты 5, 10, 10, 5, 1 */
    const double f4[] = {1.0, 1.0, 1.0, 1.0, 1.0};
    sc = polynomial_reexpand(EPS_DEMO, 1.0, &g, 4, f4[0], f4[1], f4[2], f4[3], f4[4]);
    print_demo(f4, 4, 1.0, sc, g);
    free(g);
    g = NULL;

    /* отрицательное a: x^2 = ((x + 2) - 2)^2 = 4 - 4(x + 2) + (x + 2)^2 */
    const double f5[] = {0.0, 0.0, 1.0};
    sc = polynomial_reexpand(EPS_DEMO, -2.0, &g, 2, f5[0], f5[1], f5[2]);
    print_demo(f5, 2, -2.0, sc, g);
    free(g);
    g = NULL;

    /* a = 0: (x - 0) = x, коэффициенты не меняются */
    const double f6[] = {2.0, -1.0, 4.0};
    sc = polynomial_reexpand(EPS_DEMO, 0.0, &g, 2, f6[0], f6[1], f6[2]);
    print_demo(f6, 2, 0.0, sc, g);
    free(g);
    g = NULL;

    /* дробные числа: x^2 - 0.2x + 0.01 = (x - 0.1)^2. В double 0.1 и 0.01
       хранятся неточно, и вместо g0 = 0 выходит число порядка 1e-18:
       eps превращает этот шум в 0 */
    const double f7[] = {0.01, -0.2, 1.0};
    sc = polynomial_reexpand(EPS_DEMO, 0.1, &g, 2, f7[0], f7[1], f7[2]);
    print_demo(f7, 2, 0.1, sc, g);
    free(g);
    g = NULL;

    /* дробное a и отрицательные коэффициенты */
    const double f8[] = {1.0, -3.5, 0.0, 2.0};
    sc = polynomial_reexpand(EPS_DEMO, 0.5, &g, 3, f8[0], f8[1], f8[2], f8[3]);
    print_demo(f8, 3, 0.5, sc, g);
    free(g);
    g = NULL;

    printf("Errors:\n\n");

    /* eps = 0: точность должна быть положительной */
    sc = polynomial_reexpand(0.0, 1.0, &g, 2, f1[0], f1[1], f1[2]);
    printf("eps = 0: ");
    print_demo(f1, 2, 1.0, sc, g);
    free(g);
    g = NULL;

    /* отрицательный eps */
    sc = polynomial_reexpand(-1e-9, 1.0, &g, 2, f1[0], f1[1], f1[2]);
    printf("eps = -1e-9: ");
    print_demo(f1, 2, 1.0, sc, g);
    free(g);
    g = NULL;

    /* отрицательная степень: коэффициентов после n нет вообще.
       Условие (f) напечатать нельзя - печатаем вручную */
    sc = polynomial_reexpand(EPS_DEMO, 1.0, &g, -1);
    if (sc == OK) {
        printf("degree -1: g = %.10g\n\n", g[0]);
    } else {
        const char *message = NULL;
        error_text(sc, &message);
        printf("degree -1: -> error: %s\n\n", message);
    }
    free(g);
    g = NULL;

    /* бесконечность среди коэффициентов (INFINITY - макрос из math.h) */
    const double f9[] = {1.0, INFINITY};
    sc = polynomial_reexpand(EPS_DEMO, 1.0, &g, 1, f9[0], f9[1]);
    print_demo(f9, 1, 1.0, sc, g);
    free(g);
    g = NULL;

    /* переполнение: g0 = a^2 = 1e400 - больше максимального double */
    sc = polynomial_reexpand(EPS_DEMO, 1e200, &g, 2, f5[0], f5[1], f5[2]);
    print_demo(f5, 2, 1e200, sc, g);
    free(g);
    g = NULL;

    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    /* Без аргументов - демонстрация на готовых примерах */
    if (argc == 1) {
        if (run_demo() != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        return 0;
    }

    /* С аргументами нужно минимум eps, a и один коэффициент f0 */
    if (argc < 4) {
        fprintf(stderr, "Usage: lab2_task10                            (demo)\n");
        fprintf(stderr, "       lab2_task10 <eps> <a> <f0> [f1 ... fn]\n");
        return 1;
    }

    /* eps и a - первые два аргумента. Ошибки разбора: не число -> INVALID_NUMBER,
       inf / nan / слишком большое -> OVERFLOW_ERROR */
    double eps = 0.0;
    double a = 0.0;
    status_code sc = parse_double(argv[1], &eps);
    if (sc != OK) {
        fprintf(stderr, "Error: eps '%s' is not a usual number\n", argv[1]);
        return 1;
    }
    sc = parse_double(argv[2], &a);
    if (sc != OK) {
        fprintf(stderr, "Error: a '%s' is not a usual number\n", argv[2]);
        return 1;
    }

    /* Коэффициенты f0..fn - все остальные аргументы: их argc - 3 штуки,
       значит степень n = argc - 4. Массив под них - в динамической памяти:
       сколько их, заранее неизвестно */
    int n = argc - 4;
    double *f = (double *)malloc(((size_t)n + 1) * sizeof(double));
    if (f == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    for (int i = 0; i <= n; i++) {
        /* f[i] лежит в argv[i + 3]: argv[0] - имя программы, 1 - eps, 2 - a */
        sc = parse_double(argv[i + 3], &f[i]);
        if (sc != OK) {
            fprintf(stderr, "Error: coefficient '%s' is not a usual number\n", argv[i + 3]);
            free(f);
            return 1;
        }
    }

    /* Само переразложение. Здесь вызываем вариант с массивом: число
       коэффициентов известно только сейчас, а список ... пишется в коде заранее */
    double *g = NULL;
    sc = reexpand_array(eps, a, &g, n, f);
    if (sc != OK) {
        /* INVALID_EPS (eps <= 0), OVERFLOW_ERROR (огромные числа), ALLOC_ERROR.
           g при ошибке не выделен - освобождаем только f */
        const char *message = NULL;
        error_text(sc, &message);
        fprintf(stderr, "Error: %s\n", message);
        free(f);
        return 1;
    }

    /* Печать: условие, коэффициенты g, многочлен по степеням (x - a) и проверка */
    print_task(f, n, a, eps);
    print_answer(f, g, n, a, eps);

    free(g);
    free(f);
    return 0;
}
