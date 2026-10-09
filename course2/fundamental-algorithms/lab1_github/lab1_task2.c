#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL вместо указателя, eps <= 0 и т.п. */
    INVALID_NUMBER,    /* строка не число */
    OUT_OF_RANGE,      /* число вне допустимых границ */
    NO_ROOT,           /* на отрезке нет смены знака */
    NO_MEMORY,         /* malloc вернул NULL */
    PRECISION_LIMIT    /* ответ есть, но заданную точность double не позволяет получить */
} status_code;

/* Ограничения, чтобы программа не считала вечно */
#define MAX_POW2_N   (1L << 26)    /* предел n для пределов с удвоением n */
#define MAX_SERIES_N 500000000L    /* предел числа слагаемых знакочередующихся рядов */
#define MAX_GAMMA_M  64            /* предел m для предела с биномами */
#define MAX_BLOCKS   (1L << 14)    /* предел числа блоков в ряде для gamma */
#define MAX_T        (1L << 24)    /* предел t для произведения по простым */
#define MAX_ITER     200           /* предел шагов дихотомии: double больше ~60 делений не выдерживает */

/* Строка -> double. Принимаем только конечное число без мусора */
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

/* ===== Функции для уравнений вида f(x) = 0 =====
   Значение кладем в rez, возвращаем статус. param нужен только для gamma */
status_code eq_e(const double x, const double param, double *rez) {
    (void)param;              /* параметр не нужен - говорим компилятору, что это нарочно */
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (x <= 0) {
        return OUT_OF_RANGE;  /* логарифм только от положительного числа */
    }
    *rez = log(x) - 1;        /* ln x = 1 */
    return OK;
}

status_code eq_pi(const double x, const double param, double *rez) {
    (void)param;
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    /* cos x = -1  <=>  1 + cos x = 2cos^2(x/2) = 0  <=>  cos(x/2) = 0 */
    *rez = cos(x / 2);
    return OK;
}

status_code eq_ln2(const double x, const double param, double *rez) {
    (void)param;
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = exp(x) - 2;        /* e^x = 2 */
    if (isinf(*rez)) {
        return OUT_OF_RANGE;  /* x слишком большой - e^x не помещается в double */
    }
    return OK;
}

status_code eq_sqrt2(const double x, const double param, double *rez) {
    (void)param;
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = x * x - 2;         /* x^2 = 2 */
    if (isinf(*rez)) {
        return OUT_OF_RANGE;
    }
    return OK;
}

status_code eq_gamma(const double x, const double predel, double *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = exp(-x) - predel;  /* e^(-x) = predel */
    if (isinf(*rez)) {
        return OUT_OF_RANGE;
    }
    return OK;
}

/* ===== Метод дихотомии (общий для всех уравнений) =====
   f(x, param) = 0 на [a, b]; param - дополнительное число для функции
   (нужно для gamma, глобальные переменные запрещены) */
status_code dichotomy(status_code (*f)(const double, const double, double *), const double param,
                      double a, double b, const double eps, double *root) {
    /* b - a < eps: отрезок перевернутый или короче точности */
    if (f == NULL || root == NULL || eps <= 0 || b - a < eps) {
        return INVALID_ARGS;
    }

    double fa = 0.0, fb = 0.0;
    status_code sc = f(a, param, &fa);
    if (sc != OK) {
        return sc;
    }
    sc = f(b, param, &fb);
    if (sc != OK) {
        return sc;
    }
    /* корень гарантирован, только если на концах НЕ один и тот же знак
       (0 на конце тоже подходит - тогда этот конец и есть корень) */
    if ((fa > 0 && fb > 0) || (fa < 0 && fb < 0)) {
        return NO_ROOT;
    }

    /* каждый шаг делит отрезок пополам. Число шагов ограничено: когда концы
       стали соседними double, середина уже не делит отрезок и цикл бы не кончился */
    for (int shag = 0; b - a > eps; shag++) {
        if (shag >= MAX_ITER) {
            *root = a + (b - a) / 2;
            return PRECISION_LIMIT;
        }
        double mid = a + (b - a) / 2;
        double fm = 0.0;
        sc = f(mid, param, &fm);
        if (sc != OK) {
            return sc;
        }
        /* знак строго как на левом конце - корень в правой половине */
        if ((fm > 0 && fa > 0) || (fm < 0 && fa < 0)) {
            a = mid;
            fa = fm;
        } else {
            b = mid;
        }
    }

    *root = a + (b - a) / 2;
    return OK;
}

