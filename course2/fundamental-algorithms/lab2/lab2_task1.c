/*
 * Лабораторная 2, задание 1. Действия над строками из командной строки.
 *
 * Запуск:  lab2_task1 <флаг> <строка>                      (флаги -l -r -u -n)
 *          lab2_task1 -c <строка> <seed> [строка ...]       (флаг -c)
 *
 *   -l  длина строки
 *   -r  строка задом наперёд
 *   -u  символы на нечётных позициях (1, 3, 5, ...) -> в верхний регистр
 *   -n  сначала все цифры, потом все буквы, потом всё остальное (каждая группа в исходном порядке)
 *   -c  склеить строки в случайном порядке; seed - число для srand
 *
 * По условию:
 *   - для каждого флага своя функция;
 *   - новые строки лежат в динамической памяти (malloc), main потом делает free;
 *   - string.h использовать нельзя: длину и копирование строк делаем сами, циклом.
 *
 * Все функции возвращают статус-код (status_code), а результат кладут по указателю.
 * Функции ничего не печатают: печатает только main (по условию надо разделять
 * обработку данных и вывод).
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free, srand, rand */
#include <ctype.h>    /* isdigit, isalpha, toupper - это не string.h, их использовать можно */
#include <limits.h>   /* UINT_MAX - самое большое unsigned int */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* в функцию передали NULL или плохое количество строк */
    INVALID_FLAG,     /* флаг не из списка или неправильно записан */
    INVALID_NUMBER,   /* seed - не число (буквы, минус, пустая строка) */
    OVERFLOW_ERROR,   /* число или длина не помещается в свой тип */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* ===================== вспомогательные функции ===================== */

/* Флаг: '-' или '/', потом одна буква из списка l, r, u, n, c. Букву кладем в flag.
   '/' разрешаем, как в лабе 1 (так принято в Windows: dir /s) */
status_code parse_flag(const char *arg, char *flag) {
    if (arg == NULL || flag == NULL) {
        return INVALID_ARGS;
    }
    /* Ровно 2 символа: первый '-' или '/', второй - буква.
       arg[1] == '\0' - строка из одного символа ("-"),
       arg[2] != '\0' - символов больше двух ("-ll").
       arg[2] читать безопасно: до него дойдем, только если arg[1] не конец строки
       (|| проверяет условия слева направо и останавливается на первом истинном) */
    if ((arg[0] != '-' && arg[0] != '/') || arg[1] == '\0' || arg[2] != '\0') {
        return INVALID_FLAG;
    }

    /* вторая буква должна быть одной из пяти */
    char f = arg[1];
    if (f != 'l' && f != 'r' && f != 'u' && f != 'n' && f != 'c') {
        return INVALID_FLAG;
    }

    *flag = f;
    return OK;
}

/* Строка -> unsigned int (это seed для -c). Только цифры: без знака, без пробелов.
   Минус запрещаем: unsigned int не бывает отрицательным */
status_code parse_uint(const char *str, unsigned int *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    /* пустая строка "" - не число */
    if (str[0] == '\0') {
        return INVALID_NUMBER;
    }

    /* Собираем число слева направо (схема Горнера): "123" -> 1 -> 12 -> 123,
       каждый раз value = value * 10 + цифра.
       Копим в локальной переменной, а в *number пишем только в самом конце,
       когда точно знаем, что ошибок нет */
    unsigned int value = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        /* любой символ кроме цифры - ошибка ("12a", "-5", "+5", "1.5", " 7") */
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_NUMBER;
        }
        /* значение цифры: коды символов '0'..'9' идут подряд, '7' - '0' = 7 */
        unsigned int digit = (unsigned int)(str[i] - '0');
        /* Проверка переполнения ДО умножения:
           value * 10 + digit > UINT_MAX  <=>  value > (UINT_MAX - digit) / 10.
           Если сначала умножить, то unsigned "перескочит" через ноль
           (4294967296 станет 0) и ошибку уже не заметить */
        if (value > (UINT_MAX - digit) / 10) {
            return OVERFLOW_ERROR;
        }
        value = value * 10 + digit;
    }

    *number = value;
    return OK;
}

/* Выделить память под строку из length символов + 1 байт на '\0' в конце.
   Отдельная функция, потому что это нужно в -r, -u, -n и -c */
status_code alloc_string(const size_t length, char **new_str) {
    if (new_str == NULL) {
        return INVALID_ARGS;
    }
    /* length + 1 при length == SIZE_MAX "перескочит" в 0, и malloc(0) выделил бы
       пустой блок. На практике строки такой длины не бывает, но проверка дешевая */
    if (length > SIZE_MAX - 1) {
        return OVERFLOW_ERROR;
    }
    /* sizeof(char) всегда 1, пишем его, чтобы было видно, что выделяем массив char */
    char *block = (char *)malloc((length + 1) * sizeof(char));
    if (block == NULL) {
        return ALLOC_ERROR;
    }
    *new_str = block;
    return OK;
}

