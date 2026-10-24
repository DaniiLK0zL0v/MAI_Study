/*
 * Лабораторная 3, задание 1. Перевод числа в систему счисления с основанием 2^r, r = 1..5
 * (основания 2, 4, 8, 16, 32).
 *
 * По условию в функции перевода можно только битовые операции (& | ^ ~ << >>) и обращения
 * к памяти; обычная арифметика (+ - * / %) запрещена. Поэтому:
 *   - сложение сделано своей функцией bit_add на битовых операциях;
 *   - цифра - это младшие r бит числа: value & mask, где mask = ~(~0 << r) (r единиц подряд);
 *   - к следующей цифре переходим сдвигом value >> r (это деление на 2^r без деления);
 *   - модуль отрицательного числа - дополнительный код: ~x + 1 (единицу прибавляет bit_add).
 *
 * Запуск:  lab3_task1                 - демонстрация (таблица для нескольких чисел и всех r)
 *          lab3_task1 <число> <r>      - одно число; число - целое типа long long, r = 1..5
 * Пример:  lab3_task1 255 4   ->   Result: FF  (255 в системе с основанием 16)
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free */
#include <limits.h>   /* LLONG_MIN, LLONG_MAX */

/* Самая длинная запись: long long в системе с основанием 2 - 64 цифры, плюс знак '-' и '\0' */
#define RESULT_SIZE 66

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* в функцию передали NULL */
    INVALID_BASE,     /* r не от 1 до 5 */
    INVALID_NUMBER,   /* строка - не целое число */
    OVERFLOW_ERROR,   /* число не помещается в long long / перенос вышел за 64 бита */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* ===================== арифметика на битовых операциях ===================== */

/*
 * Сложение a + b без знака '+'.
 * a ^ b - сумма разрядов без переносов (1+1 дает 0), a & b - где возникают переносы;
 * перенос идет в следующий разряд, поэтому сдвигаем его на 1 влево и снова складываем.
 * Повторяем, пока переносов не останется (не больше 64 раз).
 * Если перенос выходит за старший (63-й) бит - результат не помещается: OVERFLOW_ERROR.
 */
