/*
 * Лабораторная 2, задание 9. Конечна ли дробь в системе счисления с основанием base.
 *
 * Дано основание base и несколько чисел x из интервала (0; 1). Для каждого x нужно
 * сказать, конечна ли его запись в системе с основанием base.
 * Пример: 1/4 = 0.25 в десятичной - конечная запись, 1/3 = 0.333... - бесконечная;
 *         0.1 = 1/10 в двоичной - бесконечная (0.000110011...), в десятичной - конечная.
 *
 * Запуск:  lab2_task9                                - демонстрация (набор готовых примеров)
 *          lab2_task9 <eps> <base> <x1> [x2 ...]      - свои числа
 *          например: lab2_task9 1e-12 2 0.5 0.1  ->  0.5 конечна в двоичной, 0.1 - нет
 *
 * Главная функция задания - check_fractions с переменным числом аргументов (...):
 *   check_fractions(&result, eps, base, count, x1, x2, ..., x_count)
 * Она выделяет память под массив результатов (по одному на каждое число;
 * освобождает вызывающий функцией free_results).
 *
 * Как решаем (два шага):
 *   1) x -> несократимая дробь p/q. Число double хранится в двоичном виде, и 0.1 в нем -
 *      это не ровно 1/10, а 0.1000000000000000055511151231257827... Поэтому ищем самую
 *      простую дробь p/q, которая отличается от x меньше чем на eps (цепные дроби,
 *      функция to_fraction). Для 0.1 и eps = 1e-12 это 1/10, для 1.0/3 - это 1/3.
 *   2) Правило: несократимая дробь p/q конечна в системе с основанием base тогда и только
 *      тогда, когда каждый простой делитель q делит base.
 *      Почему: конечная запись 0.c1c2...cn - это дробь N / base^n. Если p/q = N / base^n,
 *      то q делит base^n, а простые делители base^n - это ровно простые делители base.
 *      И наоборот: если все простые делители q есть в base, то при большом n base^n
 *      делится на q, и p/q = (p * base^n / q) / base^n - конечная запись из n цифр.
 *      Проверяем без разложения на простые: делим q на НОД(q, base), пока НОД > 1.
 *      Осталась 1 - все делители q были и в base (функция is_finite_in_base).
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free, strtod */
#include <stdarg.h>   /* va_list, va_start, va_arg, va_end - работа с переменным числом аргументов */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */
#include <limits.h>   /* LLONG_MAX - самое большое long long */
#include <ctype.h>    /* isspace */
#include <math.h>     /* fabs, isfinite, INFINITY, NAN */

/* Точность для демонстрации: x и p/q, которые отличаются меньше чем на EPS_DEMO,
   считаем равными. Это не глобальная переменная, а просто имя для числа:
   в функции eps всегда передается параметром.
   Почему 1e-12: десятичная дробь с d знаками после точки (p / 10^d) находится
   точно, если eps < 10^(-2d) (см. to_fraction). 1e-12 хватает для d <= 5:
   0.12345 -> 2469/20000. С eps = 1e-9 было бы только d <= 4 */
#define EPS_DEMO 1e-12

/* Основание от 2 до 36: цифры записи - 0..9 и буквы A..Z (36 знаков всего) */
#define MIN_BASE 2
#define MAX_BASE 36

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* в функцию передали NULL */
    INVALID_EPS,      /* eps не в интервале (0; 1) (или не число) */
    INVALID_BASE,     /* основание не от 2 до 36 */
    INVALID_COUNT,    /* количество чисел меньше 1 */
    INVALID_NUMBER,   /* строка не число */
    NOT_IN_RANGE,     /* число не в интервале (0; 1) (с учетом eps), бесконечность или NaN */
    NO_FRACTION,      /* не нашлось дроби p/q с q, помещающимся в long long (x очень мал: 1e-20 = 1/10^20) */
    NOT_FINITE,       /* дробь бесконечная - записать ее цифрами до конца нельзя */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* Результат для одного числа x */
typedef struct {
    double number;        /* само число x (как его передали) */
    status_code status;   /* OK, NOT_IN_RANGE или NO_FRACTION - ошибка именно этого числа */
    long long p;          /* x = p / q (несократимая дробь с точностью eps) */
    long long q;
    int is_finite;         /* 1 - запись в системе base конечная, 0 - бесконечная */
    long long remaining;    /* что осталось от q после деления на общие с base делители:
                             1 у конечной дроби; у бесконечной - "лишний" множитель (для 1/10
                             в двоичной это 5: в 2 нет делителя 5) */
    char *digits_str;          /* конечная запись "0.0011" в системе base (своя память);
                             NULL, если дробь бесконечная или была ошибка */
} fraction_result;

