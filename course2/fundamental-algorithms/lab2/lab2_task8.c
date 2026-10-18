/*
 * Лабораторная 2, задание 8. Сумма длинных неотрицательных чисел в системе счисления [2..36].
 *
 * Запуск:  lab2_task8                              - демонстрация (набор готовых примеров)
 *          lab2_task8 <основание> <число> [число ...] - сумма своих чисел
 *          например: lab2_task8 16 FF 1 a  ->  Sum: 10A
 *
 * Числа могут быть сколь угодно длинными: в unsigned long long (максимум ~1.8 * 10^19)
 * они могут не поместиться. Поэтому числа хранятся строками, а складываются
 * "в столбик" - цифра за цифрой, как на бумаге (функция add_in_base).
 *
 * Главная функция задания - sum_in_base с переменным числом аргументов (...):
 *   sum_in_base(&rez, основание, количество, "число1", "число2", ...)
 * Она складывает числа по очереди с помощью add_in_base.
 *
 * Цифры: 0-9, потом буквы A-Z (A = 10, ..., Z = 35). На входе буквы можно писать
 * и большими, и маленькими; в ответе буквы большие. Ведущих нулей в ответе нет.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free */
#include <string.h>   /* strlen - в этом задании string.h не запрещен (запрет был только в задании 1) */
#include <stdarg.h>   /* va_list, va_start, va_arg, va_end - работа с переменным числом аргументов */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* в функцию передали NULL или отрицательное количество чисел */
    INVALID_BASE,     /* основание не от 2 до 36 */
    INVALID_NUMBER,   /* строка не число в этой системе: пустая, чужой символ, цифра >= основания */
    OVERFLOW_ERROR,   /* длина числа не помещается в size_t */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* ===================== цифры ===================== */

/* Символ -> значение цифры: '0'..'9' -> 0..9, 'A'..'Z' и 'a'..'z' -> 10..35.
   Подходит ли цифра к основанию, проверяет вызывающий (здесь основание неизвестно) */
status_code digit_value(const char c, int *znach) {
    if (znach == NULL) {
        return INVALID_ARGS;
    }
    /* Коды символов '0'..'9' идут подряд, 'A'..'Z' подряд и 'a'..'z' подряд.
       Поэтому значение = код символа минус код первого символа своей группы:
       '7' - '0' = 7,  'C' - 'A' + 10 = 12,  'c' - 'a' + 10 = 12 */
    if (c >= '0' && c <= '9') {
        *znach = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        *znach = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'z') {
        *znach = c - 'a' + 10;
    } else {
        /* пробел, знак, точка, русская буква - не цифра ни в какой системе */
        return INVALID_NUMBER;
    }
    return OK;
}

