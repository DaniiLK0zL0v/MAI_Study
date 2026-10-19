#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL, плохое основание */
    INVALID_NUMBER,    /* строка не число в данной системе */
    OVERFLOW_ERROR,    /* не помещается в long long */
    ALLOC_ERROR,       /* malloc / realloc не смог выделить память */
    END_OF_INPUT       /* ввод закончился (Ctrl+Z / Ctrl+D) */
} status_code;

#define START_SIZE 16   /* начальный размер буфера строки; если не хватит - увеличиваем вдвое */

/* Прочитать одну строку любой длины из потока in (у нас это консоль, stdin) без '\n'.
   Строка кладется в динамическую память: *line - адрес нового блока, освобождает вызывающий.
   Длину строки заранее не знаем, поэтому читаем по одному символу в буфер,
   а когда буфер заполнится - увеличиваем его вдвое через realloc */
status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t size = START_SIZE;   /* сколько байт выделено */
    size_t length = 0;             /* сколько символов уже прочитали */
    char *buf = (char *)malloc(size);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }

    /* fgetc возвращает int, а не char: кроме 256 значений байта ему нужно
       еще отдельное значение EOF (конец ввода), которое не совпадает ни с одним байтом */
    int ch = fgetc(in);
    /* ничего не прочитали - ввод закончился (Ctrl+D в Linux, Ctrl+Z в Windows) */
    if (ch == EOF) {
        free(buf);
        return END_OF_INPUT;
    }
    while (ch != EOF && ch != '\n') {
        /* Место нужно под этот символ и под '\0' в конце: если length + 1 == size,
           после записи для '\0' места не останется - увеличиваем заранее.
           Вдвое, а не на 1 байт: тогда realloc вызывается редко (16, 32, 64, ...),
           и чтение длинной строки остается быстрым */
        if (length + 1 >= size) {
            /* size * 2 не должен "перескочить" через SIZE_MAX */
            if (size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            /* Результат realloc кладем во временную переменную: если realloc вернет NULL,
               старый блок остается живым, и его надо освободить. Написали бы
               buf = realloc(buf, ...) - при ошибке потеряли бы адрес старого блока (утечка) */
            char *bigger = (char *)realloc(buf, size * 2);
            if (bigger == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = bigger;
            size *= 2;
        }
        buf[length] = (char)ch;
        length++;
        ch = fgetc(in);
    }
    buf[length] = '\0';

    /* в Windows-файлах строка кончается на "\r\n" - '\r' тоже убираем */
    if (length > 0 && buf[length - 1] == '\r') {
        buf[length - 1] = '\0';
    }
    *line = buf;
    return OK;
}

/* Цифра -> значение. По заданию цифры больше 9 - только ЗАГЛАВНЫЕ буквы.
   Другой символ - INVALID_NUMBER */
status_code digit_value(const char ch, int *val) {
    if (val == NULL) {
        return INVALID_ARGS;
    }
    if (ch >= '0' && ch <= '9') {
        *val = ch - '0';
        return OK;
    }
    if (ch >= 'A' && ch <= 'Z') {
        *val = ch - 'A' + 10;
        return OK;
    }
    return INVALID_NUMBER;
}

/* Строка в системе base -> long long (можно со знаком '-').
   Модуль копим в unsigned long long и ограничиваем LLONG_MAX - так проще проверять переполнение */
status_code parse_in_base(const char *str, const int base, long long *number) {
    if (str == NULL || number == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    /* i - откуда начинаются цифры, negative - был ли минус */
    size_t i = 0;
    int negative = 0;
    if (str[0] == '-') {
        negative = 1;
        i = 1;
    }
    if (str[i] == '\0') {
        return INVALID_NUMBER;
    }
    /* число собираем слева направо: magnitude = magnitude * base + цифра.
       Каждая цифра должна быть меньше основания: '8' в base 8 - ошибка */
    unsigned long long magnitude = 0;
    for (; str[i] != '\0'; i++) {
        int d = 0;
        if (digit_value(str[i], &d) != OK || d >= base) {
            return INVALID_NUMBER;
        }
        /* magnitude * base + d > LLONG_MAX ? */
        if (magnitude > ((unsigned long long)LLONG_MAX - (unsigned long long)d) / (unsigned long long)base) {
            return OVERFLOW_ERROR;
        }
        magnitude = magnitude * (unsigned long long)base + (unsigned long long)d;
    }
    /* magnitude <= LLONG_MAX, поэтому и (long long)magnitude, и минус от него помещаются */
    *number = negative ? -(long long)magnitude : (long long)magnitude;
    return OK;
}

/* Число -> строка в системе base (без ведущих нулей, со знаком).
   Строка создается в динамической памяти ровно нужного размера:
   *result - ее адрес, освобождает вызывающий */
status_code to_base(const long long number, const int base, char **result) {
    if (result == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    /* модуль через unsigned: для LLONG_MIN обычный -x переполнился бы */
    unsigned long long magnitude = (number < 0) ? 0ULL - (unsigned long long)number
                                            : (unsigned long long)number;

    /* 1) Сколько цифр у числа: делим копию модуля на base, пока не станет 0.
          do-while, а не while: у нуля тоже одна цифра "0" */
    size_t cnt = 0;
    unsigned long long val = magnitude;
    do {
        val /= (unsigned long long)base;
        cnt++;
    } while (val > 0);

    /* 2) Память: цифры + минус (если число отрицательное) + '\0' */
    size_t needed = cnt + (number < 0 ? 1 : 0) + 1;
    char *str = (char *)malloc(needed);
    if (str == NULL) {
        return ALLOC_ERROR;
    }

    /* 3) Цифры. Остаток от деления на base - очередная цифра с конца, digits[остаток] - ее символ.
          Поэтому пишем их сразу с конца строки, от места needed - 2 влево - разворачивать не нужно.
          Пример 10 в base 2: 10 % 2 = 0, 5 % 2 = 1, 2 % 2 = 0, 1 % 2 = 1 -> справа налево "1010" */
    str[needed - 1] = '\0';
    size_t pos = needed - 1;   /* место, левее которого пишем следующую цифру */
    val = magnitude;
    do {
        pos--;
        str[pos] = digits[val % (unsigned long long)base];
        val /= (unsigned long long)base;
    } while (val > 0);
    /* после цифр слева осталось ровно одно место - под минус (если он нужен) */
    if (number < 0) {
        str[0] = '-';
    }

    *result = str;
    return OK;
}

/* Модуль как unsigned (без переполнения на LLONG_MIN) */
status_code abs_ull(const long long x, unsigned long long *magnitude) {
    if (magnitude == NULL) {
        return INVALID_ARGS;
    }
    *magnitude = (x < 0) ? 0ULL - (unsigned long long)x : (unsigned long long)x;
    return OK;
}

/* Добавить x к сумме с проверкой переполнения */
status_code add_checked(long long *sum, const long long x) {
    if (sum == NULL) {
        return INVALID_ARGS;
    }
    /* Проверка ДО сложения, иначе переполнение уже случится:
       sum + x > LLONG_MAX  <=>  sum > LLONG_MAX - x  (для x > 0),
       sum + x < LLONG_MIN  <=>  sum < LLONG_MIN - x  (для x < 0).
       Сами LLONG_MAX - x и LLONG_MIN - x не переполняются при таком знаке x */
    if ((x > 0 && *sum > LLONG_MAX - x) || (x < 0 && *sum < LLONG_MIN - x)) {
        return OVERFLOW_ERROR;
    }
    *sum += x;
    return OK;
}

/* Напечатать число в системе base и в 9, 18, 27, 36 */
status_code print_in_bases(const char *name, const long long number, const int base) {
    if (name == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    /* bases[0] = 0 - заглушка: на первом шаге берем основание, введенное пользователем,
       дальше 9, 18, 27, 36 из массива */
    const int bases[5] = {0, 9, 18, 27, 36};
    printf("%s:\n", name);
    for (int i = 0; i < 5; i++) {
        int b = (i == 0) ? base : bases[i];
        /* to_base выделяет строку через malloc - напечатали и сразу освободили */
        char *str = NULL;
        status_code sc = to_base(number, b, &str);
        if (sc != OK) {
            return sc;
        }
        printf("  base %2d: %s\n", b, str);
        free(str);
    }
    return OK;
}

int main(int argc, char *argv[]) {
    (void)argv;
    /* все вводится с консоли, аргументов быть не должно */
    if (argc != 1) {
        fprintf(stderr, "Usage: lab1_task10 (no arguments, input from console)\n");
        return 1;
    }
    /* "internal error" ниже - запасная ветка для статусов, которые появиться
       не могут: INVALID_ARGS (main передает правильные адреса и основания).
       Пользователь это сообщение не увидит; если увидел - ошибка в самом коде программы.
       Каждую строку read_line выделяет через malloc, поэтому после разбора строки
       (и перед каждым выходом из программы) делаем free(line) */
    char *line = NULL;
    int base = 0;

    /* 1) основание: спрашиваем, пока не введут правильное */
    while (1) {
        printf("Enter base (2..36): ");
        status_code sc = read_line(stdin, &line);
        if (sc == END_OF_INPUT) {
            fprintf(stderr, "\nError: no input\n");
            return 1;
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "\nError: not enough memory\n");
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "\nError: internal error\n");
            return 1;
        }
        /* основание вводится в десятичной системе. Условия проверяются слева направо:
           это число, и оно от 2 до 36. Строка больше не нужна - освобождаем сразу,
           до проверки результата, чтобы не забыть ни в одной ветке */
        long long b = 0;
        int is_valid = (parse_in_base(line, 10, &b) == OK && b >= 2 && b <= 36);
        free(line);
        line = NULL;
        if (is_valid) {
            base = (int)b;
            break;
        }
        printf("Wrong base, try again\n");
    }

    /* 2) числа до строки "Stop" */
    printf("Enter numbers in base %d (letters A-Z, capital), \"Stop\" to finish:\n", base);
    /* sum - сумма всех чисел, max_number - число с самым большим модулем, count - сколько ввели */
    long long sum = 0;
    long long max_number = 0;
    int count = 0;

    while (1) {
        status_code sc = read_line(stdin, &line);
        if (sc == END_OF_INPUT) {
            break;   /* конец ввода без Stop - считаем тем, что есть */
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
            return 1;
        } else if (sc != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        if (strcmp(line, "Stop") == 0) {
            free(line);
            break;
        }

        /* неправильная строка - не ошибка программы: сообщаем и ждем следующую.
           Сообщение печатает line, поэтому освобождаем ее после printf */
        long long x = 0;
        sc = parse_in_base(line, base, &x);
        if (sc == INVALID_NUMBER) {
            printf("'%s' is not a number in base %d, skipped\n", line, base);
            free(line);
            continue;
        } else if (sc == OVERFLOW_ERROR) {
            printf("'%s' is too big, skipped\n", line);
            free(line);
            continue;
        }
        /* число разобрано, строка больше не нужна */
        free(line);
        line = NULL;

        if (add_checked(&sum, x) != OK) {
            fprintf(stderr, "Error: the sum is too big\n");
            return 1;
        }
        /* первое число или модуль больше текущего максимума */
        /* модули сравниваем как unsigned: |LLONG_MIN| в long long не помещается */
        unsigned long long magnitude_x = 0, magnitude_max = 0;
        if (abs_ull(x, &magnitude_x) != OK || abs_ull(max_number, &magnitude_max) != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        if (count == 0 || magnitude_x > magnitude_max) {
            max_number = x;
        }
        count++;
    }

    if (count == 0) {
        printf("No numbers were entered\n");
        return 0;
    }
    /* print_in_bases может вернуть ALLOC_ERROR (строку для перевода не выделили) */
    status_code sc = print_in_bases("Max by absolute value", max_number, base);
    if (sc == OK) {
        sc = print_in_bases("Sum", sum, base);
    }
    if (sc == ALLOC_ERROR) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    } else if (sc != OK) {
        fprintf(stderr, "Error: internal error\n");
        return 1;
    }
    return 0;
}
