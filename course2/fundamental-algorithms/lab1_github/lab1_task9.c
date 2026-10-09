#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL, пустой массив, a > b */
    INVALID_NUMBER,
    OUT_OF_RANGE,      /* число не помещается в int */
    NO_MEMORY
} status_code;

#define FIXED_SIZE 15   /* размер массива в части 1 */

/* Строка -> int. Знак + или - (можно без него), потом только цифры.
   Разбираем сами, переполнение проверяем до умножения */
status_code parse_int(const char *str, int *chislo) {
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

    /* модуль копим в long long: для int это с запасом.
       Модуль INT_MIN на 1 больше INT_MAX */
    long long predel = otric ? (long long)INT_MAX + 1 : (long long)INT_MAX;
    long long modul = 0;
    for (; str[i] != '\0'; i++) {
        modul = modul * 10 + (str[i] - '0');
        if (modul > predel) {
            return OUT_OF_RANGE;   /* проверяем после каждой цифры - long long не переполнится */
        }
    }
    *chislo = (int)(otric ? -modul : modul);
    return OK;
}

/* ===== Свой генератор псевдослучайных чисел =====
   rand()/srand() не используем: они хранят состояние в скрытой глобальной
   переменной библиотеки, а глобальные переменные запрещены.
   Здесь состояние лежит в структуре, которую создает main и передает по указателю.
   Линейный конгруэнтный генератор: x(n+1) = (A * x(n) + C) mod 2^64.
   mod 2^64 получается сам: переполнение unsigned в C определено (берется остаток). */
typedef struct {
    unsigned long long state;
} generator;

#define GEN_A 6364136223846793005ULL   /* константы Д. Кнута (MMIX) */
#define GEN_C 1442695040888963407ULL

/* Начальное состояние (зерно) */
status_code gen_init(generator *gen, const unsigned long long seed) {
    if (gen == NULL) {
        return INVALID_ARGS;
    }
    gen->state = seed;
    return OK;
}

/* Следующее число от 0 до 2^32 - 1. Берем старшие 32 бита:
   у младших битов такого генератора маленький период */
