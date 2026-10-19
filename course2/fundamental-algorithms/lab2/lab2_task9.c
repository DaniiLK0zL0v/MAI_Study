/*
 * Лаб 2, задание 9.
 * Функция с переменным числом аргументов: для каждой дроби из (0;1)
 * определить, имеет ли она конечное представление в системе счисления
 * с заданным основанием.
 *
 * Дробь p/q (в несократимом виде) конечна в системе с основанием b
 * тогда и только тогда, когда все простые делители знаменателя q
 * являются делителями основания b.
 *
 * Для double: представляем как рациональную дробь и проверяем.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

typedef enum {
    OK,
    INVALID_ARGS,
    ALLOC_ERROR
} status_code;

/* НОД */
static long long gcd(long long a, long long b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) {
        long long t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/*
 * Проверяет, имеет ли дробь (0 < value < 1) конечное представление
 * в системе счисления с основанием base.
 *
 * Алгоритм: превращаем double в рациональную дробь p/q,
 * сокращаем, затем проверяем, что все простые множители q делят base.
 */
static int has_finite_representation(double value, int base) {
    /* Превращаем в рациональную дробь */
    /* value = m / 2^k, где m, k — из представления double.
     * Проще: умножаем на 10^15 (или степень 2), чтобы получить целое. */

    /* Надёжнее: используем факт, что double = m * 2^e.
     * frexp даёт мантиссу в [0.5, 1) и экспоненту.
     * value = mantissa * 2^exp */

    int exp;
    double mantissa = frexp(value, &exp);
    /* mantissa * 2^exp = value, mantissa в [0.5, 1) */

    /* Домножим мантиссу до целого числа.
     * mantissa = p / 2^52 (для double), т.е. p = mantissa * 2^52 */
    long long p = (long long)(mantissa * (1LL << 52));
    long long q = 1LL << 52;
    /* Теперь value ≈ p * 2^(exp-52) / 1 = p / 2^(52-exp) */
    int shift = 52 - exp;

    if (shift > 0) {
        /* q = 2^shift, p уже есть */
        /* Но 2^shift может быть огромным. Вместо этого работаем иначе. */
        /* value = p / 2^shift */
    } else {
        /* value >= 1, не наш случай, но на всякий: p *= 2^(-shift), q = 1 */
        p <<= (-shift);
        q = 1;
        shift = 0;
    }

    /* Сокращаем p / 2^shift */
    /* НОД(p, 2^shift) = 2^min(v2(p), shift), где v2(p) — показатель 2 в p */
    if (p == 0) return 1; /* 0 — конечное */

    long long abs_p = (p > 0) ? p : -p;
    while (shift > 0 && (abs_p & 1) == 0) {
        abs_p >>= 1;
        shift--;
    }

    /* Теперь знаменатель = 2^shift.
     * Дробь конечна в base b <==> все простые множители знаменателя делят b.
     * Знаменатель = 2^shift, единственный простой множитель — 2.
     * Значит, дробь конечна <==> 2 делит base (т.е. base чётный). */

    /* Но это неполная картина! double не может точно представить, например, 1/3.
     * 1/3 ≈ 0.333... = p/2^52 (приближённо), знаменатель — степень 2.
     * Поэтому формально по IEEE754 любой double = p/2^k, и ответ зависит только
     * от чётности base.
     *
     * Правильнее: проверяем умножением. Домножаем value на base много раз
     * и смотрим, станет ли результат целым (с учётом точности). */

    double v = value;
    /* Максимум итераций: если за 100 итераций не стало целым — бесконечная */
    for (int i = 0; i < 100; i++) {
        v *= base;
        /* Убираем целую часть */
        v -= floor(v);
        /* Проверяем, стало ли 0 (с допуском) */
        if (v < 1e-9 || v > 1.0 - 1e-9) {
            return 1; /* конечное представление */
        }
    }
    return 0; /* бесконечное */
}

/*
 * Функция с переменным числом аргументов.
 * base — основание системы счисления
 * count — количество дробей
 * ... — дроби (double)
 * results — массив int[count], куда запишутся 1/0
 */
status_code check_finite(int base, int count, int *results, ...) {
    if (results == NULL || count <= 0 || base < 2 || base > 36) {
        return INVALID_ARGS;
    }

    va_list args;
    va_start(args, results);

    for (int i = 0; i < count; i++) {
        double value = va_arg(args, double);
        results[i] = has_finite_representation(value, base);
    }

    va_end(args);
    return OK;
}

/* ========================= main (демонстрация) ========================= */

int main(void) {
    printf("=== Finite representation check ===\n\n");

    int results[10];

    /* Base 10 */
    printf("Base 10:\n");
    check_finite(10, 5, results,
                 0.5,    /* 1/2 -> конечное */
                 0.25,   /* 1/4 -> конечное */
                 1.0/3,  /* 1/3 -> бесконечное */
                 0.1,    /* 1/10 -> конечное */
                 1.0/7); /* 1/7 -> бесконечное */

    double vals10[] = {0.5, 0.25, 1.0/3, 0.1, 1.0/7};
    for (int i = 0; i < 5; i++) {
        printf("  %.10f -> %s\n", vals10[i], results[i] ? "finite" : "infinite");
    }

    /* Base 2 */
    printf("\nBase 2:\n");
    check_finite(2, 4, results,
                 0.5,    /* 1/2 -> конечное */
                 0.25,   /* 1/4 -> конечное */
                 0.1,    /* 1/10 -> бесконечное */
                 0.125); /* 1/8 -> конечное */

    double vals2[] = {0.5, 0.25, 0.1, 0.125};
    for (int i = 0; i < 4; i++) {
        printf("  %.10f -> %s\n", vals2[i], results[i] ? "finite" : "infinite");
    }

    /* Base 3 */
    printf("\nBase 3:\n");
    check_finite(3, 3, results,
                 1.0/3,  /* 1/3 -> конечное в base 3 */
                 1.0/9,  /* 1/9 -> конечное */
                 0.5);   /* 1/2 -> бесконечное */

    double vals3[] = {1.0/3, 1.0/9, 0.5};
    for (int i = 0; i < 3; i++) {
        printf("  %.10f -> %s\n", vals3[i], results[i] ? "finite" : "infinite");
    }

    /* Base 12 */
    printf("\nBase 12:\n");
    check_finite(12, 3, results,
                 1.0/3,  /* 1/3 -> конечное (3 делит 12) */
                 1.0/4,  /* 1/4 -> конечное (4=2^2, 2 делит 12) */
                 1.0/5); /* 1/5 -> бесконечное (5 не делит 12) */

    double vals12[] = {1.0/3, 1.0/4, 1.0/5};
    for (int i = 0; i < 3; i++) {
        printf("  %.10f -> %s\n", vals12[i], results[i] ? "finite" : "infinite");
    }

    return 0;
}
