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

/* Ограничения, чтобы программа не считала вечно.
   1L << 26 - это 2^26 (сдвиг единицы влево на 26 бит), около 67 миллионов */
#define MAX_POW2_N   (1LL << 26)   /* предел n для пределов с удвоением n */
#define MAX_SERIES_N 500000000LL   /* предел числа слагаемых знакочередующихся рядов */
#define MAX_GAMMA_M  64            /* предел m для предела с биномами */
#define MAX_BLOCKS   (1LL << 14)   /* предел числа блоков в ряде для gamma */
#define MAX_T        (1LL << 24)   /* предел t для произведения по простым */
#define MAX_ITER     200           /* предел шагов дихотомии: double больше ~60 делений не выдерживает */

/* Строка -> double. Принимаем только конечное число без мусора */
status_code parse_double(const char *str, double *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    /* strtod сам пропускает пробелы в начале, а нам " 7" не подходит */
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }

    /* strtod читает число с начала строки и записывает в end адрес первого
       символа, который уже не вошел в число. По нему видно, где чтение остановилось */
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

    *number = val;
    return OK;
}

/* ===== Функции для уравнений вида f(x) = 0 =====
   Значение кладем в result, возвращаем статус. param нужен только для gamma.
   У всех пяти функций одинаковые параметры - поэтому дихотомия ниже
   может принимать любую из них */
status_code eq_e(const double x, const double param, double *result) {
    (void)param;              /* параметр не нужен - говорим компилятору, что это нарочно */
    if (result == NULL) {
        return INVALID_ARGS;
    }
    if (x <= 0) {
        return OUT_OF_RANGE;  /* логарифм только от положительного числа */
    }
    *result = log(x) - 1;        /* ln x = 1 */
    return OK;
}

status_code eq_pi(const double x, const double param, double *result) {
    (void)param;
    if (result == NULL) {
        return INVALID_ARGS;
    }
    /* cos x = -1  <=>  1 + cos x = 2cos^2(x/2) = 0  <=>  cos(x/2) = 0.
       Зачем замена: 1 + cos x не бывает меньше нуля, то есть знак не меняется,
       и дихотомия корень бы не нашла. А cos(x/2) около корня меняет знак */
    *result = cos(x / 2);
    return OK;
}

status_code eq_ln2(const double x, const double param, double *result) {
    (void)param;
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = exp(x) - 2;        /* e^x = 2 */
    if (isinf(*result)) {
        return OUT_OF_RANGE;  /* x слишком большой - e^x не помещается в double */
    }
    return OK;
}

status_code eq_sqrt2(const double x, const double param, double *result) {
    (void)param;
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = x * x - 2;         /* x^2 = 2 */
    if (isinf(*result)) {
        return OUT_OF_RANGE;
    }
    return OK;
}

status_code eq_gamma(const double x, const double limit, double *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = exp(-x) - limit;  /* e^(-x) = limit */
    if (isinf(*result)) {
        return OUT_OF_RANGE;
    }
    return OK;
}

/* ===== Метод дихотомии (общий для всех уравнений) =====
   f(x, param) = 0 на [a, b]; param - дополнительное число для функции
   (нужно для gamma, глобальные переменные запрещены).
   Первый параметр - указатель на функцию: сюда передается имя функции (eq_e, eq_pi ...),
   и внутри она вызывается как f(x, param, &value) */
