#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL, неверное количество, eps <= 0 */
    INVALID_NUMBER,    /* строка не число в данной системе счисления */
    INVALID_BASE,      /* основание не от 2 до 36 */
    OVERFLOW_ERROR,    /* результат не помещается в тип */
    NO_ROOT,           /* на концах отрезка одинаковые знаки */
    DIVISION_BY_ZERO,  /* 0 в отрицательной степени */
    NEGATIVE_NUMBER,   /* отрицательное число там, где нельзя */
    BUFFER_TOO_SMALL,
    PRECISION_LIMIT,   /* дихотомия: заданную точность double не позволяет получить */
    DEMO_FAILED        /* демонстрация: функция вернула не тот статус, что ожидали */
} status_code;

#define MAX_ITER 200   /* предел шагов дихотомии: double больше ~60 делений не выдерживает */

/* ================= 1. Выпуклый многоугольник =================
   is_convex(&rez, eps, n, x1, y1, x2, y2, ..., xn, yn)
   Все координаты передавать как double (1.0, а не 1!). rez = 1 - выпуклый.
   Идея: в выпуклом многоугольнике все повороты на вершинах в одну сторону
   (векторные произведения соседних ребер одного знака),
   а сумма углов поворота ровно 2*pi (иначе это "звезда" с самопересечением). */
typedef struct {
    double x;
    double y;
} point;

/* Поворот в вершине b на пути a -> b -> c.
   ok = 1, если поворот в ту же сторону, что и раньше (znak), иначе 0.
   Угол поворота добавляет в summa_uglov */
status_code check_turn(const point a, const point b, const point c, const double eps,
                       int *znak, double *summa_uglov, int *ok) {
    if (znak == NULL || summa_uglov == NULL || ok == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    double ux = b.x - a.x, uy = b.y - a.y;   /* ребро a->b */
    double vx = c.x - b.x, vy = c.y - b.y;   /* ребро b->c */

    double vect = ux * vy - uy * vx;   /* векторное произведение: знак = сторона поворота */
    double scal = ux * vx + uy * vy;   /* скалярное: нужно для угла */

    *ok = 0;
    if (fabs(vect) < eps) {
        return OK;   /* vect = 0 с точностью eps: три точки на одной прямой - многоугольник вырожденный */
    }
    /* |vect| >= eps, так что vect > 0 значит vect >= eps */
    int tek_znak = (vect > 0) ? 1 : -1;
    if (*znak == 0) {
        *znak = tek_znak;   /* первый поворот задает направление */
    } else if (tek_znak != *znak) {
        return OK;
    }
    *summa_uglov += atan2(vect, scal);   /* угол поворота со знаком */
    *ok = 1;
    return OK;
}

status_code is_convex(int *rez, const double eps, const int n, ...) {
    if (rez == NULL || eps <= 0 || n < 3) {
        return INVALID_ARGS;
    }

    va_list args;
    va_start(args, n);

    /* храним только первые две вершины (понадобятся в конце, круг замыкается)
       и две последние прочитанные - массив на все вершины не нужен */
    point first, second, pred2, pred1;
    first.x = va_arg(args, double);
    first.y = va_arg(args, double);
    second.x = va_arg(args, double);
    second.y = va_arg(args, double);
    pred2 = first;
    pred1 = second;

    int znak = 0;              /* знак поворотов: +1 или -1, 0 - еще не знаем */
    double summa_uglov = 0.0;
    int vypukly = 1;
    status_code sc = OK;

    for (int i = 2; i < n; i++) {
        point tek;
        tek.x = va_arg(args, double);
        tek.y = va_arg(args, double);
        /* если ответ уже ясен, дальше просто проходим по аргументам */
        if (vypukly && sc == OK) {
            sc = check_turn(pred2, pred1, tek, eps, &znak, &summa_uglov, &vypukly);
        }
        pred2 = pred1;
        pred1 = tek;
    }
    va_end(args);

    /* два последних поворота: в предпоследней и последней вершине */
    if (vypukly && sc == OK) {
        sc = check_turn(pred2, pred1, first, eps, &znak, &summa_uglov, &vypukly);
    }
    if (vypukly && sc == OK) {
        sc = check_turn(pred1, first, second, eps, &znak, &summa_uglov, &vypukly);
    }
    if (sc != OK) {
        return sc;
    }

    /* у выпуклого многоугольника сумма поворотов ровно +-2*pi,
       у "звезды" - 4*pi и больше. Сравниваем с точностью eps */
    const double pi = 3.14159265358979323846;
    *rez = vypukly && fabs(fabs(summa_uglov) - 2 * pi) < eps;
    return OK;
}

/* ================= 2. Значение многочлена =================
   polynomial(&rez, x, n, a_n, a_(n-1), ..., a_0) - коэффициенты от старшего.
   Схема Горнера: ((a_n * x + a_(n-1)) * x + ...) * x + a_0 */
status_code polynomial(double *rez, const double x, const int n, ...) {
    if (rez == NULL || n < 0) {
        return INVALID_ARGS;
    }
    va_list args;
    va_start(args, n);
    double s = 0.0;
    for (int i = 0; i <= n; i++) {     /* коэффициентов n + 1 */
        s = s * x + va_arg(args, double);
    }
    va_end(args);

    if (isinf(s) || isnan(s)) {
        return OVERFLOW_ERROR;
    }
    *rez = s;
    return OK;
}

/* ================= 3. Числа Капрекара =================
   Число n называется числом Капрекара в системе с основанием base, если n^2
   можно разрезать на левую и правую части (правая не 0), сумма которых равна n.
   Пример (base 10): 45^2 = 2025 -> 20 + 25 = 45. */

/* Цифра -> значение: '0'..'9' -> 0..9, 'a'/'A'..'z'/'Z' -> 10..35.
   Другой символ - INVALID_NUMBER */
status_code digit_value(const char ch, int *val) {
    if (val == NULL) {
        return INVALID_ARGS;
    }
    if (ch >= '0' && ch <= '9') {
        *val = ch - '0';
        return OK;
    }
    char up = (char)toupper((unsigned char)ch);
    if (up >= 'A' && up <= 'Z') {
        *val = up - 'A' + 10;
        return OK;
    }
    return INVALID_NUMBER;
}

/* Строка в системе base -> число. Ограничение: n <= 2^32 - 1, чтобы n^2 влез в unsigned long long */
status_code parse_in_base(const char *str, const int base, unsigned long long *chislo) {
    if (str == NULL || chislo == NULL) {
        return INVALID_ARGS;
    }
    if (base < 2 || base > 36) {
        return INVALID_BASE;
    }
    if (str[0] == '\0') {
        return INVALID_NUMBER;
    }
    const unsigned long long limit = 4294967295ULL;
    unsigned long long val = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        int d = 0;
        if (digit_value(str[i], &d) != OK || d >= base) {
            return INVALID_NUMBER;
        }
        val = val * (unsigned long long)base + (unsigned long long)d;
        if (val > limit) {
            return OVERFLOW_ERROR;
        }
    }
    *chislo = val;
    return OK;
}