/* Значение цифры 0..35 -> символ: 0..9 -> '0'..'9', 10..35 -> 'A'..'Z' */
status_code digit_char(const int znach, char *c) {
    if (c == NULL || znach < 0 || znach > 35) {
        return INVALID_ARGS;
    }
    /* таблица "значение -> символ": cifry[12] = 'C' */
    const char cifry[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    *c = cifry[znach];
    return OK;
}

/* Проверка строки: это неотрицательное целое число в системе с основанием base?
   Непустая, каждый символ - цифра, и каждая цифра меньше основания
   (в двоичной системе есть только 0 и 1, цифра 2 - уже ошибка) */
status_code check_number(const char *str, const int base) {
    if (str == NULL) {
        return INVALID_ARGS;
    }
    if (base < 2 || base > 36) {
        return INVALID_BASE;
    }
    /* пустая строка "" - не число */
    if (str[0] == '\0') {
        return INVALID_NUMBER;
    }
    /* Знак не разрешаем: числа по условию неотрицательные, а "+" и "-" -
       не цифры, поэтому digit_value сам вернет для них INVALID_NUMBER */
    for (size_t i = 0; str[i] != '\0'; i++) {
        int znach = 0;
        status_code sc = digit_value(str[i], &znach);
        if (sc != OK) {
            return sc;
        }
        if (znach >= base) {
            return INVALID_NUMBER;
        }
    }
    return OK;
}

/* Убрать ведущие нули прямо в строке: "000120" -> "120", "0000" -> "0".
   Строка сдвигается влево внутри своего же блока памяти */
status_code strip_leading_zeros(char *str) {
    if (str == NULL) {
        return INVALID_ARGS;
    }
    /* start - индекс первой значащей цифры. Пропускаем нули, но последний символ
       оставляем всегда: иначе от "0000" ничего бы не осталось, а ответ - "0".
       str[start + 1] != '\0' означает "после этого символа есть еще символы" */
    size_t start = 0;
    while (str[start] == '0' && str[start + 1] != '\0') {
        start++;
    }
    /* если нулей впереди не было - сдвигать нечего */
    if (start == 0) {
        return OK;
    }
    /* Сдвигаем символы влево на start позиций, вместе с '\0' в конце.
       Копируем слева направо: место, куда пишем (i), всегда левее места,
       откуда читаем (i + start), поэтому ничего нужного не затрем */
    size_t i = 0;
    while (str[i + start] != '\0') {
        str[i] = str[i + start];
        i++;
    }
    str[i] = '\0';
    return OK;
}

/* ===================== сложение в столбик ===================== */

/* a + b в системе с основанием base. Результат - новая строка в динамической памяти
   (освобождает вызывающий), большими буквами, без ведущих нулей.
   Как на бумаге: складываем цифры с конца (с младших разрядов), плюс перенос
   из прошлого разряда; в разряд пишем остаток от деления на base,
   а целую часть от деления переносим в следующий разряд.
   Пример в десятичной: 958 + 67
       8 + 7 + 0 = 15 -> пишем 5, перенос 1
       5 + 6 + 1 = 12 -> пишем 2, перенос 1
       9 + 0 + 1 = 10 -> пишем 0, перенос 1
       0 + 0 + 1 = 1  -> пишем 1, перенос 0      ответ 1025 */
status_code add_in_base(const char *a, const char *b, const int base, char **rez) {
    if (a == NULL || b == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    /* обе строки должны быть числами в этой системе (заодно проверится и base) */
    status_code sc = check_number(a, base);
    if (sc != OK) {
        return sc;
    }
    sc = check_number(b, base);
    if (sc != OK) {
        return sc;
    }

    /* Сколько разрядов нужно ответу: разрядов у длинного числа + 1 на перенос.
       Сумма двух чисел длиной n и m имеет не больше max(n, m) + 1 цифр
       (99 + 99 = 198: из двух цифр - три). Лишний ноль впереди потом уберем */
    size_t dlina_a = strlen(a);
    size_t dlina_b = strlen(b);
    size_t dlina = (dlina_a > dlina_b) ? dlina_a : dlina_b;
    /* dlina + 1 разряд и еще + 1 байт на '\0': проверяем, что не "перескочим" SIZE_MAX */
    if (dlina > SIZE_MAX - 2) {
        return OVERFLOW_ERROR;
    }
    dlina = dlina + 1;

    char *novaya = (char *)malloc((dlina + 1) * sizeof(char));
    if (novaya == NULL) {
        return ALLOC_ERROR;
    }
    novaya[dlina] = '\0';

    /* k - номер разряда с конца: k = 0 - последняя цифра (единицы), k = 1 - предпоследняя...
       У числа a разряд k стоит в строке на месте dlina_a - 1 - k.
       Если у числа столько разрядов нет (короткое число), считаем цифру нулем -
       как будто дописали нули слева: 67 = 067 */
    int perenos = 0;
    for (size_t k = 0; k < dlina; k++) {
        int cifra_a = 0;
        int cifra_b = 0;
        if (k < dlina_a) {
            sc = digit_value(a[dlina_a - 1 - k], &cifra_a);
            if (sc != OK) {
                free(novaya);   /* выходим с ошибкой - память за собой освобождаем */
                return sc;
            }
        }
        if (k < dlina_b) {
            sc = digit_value(b[dlina_b - 1 - k], &cifra_b);
            if (sc != OK) {
                free(novaya);
                return sc;
            }
        }
        /* Сумма в разряде не больше (base-1) + (base-1) + 1 = 2*base - 1 <= 71 -
           в int помещается с запасом, переполнения тут не бывает */
        int summa = cifra_a + cifra_b + perenos;
        /* в разряд - остаток, в следующий разряд - перенос (0 или 1) */
        sc = digit_char(summa % base, &novaya[dlina - 1 - k]);
        if (sc != OK) {
            free(novaya);
            return sc;
        }
        perenos = summa / base;
    }
    /* После самого старшего разряда перенос всегда 0: под него мы и оставили
       лишний разряд. Если там вышел 0 ("01025") - это ведущий ноль, убираем.
       Ведущие нули могли прийти и из входа: "007" + "0003" = "0010" -> "10" */
    sc = strip_leading_zeros(novaya);
    if (sc != OK) {
        free(novaya);
        return sc;
    }

    *rez = novaya;
    return OK;
}

/* ===================== сумма с переменным числом аргументов ===================== */

/* Сумма kolvo чисел в системе с основанием base:
     sum_in_base(&rez, 10, 3, "123", "456", "789")  ->  rez = "1368"
   rez   - сюда кладем адрес строки-ответа (освобождает вызывающий);
   base  - основание 2..36;
   kolvo - сколько чисел передано после него;
   ...   - сами числа, строки (char *).
   rez стоит первым, а kolvo - последним перед ...: va_start отсчитывает
   список с последнего именованного параметра, и это должно быть kolvo.
   kolvo = 0 - сумма пустого набора, ответ "0" (как в математике). */
status_code sum_in_base(char **rez, const int base, const int kolvo, ...) {
    if (rez == NULL || kolvo < 0) {
        return INVALID_ARGS;
    }
    if (base < 2 || base > 36) {
        return INVALID_BASE;
    }

    /* Начинаем с суммы "0" и прибавляем числа по одному: summa = summa + chislo.
       summa всегда - строка в динамической памяти (ее выделяет add_in_base) */
    char *summa = (char *)malloc(2 * sizeof(char));
    if (summa == NULL) {
        return ALLOC_ERROR;
    }
    summa[0] = '0';
    summa[1] = '\0';

    /* va_list - "указатель" на следующий безымянный аргумент.
       va_start(args, kolvo) ставит его сразу после параметра kolvo */
    va_list args;
    va_start(args, kolvo);
    for (int i = 0; i < kolvo; i++) {
        /* va_arg достает следующий аргумент как значение указанного типа.
           Тип пишем char *, а не const char *: строковые литералы и argv имеют
           тип char *, а va_arg должен получить ровно тот тип, который передали.
           Тип в va_arg функция узнать не может - это обещание вызывающего:
           если передать не строку (например, число), будет неопределенное поведение */
        const char *chislo = va_arg(args, char *);
        if (chislo == NULL) {
            free(summa);
            va_end(args);   /* va_end обязателен на любом выходе после va_start */
            return INVALID_ARGS;
        }

        /* новая сумма = старая сумма + число; add_in_base проверит и само число */
        char *novaya = NULL;
        status_code sc = add_in_base(summa, chislo, base, &novaya);
        if (sc != OK) {
            free(summa);
            va_end(args);
            return sc;
        }
        /* старая строка-сумма больше не нужна - освобождаем, иначе утечка на каждом шаге */
        free(summa);
        summa = novaya;
    }
    va_end(args);

    *rez = summa;
    return OK;
}

/* ===================== ввод и вывод (только для main) ===================== */

/* Основание из командной строки: только цифры, значение от 2 до 36 */
status_code parse_base(const char *str, int *base) {
    if (str == NULL || base == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_BASE;
    }
    /* Собираем число слева направо (znach * 10 + цифра). Как только стало больше 36,
       дальше можно не читать - это уже ошибка; так и переполнения int не бывает */
    int znach = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_BASE;
        }
        znach = znach * 10 + (str[i] - '0');
        if (znach > 36) {
            return INVALID_BASE;
        }
    }
    if (znach < 2) {
        return INVALID_BASE;
    }
    *base = znach;
    return OK;
}