/* ===================== проверки ===================== */

/* eps должен быть обычным числом из (0; 1).
   eps >= 1 не имеет смысла: тогда любое x из (0; 1) "равно" и 0, и 1 */
status_code check_eps(const double eps) {
    /* isfinite(x) - макрос из math.h (C99): "x - обычное число, не бесконечность
       и не NaN". Для NaN любое сравнение ложно (и eps <= 0 тоже), поэтому
       без isfinite eps = NaN прошел бы проверку */
    if (!isfinite(eps) || eps <= 0 || eps >= 1) {
        return INVALID_EPS;
    }
    return OK;
}

/* Общая проверка параметров check_fractions: eps из (0; 1), основание от 2 до 36 */
status_code check_params(const double eps, const int base) {
    status_code sc = check_eps(eps);
    if (sc != OK) {
        return sc;
    }
    if (base < MIN_BASE || base > MAX_BASE) {
        return INVALID_BASE;
    }
    return OK;
}

/* ===================== целые числа ===================== */

/* НОД (наибольший общий делитель) алгоритмом Евклида: НОД(a, b) = НОД(b, a % b),
   пока b не станет 0. Пример: НОД(10, 4) = НОД(4, 2) = НОД(2, 0) = 2.
   Числа здесь всегда неотрицательные (знаменатель и основание) */
status_code gcd_ll(long long a, long long b, long long *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    if (a < 0 || b < 0) {
        return INVALID_ARGS;
    }
    while (b != 0) {
        long long tmp = a % b;
        a = b;
        b = tmp;
    }
    *result = a;
    return OK;
}

/* Одна цифра при переводе дроби remaining/q в систему base.
   Обычный способ (как столбиком): умножить на base, целая часть - очередная цифра,
   остаток от деления на q - новый remaining:
       digit = remaining * base / q,   new_remainder = remaining * base % q.
   Пример: 1/4 в десятичной: 1*10 = 10 -> цифра 2, остаток 2; 2*10 = 20 -> цифра 5,
   остаток 0 -> 0.25.
   Но remaining * base может не поместиться в long long (q бывает до ~9.2e18), поэтому
   умножение заменяем сложением remaining с самим собой base раз, и после каждого
   сложения сразу вычитаем q, если сумма дошла до q (считаем, сколько раз вычли -
   это и есть цифра). Сумма все время меньше q, переполнения нет.
   Как сложить без переполнения: "sum + remaining >= q" то же самое, что
   "sum >= q - remaining", а q - remaining посчитать можно (remaining < q).
   Тогда новая сумма: sum + remaining - q = sum - (q - remaining) */
status_code next_digit(const long long remaining, const int base, const long long q,
                       int *digit, long long *new_remainder) {
    if (digit == NULL || new_remainder == NULL) {
        return INVALID_ARGS;
    }
    if (q < 1 || remaining < 0 || remaining >= q || base < MIN_BASE || base > MAX_BASE) {
        return INVALID_ARGS;
    }
    long long sum = 0;
    int q_times = 0;
    for (int i = 0; i < base; i++) {
        if (sum >= q - remaining) {
            /* sum + remaining >= q: вычитаем q, цифра растет на 1 */
            sum = sum - (q - remaining);
            q_times++;
        } else {
            sum = sum + remaining;
        }
    }
    *digit = q_times;
    *new_remainder = sum;
    return OK;
}

/* ===================== алгоритм ===================== */

