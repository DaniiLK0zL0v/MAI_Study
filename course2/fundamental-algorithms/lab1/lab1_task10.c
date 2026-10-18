#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL, плохое основание, маленький буфер */
    INVALID_NUMBER,    /* строка не число в данной системе */
    OVERFLOW_ERROR,    /* не помещается в long long */
    LINE_TOO_LONG,
    END_OF_INPUT       /* ввод закончился (Ctrl+Z / Ctrl+D) */
} status_code;

#define LINE_SIZE 128   /* максимум символов в строке ввода */

/* Прочитать одну строку из потока in (у нас это консоль, stdin) без '\n'.
   Если строка длиннее буфера - дочитываем ее до конца и сообщаем об ошибке */
status_code read_line(FILE *in, char *buf, const size_t razmer) {
    /* fgets принимает размер как int, поэтому больше INT_MAX нельзя */
    if (in == NULL || buf == NULL || razmer < 2 || razmer > INT_MAX) {
        return INVALID_ARGS;
    }
    /* fgets читает строку вместе с '\n', но не больше razmer - 1 символов,
       и ставит '\0' в конце. NULL - читать нечего (конец ввода) */
    if (fgets(buf, (int)razmer, in) == NULL) {
        return END_OF_INPUT;
    }
    size_t dlina = strlen(buf);
    /* '\n' в конце - строка влезла целиком; убираем его, заменяя на '\0' */
    if (dlina > 0 && buf[dlina - 1] == '\n') {
        buf[dlina - 1] = '\0';
        /* в Windows-файлах перед '\n' бывает '\r' */
        if (dlina > 1 && buf[dlina - 2] == '\r') {
            buf[dlina - 2] = '\0';
        }
        return OK;
    }
    if (feof(in)) {
        return OK;   /* последняя строка без '\n' */
    }
    /* '\n' не влез в буфер - строка слишком длинная, выбрасываем остаток */
    /* читаем и выбрасываем символы до конца строки, иначе остаток
       попал бы в следующий вызов как новая строка. Тело цикла пустое -
       вся работа в условии */
    int ch;
    while ((ch = fgetc(in)) != '\n' && ch != EOF) {
    }
    return LINE_TOO_LONG;
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
status_code parse_in_base(const char *str, const int base, long long *chislo) {
    if (str == NULL || chislo == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    /* i - откуда начинаются цифры, otric - был ли минус */
    size_t i = 0;
    int otric = 0;
    if (str[0] == '-') {
        otric = 1;
        i = 1;
    }
    if (str[i] == '\0') {
        return INVALID_NUMBER;
    }
    /* число собираем слева направо: modul = modul * base + цифра.
       Каждая цифра должна быть меньше основания: '8' в base 8 - ошибка */
    unsigned long long modul = 0;
    for (; str[i] != '\0'; i++) {
        int d = 0;
        if (digit_value(str[i], &d) != OK || d >= base) {
            return INVALID_NUMBER;
        }
        /* modul * base + d > LLONG_MAX ? */
        if (modul > ((unsigned long long)LLONG_MAX - (unsigned long long)d) / (unsigned long long)base) {
            return OVERFLOW_ERROR;
        }
        modul = modul * (unsigned long long)base + (unsigned long long)d;
    }
    /* modul <= LLONG_MAX, поэтому и (long long)modul, и минус от него помещаются */
    *chislo = otric ? -(long long)modul : (long long)modul;
    return OK;
}

/* Число -> строка в системе base (без ведущих нулей, со знаком) */
status_code to_base(const long long chislo, const int base, char *buf, const size_t razmer) {
    if (buf == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    const char cifry[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    /* модуль через unsigned: для LLONG_MIN обычный -x переполнился бы */
    unsigned long long val = (chislo < 0) ? 0ULL - (unsigned long long)chislo
                                          : (unsigned long long)chislo;
    /* Перевод: остаток от деления на base - очередная цифра с конца, cifry[ostatok] - ее символ.
       Пример 10 в base 2: 10 % 2 = 0, 5 % 2 = 1, 2 % 2 = 0, 1 % 2 = 1 -> в tmp "0101", наоборот "1010".
       do-while, а не while: у нуля тоже должна быть цифра "0" */
    char tmp[70];   /* 64 цифры в base 2 + запас */
    size_t cnt = 0;
    do {
        tmp[cnt] = cifry[val % (unsigned long long)base];
        val /= (unsigned long long)base;
        cnt++;
    } while (val > 0);

    size_t nuzhno = cnt + (chislo < 0 ? 1 : 0) + 1;   /* цифры + минус + '\0' */
    if (nuzhno > razmer) {
        return INVALID_ARGS;
    }
    /* buf[pos++] = ... - записать в позицию pos, потом увеличить pos.
       tmp[--cnt] - сначала уменьшить cnt, потом взять элемент: идем от последней цифры к первой */
    size_t pos = 0;
    if (chislo < 0) {
        buf[pos++] = '-';
    }
    while (cnt > 0) {
        buf[pos++] = tmp[--cnt];   /* цифры шли с конца - разворачиваем */
    }
    buf[pos] = '\0';
    return OK;
}

/* Модуль как unsigned (без переполнения на LLONG_MIN) */
status_code abs_ull(const long long x, unsigned long long *modul) {
    if (modul == NULL) {
        return INVALID_ARGS;
    }
    *modul = (x < 0) ? 0ULL - (unsigned long long)x : (unsigned long long)x;
    return OK;
}

/* Добавить x к сумме с проверкой переполнения */
status_code add_checked(long long *summa, const long long x) {
    if (summa == NULL) {
        return INVALID_ARGS;
    }
    /* Проверка ДО сложения, иначе переполнение уже случится:
       summa + x > LLONG_MAX  <=>  summa > LLONG_MAX - x  (для x > 0),
       summa + x < LLONG_MIN  <=>  summa < LLONG_MIN - x  (для x < 0).
       Сами LLONG_MAX - x и LLONG_MIN - x не переполняются при таком знаке x */
    if ((x > 0 && *summa > LLONG_MAX - x) || (x < 0 && *summa < LLONG_MIN - x)) {
        return OVERFLOW_ERROR;
    }
    *summa += x;
    return OK;
}

/* Напечатать число в системе base и в 9, 18, 27, 36 */
status_code print_in_bases(const char *name, const long long chislo, const int base) {
    if (name == NULL || base < 2 || base > 36) {
        return INVALID_ARGS;
    }
    /* bases[0] = 0 - заглушка: на первом шаге берем основание, введенное пользователем,
       дальше 9, 18, 27, 36 из массива */
    char buf[80];
    const int bases[5] = {0, 9, 18, 27, 36};
    printf("%s:\n", name);
    for (int i = 0; i < 5; i++) {
        int b = (i == 0) ? base : bases[i];
        status_code sc = to_base(chislo, b, buf, sizeof(buf));
        if (sc != OK) {
            return sc;
        }
        printf("  base %2d: %s\n", b, buf);
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
       не могут: INVALID_ARGS (main передает правильные адреса и буфер нужного размера).
       Пользователь это сообщение не увидит; если увидел - ошибка в самом коде программы */
    char line[LINE_SIZE];
    int base = 0;

    /* 1) основание: спрашиваем, пока не введут правильное */
    while (1) {
        printf("Enter base (2..36): ");
        status_code sc = read_line(stdin, line, sizeof(line));
        if (sc == END_OF_INPUT) {
            fprintf(stderr, "\nError: no input\n");
            return 1;
        }
        /* основание вводится в десятичной системе. Условия проверяются слева направо:
           строка прочиталась, это число, и оно от 2 до 36. Иначе спрашиваем снова */
        long long b = 0;
        if (sc == OK && parse_in_base(line, 10, &b) == OK && b >= 2 && b <= 36) {
            base = (int)b;
            break;
        }
        printf("Wrong base, try again\n");
    }

    /* 2) числа до строки "Stop" */
    printf("Enter numbers in base %d (letters A-Z, capital), \"Stop\" to finish:\n", base);
    /* summa - сумма всех чисел, max_chislo - число с самым большим модулем, kolvo - сколько ввели */
    long long summa = 0;
    long long max_chislo = 0;
    int kolvo = 0;

    while (1) {
        status_code sc = read_line(stdin, line, sizeof(line));
        if (sc == END_OF_INPUT) {
            break;   /* конец ввода без Stop - считаем тем, что есть */
        }
        if (sc == LINE_TOO_LONG) {
            printf("Line is too long, skipped\n");
            continue;
        }
        if (strcmp(line, "Stop") == 0) {
            break;
        }

        /* неправильная строка - не ошибка программы: сообщаем и ждем следующую */
        long long x = 0;
        sc = parse_in_base(line, base, &x);
        if (sc == INVALID_NUMBER) {
            printf("'%s' is not a number in base %d, skipped\n", line, base);
            continue;
        } else if (sc == OVERFLOW_ERROR) {
            printf("'%s' is too big, skipped\n", line);
            continue;
        }

        if (add_checked(&summa, x) != OK) {
            fprintf(stderr, "Error: the sum is too big\n");
            return 1;
        }
        /* первое число или модуль больше текущего максимума */
        /* модули сравниваем как unsigned: |LLONG_MIN| в long long не помещается */
        unsigned long long modul_x = 0, modul_max = 0;
        if (abs_ull(x, &modul_x) != OK || abs_ull(max_chislo, &modul_max) != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        if (kolvo == 0 || modul_x > modul_max) {
            max_chislo = x;
        }
        kolvo++;
    }

    if (kolvo == 0) {
        printf("No numbers were entered\n");
        return 0;
    }
    if (print_in_bases("Max by absolute value", max_chislo, base) != OK ||
        print_in_bases("Sum", summa, base) != OK) {
        fprintf(stderr, "Error: internal error\n");
        return 1;
    }
    return 0;
}