/* ===================== функции для флагов ===================== */

/* -l: длина строки (сколько символов до '\0'). Замена strlen из string.h.
   size_t - беззнаковый тип для размеров и длин, его возвращает и sizeof */
status_code string_length(const char *str, size_t *length) {
    if (str == NULL || length == NULL) {
        return INVALID_ARGS;
    }
    /* идем по строке, пока не встретим '\0' - признак конца строки в C */
    size_t n = 0;
    while (str[n] != '\0') {
        n++;
    }
    *length = n;
    return OK;
}

/* -r: новая строка - str задом наперед. "abc" -> "cba".
   Результат в динамической памяти, освобождает его вызывающий (main) */
status_code string_reverse(const char *str, char **result) {
    if (str == NULL || result == NULL) {
        return INVALID_ARGS;
    }

    /* сначала узнаем длину, чтобы выделить ровно столько памяти, сколько нужно */
    size_t length = 0;
    status_code sc = string_length(str, &length);
    if (sc != OK) {
        return sc;
    }
    char *new_str = NULL;
    sc = alloc_string(length, &new_str);
    if (sc != OK) {
        return sc;
    }

    /* i-й символ новой строки = i-й символ с конца старой.
       Последний символ стоит на месте length - 1 (нумерация с нуля), поэтому
       str[length - 1 - i]. Пример "abc": new_str[0] = str[2] = 'c', new_str[2] = str[0] = 'a'.
       Для пустой строки (length = 0) цикл не выполнится ни разу, и length - 1
       (которое для size_t было бы огромным числом) не вычисляется */
    for (size_t i = 0; i < length; i++) {
        new_str[i] = str[length - 1 - i];
    }
    /* без '\0' в конце это не строка: printf("%s") читал бы дальше чужую память */
    new_str[length] = '\0';

    /* результат отдаем только при успехе - при ошибке *result не меняется */
    *result = new_str;
    return OK;
}

/* -u: копия строки, где символы на нечетных позициях (1, 3, 5, ...) - в верхнем регистре.
   Первый символ стоит на позиции 0 (это четная позиция), его не меняем.
   "abcdef" -> "aBcDeF" */
status_code string_upper_odd(const char *str, char **result) {
    if (str == NULL || result == NULL) {
        return INVALID_ARGS;
    }

    /* длина -> память под копию (как в string_reverse) */
    size_t length = 0;
    status_code sc = string_length(str, &length);
    if (sc != OK) {
        return sc;
    }
    char *new_str = NULL;
    sc = alloc_string(length, &new_str);
    if (sc != OK) {
        return sc;
    }

    for (size_t i = 0; i < length; i++) {
        if (i % 2 == 1) {
            /* нечетная позиция: toupper меняет 'a'..'z' на 'A'..'Z', остальное не трогает.
               (unsigned char) обязателен: char может быть знаковым, и байт русской буквы
               в UTF-8 тогда отрицательный, а toupper принимает только 0..255 или EOF.
               Отрицательное значение в toupper - неопределенное поведение */
            new_str[i] = (char)toupper((unsigned char)str[i]);
        } else {
            /* четная позиция: копируем как есть */
            new_str[i] = str[i];
        }
    }
    new_str[length] = '\0';

    *result = new_str;
    return OK;
}

/* -n: новая строка из тех же символов: сначала все цифры, потом все буквы,
   потом все остальные символы. Внутри каждой группы порядок как в исходной строке.
   "a1-b2" -> "12ab-".
   Делаем три прохода по строке: 1) берем только цифры, 2) только буквы, 3) остальное */
