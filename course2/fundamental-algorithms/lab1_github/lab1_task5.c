#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <ctype.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL, eps <= 0 */
    INVALID_NUMBER,    /* строка не число */
    OUT_OF_RANGE,      /* число вне допустимых границ */
    DIVERGES,          /* ряд расходится при таком x */
    OVERFLOW_ERROR,    /* получилась бесконечность */
    PRECISION_LIMIT    /* ответ есть, но заданная точность недостижима в double */
} status_code;

#define MAX_TERMS 100000000L       /* предел числа слагаемых ряда */
#define MAX_PARTS (1L << 24)       /* предел числа отрезков для интеграла */

/* Строка -> double: конечное число без мусора */
status_code parse_double(const char *str, double *chislo) {
    if (str == NULL || chislo == NULL) {
        return INVALID_ARGS;
    }
    /* strtod сам пропускает пробелы в начале, а нам " 7" не подходит */
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }
    char *end = NULL;
    double val = strtod(str, &end);
    /* end == str - не прочитали ни одной цифры; *end != '\0' - после числа мусор */
    if (end == str || *end != '\0') {
        return INVALID_NUMBER;
    }
    /* слишком большое число strtod превращает в бесконечность;
       кроме того, strtod понимает слова "inf" и "nan" - их тоже не принимаем */
    if (isinf(val) || isnan(val)) {
        return OUT_OF_RANGE;
    }
    *chislo = val;
    return OK;
}

/* ===================== Ряды =====================
   Все 4 ряда считаем одинаково: следующее слагаемое = текущее * otnoshenie(n).
   Так не нужны огромные факториалы и степени.
   Останавливаемся, когда |слагаемое| < eps. */

/* otnoshenie = slag(n+1) / slag(n) для каждого ряда; кладем его в rez.
   n - номер слагаемого, он не может быть отрицательным */
status_code ratio_a(const double x, const long n, double *rez) {
    if (rez == NULL || n < 0) {
        return INVALID_ARGS;
    }
    /* x^n / n!  ->  x / (n+1) */
    *rez = x / (double)(n + 1);
    return OK;
}

status_code ratio_b(const double x, const long n, double *rez) {
    if (rez == NULL || n < 0) {
        return INVALID_ARGS;
    }
    /* (-1)^n x^2n / (2n)!  ->  -x^2 / ((2n+1)(2n+2)) */
    double dn = (double)n;
    *rez = -x * x / ((2 * dn + 1) * (2 * dn + 2));
    return OK;
}

status_code ratio_c(const double x, const long n, double *rez) {
    if (rez == NULL || n < 0) {
        return INVALID_ARGS;
    }
    /* 3^3n (n!)^3 x^2n / (3n)!  ->  27 (n+1)^3 x^2 / ((3n+1)(3n+2)(3n+3)) = 9 (n+1)^2 x^2 / ((3n+1)(3n+2)) */
    double dn = (double)n;
    *rez = 9 * (dn + 1) * (dn + 1) * x * x / ((3 * dn + 1) * (3 * dn + 2));
    return OK;
}

status_code ratio_d(const double x, const long n, double *rez) {
    if (rez == NULL || n < 0) {
        return INVALID_ARGS;
    }
    /* (-1)^n (2n-1)!! x^2n / (2n)!!  ->  -(2n+1) x^2 / (2n+2) */
    double dn = (double)n;
    *rez = -(2 * dn + 1) * x * x / (2 * dn + 2);
    return OK;
}

/* Общая функция суммирования ряда.
   first - первое слагаемое, n0 - его номер, ratio - отношение соседних слагаемых */