/* x -> несократимая дробь p/q, которая отличается от x меньше чем на eps.
   Цепная дробь:  x = 1 / (a1 + 1 / (a2 + 1 / (a3 + ...))),  a1, a2, ... - целые >= 1.
   Как получить a1, a2, ...: перевернуть число (1 / r), целая часть - очередное a,
   дробная часть - новое r, и так далее (вначале r = x).
   Пример: x = 0.3125 = 5/16:  1/0.3125 = 3.2 -> a1 = 3, r = 0.2;  1/0.2 = 5 -> a2 = 5, r = 0.
           Значит 0.3125 = 1 / (3 + 1/5).
   Если оборвать цепную дробь на k-м шаге, получится дробь h/k ("подходящая дробь").
   Числители и знаменатели считаются по формулам (каждая через две предыдущие):
       h_новое = a * h + h_старое,    k_новое = a * k + k_старое.
   Начало: h = 0, k = 1 (дробь 0/1) и перед ней h_старое = 1, k_старое = 0.
   Пример 5/16: a1 = 3 -> h = 3*0 + 1 = 1, k = 3*1 + 0 = 3 (дробь 1/3);
                a2 = 5 -> h = 5*1 + 0 = 5, k = 5*3 + 1 = 16 (дробь 5/16).
   Подходящие дроби приближают x все точнее, и знаменатели у них самые маленькие для
   такой точности. Останавливаемся на первой, которая ближе к x, чем eps.
   Подходящие дроби всегда несократимые (это свойство цепных дробей:
   h * k_старое - h_старое * k = +1 или -1, значит общих делителей у h и k нет).
   x должен быть из (0; 1) - это проверяет analyze_one.

   Как выбирать eps. Две разные дроби a/b и c/d отличаются хотя бы на 1 / (b * d)
   (разность (a*d - c*b) / (b*d), а целое a*d - c*b не 0, значит по модулю >= 1).
   Поэтому от десятичной дроби с d знаками (знаменатель 10^d) все более простые дроби
   (знаменатель меньше 10^d) отстоят больше чем на 1 / (10^d * 10^d) = 10^(-2d).
   Если eps <= 10^(-2d), они не подходят, и находится сама десятичная дробь:
   0.33333 с eps = 1e-12 -> 33333/100000. А при большом eps находится более простая
   соседка: 0.33333 с eps = 1e-3 -> 1/3 (они отличаются на 0.0000033).
   Слишком маленький eps (меньше погрешности double, ~1e-16) смысла не имеет:
   разность x - h/k считается в double, и как только h/k округляется в то же
   double, что и x, разность становится ровно 0 - меньше любого eps. Получится
   первая дробь, "неотличимая" от x в double, а не та, что имел в виду человек:
   0.987654321 с eps = 1e-300 -> 987662641/1000008424.
   Очень маленькому x нужен огромный знаменатель: 1e-20 = 1/10^20, а 10^20 не
   помещается в long long (максимум ~9.2e18) -> NO_FRACTION */
status_code to_fraction(const double x, const double eps, long long *p, long long *q) {
    if (p == NULL || q == NULL) {
        return INVALID_ARGS;
    }
    if (!isfinite(eps) || eps <= 0) {
        return INVALID_EPS;
    }
    if (!isfinite(x) || x <= 0 || x >= 1) {
        return NOT_IN_RANGE;
    }
    long long h_old = 1;
    long long k_old = 0;
    long long h = 0;
    long long k = 1;
    double r = x;   /* то, что еще осталось разложить; всегда в [0; 1) */

    /* Цикл конечен: знаменатель k на каждом шаге растет (a >= 1), и рано или поздно
       либо дробь станет ближе eps, либо k перестанет помещаться в long long */
    while (fabs(x - (double)h / (double)k) >= eps) {
        /* r = 0 - цепная дробь кончилась, а точности не хватило (eps меньше, чем
           погрешность double): 1/0 = бесконечность. Огромное 1/r (>= 2^63)
           не поместится в long long. В обоих случаях дроби не найти */
        double inverse = 1.0 / r;
        if (!isfinite(inverse) || inverse >= (double)LLONG_MAX) {
            return NO_FRACTION;
        }
        /* целая часть числа inverse > 1: приведение к long long отбрасывает дробную часть */
        long long a = (long long)inverse;
        r = inverse - (double)a;

        /* k_новое = a * k + k_старое не должно превысить LLONG_MAX.
           Проверяем без умножения (оно само могло бы переполниться):
           a * k + k_старое > LLONG_MAX  <=>  a > (LLONG_MAX - k_старое) / k.
           Числитель h проверять не нужно: дробь h/k меньше или равна 1, значит h <= k */
        if (a > (LLONG_MAX - k_old) / k) {
            return NO_FRACTION;
        }
        long long h_new = a * h + h_old;
        long long k_new = a * k + k_old;
        h_old = h;
        k_old = k;
        h = h_new;
        k = k_new;
    }
    *p = h;
    *q = k;
    return OK;
}