status_code string_regroup(const char *str, char **result) {
    if (str == NULL || result == NULL) {
        return INVALID_ARGS;
    }

    /* длина -> память: символов в новой строке столько же, сколько в старой */
    size_t length = 0;
    status_code sc = string_length(str, &length);
    if (sc != OK) {
        return sc;
    }
    char *new_str = NULL;
    sc = alloc_string(length, &new_str);
    if (sc != OK) {
        return sc;
    }

    /* pos - сколько символов уже записали в новую строку (и куда писать следующий).
       Общий для всех трех проходов: второй проход дописывает после первого */
    size_t pos = 0;

    /* 1 проход - цифры '0'..'9'.
       isdigit/isalpha тоже требуют (unsigned char) - причина как в string_upper_odd */
    for (size_t i = 0; i < length; i++) {
        if (isdigit((unsigned char)str[i])) {
            new_str[pos] = str[i];
            pos++;
        }
    }
    /* 2 проход - латинские буквы (в стандартной локали "C" isalpha - это только a..z, A..Z) */
    for (size_t i = 0; i < length; i++) {
        if (isalpha((unsigned char)str[i])) {
            new_str[pos] = str[i];
            pos++;
        }
    }
    /* 3 проход - все остальное: пробелы, знаки, байты русских букв.
       Условие - "не цифра и не буква", поэтому каждый символ попадет ровно в одну
       группу, и в сумме pos дойдет ровно до length - за границу массива не выйдем */
    for (size_t i = 0; i < length; i++) {
        if (!isdigit((unsigned char)str[i]) && !isalpha((unsigned char)str[i])) {
            new_str[pos] = str[i];
            pos++;
        }
    }
    new_str[pos] = '\0';

    *result = new_str;
    return OK;
}

/* -c: склеить count строк в одну в псевдослучайном порядке.
   strings - массив указателей на строки, seed - число для srand (по условию).
   const char *const *strings: функция не меняет ни сами строки, ни массив указателей.

   Про srand/rand: у rand есть скрытое внутреннее состояние внутри библиотеки (по сути
   глобальная переменная). Обычно мы этого избегаем, но здесь условие прямо требует
   "для засеивания генератора используйте функцию srand", поэтому используем ее.
   Одинаковый seed -> одинаковая последовательность rand() -> одинаковый порядок
   (на одной и той же системе; на Windows и Linux rand устроен по-разному) */
status_code string_concat_random(const char *const *strings, const int count,
                                 const unsigned int seed, char **result) {
    if (strings == NULL || result == NULL || count <= 0) {
        return INVALID_ARGS;
    }

    /* 1) Общая длина результата = сумма длин всех строк.
          Заодно проверяем, что ни один указатель не NULL */
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        if (strings[i] == NULL) {
            return INVALID_ARGS;
        }
        size_t length = 0;
        status_code sc = string_length(strings[i], &length);
        if (sc != OK) {
            return sc;
        }
        /* total + length > SIZE_MAX  <=>  total > SIZE_MAX - length (проверка до сложения) */
        if (total > SIZE_MAX - length) {
            return OVERFLOW_ERROR;
        }
        total += length;
    }

    /* 2) Порядок склейки. Сами строки не двигаем - перемешиваем копию массива
          указателей на них (переставить указатель быстрее, чем переписывать строку) */
    const char **order = (const char **)malloc((size_t)count * sizeof(const char *));
    if (order == NULL) {
        return ALLOC_ERROR;
    }
    for (int i = 0; i < count; i++) {
        order[i] = strings[i];
    }

    /* 3) Память под результат. Если не дали - освобождаем order, иначе утечка */
    char *new_str = NULL;
    status_code sc = alloc_string(total, &new_str);
    if (sc != OK) {
        free(order);
        return sc;
    }

    /* 4) Перемешивание Фишера-Йетса. Идем с конца массива: для места i выбираем
          случайное место j от 0 до i и меняем order[i] и order[j] местами.
          После шага место i больше не трогаем - там уже стоит "случайно выбранная" строка.
          rand() % (i + 1) - остаток от деления, всегда от 0 до i.
          Каждая из count! перестановок получается с одинаковой вероятностью
          (если не считать небольшого перекоса от % - для лабы это не важно) */
    srand(seed);
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        const char *tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    /* 5) Склейка: копируем символы строк одну за другой в новый блок.
          pos - куда писать следующий символ (замена strcat из string.h) */
    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        const char *s = order[i];
        for (size_t k = 0; s[k] != '\0'; k++) {
            new_str[pos] = s[k];
            pos++;
        }
    }
    new_str[pos] = '\0';

    /* массив указателей больше не нужен; сами строки не освобождаем - они чужие (argv) */
    free(order);
    *result = new_str;
    return OK;
}

/* ===================== main: разбор аргументов и вывод ===================== */