status_code sum_series(const double x, const double eps, const double first, const long n0,
                       status_code (*ratio)(const double, const long, double *), double *summa) {
    if (ratio == NULL || summa == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double slag = first;
    double s = 0.0;
    double max_slag = fabs(first);   /* самое большое слагаемое - для оценки ошибки округления */

    for (long n = n0; n < n0 + MAX_TERMS; n++) {
        if (fabs(slag) < eps) {
            *summa = s;
            /* при больших слагаемых погрешность округления ~ max_slag * DBL_EPSILON.
               Если она больше eps - честно говорим, что точность не достигнута */
            if (max_slag * DBL_EPSILON > eps) {
                return PRECISION_LIMIT;
            }
            return OK;
        }
        s += slag;
        double otnoshenie = 0.0;
        status_code sc = ratio(x, n, &otnoshenie);
        if (sc != OK) {
            return sc;
        }
        slag *= otnoshenie;
        if (isinf(s) || isinf(slag)) {
            return OVERFLOW_ERROR;
        }
        /* запоминаем самое большое слагаемое (разница меньше eps для оценки не важна) */
        if (fabs(slag) - max_slag > eps) {
            max_slag = fabs(slag);
        }
    }
    *summa = s;
    return PRECISION_LIMIT;
}

/* a. sum x^n / n!, n от 0 (это e^x) */
status_code series_a(const double x, const double eps, double *summa) {
    return sum_series(x, eps, 1.0, 0, ratio_a, summa);
}

/* b. sum (-1)^n x^2n / (2n)!, n от 0 (это cos x) */
status_code series_b(const double x, const double eps, double *summa) {
    return sum_series(x, eps, 1.0, 0, ratio_b, summa);
}

/* c. sum 3^3n (n!)^3 x^2n / (3n)!, n от 0. Сходится только при |x| < 1.
   Граница области сходимости точная, поэтому тут сравнение без eps */
status_code series_c(const double x, const double eps, double *summa) {
    if (fabs(x) >= 1) {
        return DIVERGES;
    }
    return sum_series(x, eps, 1.0, 0, ratio_c, summa);
}

/* d. sum (-1)^n (2n-1)!! x^2n / (2n)!!, n от 1. Первое слагаемое -x^2/2.
   Берем |x| < 1 (при |x| = 1 сходится очень медленно, при |x| > 1 расходится) */
status_code series_d(const double x, const double eps, double *summa) {
    if (fabs(x) >= 1) {
        return DIVERGES;
    }
    return sum_series(x, eps, -x * x / 2, 1, ratio_d, summa);
}

/* ===================== Интегралы на [0, 1] =====================
   Метод средних прямоугольников: отрезок делим на n частей,
   в каждой берем значение в середине. Концы 0 и 1 не используются -
   это важно для c (в x = 1 функция уходит в бесконечность).
   Точность: удваиваем n, пока |I(2n) - I(n)| >= eps (правило Рунге).
   Подынтегральные функции проверяют, что x в области определения
   (границы точные, поэтому без eps), значение кладут в rez. */

status_code f_a(const double x, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (x <= 0 || x > 1) {
        return OUT_OF_RANGE;         /* x = 0 - деление на 0 */
    }
    *rez = log(1 + x) / x;
    return OK;
}

status_code f_b(const double x, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (x < 0 || x > 1) {
        return OUT_OF_RANGE;
    }
    *rez = exp(-x * x / 2);
    return OK;
}

status_code f_c(const double x, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (x < 0 || x >= 1) {
        return OUT_OF_RANGE;         /* x = 1 - деление на 0 и ln бесконечности */
    }
    *rez = log(1 / (1 - x));
    return OK;
}

status_code f_d(const double x, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (x < 0 || x > 1) {
        return OUT_OF_RANGE;
    }
    *rez = pow(x, x);
    return OK;
}

/* Интеграл f на [0, 1] при n частях */
status_code midpoint(status_code (*f)(const double, double *), const long n, double *rez) {
    if (f == NULL || rez == NULL || n <= 0) {
        return INVALID_ARGS;
    }
    double h = 1.0 / (double)n;
    double s = 0.0;
    for (long i = 0; i < n; i++) {
        double y = 0.0;
        status_code sc = f(((double)i + 0.5) * h, &y);
        if (sc != OK) {
            return sc;
        }
        s += y;
    }
    *rez = s * h;
    return OK;
}

status_code integral(status_code (*f)(const double, double *), const double eps, double *rez) {
    if (f == NULL || rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double prev = 0.0;
    status_code sc = midpoint(f, 1, &prev);
    if (sc != OK) {
        return sc;
    }
    for (long n = 2; n <= MAX_PARTS; n *= 2) {
        double cur = 0.0;
        sc = midpoint(f, n, &cur);
        if (sc != OK) {
            return sc;
        }
        if (fabs(cur - prev) < eps) {
            *rez = cur;
            return OK;
        }
        prev = cur;
    }
    *rez = prev;
    return PRECISION_LIMIT;
}

/* Печать одного результата (вывод отделен от вычислений) */
status_code print_result(const char *name, const status_code sc, const double val, const int digits) {
    if (name == NULL || digits < 0) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case OK:
            printf("  %-4s %.*f\n", name, digits, val);
            break;
        case PRECISION_LIMIT:
            printf("  %-4s %.*f  (eps is too small, this is the best we can get)\n", name, digits, val);
            break;
        case DIVERGES:
            printf("  %-4s series does not converge for this x (need |x| < 1)\n", name);
            break;
        case OVERFLOW_ERROR:
            printf("  %-4s result is too big (overflow)\n", name);
            break;
        default:
            printf("  %-4s error\n", name);
            break;
    }
    return OK;
}

/* Сколько знаков после точки печатать для данного eps (не больше 15 - предел double) */
status_code digits_for_eps(const double eps, int *digits) {
    if (digits == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    *digits = 1;
    double step = 0.1;
    while (step > eps && *digits < 15) {
        step /= 10;
        (*digits)++;
    }
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: lab1_task5 <eps> <x>\n");
        fprintf(stderr, "Example: lab1_task5 1e-6 0.5\n");
        return 1;
    }

    double eps = 0.0, x = 0.0;
    for (int i = 1; i <= 2; i++) {
        double *kuda = (i == 1) ? &eps : &x;
        status_code sc = parse_double(argv[i], kuda);
        if (sc == INVALID_NUMBER) {
            fprintf(stderr, "Error: '%s' is not a number\n", argv[i]);
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: bad number '%s'\n", argv[i]);
            return 1;
        }
    }
    if (eps <= 0 || eps >= 1) {
        fprintf(stderr, "Error: eps must be between 0 and 1\n");
        return 1;
    }

    int digits = 0;
    if (digits_for_eps(eps, &digits) != OK) {
        fprintf(stderr, "Error: something went wrong\n");
        return 1;
    }
    const char *names[4] = {"a.", "b.", "c.", "d."};
    status_code (*series[4])(const double, const double, double *) = {
        series_a, series_b, series_c, series_d
    };
    status_code (*funcs[4])(const double, double *) = {f_a, f_b, f_c, f_d};

    printf("eps = %g, x = %g\n\nSeries:\n", eps, x);
    for (int i = 0; i < 4; i++) {
        double val = 0.0;
        status_code sc = series[i](x, eps, &val);
        if (print_result(names[i], sc, val, digits) != OK) {
            fprintf(stderr, "Error: something went wrong\n");
            return 1;
        }
    }

    printf("\nIntegrals from 0 to 1:\n");
    for (int i = 0; i < 4; i++) {
        double val = 0.0;
        status_code sc = integral(funcs[i], eps, &val);
        if (print_result(names[i], sc, val, digits) != OK) {
            fprintf(stderr, "Error: something went wrong\n");
            return 1;
        }
    }
    return 0;
}
