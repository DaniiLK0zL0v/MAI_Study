#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL, eps <= 0 и т.п. */
    INVALID_FLAG,
    INVALID_NUMBER,    /* строка не число */
    OUT_OF_RANGE,      /* число слишком большое или inf/nan */
    ZERO_NUMBER        /* для -m: число равно 0 */
} status_code;

/* Сколько корней у уравнения */
typedef enum {
    ROOTS_NONE,        /* решений нет (например, 0x + 5 = 0) */
    ROOTS_ONE,         /* один корень (линейное или D = 0) */
    ROOTS_TWO,         /* два действительных */
    ROOTS_COMPLEX,     /* два комплексных: re +- im*i */
    ROOTS_INFINITE     /* любое x (0x + 0 = 0) */
} roots_type;

/* Результат решения одного уравнения */
typedef struct {
    roots_type type;
    double x1;
    double x2;
    double re;   /* для комплексных корней */
    double im;
} roots;

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

/* Строка -> long long. Знак + или - (можно без него), потом только цифры.
   Разбираем сами, переполнение проверяем до умножения.
   long long, а не long: long на Windows 4 байта, на Linux 8 - результаты различались бы */
status_code parse_long_long(const char *str, long long *chislo) {
    if (str == NULL || chislo == NULL) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    int otric = 0;
    if (str[0] == '-' || str[0] == '+') {
        otric = (str[0] == '-');
        i = 1;
    }
    if (str[i] == '\0') {
        return INVALID_NUMBER;
    }
    for (size_t j = i; str[j] != '\0'; j++) {
        if (str[j] < '0' || str[j] > '9') {
            return INVALID_NUMBER;
        }
    }

    /* модуль копим в unsigned long long: модуль LLONG_MIN на 1 больше LLONG_MAX */
    unsigned long long predel = otric ? (unsigned long long)LLONG_MAX + 1 : (unsigned long long)LLONG_MAX;
    unsigned long long modul = 0;
    for (; str[i] != '\0'; i++) {
        unsigned long long cifra = (unsigned long long)(str[i] - '0');
        /* modul * 10 + cifra > predel  <=>  modul > (predel - cifra) / 10 */
        if (modul > (predel - cifra) / 10) {
            return OUT_OF_RANGE;
        }
        modul = modul * 10 + cifra;
    }

    if (!otric) {
        *chislo = (long long)modul;
    } else if (modul == predel) {
        *chislo = LLONG_MIN;   /* -(LLONG_MAX + 1) в long long не посчитать */
    } else {
        *chislo = -(long long)modul;
    }
    return OK;
}

/* Флаг: '-' или '/' и буква q, m или t */
status_code parse_flag(const char *arg, char *flag) {
    if (arg == NULL || flag == NULL) {
        return INVALID_ARGS;
    }
    if ((arg[0] != '-' && arg[0] != '/') || arg[1] == '\0' || arg[2] != '\0') {
        return INVALID_FLAG;
    }
    if (arg[1] != 'q' && arg[1] != 'm' && arg[1] != 't') {
        return INVALID_FLAG;
    }
    *flag = arg[1];
    return OK;
}

/* -q: решить a*x^2 + b*x + c = 0.
   Все сравнения вещественных - с точностью eps: |u - v| < eps значит u = v */