int main(int argc, char *argv[]) {
    /* Минимум 2 аргумента: флаг и строка (argv[0] - имя самой программы).
       Точное количество зависит от флага - проверим после разбора флага */
    if (argc < 3) {
        fprintf(stderr, "Usage: lab2_task1 <flag> <string>\n");
        fprintf(stderr, "       lab2_task1 -c <string> <seed> [string ...]\n");
        fprintf(stderr, "Flags: -l, -r, -u, -n, -c (or /l, /r, ...)\n");
        return 1;
    }

    /* флаг - всегда первый аргумент */
    char flag = '\0';
    if (parse_flag(argv[1], &flag) != OK) {
        fprintf(stderr, "Error: wrong flag '%s'\n", argv[1]);
        return 1;
    }

    /* Проверка количества аргументов для флага:
       -l -r -u -n - ровно флаг и одна строка (argc == 3);
       -c          - флаг, строка, seed и дальше сколько угодно строк (argc >= 4) */
    if (flag != 'c' && argc != 3) {
        fprintf(stderr, "Error: flag -%c needs exactly one string\n", flag);
        return 1;
    }
    if (flag == 'c' && argc < 4) {
        fprintf(stderr, "Error: flag -c needs a string and a seed\n");
        return 1;
    }

    /* Выполняем действие по флагу; каждый case в {} - чтобы объявлять в нем переменные.
       Все case устроены одинаково:
         1) завели переменную под ответ и передали ее адрес в функцию;
         2) проверили статус: если не OK - сообщение в stderr и выход с кодом 1;
         3) если OK - печатаем ответ и освобождаем строку, которую выделила функция.
       ALLOC_ERROR - реальная ошибка (кончилась память), про нее пишем отдельно.
       "internal error" - запасная ветка для статусов, которые здесь появиться не могут:
       INVALID_ARGS (передали NULL) и OVERFLOW_ERROR (строка длиной почти во всю память).
       Если пользователь его увидел - ошибка в самом коде программы */
    const char *str = argv[2];
    status_code sc = OK;
    switch (flag) {
        case 'l': {
            /* -l: длина строки. Память не выделяется, ошибок от пользователя нет.
               %zu - печать size_t */
            size_t length = 0;
            sc = string_length(str, &length);
            if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            printf("Length: %zu\n", length);
            break;
        }

        case 'r': {
            /* -r: строка задом наперед. Ошибок от пользователя нет,
               возможна только нехватка памяти */
            char *result = NULL;
            sc = string_reverse(str, &result);
            if (sc == ALLOC_ERROR) {
                fprintf(stderr, "Error: not enough memory\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            printf("Result: %s\n", result);
            free(result);   /* строку выделила функция через malloc - освобождаем здесь */
            break;
        }

        case 'u': {
            /* -u: нечетные позиции в верхний регистр. Ошибок от пользователя нет */
            char *result = NULL;
            sc = string_upper_odd(str, &result);
            if (sc == ALLOC_ERROR) {
                fprintf(stderr, "Error: not enough memory\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            printf("Result: %s\n", result);
            free(result);
            break;
        }

        case 'n': {
            /* -n: цифры, потом буквы, потом остальное. Ошибок от пользователя нет */
            char *result = NULL;
            sc = string_regroup(str, &result);
            if (sc == ALLOC_ERROR) {
                fprintf(stderr, "Error: not enough memory\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            printf("Result: %s\n", result);
            free(result);
            break;
        }

        case 'c': {
            /* -c: склейка в случайном порядке. Ошибки пользователя: seed не число
               (INVALID_NUMBER) или больше UINT_MAX (OVERFLOW_ERROR) */
            unsigned int seed = 0;
            sc = parse_uint(argv[3], &seed);
            if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: seed '%s' must be a whole number 0 or more\n", argv[3]);
                return 1;
            } else if (sc == OVERFLOW_ERROR) {
                fprintf(stderr, "Error: seed '%s' is too big (max %u)\n", argv[3], UINT_MAX);
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }

            /* Склеиваем 2-ю, 4-ю, 5-ю, ... строки командной строки:
               argv[2], затем argv[4], argv[5], ..., argv[argc - 1].
               argv[3] - это seed, его пропускаем.
               Сколько строк: одна (argv[2]) + все после seed (argc - 4 штук) */
            int count = 1 + (argc - 4);
            const char **strings = (const char **)malloc((size_t)count * sizeof(const char *));
            if (strings == NULL) {
                fprintf(stderr, "Error: not enough memory\n");
                return 1;
            }
            /* strings[0] = argv[2]; strings[1] = argv[4]; strings[2] = argv[5]; ...
               то есть argv[i] кладем на место i - 3 (для i от 4) */
            strings[0] = argv[2];
            for (int i = 4; i < argc; i++) {
                strings[i - 3] = argv[i];
            }

            char *result = NULL;
            sc = string_concat_random(strings, count, seed, &result);
            /* массив указателей больше не нужен при любом исходе - освобождаем сразу,
               до проверки статуса, чтобы не забыть его в ветках с ошибкой */
            free(strings);
            if (sc == ALLOC_ERROR) {
                fprintf(stderr, "Error: not enough memory\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            printf("Result: %s\n", result);
            free(result);
            break;
        }

        default:
            /* сюда не попадем: parse_flag уже проверил букву */
            fprintf(stderr, "Error: wrong flag '%s'\n", argv[1]);
            return 1;
    }

    return 0;
}