status_code gen_next(generator *gen, unsigned long long *rez) {
    if (gen == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    gen->state = gen->state * GEN_A + GEN_C;
    *rez = gen->state >> 32;
    return OK;
}

/* Случайное целое в [a, b].
   Склеиваем два 32-битных числа в одно 64-битное: длина диапазона до 2^32,
   и остаток r % dlina почти не дает перекоса (с одним 32-битным числом
   при огромном диапазоне некоторые значения выпадали бы вдвое чаще) */
status_code random_in_range(generator *gen, const int a, const int b, int *rez) {
    if (gen == NULL || rez == NULL || a > b) {
        return INVALID_ARGS;
    }
    unsigned long long dlina = (unsigned long long)((long long)b - a + 1);   /* до 2^32 */
    unsigned long long hi = 0, lo = 0;
    status_code sc = gen_next(gen, &hi);
    if (sc == OK) {
        sc = gen_next(gen, &lo);
    }
    if (sc != OK) {
        return sc;
    }
    unsigned long long r = (hi << 32) | lo;
    *rez = (int)((long long)a + (long long)(r % dlina));
    return OK;
}

/* Заполнить массив случайными числами из [a, b] */
status_code fill_random(generator *gen, int *arr, const size_t n, const int a, const int b) {
    if (gen == NULL || arr == NULL || a > b) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < n; i++) {
        status_code sc = random_in_range(gen, a, b, &arr[i]);
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

/* Часть 1: за ОДИН проход найти минимум и максимум и поменять их местами */
status_code swap_min_max(int *arr, const size_t n, size_t *i_min, size_t *i_max) {
    if (arr == NULL || i_min == NULL || i_max == NULL || n == 0) {
        return INVALID_ARGS;
    }
    *i_min = 0;
    *i_max = 0;
    for (size_t i = 1; i < n; i++) {
        if (arr[i] < arr[*i_min]) {
            *i_min = i;
        }
        if (arr[i] > arr[*i_max]) {
            *i_max = i;
        }
    }
    int tmp = arr[*i_min];
    arr[*i_min] = arr[*i_max];
    arr[*i_max] = tmp;
    return OK;
}

/* Сравнение для qsort: возвращает <0, 0, >0. (x > y) - (x < y) не переполняется, в отличие от x - y.
   Единственная функция без статус-кода: ее вид int f(const void *, const void *) задает qsort */
int compare_ints(const void *p1, const void *p2) {
    int x = *(const int *)p1;
    int y = *(const int *)p2;
    return (x > y) - (x < y);
}

/* Ближайшее к x значение в ОТСОРТИРОВАННОМ массиве (двоичный поиск) */
status_code find_nearest(const int *sorted, const size_t n, const int x, int *nearest) {
    if (sorted == NULL || nearest == NULL || n == 0) {
        return INVALID_ARGS;
    }
    /* ищем первый элемент >= x: он в позиции lo */
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (sorted[mid] < x) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    /* ближайший - либо sorted[lo] (первый >= x), либо sorted[lo-1] (последний < x) */
    if (lo == n) {
        *nearest = sorted[n - 1];
    } else if (lo == 0) {
        *nearest = sorted[0];
    } else {
        long long right = (long long)sorted[lo] - x;
        long long left = (long long)x - sorted[lo - 1];
        *nearest = (left <= right) ? sorted[lo - 1] : sorted[lo];
    }
    return OK;
}

/* Часть 2: C[i] = A[i] + ближайший к A[i] элемент из B.
   B сортируем (копию, чтобы не портить исходный), потом для каждого A[i] - двоичный поиск.
   Память под C выделяется здесь, освобождает вызывающий */
status_code build_c(const int *a, const size_t n_a, const int *b, const size_t n_b, int **c) {
    if (a == NULL || b == NULL || c == NULL || n_a == 0 || n_b == 0) {
        return INVALID_ARGS;
    }
    int *b_sorted = malloc(n_b * sizeof(int));
    if (b_sorted == NULL) {
        return NO_MEMORY;
    }
    int *rez = malloc(n_a * sizeof(int));
    if (rez == NULL) {
        free(b_sorted);
        return NO_MEMORY;
    }

    for (size_t i = 0; i < n_b; i++) {
        b_sorted[i] = b[i];
    }
    qsort(b_sorted, n_b, sizeof(int), compare_ints);

    for (size_t i = 0; i < n_a; i++) {
        int blizh = 0;
        status_code sc = find_nearest(b_sorted, n_b, a[i], &blizh);
        if (sc != OK) {
            free(b_sorted);
            free(rez);
            return sc;
        }
        rez[i] = a[i] + blizh;   /* |a[i]| <= 1000 и |blizh| <= 1000 - переполнения нет */
    }

    free(b_sorted);
    *c = rez;
    return OK;
}

status_code print_array(const char *name, const int *arr, const size_t n) {
    if (name == NULL || arr == NULL) {
        return INVALID_ARGS;
    }
    printf("%s:", name);
    for (size_t i = 0; i < n; i++) {
        printf(" %d", arr[i]);
    }
    printf("\n");
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: lab1_task9 <a> <b>\n");
        fprintf(stderr, "Example: lab1_task9 -50 50\n");
        return 1;
    }
    int a = 0, b = 0;
    for (int i = 1; i <= 2; i++) {
        int *kuda = (i == 1) ? &a : &b;
        status_code sc = parse_int(argv[i], kuda);
        if (sc == INVALID_NUMBER) {
            fprintf(stderr, "Error: '%s' is not an integer\n", argv[i]);
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: number '%s' is too big\n", argv[i]);
            return 1;
        }
    }
    if (a > b) {
        fprintf(stderr, "Error: a must be not more than b\n");
        return 1;
    }

    /* зерно генератора - текущее время, иначе числа будут одинаковые при каждом запуске.
       time может вернуть -1 (время недоступно) - тогда берем любое число, например 1 */
    time_t seychas = time(NULL);
    generator gen;
    if (gen_init(&gen, (seychas == (time_t)-1) ? 1ULL : (unsigned long long)seychas) != OK) {
        fprintf(stderr, "Error: something went wrong\n");
        return 1;
    }

    /* ===== Часть 1 ===== */
    printf("Part 1\n");
    int arr[FIXED_SIZE];
    if (fill_random(&gen, arr, FIXED_SIZE, a, b) != OK ||
        print_array("before", arr, FIXED_SIZE) != OK) {
        fprintf(stderr, "Error: something went wrong\n");
        return 1;
    }
    size_t i_min = 0, i_max = 0;
    if (swap_min_max(arr, FIXED_SIZE, &i_min, &i_max) != OK ||
        print_array("after ", arr, FIXED_SIZE) != OK) {
        fprintf(stderr, "Error: something went wrong\n");
        return 1;
    }
    printf("min was at index %zu, max was at index %zu\n", i_min, i_max);

    /* ===== Часть 2 ===== */
    printf("\nPart 2\n");
    /* случайные размеры от 10 до 10000 */
    int n_a_int = 0, n_b_int = 0;
    if (random_in_range(&gen, 10, 10000, &n_a_int) != OK ||
        random_in_range(&gen, 10, 10000, &n_b_int) != OK) {
        fprintf(stderr, "Error: something went wrong\n");
        return 1;
    }
    size_t n_a = (size_t)n_a_int, n_b = (size_t)n_b_int;

    int *arr_a = malloc(n_a * sizeof(int));
    int *arr_b = malloc(n_b * sizeof(int));
    if (arr_a == NULL || arr_b == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        free(arr_a);   /* free(NULL) безопасен, поэтому можно не проверять каждый */
        free(arr_b);
        return 1;
    }
    if (fill_random(&gen, arr_a, n_a, -1000, 1000) != OK ||
        fill_random(&gen, arr_b, n_b, -1000, 1000) != OK) {
        fprintf(stderr, "Error: something went wrong\n");
        free(arr_a);
        free(arr_b);
        return 1;
    }

    /* C[i] = A[i] + ближайший к A[i] из B */
    int *arr_c = NULL;
    status_code sc = build_c(arr_a, n_a, arr_b, n_b, &arr_c);
    if (sc != OK) {
        fprintf(stderr, "Error: %s\n", (sc == NO_MEMORY) ? "not enough memory" : "something went wrong");
        free(arr_a);
        free(arr_b);
        return 1;
    }

    printf("size of A = %zu, size of B = %zu\n", n_a, n_b);
    printf("first 10 elements:\n");
    for (size_t i = 0; i < 10; i++) {
        printf("  A[%zu] = %5d   nearest in B = %5d   C[%zu] = %5d\n",
               i, arr_a[i], arr_c[i] - arr_a[i], i, arr_c[i]);
    }

    free(arr_a);
    free(arr_b);
    free(arr_c);
    return 0;
}