/* Конечна ли несократимая дробь со знаменателем q в системе base.
   Делим q на НОД(q, base), пока этот НОД больше 1: так из q уходят все простые
   делители, которые есть и в base (в любой степени).
   Пример: q = 40 = 2*2*2*5, base = 10:  НОД = 10 -> q = 4;  НОД = 2 -> q = 2;
           НОД = 2 -> q = 1  ->  конечная (1/40 = 0.025).
           q = 12 = 2*2*3, base = 10:  НОД = 2 -> q = 6;  НОД = 2 -> q = 3;
           НОД(3, 10) = 1 -> осталось 3  ->  бесконечная (1/12 = 0.08333...).
   *remaining - что осталось от q: 1 у конечной дроби, иначе множитель, которого нет в base */
status_code is_finite_in_base(const long long q, const int base, int *is_finite, long long *remaining) {
    if (is_finite == NULL || remaining == NULL) {
        return INVALID_ARGS;
    }
    if (q < 1 || base < MIN_BASE || base > MAX_BASE) {
        return INVALID_ARGS;
    }
    long long rest = q;
    long long gcd = 0;
    gcd_ll(rest, base, &gcd);
    /* q делится на gcd, поэтому деление точное; rest уменьшается, цикл конечен */
    while (gcd > 1) {
        rest = rest / gcd;
        gcd_ll(rest, base, &gcd);
    }
    *is_finite = (rest == 1);
    *remaining = rest;
    return OK;
}

/* Конечная запись дроби p/q (0 < p < q) в системе base: строка "0.25", "0.0011", "0.4"
   в динамической памяти (освобождает вызывающий). Цифры больше 9 - буквы A..Z.
   Сначала проверяем, что дробь конечная: иначе цифры шли бы бесконечно -> NOT_FINITE.
   Два прохода: первый только считает цифры (чтобы выделить память ровно по размеру),
   второй пишет их в строку. Цифр немного: каждая цифра "съедает" из q хотя бы
   один простой множитель (q делит base^n), а простых множителей в q меньше 63,
   потому что каждый не меньше 2 и q < 2^63 */
status_code fraction_digits(const long long p, const long long q, const int base, char **digits_str) {
    if (digits_str == NULL) {
        return INVALID_ARGS;
    }
    if (q < 1 || p <= 0 || p >= q || base < MIN_BASE || base > MAX_BASE) {
        return INVALID_ARGS;
    }
    int is_finite = 0;
    long long q_rest = 0;
    is_finite_in_base(q, base, &is_finite, &q_rest);
    if (!is_finite) {
        return NOT_FINITE;
    }

    /* проход 1: сколько цифр после точки (пока остаток не станет 0) */
    size_t digit_count = 0;
    long long remaining = p;
    while (remaining != 0) {
        int digit = 0;
        next_digit(remaining, base, q, &digit, &remaining);
        digit_count++;
    }

    /* "0." + цифры + '\0' в конце строки */
    char *str = (char *)malloc(digit_count + 3);
    if (str == NULL) {
        return ALLOC_ERROR;
    }
    str[0] = '0';
    str[1] = '.';

    /* проход 2: те же шаги еще раз, теперь записываем каждую цифру.
       Цифра 0..9 -> символ '0'..'9', 10..35 -> 'A'..'Z' */
    remaining = p;
    for (size_t i = 0; i < digit_count; i++) {
        int digit = 0;
        next_digit(remaining, base, q, &digit, &remaining);
        if (digit < 10) {
            str[2 + i] = (char)('0' + digit);
        } else {
            str[2 + i] = (char)('A' + digit - 10);
        }
    }
    str[2 + digit_count] = '\0';
    *digits_str = str;
    return OK;
}

/* Полная обработка одного числа x: заполняет *r.
   Возвращает OK, если с самим x все в порядке (ответ - в r->is_finite и r->digits_str),
   NOT_IN_RANGE или NO_FRACTION - ошибка этого числа, ALLOC_ERROR - нет памяти.
   r->status - то же, что вернула функция: так результат каждого числа хранится в массиве.
   eps и base уже проверены вызывающим (check_params) */