status_code dichotomy(status_code (*f)(const double, const double, double *), const double param,
                      double a, double b, const double eps, double *root) {
    /* b - a < eps: отрезок перевернутый или короче точности */
    if (f == NULL || root == NULL || eps <= 0 || b - a < eps) {
        return INVALID_ARGS;
    }

    /* значения функции на концах отрезка */
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

    /* Идея: берем середину mid и смотрим знак f(mid).
       Корень лежит в той половине, на концах которой знаки разные.
       Каждый шаг делит отрезок пополам, пока он не станет короче eps.
       Число шагов ограничено: когда концы стали соседними double,
       середина уже не делит отрезок и цикл бы не кончился */
    for (int iter = 0; b - a > eps; iter++) {
        if (iter >= MAX_ITER) {
            *root = a + (b - a) / 2;
            return PRECISION_LIMIT;
        }
        /* a + (b - a) / 2 вместо (a + b) / 2: сумма a + b могла бы переполниться */
        double mid = a + (b - a) / 2;
        double fm = 0.0;
        sc = f(mid, param, &fm);
        if (sc != OK) {
            return sc;
        }
        /* знак строго как на левом конце - корень в правой половине: сдвигаем a.
           Иначе корень в левой половине (или mid сам корень): сдвигаем b */
        if ((fm > 0 && fa > 0) || (fm < 0 && fa < 0)) {
            a = mid;
            fa = fm;
        } else {
            b = mid;
        }
    }

    /* отрезок короче eps - любая его точка подходит, берем середину */
    *root = a + (b - a) / 2;
    return OK;
}

/* ===================== e ===================== */

/* Общая схема для пределов: считаем значение при n = 1, 2, 4, 8 ...
   Как только два соседних значения отличаются меньше чем на eps - останавливаемся.
   Если дошли до предела MAX_POW2_N и не остановились - PRECISION_LIMIT */

/* e = lim (1 + 1/n)^n. Сравниваем значения при n и 2n */
status_code e_limit(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double prev = pow(1.0 + 1.0, 1.0);   /* n = 1 */
    for (long long n = 2; n <= MAX_POW2_N; n *= 2) {
        double cur = pow(1.0 + 1.0 / (double)n, (double)n);
        if (fabs(cur - prev) < eps) {
            *result = cur;
            return OK;
        }
        prev = cur;
    }
    *result = prev;
    return PRECISION_LIMIT;
}

/* e = sum 1/n!. Каждое слагаемое = предыдущее / n:
   1/n! = (1/(n-1)!) / n - так факториал не считаем отдельно и не переполняемся */
status_code e_series(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double term = 1.0;   /* 1/0! */
    double sum = 0.0;
    long long n = 0;
    /* слагаемые быстро убывают; когда очередное меньше eps, оставшиеся уже не важны */
    while (term >= eps) {
        sum += term;
        n++;
        term /= (double)n;
    }
    *result = sum;
    return OK;
}

/* e: корень ln x = 1 на [2, 3] */
status_code e_equation(const double eps, double *result) {
    return dichotomy(eq_e, 0.0, 2.0, 3.0, eps, result);
}

/* ===================== pi ===================== */

/* pi = lim (2^n n!)^4 / (n ((2n)!)^2).
   Факториалы огромные, поэтому считаем через отношение соседних членов:
   a(n+1) = a(n) * 4n(n+1) / (2n+1)^2, a(1) = 4 */
status_code pi_limit(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* Здесь n растет на 1 (иначе не получить a(n+1) из a(n)),
       но сравниваем, как и в других пределах, только при n = 2, 4, 8 ...
       next_check - ближайшее n, при котором сравниваем */
    double a = 4.0;
    double prev = a;          /* значение при последней степени двойки */
    long long next_check = 2;
    for (long long n = 1; n < MAX_POW2_N; n++) {
        double dn = (double)n;
        a *= 4.0 * dn * (dn + 1) / ((2 * dn + 1) * (2 * dn + 1));
        /* теперь в a лежит a(n+1); при n+1 = 2, 4, 8 ... сравниваем с a(n/2) */
        if (n + 1 == next_check) {
            if (fabs(a - prev) < eps) {
                *result = a;
                return OK;
            }
            prev = a;
            next_check *= 2;
        }
    }
    *result = a;
    return PRECISION_LIMIT;
}

/* pi = 4 * sum (-1)^(n-1) / (2n-1). Ряд знакочередующийся:
   ошибка меньше первого отброшенного слагаемого */
status_code pi_series(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double sum = 0.0;
    double sign = 1.0;   /* (-1)^(n-1): +1, -1, +1 ... меняем знак на каждом шаге */
    for (long long n = 1; n <= MAX_SERIES_N; n++) {
        sum += sign * 4.0 / (double)(2 * n - 1);
        sign = -sign;
        /* следующее слагаемое (по модулю) меньше eps - значит, и ошибка меньше eps */
        if (4.0 / (double)(2 * n + 1) < eps) {
            *result = sum;
            return OK;
        }
    }
    *result = sum;
    return PRECISION_LIMIT;
}