status_code solve_quadratic(const double a, const double b, const double c,
                            const double eps, roots *otvet) {
    if (otvet == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    otvet->x1 = otvet->x2 = otvet->re = otvet->im = 0.0;

    /* a = 0 - уравнение линейное: b*x + c = 0 */
    if (fabs(a) < eps) {
        if (fabs(b) < eps) {
            otvet->type = (fabs(c) < eps) ? ROOTS_INFINITE : ROOTS_NONE;
        } else {
            otvet->type = ROOTS_ONE;
            otvet->x1 = -c / b;
        }
    } else {
        double d = b * b - 4 * a * c;   /* дискриминант */
        if (fabs(d) < eps) {
            otvet->type = ROOTS_ONE;
            otvet->x1 = -b / (2 * a);
        } else if (d > 0) {             /* |d| >= eps уже знаем, так что d > 0 значит d >= eps */
            double koren = sqrt(d);
            otvet->type = ROOTS_TWO;
            otvet->x1 = (-b + koren) / (2 * a);
            otvet->x2 = (-b - koren) / (2 * a);
        } else {
            otvet->type = ROOTS_COMPLEX;
            otvet->re = -b / (2 * a);
            otvet->im = sqrt(-d) / (2 * fabs(a));
        }
    }

    /* число меньше eps по модулю - это 0. Заодно убираем "-0" (printf печатает его как "-0") */
    if (fabs(otvet->x1) < eps) {
        otvet->x1 = 0.0;
    }
    if (fabs(otvet->x2) < eps) {
        otvet->x2 = 0.0;
    }
    if (fabs(otvet->re) < eps) {
        otvet->re = 0.0;
    }
    return OK;
}

/* -q: все уникальные перестановки трех коэффициентов.
   perest - массив [6][3] для ответа, kolvo - сколько уникальных получилось */
status_code unique_permutations(const double koef[3], const double eps,
                                double perest[6][3], int *kolvo) {
    if (koef == NULL || perest == NULL || kolvo == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* все 6 способов расставить индексы 0, 1, 2 */
    const int poryadok[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
    };

    *kolvo = 0;
    for (int i = 0; i < 6; i++) {
        double a = koef[poryadok[i][0]];
        double b = koef[poryadok[i][1]];
        double c = koef[poryadok[i][2]];

        /* проверяем, не было ли уже такой же тройки (с точностью eps) */
        int uzhe_bylo = 0;
        for (int j = 0; j < *kolvo; j++) {
            if (fabs(perest[j][0] - a) < eps && fabs(perest[j][1] - b) < eps &&
                fabs(perest[j][2] - c) < eps) {
                uzhe_bylo = 1;
                break;
            }
        }
        if (!uzhe_bylo) {
            perest[*kolvo][0] = a;
            perest[*kolvo][1] = b;
            perest[*kolvo][2] = c;
            (*kolvo)++;
        }
    }
    return OK;
}

/* -m: кратно ли a числу b (оба не 0). rez = 1 - да, 0 - нет */
status_code is_multiple(const long long a, const long long b, int *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (a == 0 || b == 0) {
        return ZERO_NUMBER;
    }
    /* LLONG_MIN % -1 в C - неопределенное поведение (может упасть), а на +-1 делится все */
    if (b == 1 || b == -1) {
        *rez = 1;
        return OK;
    }
    *rez = (a % b == 0);
    return OK;
}

/* -t: могут ли a, b, c быть сторонами прямоугольного треугольника.
   rez = 1 - да, 0 - нет */
status_code is_right_triangle(const double a, const double b, const double c,
                              const double eps, int *rez) {
    if (rez == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* стороны должны быть положительными (больше 0 с точностью eps) */
    if (a < eps || b < eps || c < eps) {
        *rez = 0;
        return OK;
    }

    /* теорема Пифагора. Гипотенузой может быть любая из трех сторон -
       проверяем все три варианта, так не нужно искать самую длинную */
    double aa = a * a, bb = b * b, cc = c * c;
    *rez = fabs(aa + bb - cc) < eps || fabs(aa + cc - bb) < eps || fabs(bb + cc - aa) < eps;
    return OK;
}

/* Печать корней одного уравнения */
status_code print_roots(const double a, const double b, const double c, const roots *otvet) {
    if (otvet == NULL) {
        return INVALID_ARGS;
    }
    printf("%g*x^2 + %g*x + %g = 0:  ", a, b, c);
    switch (otvet->type) {
        case ROOTS_NONE:
            printf("no roots\n");
            break;
        case ROOTS_INFINITE:
            printf("any x is a root\n");
            break;
        case ROOTS_ONE:
            printf("x = %g\n", otvet->x1);
            break;
        case ROOTS_TWO:
            printf("x1 = %g, x2 = %g\n", otvet->x1, otvet->x2);
            break;
        case ROOTS_COMPLEX:
            printf("x1 = %g + %gi, x2 = %g - %gi\n", otvet->re, otvet->im, otvet->re, otvet->im);
            break;
        default:
            return INVALID_ARGS;   /* в type мусор */
    }
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: lab1_task3 -q <eps> <a> <b> <c>\n");
        fprintf(stderr, "       lab1_task3 -m <a> <b>\n");
        fprintf(stderr, "       lab1_task3 -t <eps> <a> <b> <c>\n");
        return 1;
    }

    char flag = '\0';
    if (parse_flag(argv[1], &flag) != OK) {
        fprintf(stderr, "Error: wrong flag '%s'\n", argv[1]);
        return 1;
    }

    /* число аргументов зависит от флага */
    int nuzhno = (flag == 'm') ? 4 : 6;
    if (argc != nuzhno) {
        fprintf(stderr, "Error: flag -%c needs %d numbers\n", flag, nuzhno - 2);
        return 1;
    }

    if (flag == 'm') {
        long long a = 0, b = 0;
        for (int i = 2; i <= 3; i++) {
            long long *kuda = (i == 2) ? &a : &b;
            status_code sc = parse_long_long(argv[i], kuda);
            if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: '%s' is not an integer\n", argv[i]);
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: number '%s' is too big\n", argv[i]);
                return 1;
            }
        }

        int rez = 0;
        status_code sc = is_multiple(a, b, &rez);
        if (sc == ZERO_NUMBER) {
            fprintf(stderr, "Error: numbers must not be 0\n");
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: something went wrong\n");
            return 1;
        }
        printf("%lld %s a multiple of %lld\n", a, rez ? "is" : "is not", b);
        return 0;
    }

    /* для -q и -t: eps и три вещественных числа */
    double chisla[4];
    for (int i = 0; i < 4; i++) {
        status_code sc = parse_double(argv[i + 2], &chisla[i]);
        if (sc == INVALID_NUMBER) {
            fprintf(stderr, "Error: '%s' is not a number\n", argv[i + 2]);
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: bad number '%s'\n", argv[i + 2]);
            return 1;
        }
    }
    double eps = chisla[0];
    if (eps <= 0) {
        fprintf(stderr, "Error: eps must be more than 0\n");
        return 1;
    }

    if (flag == 'q') {
        double perest[6][3];
        int kolvo = 0;
        status_code sc = unique_permutations(&chisla[1], eps, perest, &kolvo);
        if (sc != OK) {
            fprintf(stderr, "Error: something went wrong\n");
            return 1;
        }
        for (int i = 0; i < kolvo; i++) {
            roots otvet;
            sc = solve_quadratic(perest[i][0], perest[i][1], perest[i][2], eps, &otvet);
            if (sc == OK) {
                sc = print_roots(perest[i][0], perest[i][1], perest[i][2], &otvet);
            }
            if (sc != OK) {
                fprintf(stderr, "Error: something went wrong\n");
                return 1;
            }
        }
    } else {
        int rez = 0;
        status_code sc = is_right_triangle(chisla[1], chisla[2], chisla[3], eps, &rez);
        if (sc != OK) {
            fprintf(stderr, "Error: something went wrong\n");
            return 1;
        }
        printf("%g, %g, %g %s sides of a right triangle\n", chisla[1], chisla[2], chisla[3],
               rez ? "can be" : "can not be");
    }
    return 0;
}