status_code analyze_one(const double x, const double eps, const int base, fraction_result *r) {
    if (r == NULL) {
        return INVALID_ARGS;
    }
    /* сначала заполняем "пустыми" значениями: при ошибке структура тоже аккуратная,
       а digits_str = NULL можно спокойно передать в free */
    r->number = x;
    r->status = OK;
    r->p = 0;
    r->q = 1;
    r->is_finite = 0;
    r->remaining = 1;
    r->digits_str = NULL;

    /* x должен быть из (0; 1). Здесь сравнение с точными границами интервала
       (0 и 1 сами не входят) - для этого eps не нужен.
       Бесконечность и NaN тоже сюда (isfinite) */
    if (!isfinite(x) || x <= 0 || x >= 1) {
        r->status = NOT_IN_RANGE;
        return NOT_IN_RANGE;
    }

    /* шаг 1: x -> p/q */
    status_code sc = to_fraction(x, eps, &r->p, &r->q);
    if (sc != OK) {
        r->status = sc;
        return sc;
    }
    /* С учетом eps x может оказаться "равным" 0 или 1: например 1e-13 при eps = 1e-12
       отличается от 0/1 меньше чем на eps, и to_fraction вернет 0/1; 0.999999999999 -
       вернет 1/1. Такие x тоже не из (0; 1). Проверяем уже по целым p и q - так
       точнее, чем сравнивать x с eps и 1 - eps (там мешает округление double) */
    if (r->p == 0 || r->p == r->q) {
        r->status = NOT_IN_RANGE;
        return NOT_IN_RANGE;
    }

    /* шаг 2: конечна ли p/q в системе base */
    is_finite_in_base(r->q, base, &r->is_finite, &r->remaining);

    /* для конечной дроби - еще и сама запись цифрами */
    if (r->is_finite) {
        sc = fraction_digits(r->p, r->q, base, &r->digits_str);
        if (sc != OK) {
            r->status = sc;
            return sc;
        }
    }
    return OK;
}

/* Освободить массив результатов: сначала строки digits_str внутри, потом сам массив.
   free(NULL) ничего не делает, поэтому NULL в digits_str (и result == NULL) - не ошибка */
status_code free_results(fraction_result *result, const int count) {
    if (result == NULL) {
        return OK;
    }
    for (int i = 0; i < count; i++) {
        free(result[i].digits_str);
    }
    free(result);
    return OK;
}

/* ===================== функция с переменным числом аргументов ===================== */

/* Для каждого из count чисел x определить, конечна ли его запись в системе base:
     check_fractions(&result, 1e-12, 10, 3, 0.5, 0.1, 1.0 / 3)
       -> result[0]: 1/2 конечна "0.5",  result[1]: 1/10 конечна "0.1",  result[2]: 1/3 бесконечна.
   result   - сюда кладем адрес массива из count результатов в динамической памяти
           (освобождать функцией free_results(result, count));
   eps   - точность перевода x в дробь p/q, 0 < eps < 1;
   base  - основание системы счисления, от 2 до 36;
   count - сколько чисел передано дальше, count >= 1;
   ...   - сами числа, ровно count штук, тип double, каждое из (0; 1).
   count стоит последним перед ...: va_start отсчитывает список с последнего
   именованного параметра, а сколько чисел читать, функция знает только из count.

   Ошибка одного числа (например 1.5 - не из (0; 1)) не останавливает остальные: она
   записывается в result[i].status, а функция возвращает OK. Ошибкой всей функции
   (result не выделяется) бывают только неверные eps, base, count и нехватка памяти.

   ВАЖНО: числа передавать именно как double: 0.5 или 1.0 / 3, а не 1 / 3
   (это деление целых, получится int 0). Тип аргумента функция узнать не может -
   va_arg(args, double) просто читает 8 байт как double; если передать int,
   прочитается мусор (неопределенное поведение). float передавать можно:
   в ... он сам превращается в double */