/* pi: cos x = -1 на [2, 4] (через cos(x/2) = 0, см. eq_pi) */
status_code pi_equation(const double eps, double *result) {
    return dichotomy(eq_pi, 0.0, 2.0, 4.0, eps, result);
}

/* ===================== ln 2 ===================== */

/* ln 2 = lim n (2^(1/n) - 1), n = 1, 2, 4, ... */
status_code ln2_limit(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double prev = 1.0 * (pow(2.0, 1.0) - 1);   /* n = 1 */
    for (long long n = 2; n <= MAX_POW2_N; n *= 2) {
        double dn = (double)n;
        double cur = dn * (pow(2.0, 1.0 / dn) - 1);
        if (fabs(cur - prev) < eps) {
            *result = cur;
            return OK;
        }
        prev = cur;
    }
    *result = prev;
    return PRECISION_LIMIT;
}

/* ln 2 = sum (-1)^(n-1) / n, ряд знакочередующийся (останавливаемся так же, как в pi_series) */
status_code ln2_series(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double sum = 0.0;
    double sign = 1.0;
    for (long long n = 1; n <= MAX_SERIES_N; n++) {
        sum += sign / (double)n;
        sign = -sign;
        if (1.0 / (double)(n + 1) < eps) {
            *result = sum;
            return OK;
        }
    }
    *result = sum;
    return PRECISION_LIMIT;
}

/* ln 2: корень e^x = 2 на [0, 1] */
status_code ln2_equation(const double eps, double *result) {
    return dichotomy(eq_ln2, 0.0, 0.0, 1.0, eps, result);
}

/* ===================== sqrt(2) ===================== */

/* sqrt2 = lim x(n), x(n+1) = x(n) - x(n)^2 / 2 + 1, x(0) = -0.5.
   Каждый шаг строит следующее x из предыдущего; стоп, когда x почти перестал меняться */
status_code sqrt2_limit(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double x = -0.5;
    for (int iter = 0; iter < 10000; iter++) {
        double next = x - x * x / 2 + 1;
        if (fabs(next - x) < eps) {
            *result = next;
            return OK;
        }
        x = next;
    }
    *result = x;
    return PRECISION_LIMIT;
}

/* sqrt2 = произведение 2^(2^-k), k = 2, 3, ...
   Множители стремятся к 1; останавливаемся, когда произведение почти не меняется */
status_code sqrt2_series(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double product = 1.0;
    /* показатель 2^-k: 1/4, 1/8, 1/16 ... - на каждом шаге делим на 2 */
    double exponent = 0.25;   /* 2^-2 */
    for (int k = 2; k < 200; k++) {
        double next = product * pow(2.0, exponent);
        if (fabs(next - product) < eps) {
            *result = next;
            return OK;
        }
        product = next;
        exponent /= 2;
    }
    *result = product;
    return PRECISION_LIMIT;
}

/* sqrt2: положительный корень x^2 = 2 на [1, 2] */
status_code sqrt2_equation(const double eps, double *result) {
    return dichotomy(eq_sqrt2, 0.0, 1.0, 2.0, eps, result);
}

/* ===================== gamma ===================== */

/* gamma = lim sum_{k=1}^{m} C(m,k) (-1)^k / k * ln(k!).
   Слагаемые огромные и с разными знаками - в double формула
   теряет точность примерно после m = 40. Сравниваем m и 2m. */
