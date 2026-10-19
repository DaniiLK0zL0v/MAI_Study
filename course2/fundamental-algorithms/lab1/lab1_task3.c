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

/* Результат решения одного уравнения. Какие поля заполнены, зависит от type:
   ROOTS_ONE - x1, ROOTS_TWO - x1 и x2, ROOTS_COMPLEX - re и im, остальные - ничего */
typedef struct {
    roots_type type;
    double x1;
    double x2;
    double re;   /* для комплексных корней */
    double im;
} roots;

/* Строка -> double: конечное число без мусора */
status_code parse_double(const char *str, double *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    /* strtod сам пропускает пробелы в начале, а нам " 7" не подходит */
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }
    /* strtod читает число с начала строки и записывает в end адрес первого
       символа, который уже не вошел в число */
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

/* Строка -> long long. Знак + или - (можно без него), потом только цифры.
   Разбираем сами, переполнение проверяем до умножения.
   long long, а не long: long на Windows 4 байта, на Linux 8 - результаты различались бы */
status_code parse_long_long(const char *str, long long *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    /* i - откуда начинаются цифры, negative - был ли минус (1 - да) */
    size_t i = 0;
    int negative = 0;
    if (str[0] == '-' || str[0] == '+') {
        negative = (str[0] == '-');
        i = 1;
    }
    /* пустая строка или один знак - не число */
    if (str[i] == '\0') {
        return INVALID_NUMBER;
    }
    /* дальше должны быть только цифры */
    for (size_t j = i; str[j] != '\0'; j++) {
        if (str[j] < '0' || str[j] > '9') {
            return INVALID_NUMBER;
        }
    }

    /* Число собираем без знака (модуль), знак ставим в конце.
       limit - самый большой модуль, который влезет в long long:
       LLONG_MAX для положительных, LLONG_MAX + 1 для отрицательных (модуль LLONG_MIN).
       LLONG_MAX + 1 в long long не влезает, поэтому копим в unsigned long long */
    unsigned long long limit = negative ? (unsigned long long)LLONG_MAX + 1 : (unsigned long long)LLONG_MAX;
    unsigned long long magnitude = 0;
    /* "123" -> 1 -> 12 -> 123: каждый раз magnitude * 10 + цифра */
    for (; str[i] != '\0'; i++) {
        unsigned long long digit = (unsigned long long)(str[i] - '0');   /* '7' - '0' = 7 */
        /* проверяем ДО умножения, иначе переполнение уже случится и его не заметить.
           magnitude * 10 + digit > limit  <=>  magnitude > (limit - digit) / 10 */
        if (magnitude > (limit - digit) / 10) {
            return OUT_OF_RANGE;
        }
        magnitude = magnitude * 10 + digit;
    }

    /* Ставим знак: без минуса - модуль и есть ответ.
       Минус и модуль ровно limit - это LLONG_MIN: -(long long)magnitude не сработает,
       потому что magnitude сначала переводится в long long, а туда он не влезает.
       Иначе модуль влезает в long long, переводим и меняем знак */
    if (!negative) {
        *number = (long long)magnitude;
    } else if (magnitude == limit) {
        *number = LLONG_MIN;   /* -(LLONG_MAX + 1) в long long не посчитать */
    } else {
        *number = -(long long)magnitude;
    }
    return OK;
}