/* Проверка одного числа. rez = 1 - число Капрекара */
status_code check_kaprekar(const unsigned long long n, const int base, int *rez) {
    if (rez == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    *rez = 0;
    if (n == 0) {
        return OK;   /* 0^2 = 0, правая часть не может быть ненулевой */
    }
    unsigned long long kvadrat = n * n;   /* n <= 2^32 - 1, переполнения нет */
    unsigned long long delitel = (unsigned long long)base;   /* base^p - где режем */

    /* режем после 1, 2, 3 ... цифр справа, пока левая часть не 0 */
    while (delitel <= kvadrat) {
        unsigned long long levaya = kvadrat / delitel;
        unsigned long long pravaya = kvadrat % delitel;
        if (pravaya > 0 && levaya + pravaya == n) {
            *rez = 1;
            return OK;
        }
        /* следующий разрез; проверка, чтобы delitel * base не переполнился */
        if (delitel > kvadrat / (unsigned long long)base) {
            break;
        }
        delitel *= (unsigned long long)base;
    }
    /* разрез, когда левая часть пустая (= 0): тогда n^2 = n, это только n = 1 */
    if (kvadrat == n) {
        *rez = 1;
    }
    return OK;
}

/* find_kaprekar(found, max_found, &found_count, base, count, "str1", "str2", ...)
   В found записываются указатели на те строки, что оказались числами Капрекара */
status_code find_kaprekar(const char **found, const int max_found, int *found_count,
                          const int base, const int count, ...) {
    if (found == NULL || found_count == NULL || max_found <= 0 || count < 0) {
        return INVALID_ARGS;
    }
    if (base < 2 || base > 36) {
        return INVALID_BASE;
    }

    va_list args;
    va_start(args, count);
    *found_count = 0;
    status_code sc = OK;
    for (int i = 0; i < count; i++) {
        const char *str = va_arg(args, const char *);
        unsigned long long n = 0;
        sc = parse_in_base(str, base, &n);
        if (sc != OK) {
            break;   /* va_end все равно нужно вызвать, поэтому не return */
        }
        int is_k = 0;
        sc = check_kaprekar(n, base, &is_k);
        if (sc != OK) {
            break;
        }
        if (is_k) {
            if (*found_count >= max_found) {
                sc = BUFFER_TOO_SMALL;
                break;
            }
            found[*found_count] = str;
            (*found_count)++;
        }
    }
    va_end(args);
    return sc;
}

/* ================= 4. Среднее геометрическое =================
   geom_mean(&rez, eps, x1, x2, ..., xn, n) - нельзя! Количество обязано стоять
   ПЕРЕД "...", поэтому по заданию оно - последний ОБЯЗАТЕЛЬНЫЙ параметр:
   geom_mean(&rez, eps, n, x1, x2, ..., xn).
   eps нужен, потому что числа сравниваются с нулем (а вещественные сравниваем только через eps).
   Считаем через логарифмы: (x1*...*xn)^(1/n) = exp((ln x1 + ... + ln xn) / n),
   так произведение не переполнится. */
status_code geom_mean(double *rez, const double eps, const int n, ...) {
    if (rez == NULL || eps <= 0 || n <= 0) {
        return INVALID_ARGS;
    }
    va_list args;
    va_start(args, n);
    double sum_log = 0.0;
    int est_nol = 0;
    status_code sc = OK;
    for (int i = 0; i < n; i++) {
        double x = va_arg(args, double);
        if (fabs(x) < eps) {
            est_nol = 1;   /* x = 0 с точностью eps: ln 0 не существует, но ответ тогда просто 0 */
        } else if (x < 0) {
            sc = NEGATIVE_NUMBER;   /* |x| >= eps, так что x < 0 значит x <= -eps */
            break;
        } else {
            sum_log += log(x);
        }
    }
    va_end(args);

    if (sc != OK) {
        return sc;
    }
    *rez = est_nol ? 0.0 : exp(sum_log / n);
    return OK;
}

/* ================= 5. Быстрое возведение в степень (рекурсия) =================
   a^n = (a^(n/2))^2,      если n четное
   a^n = (a^(n/2))^2 * a,  если n нечетное
   Глубина рекурсии ~ log2(n), а не n.
   Для отрицательных n: n/2 и n%2 в C округляют к нулю, поэтому
   остаток бывает -1 - тогда делим на a. Так не нужно считать -n
   (для LLONG_MIN это было бы переполнение).
   eps нужен для проверки a = 0 (вещественные сравниваем только через eps). */
status_code fast_pow(const double a, const long long n, const double eps, double *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    if (n == 0) {
        *rez = 1.0;   /* в том числе 0^0 = 1 по договоренности */
        return OK;
    }
    if (fabs(a) < eps && n < 0) {
        return DIVISION_BY_ZERO;   /* a = 0 с точностью eps, а делить на 0 нельзя */
    }

    double polovina = 0.0;
    status_code sc = fast_pow(a, n / 2, eps, &polovina);   /* рекурсивный вызов */
    if (sc != OK) {
        return sc;
    }

    double r = polovina * polovina;
    if (n % 2 == 1) {
        r *= a;
    } else if (n % 2 == -1) {
        r /= a;
    }
    if (isinf(r)) {
        return OVERFLOW_ERROR;
    }
    *rez = r;
    return OK;
}

/* ================= 6. Дихотомия =================
   Корень f(x) = 0 на [a, b] с точностью eps, нужно a < b.
   f кладет значение в свой второй параметр и возвращает статус.
   На концах не должно быть одного и того же знака. */
status_code dichotomy(double a, double b, const double eps,
                      status_code (*f)(const double, double *), double *root) {
    /* b - a < eps: отрезок перевернутый или короче точности */
    if (f == NULL || root == NULL || eps <= 0 || b - a < eps) {
        return INVALID_ARGS;
    }
    double fa = 0.0, fb = 0.0;
    status_code sc = f(a, &fa);
    if (sc != OK) {
        return sc;
    }
    sc = f(b, &fb);
    if (sc != OK) {
        return sc;
    }
    /* оба значения строго одного знака - корень не гарантирован
       (0 на конце подходит - тогда этот конец и есть корень) */
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
        sc = f(mid, &fm);
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

/* Уравнения для демонстрации дихотомии: значение в rez */
status_code eq1(const double x, double *rez) {   /* корень sqrt 2 */
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = x * x - 2;
    return OK;
}

status_code eq2(const double x, double *rez) {   /* ~0.739 */
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = cos(x) - x;
    return OK;
}

status_code eq3(const double x, double *rez) {   /* ~1.521 */
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = x * x * x - x - 2;
    return OK;
}

status_code eq4(const double x, double *rez) {   /* ~0.619 и ~1.512 */
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = exp(x) - 3 * x;
    if (isinf(*rez)) {
        return OVERFLOW_ERROR;   /* x слишком большой */
    }
    return OK;
}

/* ================= Демонстрация =================
   Каждая demo_ проверяет, что функции вернули ожидаемый статус.
   Если нет - возвращает DEMO_FAILED (ошибку печатает main) */

status_code demo_convex(void) {
    printf("1. Convex polygon\n");
    int rez = 0;

    if (is_convex(&rez, 1e-9, 4, 0.0, 0.0, 2.0, 0.0, 2.0, 2.0, 0.0, 2.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   square (0,0) (2,0) (2,2) (0,2): %s\n", rez ? "convex" : "not convex");
    if (is_convex(&rez, 1e-9, 4, 0.0, 0.0, 2.0, 0.0, 1.0, 0.5, 1.0, 2.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   (0,0) (2,0) (1,0.5) (1,2):       %s\n", rez ? "convex" : "not convex");
    if (is_convex(&rez, 1e-9, 3, 0.0, 0.0, 1.0, 1.0, 2.0, 2.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   (0,0) (1,1) (2,2) on one line:   %s\n", rez ? "convex" : "not convex");
    /* пятиконечная звезда: все повороты в одну сторону, но фигура не выпуклая */
    if (is_convex(&rez, 1e-9, 5, 0.0, 1.0, 0.588, -0.809, -0.951, 0.309,
                  0.951, 0.309, -0.588, -0.809) != OK) {
        return DEMO_FAILED;
    }
    printf("   star (5 points):                 %s\n", rez ? "convex" : "not convex");
    if (is_convex(&rez, 1e-9, 2, 0.0, 0.0, 1.0, 1.0) != INVALID_ARGS) {
        return DEMO_FAILED;
    }
    printf("   2 points:                        error, need at least 3\n");
    return OK;
}

status_code demo_polynomial(void) {
    printf("\n2. Polynomial\n");
    double rez = 0.0;
    if (polynomial(&rez, 2.0, 2, 1.0, -3.0, 2.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   x^2 - 3x + 2 at x = 2:     %g\n", rez);
    if (polynomial(&rez, 3.0, 3, 2.0, 0.0, 0.0, -1.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   2x^3 - 1 at x = 3:         %g\n", rez);
    if (polynomial(&rez, -1.5, 0, 7.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   7 (degree 0) at x = -1.5:  %g\n", rez);
    if (polynomial(&rez, 1e200, 2, 1.0, 0.0, 0.0) != OVERFLOW_ERROR) {
        return DEMO_FAILED;
    }
    printf("   x^2 at x = 1e200:          overflow\n");
    return OK;
}

status_code demo_kaprekar(void) {
    printf("\n3. Kaprekar numbers\n");
    const char *found[20];
    int kolvo = 0;

    if (find_kaprekar(found, 20, &kolvo, 10, 9,
                      "1", "9", "10", "45", "55", "99", "100", "297", "703") != OK) {
        return DEMO_FAILED;
    }
    printf("   base 10, from 1 9 10 45 55 99 100 297 703:\n   ");
    for (int i = 0; i < kolvo; i++) {
        printf("%s ", found[i]);
    }
    printf("\n");

    if (find_kaprekar(found, 20, &kolvo, 16, 8, "1", "6", "a", "F", "10", "33", "ff", "100") != OK) {
        return DEMO_FAILED;
    }
    printf("   base 16, from 1 6 a F 10 33 ff 100:\n   ");
    for (int i = 0; i < kolvo; i++) {
        printf("%s ", found[i]);
    }
    printf("\n");

    if (find_kaprekar(found, 20, &kolvo, 10, 2, "45", "4z") != INVALID_NUMBER) {
        return DEMO_FAILED;
    }
    printf("   base 10, \"4z\": error, not a number in this base\n");
    return OK;
}

status_code demo_geom_mean(void) {
    printf("\n4. Geometric mean (eps 1e-9)\n");
    double rez = 0.0;
    if (geom_mean(&rez, 1e-9, 2, 4.0, 9.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   4, 9:          %g\n", rez);
    if (geom_mean(&rez, 1e-9, 3, 1.0, 10.0, 100.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   1, 10, 100:    %g\n", rez);
    if (geom_mean(&rez, 1e-9, 3, 1e200, 1e200, 1e200) != OK) {
        return DEMO_FAILED;
    }
    printf("   1e200 x 3:     %g (no overflow thanks to logarithms)\n", rez);
    if (geom_mean(&rez, 1e-9, 3, 5.0, 0.0, 7.0) != OK) {
        return DEMO_FAILED;
    }
    printf("   5, 0, 7:       %g\n", rez);
    if (geom_mean(&rez, 1e-9, 2, -4.0, 9.0) != NEGATIVE_NUMBER) {
        return DEMO_FAILED;
    }
    printf("   -4, 9:         error, negative number\n");
    return OK;
}

status_code demo_pow(void) {
    printf("\n5. Fast power (eps 1e-9)\n");
    const double osn[6] = {2.0, 2.0, 1.5, 0.0, -3.0, 10.0};
    const long long pok[6] = {10, -3, 0, -1, 3, 400};
    for (int i = 0; i < 6; i++) {
        double rez = 0.0;
        status_code sc = fast_pow(osn[i], pok[i], 1e-9, &rez);
        printf("   %g ^ %lld = ", osn[i], pok[i]);
        if (sc == OK) {
            printf("%g\n", rez);
        } else if (sc == DIVISION_BY_ZERO) {
            printf("error, division by zero\n");
        } else if (sc == OVERFLOW_ERROR) {
            printf("error, too big\n");
        } else {
            printf("\n");
            return DEMO_FAILED;
        }
    }
    return OK;
}

status_code demo_dichotomy(void) {
    printf("\n6. Dichotomy\n");
    double root = 0.0;
    struct {
        const char *text;
        status_code (*f)(const double, double *);
        double a, b, eps;
    } tests[7] = {
        {"x^2 - 2 = 0   on [0, 2], eps 1e-3 ", eq1, 0, 2, 1e-3},
        {"x^2 - 2 = 0   on [0, 2], eps 1e-12", eq1, 0, 2, 1e-12},
        {"cos x = x     on [0, 1], eps 1e-6 ", eq2, 0, 1, 1e-6},
        {"x^3 - x = 2   on [1, 2], eps 1e-9 ", eq3, 1, 2, 1e-9},
        {"e^x = 3x      on [1, 2], eps 1e-6 ", eq4, 1, 2, 1e-6},
        {"e^x = 3x      on [2, 3], eps 1e-6 ", eq4, 2, 3, 1e-6},
        {"x^2 - 2 = 0   on [2, 0], eps 1e-6 ", eq1, 2, 0, 1e-6}
    };
    for (int i = 0; i < 7; i++) {
        status_code sc = dichotomy(tests[i].a, tests[i].b, tests[i].eps, tests[i].f, &root);
        if (sc == OK) {
            printf("   %s: x = %.12f\n", tests[i].text, root);
        } else if (sc == PRECISION_LIMIT) {
            printf("   %s: x = %.12f (eps is too small)\n", tests[i].text, root);
        } else if (sc == NO_ROOT) {
            printf("   %s: no sign change, no root here\n", tests[i].text);
        } else if (sc == INVALID_ARGS) {
            printf("   %s: error, need a < b\n", tests[i].text);
        } else {
            return DEMO_FAILED;
        }
    }
    return OK;
}

int main(int argc, char *argv[]) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "Usage: lab1_task6 (no arguments)\n");
        return 1;
    }
    /* все демонстрации по очереди; первая же ошибка - выходим */
    status_code (*demos[6])(void) = {
        demo_convex, demo_polynomial, demo_kaprekar, demo_geom_mean, demo_pow, demo_dichotomy
    };
    for (int i = 0; i < 6; i++) {
        if (demos[i]() != OK) {
            fprintf(stderr, "Error: demo %d failed\n", i + 1);
            return 1;
        }
    }
    return 0;
}