status_code gamma_limit(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double prev = 0.0;   /* при m = 1: C(1,1) * (-1) * ln(1!) = 0 */
    for (int m = 2; m <= MAX_GAMMA_M; m *= 2) {
        /* для каждого m сумму считаем заново, от k = 1 до m.
           binom, ln_fact и sign обновляем на каждом k из их прошлых значений,
           чтобы не считать биномы и факториалы с нуля */
        double sum = 0.0;
        double binom = 1.0;      /* C(m, 0) */
        double ln_fact = 0.0;    /* ln(0!) */
        double sign = 1.0;
        for (int k = 1; k <= m; k++) {
            binom = binom * (double)(m - k + 1) / (double)k;   /* C(m,k) из C(m,k-1) */
            ln_fact += log((double)k);                          /* ln(k!) = ln((k-1)!) + ln k */
            sign = -sign;
            sum += binom * sign / (double)k * ln_fact;
        }
        if (fabs(sum - prev) < eps) {
            *result = sum;
            return OK;
        }
        /* последовательность растет; если упала больше чем на eps - это уже ошибки округления */
        if (prev - sum >= eps) {
            *result = prev;
            return PRECISION_LIMIT;
        }
        prev = sum;
    }
    *result = prev;
    return PRECISION_LIMIT;
}

/* gamma = -pi^2/6 + sum_{k>=2} (1/floor(sqrt k)^2 - 1/k).
   Внутри блока k = n^2 .. (n+1)^2 - 1 floor(sqrt k) = n, поэтому суммируем блоками.
   Ряд сходится медленно (хвост примерно block * n), поэтому добавляем
   оценку хвоста и сравниваем результат при n и 2n. */
status_code gamma_series(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    const double pi = 3.14159265358979323846;
    double sum = -pi * pi / 6;
    double prev = 0.0;
    long long next_check = 1;
    /* n - номер блока: блок 1 - это k = 2, 3; блок 2 - k = 4..8; блок 3 - k = 9..15 ... */
    for (long long n = 1; n <= MAX_BLOCKS; n++) {
        double n_square = (double)n * (double)n;   /* floor(sqrt k)^2 = n^2 для всего блока */
        double block = 0.0;
        long long start = (n == 1) ? 2 : n * n;   /* ряд начинается с k = 2 */
        for (long long k = start; k < (n + 1) * (n + 1); k++) {
            block += 1.0 / n_square - 1.0 / (double)k;
        }
        sum += block;
        double with_tail = sum + block * (double)n;   /* + оценка хвоста */

        /* сравниваем при n = 1, 2, 4, 8 ... (при n = 1 сравнивать еще не с чем) */
        if (n == next_check) {
            if (n > 1 && fabs(with_tail - prev) < eps) {
                *result = with_tail;
                return OK;
            }
            prev = with_tail;
            next_check *= 2;
        }
    }
    *result = prev;
    return PRECISION_LIMIT;
}

/* limit = lim ln t * prod_{p <= t, p простое} (p - 1) / p.
   Простые ищем решетом Эратосфена (массив на MAX_T байт - в куче) */