status_code check_fractions(fraction_result **result, const double eps, const int base, const int count, ...) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = check_params(eps, base);
    if (sc != OK) {
        return sc;
    }
    if (count < 1) {
        return INVALID_COUNT;
    }
    /* Размер памяти = count * sizeof(fraction_result). Проверка "count > SIZE_MAX / sizeof"
       - то же, что "count * sizeof > SIZE_MAX", но без умножения, которое само могло бы
       переполниться (на 64-битной системе int для этого слишком мал, на 32-битной - нет) */
    if ((size_t)count > SIZE_MAX / sizeof(fraction_result)) {
        return ALLOC_ERROR;
    }
    fraction_result *arr = (fraction_result *)malloc((size_t)count * sizeof(fraction_result));
    if (arr == NULL) {
        return ALLOC_ERROR;
    }
    /* Сразу ставим digits_str = NULL во всех элементах: если на середине кончится память,
       free_results освободит уже готовые строки, а в остальных будет free(NULL) */
    for (int i = 0; i < count; i++) {
        arr[i].digits_str = NULL;
    }

    /* va_list - "указатель" на следующий безымянный аргумент.
       va_start(args, count) ставит его сразу после параметра count */
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        /* va_arg достает следующий аргумент и считает, что он типа double */
        double x = va_arg(args, double);
        sc = analyze_one(x, eps, base, &arr[i]);
        /* ошибки самого числа (NOT_IN_RANGE, NO_FRACTION) уже записаны в arr[i].status -
           идем дальше. Нехватка памяти - ошибка всей функции: выходим */
        if (sc == ALLOC_ERROR) {
            /* va_end обязателен после va_start, даже при досрочном выходе */
            va_end(args);
            free_results(arr, count);
            return ALLOC_ERROR;
        }
    }
    va_end(args);

    *result = arr;
    return OK;
}

/* ===================== ввод (только для main) ===================== */

/* Строка из командной строки -> double. Разрешены обычные записи: 0.5, .25, 1e-9
   strtod(str, &end) читает число с начала строки и в end кладет адрес первого
   непрочитанного символа. Если *end != '\0' - после числа есть мусор ("0.5abc").
   strtod сам пропускает пробелы в начале, а мы их не разрешаем: " 0.5" - ошибка.
   "inf", "nan", 1e999 (strtod дает бесконечность) читаются как числа: их отсеет
   проверка интервала (NOT_IN_RANGE) или eps (INVALID_EPS) */
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
    *number = value;
    return OK;
}

/* Основание из командной строки: только цифры, значение от 2 до 36 */
status_code parse_base(const char *str, int *base) {
    if (str == NULL || base == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_BASE;
    }
    /* Собираем число слева направо (value * 10 + цифра). Как только стало больше 36,
       дальше можно не читать - это уже ошибка; так и переполнения int не бывает */
    int value = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_BASE;
        }
        value = value * 10 + (str[i] - '0');
        if (value > MAX_BASE) {
            return INVALID_BASE;
        }
    }
    if (value < MIN_BASE) {
        return INVALID_BASE;
    }
    *base = value;
    return OK;
}

/* ===================== вывод (только для main) ===================== */

/* Текст ошибки для статуса. Строка сообщения кладется в *message
   (строковые литералы живут всю программу, поэтому возвращать их адрес можно) */
status_code error_text(const status_code sc, const char **message) {
    if (message == NULL) {
        return INVALID_ARGS;
    }
    if (sc == INVALID_EPS) {
        *message = "eps must be greater than 0 and less than 1";
    } else if (sc == INVALID_BASE) {
        *message = "base must be from 2 to 36";
    } else if (sc == INVALID_COUNT) {
        *message = "count of numbers must be 1 or more";
    } else if (sc == NOT_IN_RANGE) {
        *message = "the number is not in (0; 1)";
    } else if (sc == NO_FRACTION) {
        *message = "can not find p/q, q would be too big";
    } else if (sc == ALLOC_ERROR) {
        *message = "not enough memory";
    } else {
        /* INVALID_ARGS (NULL), INVALID_NUMBER, NOT_FINITE сюда прийти не могут:
           main и демонстрация их не передают. Если сообщение появилось -
           ошибка в самой программе */
        *message = "internal error";
    }
    return OK;
}

/* Печать результата одного числа, одна строка:
     0.25 = 1/4 -> finite: 0.25
     0.1 = 1/10 -> infinite (factor 5 of q has no common divisor with base 2)
   Ошибка числа печатается в err ("Error: 1.5: the number is not in (0; 1)"),
   остальное - в out. Числа печатаем через %.15g: до 15 значащих цифр, без лишних
   нулей, - так 0.1 печатается как 0.1, а не 0.10000000000000001 */
status_code print_one(FILE *out, FILE *err, const fraction_result *r, const int base) {
    if (out == NULL || err == NULL || r == NULL) {
        return INVALID_ARGS;
    }
    if (r->status != OK) {
        const char *message = NULL;
        error_text(r->status, &message);
        fprintf(err, "Error: %.15g: %s\n", r->number, message);
        return OK;
    }
    fprintf(out, "%.15g = %lld/%lld -> ", r->number, r->p, r->q);
    if (r->is_finite) {
        fprintf(out, "finite: %s\n", r->digits_str);
    } else {
        fprintf(out, "infinite (factor %lld of q has no common divisor with base %d)\n",
                r->remaining, base);
    }
    return OK;
}