/* ===================== e ===================== */

/* e = lim (1 + 1/n)^n. Сравниваем значения при n и 2n */
status_code e_limit(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double prev = pow(1.0 + 1.0, 1.0);   /* n = 1 */
    for (long n = 2; n <= MAX_POW2_N; n *= 2) {
        double cur = pow(1.0 + 1.0 / (double)n, (double)n);
        if (fabs(cur - prev) < eps) {
            *rez = cur;
            return OK;
        }
        prev = cur;
    }
    *rez = prev;
    return PRECISION_LIMIT;
}

/* e = sum 1/n!. Каждое слагаемое = предыдущее / n */
status_code e_series(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double slag = 1.0;   /* 1/0! */
    double summa = 0.0;
    long n = 0;
    while (slag >= eps) {
        summa += slag;
        n++;
        slag /= (double)n;
    }
    *rez = summa;
    return OK;
}

/* e: корень ln x = 1 на [2, 3] */
status_code e_equation(const double eps, double *rez) {
    return dichotomy(eq_e, 0.0, 2.0, 3.0, eps, rez);
}

/* ===================== pi ===================== */

/* pi = lim (2^n n!)^4 / (n ((2n)!)^2).
   Факториалы огромные, поэтому считаем через отношение соседних членов:
   a(n+1) = a(n) * 4n(n+1) / (2n+1)^2, a(1) = 4 */
status_code pi_limit(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double a = 4.0;
    double prev = a;          /* значение при последней степени двойки */
    long next_check = 2;
    for (long n = 1; n < MAX_POW2_N; n++) {
        double dn = (double)n;
        a *= 4.0 * dn * (dn + 1) / ((2 * dn + 1) * (2 * dn + 1));
        /* теперь в a лежит a(n+1); при n+1 = 2, 4, 8 ... сравниваем с a(n/2) */
        if (n + 1 == next_check) {
            if (fabs(a - prev) < eps) {
                *rez = a;
                return OK;
            }
            prev = a;
            next_check *= 2;
        }
    }
    *rez = a;
    return PRECISION_LIMIT;
}

/* pi = 4 * sum (-1)^(n-1) / (2n-1). Ряд знакочередующийся:
   ошибка меньше первого отброшенного слагаемого */
status_code pi_series(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double summa = 0.0;
    double znak = 1.0;
    for (long n = 1; n <= MAX_SERIES_N; n++) {
        summa += znak * 4.0 / (double)(2 * n - 1);
        znak = -znak;
        if (4.0 / (double)(2 * n + 1) < eps) {
            *rez = summa;
            return OK;
        }
    }
    *rez = summa;
    return PRECISION_LIMIT;
}

/* pi: cos x = -1 на [2, 4] (через cos(x/2) = 0, см. eq_pi) */
status_code pi_equation(const double eps, double *rez) {
    return dichotomy(eq_pi, 0.0, 2.0, 4.0, eps, rez);
}

/* ===================== ln 2 ===================== */

/* ln 2 = lim n (2^(1/n) - 1), n = 1, 2, 4, ... */
status_code ln2_limit(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double prev = 1.0 * (pow(2.0, 1.0) - 1);   /* n = 1 */
    for (long n = 2; n <= MAX_POW2_N; n *= 2) {
        double dn = (double)n;
        double cur = dn * (pow(2.0, 1.0 / dn) - 1);
        if (fabs(cur - prev) < eps) {
            *rez = cur;
            return OK;
        }
        prev = cur;
    }
    *rez = prev;
    return PRECISION_LIMIT;
}

/* ln 2 = sum (-1)^(n-1) / n, ряд знакочередующийся */
status_code ln2_series(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double summa = 0.0;
    double znak = 1.0;
    for (long n = 1; n <= MAX_SERIES_N; n++) {
        summa += znak / (double)n;
        znak = -znak;
        if (1.0 / (double)(n + 1) < eps) {
            *rez = summa;
            return OK;
        }
    }
    *rez = summa;
    return PRECISION_LIMIT;
}

/* ln 2: корень e^x = 2 на [0, 1] */
status_code ln2_equation(const double eps, double *rez) {
    return dichotomy(eq_ln2, 0.0, 0.0, 1.0, eps, rez);
}

/* ===================== sqrt(2) ===================== */