status_code bit_add(const unsigned long long a, const unsigned long long b, unsigned long long *sum) {
    if (sum == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long x = a;
    unsigned long long y = b;
    /* 1ULL << 63 - единица в самом старшем бите */
    const unsigned long long top_bit = 1ULL << 63;
    while (y != 0) {
        unsigned long long carry = x & y;
        if (carry & top_bit) {
            return OVERFLOW_ERROR;
        }
        x = x ^ y;
        y = carry << 1;
    }
    *sum = x;
    return OK;
}

/*
 * Сложение по модулю 2^64: те же шаги, что в bit_add, но перенос из старшего бита
 * просто теряется (так и нужно при вычитании через дополнительный код).
 */
status_code add_modulo(const unsigned long long a, const unsigned long long b, unsigned long long *sum) {
    if (sum == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long x = a;
    unsigned long long y = b;
    while (y != 0) {
        unsigned long long carry = x & y;
        x = x ^ y;
        y = carry << 1;   /* перенос из 63-го бита при сдвиге пропадает */
    }
    *sum = x;
    return OK;
}

/*
 * Вычитание a - b (a >= b) без знака '-': a + ~b + 1.
 * ~b + 1 - дополнительный код, то есть "минус b" по модулю 2^64.
 */
status_code bit_subtract(const unsigned long long a, const unsigned long long b, unsigned long long *difference) {
    if (difference == NULL) {
        return INVALID_ARGS;
    }
    if (a < b) {
        return OVERFLOW_ERROR;   /* результат был бы отрицательным */
    }
    unsigned long long x = 0;
    add_modulo(a, ~b, &x);   /* a + ~b */
    add_modulo(x, 1, &x);    /* + 1 */
    *difference = x;
    return OK;
}

/* ===================== перевод ===================== */

/*
 * Главная функция задания: число number -> строка в системе с основанием 2^r.
 * Цифры больше 9 - заглавные латинские буквы: A = 10, ..., V = 31.
 * Строка выделяется через malloc и возвращается через *result (освобождает вызывающий).
 */
status_code to_base_pow2(const long long number, const int r, char **result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    if (r < 1 || r > 5) {
        return INVALID_BASE;
    }
    /* символ цифры по ее значению - обращение к памяти (элемент массива), а не арифметика */
    const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

    /* модуль числа. Приведение к unsigned long long не меняет биты: отрицательное число
       хранится в дополнительном коде, и ~x + 1 дает его модуль. Так работает и для LLONG_MIN,
       у которого модуль (2^63) не помещается в long long, но помещается в unsigned long long */
    unsigned long long value = (unsigned long long)number;
    int negative = number < 0;
    if (negative) {
        status_code sc = bit_add(~value, 1, &value);
        if (sc != OK) {
            return sc;   /* сюда не попадаем: ~value + 1 не больше 2^63 */
        }
    }

    /* маска из r единиц: ~0 - все единицы, сдвиг влево на r освобождает r младших бит,
       ~ превращает их в единицы, а остальные - в нули. r = 4: ...11110000 -> 00001111 */
    const unsigned long long mask = ~(~0ULL << r);

    char *buf = (char *)malloc(RESULT_SIZE);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }

    /* цифры получаются от младшей к старшей - пишем их с конца буфера к началу.
       pos - куда писать следующую цифру; уменьшаем его через bit_subtract */
    unsigned long long pos = RESULT_SIZE;
    bit_subtract(pos, 1, &pos);
    buf[pos] = '\0';
    do {
        bit_subtract(pos, 1, &pos);
        buf[pos] = digits[value & mask];   /* младшие r бит - очередная цифра */
        value = value >> r;                /* отбросили эту цифру */
    } while (value != 0);                  /* do-while: у нуля одна цифра "0" */
    if (negative) {
        bit_subtract(pos, 1, &pos);
        buf[pos] = '-';
    }

    /* сдвигаем запись в начало буфера (вместе с '\0'): копируем по одному символу */
    unsigned long long from = pos;
    unsigned long long to = 0;
    while (buf[from] != '\0') {
        buf[to] = buf[from];
        bit_add(from, 1, &from);
        bit_add(to, 1, &to);
    }
    buf[to] = '\0';

    *result = buf;
    return OK;
}

/* ===================== ввод ===================== */

/*
 * Строка -> long long. Вся строка должна быть числом (можно со знаком), без пробелов и мусора.
 * Это ввод, а не перевод: здесь обычная арифметика разрешена.
 * strtoll не используем: о переполнении он сообщает через errno, а это глобальная переменная
 * (запрещена). Поэтому разбираем сами и переполнение проверяем по числу цифр и по границе.
 */
status_code parse_long_long(const char *str, long long *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    /* первый символ: цифра или знак, за знаком - цифра (strtoll пропустил бы пробелы - нам не надо) */
    size_t i = 0;
    if (str[0] == '-' || str[0] == '+') {
        i = 1;
    }
    if (str[i] < '0' || str[i] > '9') {
        return INVALID_NUMBER;
    }
    /* все остальные символы - цифры; считаем цифры без ведущих нулей, чтобы понять,
       не слишком ли длинное число */
    size_t significant = 0;
    for (size_t j = i; str[j] != '\0'; j++) {
        if (str[j] < '0' || str[j] > '9') {
            return INVALID_NUMBER;
        }
        if (significant > 0 || str[j] != '0') {
            significant++;
        }
    }
    /* в long long не больше 19 цифр */
    if (significant > 19) {
        return OVERFLOW_ERROR;
    }
    /* 19 цифр: число может быть больше 9223372036854775807. Собираем его сами в unsigned long long
       (19 цифр туда помещаются всегда) и сравниваем с границей */
    unsigned long long value = 0;
    for (size_t j = i; str[j] != '\0'; j++) {
        value = value * 10 + (unsigned long long)(str[j] - '0');
    }
    if (str[0] == '-') {
        /* модуль отрицательного числа может быть на 1 больше LLONG_MAX (это LLONG_MIN) */
        if (value > (unsigned long long)LLONG_MAX + 1) {
            return OVERFLOW_ERROR;
        }
        /* -value без переполнения: для value = 2^63 сначала вычитаем 1, потом еще 1 */
        *number = value == 0 ? 0 : -(long long)(value - 1) - 1;
    } else {
        if (value > (unsigned long long)LLONG_MAX) {
            return OVERFLOW_ERROR;
        }
        *number = (long long)value;
    }
    return OK;
}

/* ===================== демонстрация и main ===================== */

/* Напечатать строку таблицы: число и его запись во всех основаниях 2, 4, 8, 16, 32 */
status_code print_demo_row(const long long number) {
    printf("%lld\n", number);
    for (int r = 1; r <= 5; r++) {
        char *str = NULL;
        status_code sc = to_base_pow2(number, r, &str);
        if (sc != OK) {
            return sc;
        }
        printf("    base %2d (r = %d): %s\n", 1 << r, r, str);
        free(str);
    }
    return OK;
}

int main(int argc, char *argv[]) {
    /* без аргументов - демонстрация на нескольких характерных числах */
    if (argc == 1) {
        const long long demo[] = {0, 1, 5, 31, 32, 255, 1000, -1, -255, LLONG_MAX, LLONG_MIN};
        const size_t count = sizeof(demo) / sizeof(demo[0]);
        printf("Demonstration: a number in bases 2, 4, 8, 16, 32\n");
        for (size_t i = 0; i < count; i++) {
            status_code sc = print_demo_row(demo[i]);
            if (sc == ALLOC_ERROR) {
                fprintf(stderr, "Error: not enough memory\n");
                return 1;
            } else if (sc != OK) {
                /* r в демонстрации всегда 1..5, указатели не NULL - ошибка в самой программе */
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
        }
        printf("Run with arguments to convert your number: lab3_task1 <number> <r>, r = 1..5\n");
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "Usage: lab3_task1 <number> <r>   (r = 1..5, base = 2^r)\n");
        fprintf(stderr, "       lab3_task1                (demonstration)\n");
        return 1;
    }

    /* 1) число */
    long long number = 0;
    status_code sc = parse_long_long(argv[1], &number);
    if (sc == INVALID_NUMBER) {
        fprintf(stderr, "Error: '%s' is not a whole number\n", argv[1]);
        return 1;
    } else if (sc == OVERFLOW_ERROR) {
        fprintf(stderr, "Error: '%s' does not fit into long long (%lld .. %lld)\n", argv[1], LLONG_MIN, LLONG_MAX);
        return 1;
    } else if (sc != OK) {
        fprintf(stderr, "Error: internal error\n");   /* argv[1] не NULL - сюда не попадаем */
        return 1;
    }

    /* 2) r: одна цифра от 1 до 5 */
    if (argv[2][0] < '1' || argv[2][0] > '5' || argv[2][1] != '\0') {
        fprintf(stderr, "Error: r must be 1, 2, 3, 4 or 5, not '%s'\n", argv[2]);
        return 1;
    }
    int r = argv[2][0] - '0';

    /* 3) перевод и вывод */
    char *result = NULL;
    sc = to_base_pow2(number, r, &result);
    switch (sc) {
        case OK:
            printf("Result: %s\n", result);
            free(result);
            return 0;
        case ALLOC_ERROR:
            fprintf(stderr, "Error: not enough memory\n");
            return 1;
        default:
            /* r уже проверен, указатель не NULL - ошибка в самой программе */
            fprintf(stderr, "Error: internal error\n");
            return 1;
    }
}