/* Печать всех результатов. *error_count - сколько чисел с ошибкой (для кода возврата) */
status_code print_results(FILE *out, FILE *err, const fraction_result *result, const int count,
                          const int base, int *error_count) {
    if (out == NULL || err == NULL || result == NULL || error_count == NULL) {
        return INVALID_ARGS;
    }
    *error_count = 0;
    for (int i = 0; i < count; i++) {
        print_one(out, err, &result[i], base);
        if (result[i].status != OK) {
            (*error_count)++;
        }
    }
    return OK;
}

/* Один пример демонстрации: заголовок "Base 10, eps = 1e-09:", потом результаты
   (или ошибка всей функции). sc и result - то, что вернула check_fractions.
   Ошибки чисел в демонстрации печатаем в stdout - чтобы все шло по порядку */
status_code print_demo(const double eps, const int base, const int count, const status_code sc,
                       const fraction_result *result) {
    printf("Base %d, eps = %g:\n", base, eps);
    if (sc == OK) {
        if (result == NULL) {
            return INVALID_ARGS;
        }
        int error_count = 0;
        print_results(stdout, stdout, result, count, base, &error_count);
    } else {
        const char *message = NULL;
        error_text(sc, &message);
        printf("Error: %s\n", message);
    }
    printf("\n");
    return OK;
}

/* Демонстрация: готовые вызовы check_fractions с разным количеством чисел.
   Каждый пример: 1) вызов, числа передаются через ... прямо в вызове;
   2) печать; 3) free_results. После ошибки всей функции result остается NULL,
   и free_results(NULL, ...) ничего не делает - поэтому освобождаем всегда */