/* sqrt2 = lim x(n), x(n+1) = x(n) - x(n)^2 / 2 + 1, x(0) = -0.5 */
status_code sqrt2_limit(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double x = -0.5;
    for (int shag = 0; shag < 10000; shag++) {
        double next = x - x * x / 2 + 1;
        if (fabs(next - x) < eps) {
            *rez = next;
            return OK;
        }
        x = next;
    }
    *rez = x;
    return PRECISION_LIMIT;
}

/* sqrt2 = произведение 2^(2^-k), k = 2, 3, ...
   Множители стремятся к 1; останавливаемся, когда произведение почти не меняется */
status_code sqrt2_series(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double proizv = 1.0;
    double pokazatel = 0.25;   /* 2^-2 */
    for (int k = 2; k < 200; k++) {
        double next = proizv * pow(2.0, pokazatel);
        if (fabs(next - proizv) < eps) {
            *rez = next;
            return OK;
        }
        proizv = next;
        pokazatel /= 2;
    }
    *rez = proizv;
    return PRECISION_LIMIT;
}

/* sqrt2: положительный корень x^2 = 2 на [1, 2] */
status_code sqrt2_equation(const double eps, double *rez) {
    return dichotomy(eq_sqrt2, 0.0, 1.0, 2.0, eps, rez);
}

/* ===================== gamma ===================== */

/* gamma = lim sum_{k=1}^{m} C(m,k) (-1)^k / k * ln(k!).
   Слагаемые огромные и с разными знаками - в double формула
   теряет точность примерно после m = 40. Сравниваем m и 2m. */
status_code gamma_limit(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double prev = 0.0;   /* при m = 1: C(1,1) * (-1) * ln(1!) = 0 */
    for (int m = 2; m <= MAX_GAMMA_M; m *= 2) {
        double summa = 0.0;
        double binom = 1.0;      /* C(m, 0) */
        double ln_fact = 0.0;    /* ln(0!) */
        double znak = 1.0;
        for (int k = 1; k <= m; k++) {
            binom = binom * (double)(m - k + 1) / (double)k;   /* C(m,k) из C(m,k-1) */
            ln_fact += log((double)k);                          /* ln(k!) = ln((k-1)!) + ln k */
            znak = -znak;
            summa += binom * znak / (double)k * ln_fact;
        }
        if (fabs(summa - prev) < eps) {
            *rez = summa;
            return OK;
        }
        /* последовательность растет; если упала больше чем на eps - это уже ошибки округления */
        if (prev - summa >= eps) {
            *rez = prev;
            return PRECISION_LIMIT;
        }
        prev = summa;
    }
    *rez = prev;
    return PRECISION_LIMIT;
}

/* gamma = -pi^2/6 + sum_{k>=2} (1/floor(sqrt k)^2 - 1/k).
   Внутри блока k = n^2 .. (n+1)^2 - 1 floor(sqrt k) = n, поэтому суммируем блоками.
   Ряд сходится медленно (хвост примерно block * n), поэтому добавляем
   оценку хвоста и сравниваем результат при n и 2n. */
status_code gamma_series(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    const double pi = 3.14159265358979323846;
    double summa = -pi * pi / 6;
    double prev = 0.0;
    long next_check = 1;
    for (long n = 1; n <= MAX_BLOCKS; n++) {
        double kv = (double)n * (double)n;
        double block = 0.0;
        long start = (n == 1) ? 2 : n * n;   /* ряд начинается с k = 2 */
        for (long k = start; k < (n + 1) * (n + 1); k++) {
            block += 1.0 / kv - 1.0 / (double)k;
        }
        summa += block;
        double s_hvostom = summa + block * (double)n;   /* + оценка хвоста */

        if (n == next_check) {
            if (n > 1 && fabs(s_hvostom - prev) < eps) {
                *rez = s_hvostom;
                return OK;
            }
            prev = s_hvostom;
            next_check *= 2;
        }
    }
    *rez = prev;
    return PRECISION_LIMIT;
}

/* predel = lim ln t * prod_{p <= t, p простое} (p - 1) / p.
   Простые ищем решетом Эратосфена (массив на MAX_T байт - в куче) */