/* Флаг: '-' или '/' и буква q, m или t */
status_code parse_flag(const char *arg, char *flag) {
    if (arg == NULL || flag == NULL) {
        return INVALID_ARGS;
    }
    /* ровно 2 символа: arg[1] == '\0' - один символ, arg[2] != '\0' - больше двух */
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
   Все сравнения вещественных - с точностью eps: |u - v| < eps значит u = v.
   Обычное == для double не годится: из-за округления 0.1 + 0.2 != 0.3 */
status_code solve_quadratic(const double a, const double b, const double c,
                            const double eps, roots *answer) {
    if (answer == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* обнуляем все поля, чтобы в неиспользуемых не лежал мусор */
    answer->x1 = answer->x2 = answer->re = answer->im = 0.0;

    /* a = 0 - уравнение линейное: b*x + c = 0 */
    if (fabs(a) < eps) {
        /* b = 0 тоже: остается c = 0. Верно при любом x, если c = 0, и никогда - иначе */
        if (fabs(b) < eps) {
            answer->type = (fabs(c) < eps) ? ROOTS_INFINITE : ROOTS_NONE;
        } else {
            answer->type = ROOTS_ONE;
            answer->x1 = -c / b;
        }
    } else {
        /* обычное квадратное уравнение: число корней по знаку дискриминанта */
        double d = b * b - 4 * a * c;   /* дискриминант */
        if (fabs(d) < eps) {
            answer->type = ROOTS_ONE;
            answer->x1 = -b / (2 * a);
        } else if (d > 0) {             /* |d| >= eps уже знаем, так что d > 0 значит d >= eps */
            double sqrt_d = sqrt(d);
            answer->type = ROOTS_TWO;
            answer->x1 = (-b + sqrt_d) / (2 * a);
            answer->x2 = (-b - sqrt_d) / (2 * a);
        } else {
            /* d < 0: корни комплексные, x = -b/(2a) +- sqrt(-d)/(2a) * i.
               fabs(a) - чтобы мнимая часть была положительной, знак +- печатаем сами */
            answer->type = ROOTS_COMPLEX;
            answer->re = -b / (2 * a);
            answer->im = sqrt(-d) / (2 * fabs(a));
        }
    }

    /* число меньше eps по модулю - это 0. Заодно убираем "-0" (printf печатает его как "-0") */
    if (fabs(answer->x1) < eps) {
        answer->x1 = 0.0;
    }
    if (fabs(answer->x2) < eps) {
        answer->x2 = 0.0;
    }
    if (fabs(answer->re) < eps) {
        answer->re = 0.0;
    }
    return OK;
}

/* -q: все уникальные перестановки трех коэффициентов.
   perms - массив [6][3] для ответа, count - сколько уникальных получилось */
status_code unique_permutations(const double coefs[3], const double eps,
                                double perms[6][3], int *count) {
    if (coefs == NULL || perms == NULL || count == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* все 6 способов расставить индексы 0, 1, 2.
       Например {1, 2, 0}: a = coefs[1], b = coefs[2], c = coefs[0] */
    const int order[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
    };

    *count = 0;
    for (int i = 0; i < 6; i++) {
        double a = coefs[order[i][0]];
        double b = coefs[order[i][1]];
        double c = coefs[order[i][2]];

        /* Проверяем, не было ли уже такой же тройки (с точностью eps).
           Повторы бывают, когда коэффициенты совпадают: у 1 1 2 всего 3 разные перестановки */
        int seen = 0;
        for (int j = 0; j < *count; j++) {
            if (fabs(perms[j][0] - a) < eps && fabs(perms[j][1] - b) < eps &&
                fabs(perms[j][2] - c) < eps) {
                seen = 1;
                break;
            }
        }
        /* новая тройка - дописываем в конец ответа */
        if (!seen) {
            perms[*count][0] = a;
            perms[*count][1] = b;
            perms[*count][2] = c;
            (*count)++;
        }
    }
    return OK;
}

/* -m: кратно ли a числу b (оба не 0). result = 1 - да, 0 - нет */
status_code is_multiple(const long long a, const long long b, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    if (a == 0 || b == 0) {
        return ZERO_NUMBER;
    }
    /* LLONG_MIN % -1 в C - неопределенное поведение (может упасть), а на +-1 делится все */
    if (b == 1 || b == -1) {
        *result = 1;
        return OK;
    }
    /* остаток 0 - делится нацело. Сравнение дает 1 или 0 - его и кладем в ответ */
    *result = (a % b == 0);
    return OK;
}

/* -t: могут ли a, b, c быть сторонами прямоугольного треугольника.
   result = 1 - да, 0 - нет */
status_code is_right_triangle(const double a, const double b, const double c,
                              const double eps, int *result) {
    if (result == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* стороны должны быть положительными (больше 0 с точностью eps) */
    if (a < eps || b < eps || c < eps) {
        *result = 0;
        return OK;
    }

    /* теорема Пифагора. Гипотенузой может быть любая из трех сторон -
       проверяем все три варианта, так не нужно искать самую длинную.
       Результат || - 1 или 0, его и кладем в ответ */
    double aa = a * a, bb = b * b, cc = c * c;
    *result = fabs(aa + bb - cc) < eps || fabs(aa + cc - bb) < eps || fabs(bb + cc - aa) < eps;
    return OK;
}

/* Печать корней одного уравнения. %g - печать double без лишних нулей: 2.5, а не 2.500000 */
status_code print_roots(const double a, const double b, const double c, const roots *answer) {
    if (answer == NULL) {
        return INVALID_ARGS;
    }
    printf("%g*x^2 + %g*x + %g = 0:  ", a, b, c);
    switch (answer->type) {
        case ROOTS_NONE:
            printf("no roots\n");
            break;
        case ROOTS_INFINITE:
            printf("any x is a root\n");
            break;
        case ROOTS_ONE:
            printf("x = %g\n", answer->x1);
            break;
        case ROOTS_TWO:
            printf("x1 = %g, x2 = %g\n", answer->x1, answer->x2);
            break;
        case ROOTS_COMPLEX:
            printf("x1 = %g + %gi, x2 = %g - %gi\n", answer->re, answer->im, answer->re, answer->im);
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

    /* число аргументов зависит от флага: -m - два числа, -q и -t - четыре.
       В argc еще входят имя программы и сам флаг, отсюда 4 и 6 */
    int needed = (flag == 'm') ? 4 : 6;
    if (argc != needed) {
        fprintf(stderr, "Error: flag -%c needs %d numbers\n", flag, needed - 2);
        return 1;
    }

    /* "internal error" ниже - запасная ветка для статусов, которые здесь появиться
       не могут: INVALID_ARGS (main передает правильные адреса и eps > 0).
       Пользователь это сообщение не увидит; если увидел - ошибка в самом коде программы */

    if (flag == 'm') {
        /* читаем два целых: argv[2] в a, argv[3] в b.
           target - адрес переменной, куда положить очередное число */
        long long a = 0, b = 0;
        for (int i = 2; i <= 3; i++) {
            long long *target = (i == 2) ? &a : &b;
            status_code sc = parse_long_long(argv[i], target);
            if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: '%s' is not an integer\n", argv[i]);
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: number '%s' is too big\n", argv[i]);
                return 1;
            }
        }

        int result = 0;
        status_code sc = is_multiple(a, b, &result);
        if (sc == ZERO_NUMBER) {
            fprintf(stderr, "Error: numbers must not be 0\n");
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        printf("%lld %s a multiple of %lld\n", a, result ? "is" : "is not", b);
        return 0;
    }

    /* для -q и -t: eps и три вещественных числа.
       numbers[0] - eps, numbers[1..3] - три числа (argv[2..5]) */
    double numbers[4];
    for (int i = 0; i < 4; i++) {
        status_code sc = parse_double(argv[i + 2], &numbers[i]);
        if (sc == INVALID_NUMBER) {
            fprintf(stderr, "Error: '%s' is not a number\n", argv[i + 2]);
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: bad number '%s'\n", argv[i + 2]);
            return 1;
        }
    }
    double eps = numbers[0];
    if (eps <= 0) {
        fprintf(stderr, "Error: eps must be more than 0\n");
        return 1;
    }

    if (flag == 'q') {
        /* &numbers[1] - адрес второго элемента: функция видит numbers[1..3]
           как массив из трех коэффициентов */
        double perms[6][3];
        int count = 0;
        status_code sc = unique_permutations(&numbers[1], eps, perms, &count);
        if (sc != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        /* для каждой перестановки: решить уравнение и, если все хорошо, напечатать.
           Обе ошибки обрабатываем одним if после */
        for (int i = 0; i < count; i++) {
            roots answer;
            sc = solve_quadratic(perms[i][0], perms[i][1], perms[i][2], eps, &answer);
            if (sc == OK) {
                sc = print_roots(perms[i][0], perms[i][1], perms[i][2], &answer);
            }
            if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
        }
    } else {
        int result = 0;
        status_code sc = is_right_triangle(numbers[1], numbers[2], numbers[3], eps, &result);
        if (sc != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        printf("%g, %g, %g %s sides of a right triangle\n", numbers[1], numbers[2], numbers[3],
               result ? "can be" : "can not be");
    }
    return 0;
}