status_code run_demo(void) {
    fraction_result *result = NULL;
    status_code sc = OK;

    printf("Is the number x from (0; 1) a finite fraction in base b?\n");
    printf("x is turned into p/q with |x - p/q| < eps, then p/q is finite\n");
    printf("if every prime factor of q divides b.\n\n");

    /* 1) десятичная: 2 и 5 делят 10, поэтому 1/2, 1/4, 1/10, 1/8 конечны,
          а 1/3 и 1/7 - нет */
    sc = check_fractions(&result, EPS_DEMO, 10, 6, 0.5, 0.25, 0.1, 0.125, 1.0 / 3, 1.0 / 7);
    print_demo(EPS_DEMO, 10, 6, sc, result);
    free_results(result, 6);
    result = NULL;   /* чтобы в следующем примере при ошибке не напечатать и не освободить старое */

    /* 2) двоичная: конечны только дроби со знаменателем 2^k. 0.1 = 1/10 - бесконечна
          (0.000110011... в двоичной), хотя в десятичной она конечная */
    sc = check_fractions(&result, EPS_DEMO, 2, 4, 0.5, 0.75, 0.0625, 0.1);
    print_demo(EPS_DEMO, 2, 4, sc, result);
    free_results(result, 4);
    result = NULL;

    /* 3) троичная: 1/3 и 2/9 конечны (0.1 и 0.02), а 1/2 - нет */
    sc = check_fractions(&result, EPS_DEMO, 3, 3, 1.0 / 3, 2.0 / 9, 0.5);
    print_demo(EPS_DEMO, 3, 3, sc, result);
    free_results(result, 3);
    result = NULL;

    /* 4) основание 12 = 2*2*3: конечны знаменатели из двоек и троек (1/3, 1/4, 1/6),
          а 1/5 - нет. 1/6 = 0.2 в двенадцатеричной: 2/12 = 1/6 */
    sc = check_fractions(&result, EPS_DEMO, 12, 4, 1.0 / 3, 0.25, 1.0 / 6, 0.2);
    print_demo(EPS_DEMO, 12, 4, sc, result);
    free_results(result, 4);
    result = NULL;

    /* 5) основание 16: цифры больше 9 - буквы. 0.6875 = 11/16 = 0.B */
    sc = check_fractions(&result, EPS_DEMO, 16, 2, 0.6875, 0.1);
    print_demo(EPS_DEMO, 16, 2, sc, result);
    free_results(result, 2);
    result = NULL;

    /* 6) роль eps: 0.33333 (5 троек) с eps = 1e-12 - это 33333/100000, конечная;
          с eps = 1e-3 то же число "равно" 1/3 (отличается на 0.0000033) - бесконечная */
    sc = check_fractions(&result, EPS_DEMO, 10, 1, 0.33333);
    print_demo(EPS_DEMO, 10, 1, sc, result);
    free_results(result, 1);
    result = NULL;
    sc = check_fractions(&result, 1e-3, 10, 1, 0.33333);
    print_demo(1e-3, 10, 1, sc, result);
    free_results(result, 1);
    result = NULL;

    printf("Errors:\n\n");

    /* 7) ошибки отдельных чисел: остальные числа все равно обработаны.
          1.5 и -0.25 не из (0; 1), 0 и 1 - границы (не входят), 1e-13 меньше eps -
          "почти 0", бесконечность и NaN - не числа (INFINITY, NAN - макросы из math.h) */
    sc = check_fractions(&result, EPS_DEMO, 10, 8, 0.5, 1.5, -0.25, 0.0, 1.0, 1e-13, INFINITY, NAN);
    print_demo(EPS_DEMO, 10, 8, sc, result);
    free_results(result, 8);
    result = NULL;

    /* 8) ошибки всей функции: результата нет вообще */
    sc = check_fractions(&result, 0.0, 10, 1, 0.5);
    print_demo(0.0, 10, 1, sc, result);
    free_results(result, 1);
    result = NULL;

    sc = check_fractions(&result, EPS_DEMO, 1, 1, 0.5);
    print_demo(EPS_DEMO, 1, 1, sc, result);
    free_results(result, 1);
    result = NULL;

    sc = check_fractions(&result, EPS_DEMO, 37, 1, 0.5);
    print_demo(EPS_DEMO, 37, 1, sc, result);
    free_results(result, 1);
    result = NULL;

    /* count = 0: после count чисел нет */
    sc = check_fractions(&result, EPS_DEMO, 10, 0);
    print_demo(EPS_DEMO, 10, 0, sc, result);
    free_results(result, 0);
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

    /* 2) С аргументами нужно минимум eps, основание и одно число */
    if (argc < 4) {
        fprintf(stderr, "Usage: lab2_task9                          (demo)\n");
        fprintf(stderr, "       lab2_task9 <eps> <base> <x1> [x2 ...]\n");
        return 1;
    }

    /* eps и основание - первые два аргумента; проверяем их сразу (check_eps -
       та же проверка, что внутри check_fractions): если они плохие, ни одно число
       обработать нельзя, и сообщение об ошибке должно быть одно, а не на каждое число */
    double eps = 0.0;
    int base = 0;
    if (parse_double(argv[1], &eps) != OK || check_eps(eps) != OK) {
        fprintf(stderr, "Error: eps '%s' must be a number greater than 0 and less than 1\n", argv[1]);
        return 1;
    }
    if (parse_base(argv[2], &base) != OK) {
        fprintf(stderr, "Error: base '%s' must be a number from 2 to 36\n", argv[2]);
        return 1;
    }

    /* 3) Числа обрабатываем по одному: check_fractions(&result, eps, base, 1, x).
       Число аргументов "..." должно быть известно при компиляции, а чисел в командной
       строке может быть сколько угодно - поэтому вызов на каждое число.
       Ошибка одного числа не останавливает остальные; код возврата 1, если ошибки были */
    int exit_code = 0;
    for (int i = 3; i < argc; i++) {
        double x = 0.0;
        if (parse_double(argv[i], &x) != OK) {
            /* не число вообще ("abc", "0.5x") - сообщаем и идем к следующему */
            fprintf(stderr, "Error: '%s' is not a number\n", argv[i]);
            exit_code = 1;
            continue;
        }
        fraction_result *result = NULL;
        status_code sc = check_fractions(&result, eps, base, 1, x);
        if (sc != OK) {
            /* eps и base уже проверены, count = 1 - остается только ALLOC_ERROR.
               "internal error" - для статусов, которых тут быть не может (ошибка в программе) */
            fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n"
                                              : "Error: internal error\n");
            return 1;
        }
        int error_count = 0;
        print_results(stdout, stderr, result, 1, base, &error_count);
        if (error_count > 0) {
            exit_code = 1;
        }
        free_results(result, 1);
    }
    return exit_code;
}