status_code mertens_limit(const double eps, double *predel) {
    if (predel == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    char *sostavnoe = calloc((size_t)MAX_T + 1, 1);   /* 0 - простое, 1 - составное */
    if (sostavnoe == NULL) {
        return NO_MEMORY;
    }

    double proizv = 1.0;
    double prev = 0.0;    /* значение в прошлой контрольной точке */
    int est_prev = 0;     /* 1 - прошлая точка уже была, есть с чем сравнивать */
    double cur = 0.0;
    long next_check = 16;
    status_code sc = PRECISION_LIMIT;
    for (long t = 2; t <= MAX_T; t++) {
        if (!sostavnoe[t]) {
            proizv *= (double)(t - 1) / (double)t;
            /* вычеркиваем кратные t, начиная с t*t (long long: t*t не влезет в long на Windows) */
            for (long long j = (long long)t * t; j <= MAX_T; j += t) {
                sostavnoe[j] = 1;
            }
        }
        /* при t = 16, 32, 64 ... сравниваем с прошлым значением */
        if (t == next_check) {
            cur = log((double)t) * proizv;
            if (est_prev && fabs(cur - prev) < eps) {
                sc = OK;
                break;
            }
            prev = cur;
            est_prev = 1;
            next_check *= 2;
        }
    }
    *predel = cur;
    free(sostavnoe);
    return sc;
}

/* gamma: корень e^(-x) = predel на [0, 1] */
status_code gamma_equation(const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double predel = 0.0;
    status_code sc_limit = mertens_limit(eps, &predel);
    if (sc_limit != OK && sc_limit != PRECISION_LIMIT) {
        return sc_limit;
    }
    status_code sc = dichotomy(eq_gamma, predel, 0.0, 1.0, eps, rez);
    if (sc != OK) {
        return sc;     /* ошибка или PRECISION_LIMIT самой дихотомии */
    }
    return sc_limit;   /* OK или PRECISION_LIMIT от предела */
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
    if (argc != 2) {
        fprintf(stderr, "Usage: lab1_task2 <eps>\n");
        fprintf(stderr, "Example: lab1_task2 1e-6\n");
        return 1;
    }

    double eps = 0.0;
    status_code sc = parse_double(argv[1], &eps);
    if (sc == INVALID_NUMBER) {
        fprintf(stderr, "Error: '%s' is not a number\n", argv[1]);
        return 1;
    } else if (sc != OK) {
        fprintf(stderr, "Error: bad number '%s'\n", argv[1]);
        return 1;
    }
    if (eps <= 0 || eps >= 1) {
        fprintf(stderr, "Error: eps must be between 0 and 1\n");
        return 1;
    }

    /* таблица: какую константу каким методом считать.
       Массив указателей на функции - чтобы не писать 15 одинаковых блоков */
    const char *names[5] = {"e", "pi", "ln 2", "sqrt 2", "gamma"};
    const double tochno[5] = {
        2.71828182845904523536, 3.14159265358979323846, 0.69314718055994530942,
        1.41421356237309504880, 0.57721566490153286061
    };
    status_code (*metody[5][3])(const double, double *) = {
        {e_limit, e_series, e_equation},
        {pi_limit, pi_series, pi_equation},
        {ln2_limit, ln2_series, ln2_equation},
        {sqrt2_limit, sqrt2_series, sqrt2_equation},
        {gamma_limit, gamma_series, gamma_equation}
    };
    const char *method_names[3] = {"limit", "series", "equation"};
    int digits = 0;
    if (digits_for_eps(eps, &digits) != OK) {
        fprintf(stderr, "Error: something went wrong\n");
        return 1;
    }

    printf("eps = %g\n\n", eps);
    printf("%-7s %-9s %-20s %-12s %s\n", "const", "method", "value", "error", "note");
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 3; j++) {
            double val = 0.0;
            sc = metody[i][j](eps, &val);
            if (sc == OK || sc == PRECISION_LIMIT) {
                printf("%-7s %-9s %-20.*f %-12.2e %s\n", names[i], method_names[j],
                       digits, val, fabs(val - tochno[i]),
                       (sc == OK) ? "" : "eps is too small for this method");
            } else if (sc == NO_MEMORY) {
                printf("%-7s %-9s Error: not enough memory\n", names[i], method_names[j]);
            } else if (sc == NO_ROOT || sc == OUT_OF_RANGE) {
                printf("%-7s %-9s Error: no root on the segment\n", names[i], method_names[j]);
            } else {
                printf("%-7s %-9s Error: something went wrong\n", names[i], method_names[j]);
            }
        }
    }
    printf("\nerror = |value - real value|, real value is taken from a table\n");
    return 0;
}