/* Одна строка демонстрации: что складывали (opisanie) и итог.
   sc и rez - то, что вернула sum_in_base; при ошибке вместо ответа - текст ошибки */
status_code print_demo(const char *opisanie, const status_code sc, const char *rez) {
    if (opisanie == NULL) {
        return INVALID_ARGS;
    }
    if (sc == OK) {
        if (rez == NULL) {
            return INVALID_ARGS;
        }
        printf("%s = %s\n", opisanie, rez);
    } else if (sc == INVALID_BASE) {
        printf("%s -> error: base must be from 2 to 36\n", opisanie);
    } else if (sc == INVALID_NUMBER) {
        printf("%s -> error: not a number in this base\n", opisanie);
    } else if (sc == ALLOC_ERROR) {
        printf("%s -> error: not enough memory\n", opisanie);
    } else {
        printf("%s -> error: internal error\n", opisanie);
    }
    return OK;
}

/* Демонстрация: набор готовых вызовов sum_in_base с разным количеством аргументов.
   Каждый пример: 1) вызвали sum_in_base; 2) напечатали итог; 3) освободили ответ.
   free(NULL) ничего не делает, поэтому после ошибки (rez остался NULL) free тоже безопасен */
status_code run_demo(void) {
    char *rez = NULL;
    status_code sc = OK;

    printf("Normal sums:\n");

    sc = sum_in_base(&rez, 10, 3, "123", "456", "789");
    print_demo("base 10: 123 + 456 + 789", sc, rez);
    free(rez);
    rez = NULL;   /* чтобы в следующем примере при ошибке не напечатать и не освободить старое */

    sc = sum_in_base(&rez, 2, 2, "1010", "1011");
    print_demo("base 2: 1010 + 1011", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 16, 2, "ff", "1");
    print_demo("base 16: ff + 1", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 16, 2, "FF", "ff");
    print_demo("base 16: FF + ff", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 36, 3, "zz", "1", "A");
    print_demo("base 36: zz + 1 + A", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 8, 1, "0777");
    print_demo("base 8: one number 0777", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 10, 0);
    print_demo("base 10: no numbers", sc, rez);
    free(rez);
    rez = NULL;

    printf("\nLeading zeros:\n");

    sc = sum_in_base(&rez, 10, 2, "007", "0003");
    print_demo("base 10: 007 + 0003", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 10, 3, "000", "0", "00");
    print_demo("base 10: 000 + 0 + 00", sc, rez);
    free(rez);
    rez = NULL;

    printf("\nNumbers bigger than unsigned long long:\n");

    /* 18446744073709551615 - это ULLONG_MAX, самое большое unsigned long long */
    sc = sum_in_base(&rez, 10, 2, "18446744073709551615", "1");
    print_demo("base 10: 18446744073709551615 + 1", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 10, 2, "99999999999999999999999999999999999999", "1");
    print_demo("base 10: 99999999999999999999999999999999999999 + 1", sc, rez);
    free(rez);
    rez = NULL;

    /* 64 единицы в двоичной = ULLONG_MAX; + 1 = 1 и 64 нуля */
    sc = sum_in_base(&rez, 2, 2,
                     "1111111111111111111111111111111111111111111111111111111111111111", "1");
    print_demo("base 2: (64 ones) + 1", sc, rez);
    free(rez);
    rez = NULL;

    printf("\nErrors:\n");

    sc = sum_in_base(&rez, 2, 2, "101", "102");
    print_demo("base 2: 101 + 102", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 10, 2, "12a", "5");
    print_demo("base 10: 12a + 5", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 10, 2, "-5", "5");
    print_demo("base 10: -5 + 5", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 10, 2, "", "5");
    print_demo("base 10: (empty) + 5", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 1, 2, "0", "0");
    print_demo("base 1: 0 + 0", sc, rez);
    free(rez);
    rez = NULL;

    sc = sum_in_base(&rez, 37, 2, "1", "1");
    print_demo("base 37: 1 + 1", sc, rez);
    free(rez);
    rez = NULL;

    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    /* Без аргументов - демонстрация на готовых примерах */
    if (argc == 1) {
        if (run_demo() != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        return 0;
    }

    /* С аргументами нужно минимум основание и одно число */
    if (argc < 3) {
        fprintf(stderr, "Usage: lab2_task8                        (demo)\n");
        fprintf(stderr, "       lab2_task8 <base> <number> [number ...]\n");
        return 1;
    }

    int base = 0;
    if (parse_base(argv[1], &base) != OK) {
        fprintf(stderr, "Error: base '%s' must be a number from 2 to 36\n", argv[1]);
        return 1;
    }

    /* Сумма чисел пользователя. Функцию с ... нельзя вызвать с количеством
       аргументов, которое известно только во время работы программы: список
       аргументов пишется в коде заранее. Поэтому:
         1) summa = sum_in_base(1 число: argv[2]) - первое число (заодно проверка
            и удаление ведущих нулей);
         2) дальше прибавляем по одному: summa = sum_in_base(2 числа: summa, argv[nomer]).
       nomer - индекс в argv числа, которое обрабатываем сейчас (для сообщения об ошибке) */
    int nomer = 2;
    char *summa = NULL;
    status_code sc = sum_in_base(&summa, base, 1, argv[nomer]);
    while (sc == OK && nomer + 1 < argc) {
        nomer++;
        char *novaya = NULL;
        sc = sum_in_base(&novaya, base, 2, summa, argv[nomer]);
        if (sc == OK) {
            /* старая сумма больше не нужна - освобождаем и берем новую */
            free(summa);
            summa = novaya;
        }
    }

    /* Ошибка - сообщение и выход. summa освобождаем в любом случае:
       это сумма, накопленная до ошибки (или NULL, если ошибка в первом числе -
       free(NULL) ничего не делает) */
    if (sc != OK) {
        free(summa);
        if (sc == INVALID_NUMBER) {
            fprintf(stderr, "Error: '%s' is not a number in base %d\n", argv[nomer], base);
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
        } else {
            /* INVALID_ARGS, INVALID_BASE, OVERFLOW_ERROR здесь быть не может:
               указатели правильные, основание уже проверено. Если сообщение
               появилось - ошибка в самой программе */
            fprintf(stderr, "Error: internal error\n");
        }
        return 1;
    }

    printf("Sum: %s\n", summa);
    free(summa);
    return 0;
}