status_code mertens_limit(const double eps, double *limit) {
    if (limit == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* calloc - как malloc, но заполняет память нулями: сначала все числа "простые".
       16 МБ на стеке не поместились бы, поэтому в куче */
    char *composite = calloc((size_t)MAX_T + 1, 1);   /* 0 - простое, 1 - составное */
    if (composite == NULL) {
        return NO_MEMORY;
    }

    /* Решето Эратосфена: идем по t по порядку. Если t еще не вычеркнуто - оно простое,
       и все его кратные вычеркиваем. Так же по ходу домножаем произведение */
    double product = 1.0;
    double prev = 0.0;    /* значение в прошлой контрольной точке */
    int has_prev = 0;     /* 1 - прошлая точка уже была, есть с чем сравнивать */
    double cur = 0.0;
    long long next_check = 16;
    status_code sc = PRECISION_LIMIT;
    for (long long t = 2; t <= MAX_T; t++) {
        if (!composite[t]) {
            product *= (double)(t - 1) / (double)t;
            /* вычеркиваем кратные t, начиная с t*t: меньшие кратные (2t, 3t ...)
               уже вычеркнуты меньшими простыми.
               long long: t*t не влезет в long на Windows */
            for (long long j = (long long)t * t; j <= MAX_T; j += t) {
                composite[j] = 1;
            }
        }
        /* при t = 16, 32, 64 ... сравниваем с прошлым значением */
        if (t == next_check) {
            cur = log((double)t) * product;
            if (has_prev && fabs(cur - prev) < eps) {
                sc = OK;
                break;
            }
            prev = cur;
            has_prev = 1;
            next_check *= 2;
        }
    }
    /* break выше нужен, чтобы дойти сюда и освободить память (return сразу ее бы потерял) */
    *limit = cur;
    free(composite);
    return sc;
}

/* gamma: корень e^(-x) = limit на [0, 1] */
status_code gamma_equation(const double eps, double *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* сначала считаем число limit, потом решаем уравнение с ним.
       PRECISION_LIMIT у предела - не ошибка: число есть, просто менее точное */
    double limit = 0.0;
    status_code sc_limit = mertens_limit(eps, &limit);
    if (sc_limit != OK && sc_limit != PRECISION_LIMIT) {
        return sc_limit;
    }
    status_code sc = dichotomy(eq_gamma, limit, 0.0, 1.0, eps, result);
    if (sc != OK) {
        return sc;     /* ошибка или PRECISION_LIMIT самой дихотомии */
    }
    return sc_limit;   /* OK или PRECISION_LIMIT от предела */
}

/* Сколько знаков после точки печатать для данного eps (не больше 15 - предел double).
   Например eps = 1e-6: шаг 0.1 -> 0.01 -> ... -> 0.000001, digits = 6 */
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
    /* точные значения - только чтобы показать в таблице, насколько ошибся метод */
    const double exact[5] = {
        2.71828182845904523536, 3.14159265358979323846, 0.69314718055994530942,
        1.41421356237309504880, 0.57721566490153286061
    };
    /* methods[i][j] - функция для константы i и метода j (0 - предел, 1 - ряд, 2 - уравнение).
       Все 15 функций принимают (eps, адрес ответа), поэтому помещаются в один массив,
       и вызов methods[i][j](eps, &val) запускает нужную */
    status_code (*methods[5][3])(const double, double *) = {
        {e_limit, e_series, e_equation},
        {pi_limit, pi_series, pi_equation},
        {ln2_limit, ln2_series, ln2_equation},
        {sqrt2_limit, sqrt2_series, sqrt2_equation},
        {gamma_limit, gamma_series, gamma_equation}
    };
    const char *method_names[3] = {"limit", "series", "equation"};
    /* "internal error" здесь и ниже - запасная ветка для статусов, которые появиться
       не могут: INVALID_ARGS (main передает правильные адреса и eps уже проверен).
       Пользователь это сообщение не увидит; если увидел - ошибка в самом коде программы */
    int digits = 0;
    if (digits_for_eps(eps, &digits) != OK) {
        fprintf(stderr, "Error: internal error\n");
        return 1;
    }

    /* %-7s: строка шириной 7, выровненная по левому краю - так столбцы стоят ровно */
    printf("eps = %g\n\n", eps);
    printf("%-7s %-9s %-20s %-12s %s\n", "const", "method", "value", "error", "note");
    /* перебираем все 5 констант и все 3 метода */
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 3; j++) {
            double val = 0.0;
            sc = methods[i][j](eps, &val);
            /* PRECISION_LIMIT - значение все равно есть, печатаем его с пометкой.
               %-20.*f: число знаков после точки берется из аргумента digits */
            if (sc == OK || sc == PRECISION_LIMIT) {
                printf("%-7s %-9s %-20.*f %-12.2e %s\n", names[i], method_names[j],
                       digits, val, fabs(val - exact[i]),
                       (sc == OK) ? "" : "eps is too small for this method");
            } else if (sc == NO_MEMORY) {
                printf("%-7s %-9s Error: not enough memory\n", names[i], method_names[j]);
            } else if (sc == NO_ROOT || sc == OUT_OF_RANGE) {
                printf("%-7s %-9s Error: no root on the segment\n", names[i], method_names[j]);
            } else {
                printf("%-7s %-9s Error: internal error\n", names[i], method_names[j]);
            }
        }
    }
    printf("\nerror = |value - real value|, real value is taken from a table\n");
    return 0;
}
