#include <stdio.h>
#include <limits.h>

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,        /* в функцию передали NULL или плохой размер */
    INVALID_FLAG,        /* флаг не из списка */
    INVALID_NUMBER,      /* строка не число или число не подходит */
    OVERFLOW_ERROR,      /* не помещается в тип */
    NO_MULTIPLES,        /* для -h: кратных нет */
    VALUE_OUT_OF_RANGE,  /* для -e: x не от 1 до 10 */
    BUFFER_TOO_SMALL     /* массив для ответа слишком маленький */
} status_code;

/* Строка -> long long. Вся строка должна быть числом: знак + или - (можно без него), потом цифры.
   Разбираем сами, переполнение проверяем до умножения.
   long long, а не long: long на Windows 4 байта, на Linux 8 - результаты различались бы */
status_code parse_long_long(const char *str, long long *chislo) {
    if (str == NULL || chislo == NULL) {
        return INVALID_ARGS;
    }
    /* i - откуда начинаются цифры, otric - был ли минус (1 - да, 0 - нет).
       Если первый символ знак, запоминаем его и цифры читаем со второго символа */
    size_t i = 0;
    int otric = 0;
    if (str[0] == '-' || str[0] == '+') {
        otric = (str[0] == '-');
        i = 1;
    }
    /* пустая строка или один знак - не число */
    if (str[i] == '\0') {
        return INVALID_NUMBER;
    }
    /* сначала проверяем, что дальше только цифры (пробел, точка, буква - ошибка) */
    for (size_t j = i; str[j] != '\0'; j++) {
        if (str[j] < '0' || str[j] > '9') {
            return INVALID_NUMBER;
        }
    }

    /* Число собираем без знака (модуль), знак ставим в самом конце.
       predel - самый большой модуль, который еще влезет в long long:
         положительное: LLONG_MAX     = 9223372036854775807
         отрицательное: LLONG_MAX + 1 = 9223372036854775808 (это модуль LLONG_MIN)
       Второе в long long уже не влезает, поэтому модуль копим в unsigned long long */
    unsigned long long predel = otric ? (unsigned long long)LLONG_MAX + 1 : (unsigned long long)LLONG_MAX;
    unsigned long long modul = 0;
    /* читаем цифры слева направо: "123" -> 1 -> 12 -> 123 (каждый раз modul * 10 + цифра) */
    for (; str[i] != '\0'; i++) {
        /* символ цифры -> ее значение: '7' - '0' = 7, коды цифр в таблице идут подряд */
        unsigned long long cifra = (unsigned long long)(str[i] - '0');
        /* Проверяем ДО умножения: если сначала умножить, а число не влезет,
           значение "перескочит" через ноль и ошибку уже не заметить.
           modul * 10 + cifra > predel  <=>  modul > (predel - cifra) / 10 */
        if (modul > (predel - cifra) / 10) {
            return OVERFLOW_ERROR;
        }
        modul = modul * 10 + cifra;
    }

    /* Ставим знак. Три случая:
       1) минуса не было - модуль и есть ответ;
       2) минус и модуль ровно predel - это LLONG_MIN. Его нельзя получить как -(long long)modul:
          сначала modul переводится в long long, а 9223372036854775808 туда не помещается;
       3) обычное отрицательное число - переводим в long long (влезет) и меняем знак */
    if (!otric) {
        *chislo = (long long)modul;
    } else if (modul == predel) {
        *chislo = LLONG_MIN;   /* -(LLONG_MAX + 1) в long long не посчитать, берем готовую константу */
    } else {
        *chislo = -(long long)modul;
    }
    return OK;
}

/* Флаг: '-' или '/' и одна буква из списка. Букву кладем в flag */
status_code parse_flag(const char *arg, char *flag) {
    if (arg == NULL || flag == NULL) {
        return INVALID_ARGS;
    }
    /* ровно 2 символа: первый '-' или '/', второй - буква.
       arg[1] == '\0' - строка из одного символа ("-"),
       arg[2] != '\0' - символов больше двух ("-pp").
       arg[2] читать безопасно: до него дойдем, только если arg[1] не конец строки */
    if ((arg[0] != '-' && arg[0] != '/') || arg[1] == '\0' || arg[2] != '\0') {
        return INVALID_FLAG;
    }

    char f = arg[1];
    if (f != 'h' && f != 'p' && f != 's' && f != 'e' && f != 'a' && f != 'f') {
        return INVALID_FLAG;
    }

    *flag = f;
    return OK;
}

/* -h: натуральные числа от 1 до 100, кратные x.
   kratnye - массив для ответа размером max_kolvo, kolvo - сколько нашли */
status_code find_multiples(const long long x, long long *kratnye, const int max_kolvo, int *kolvo) {
    if (kratnye == NULL || kolvo == NULL || max_kolvo <= 0) {
        return INVALID_ARGS;
    }
    /* у нуля нет натуральных кратных, и шаг 0 дал бы вечный цикл */
    if (x == 0) {
        return INVALID_NUMBER;
    }
    /* проверяем ДО взятия модуля: -LLONG_MIN не помещается в long long */
    if (x > 100 || x < -100) {
        return NO_MULTIPLES;
    }

    /* кратные -13 те же, что у 13, поэтому берем модуль */
    long long shag = (x > 0) ? x : -x;
    *kolvo = 0;
    /* идем шагами: shag, 2*shag, 3*shag ... пока не больше 100 */
    for (long long i = shag; i <= 100; i += shag) {
        /* в массиве нет места под еще одно число */
        if (*kolvo >= max_kolvo) {
            return BUFFER_TOO_SMALL;
        }
        /* кладем число в первую свободную ячейку и сдвигаем счетчик.
           Скобки в (*kolvo)++ обязательны: без них ++ сдвинул бы сам указатель */
        kratnye[*kolvo] = i;
        (*kolvo)++;
    }
    return OK;
}

/* -p: rez = 1 - простое, 0 - составное, -1 - ни то ни другое (x <= 1) */
status_code check_prime(const long long x, int *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    /* 0, 1 и отрицательные - не простые и не составные */
    if (x <= 1) {
        *rez = -1;
        return OK;
    }
    /* 2 - единственное четное простое */
    if (x == 2) {
        *rez = 1;
        return OK;
    }
    if (x % 2 == 0) {
        *rez = 0;
        return OK;
    }

    /* Перебираем нечетные делители до корня из x.
       Почему хватит корня: если x = a * b и a > корня, то b < корня - его мы уже проверили.
       delitel <= x / delitel - то же, что delitel * delitel <= x, но без переполнения */
    for (long long delitel = 3; delitel <= x / delitel; delitel += 2) {
        if (x % delitel == 0) {
            *rez = 0;
            return OK;
        }
    }

    /* делителей не нашли - простое */
    *rez = 1;
    return OK;
}

/* -s: цифры |x| в 16-ричной системе, от старших к младшим.
   cifry - буфер размером razmer, dlina - сколько цифр записали. Минус выводит main */
status_code hex_digits(const long long x, char *cifry, const int razmer, int *dlina) {
    if (cifry == NULL || dlina == NULL || razmer <= 0) {
        return INVALID_ARGS;
    }

    /* hex_chars[ostatok] - символ цифры: hex_chars[10] = 'a', hex_chars[15] = 'f' */
    const char hex_chars[] = "0123456789abcdef";

    /* Модуль x. Для LLONG_MIN обычный -x переполнится, поэтому считаем в unsigned.
       0ULL - (unsigned long long)x: вычитание в unsigned идет "по кругу",
       и для отрицательного x получается ровно его модуль, даже для LLONG_MIN */
    unsigned long long val = (x < 0) ? 0ULL - (unsigned long long)x : (unsigned long long)x;

    /* у нуля одна цифра, цикл ниже его бы пропустил */
    if (val == 0) {
        cifry[0] = '0';
        *dlina = 1;
        return OK;
    }

    /* в одном байте ровно 2 hex-цифры: 8 байт -> максимум 16 цифр */
    char tmp[2 * sizeof(unsigned long long)];
    int cnt = 0;
    /* делим на 16: остаток - очередная цифра с конца.
       Например 255: 255 % 16 = 15 -> 'f', 255 / 16 = 15; 15 % 16 = 15 -> 'f', 15 / 16 = 0 - стоп */
    while (val > 0) {
        tmp[cnt] = hex_chars[val % 16];
        val /= 16;
        cnt++;
    }

    if (cnt > razmer) {
        return BUFFER_TOO_SMALL;
    }
    /* в tmp цифры задом наперед - разворачиваем: последняя из tmp идет первой в cifry */
    for (int i = 0; i < cnt; i++) {
        cifry[i] = tmp[cnt - 1 - i];
    }
    *dlina = cnt;
    return OK;
}

/* -e: tablica[osn-1][pok-1] = osn^pok, где osn = 1..10, pok = 1..x */
status_code power_table(const long long x, long long tablica[10][10]) {
    if (tablica == NULL) {
        return INVALID_ARGS;
    }
    if (x < 1 || x > 10) {
        return VALUE_OUT_OF_RANGE;
    }

    /* Внешний цикл - строка таблицы (основание), внутренний - столбец (показатель).
       Индексы массива с нуля, а числа с единицы, поэтому пишем в [osn - 1][pok - 1].
       Самое большое значение 10^10 - в long long помещается */
    for (int osn = 1; osn <= 10; osn++) {
        long long stepen = 1;
        for (int pok = 1; pok <= x; pok++) {
            stepen *= osn;  /* osn^pok = osn^(pok-1) * osn */
            tablica[osn - 1][pok - 1] = stepen;
        }
    }
    return OK;
}

/* -a: 1 + 2 + ... + x = x * (x + 1) / 2 */
status_code sum_naturals(const long long x, unsigned long long *summa) {
    if (summa == NULL) {
        return INVALID_ARGS;
    }
    if (x < 1) {
        return INVALID_NUMBER;
    }

    /* считаем x * (x + 1) / 2 как a * b, где a = x, b = x + 1 */
    unsigned long long a = (unsigned long long)x;
    unsigned long long b = a + 1;
    /* Из двух соседних чисел одно всегда четное - делим на 2 его.
       Если сначала перемножить, а потом делить, x * (x + 1) может не влезть,
       хотя сам ответ (в 2 раза меньше) влез бы */
    if (a % 2 == 0) {
        a /= 2;
    } else {
        b /= 2;
    }

    /* a * b > ULLONG_MAX  <=>  a > ULLONG_MAX / b */
    if (a > ULLONG_MAX / b) {
        return OVERFLOW_ERROR;
    }
    *summa = a * b;
    return OK;
}

/* -f: x! */
status_code calc_factorial(const long long x, unsigned long long *fact) {
    if (fact == NULL) {
        return INVALID_ARGS;
    }
    if (x < 0) {
        return INVALID_NUMBER;
    }

    /* 0! = 1! = 1 - цикл для них не выполнится */
    *fact = 1;
    for (long long i = 2; i <= x; i++) {
        /* fact * i > ULLONG_MAX  <=>  fact > ULLONG_MAX / i */
        if (*fact > ULLONG_MAX / (unsigned long long)i) {
            return OVERFLOW_ERROR;
        }
        *fact *= (unsigned long long)i;
    }
    return OK;
}

int main(int argc, char *argv[]) {
    /* нужно ровно 2 аргумента: флаг и число (argv[0] - имя самой программы) */
    if (argc != 3) {
        fprintf(stderr, "Usage: lab1_task1 <flag> <number>\n");
        fprintf(stderr, "Flags: -h, -p, -s, -e, -a, -f (or /h, /p, ...)\n");
        return 1;
    }

    /* Флаг может стоять и первым, и вторым: "-p 17" и "17 -p".
       Сначала считаем, что флаг - argv[1], а число - argv[2].
       Если argv[1] не флаг, пробуем наоборот: флаг argv[2], число argv[1] */
    char flag = '\0';
    const char *chislo_str = argv[2];
    if (parse_flag(argv[1], &flag) != OK) {
        if (parse_flag(argv[2], &flag) != OK) {
            fprintf(stderr, "Error: wrong flag\n");
            return 1;
        }
        chislo_str = argv[1];
    }

    /* строку с числом превращаем в long long */
    long long x = 0;
    status_code sc = parse_long_long(chislo_str, &x);
    switch (sc) {
        case OK:
            break;
        case INVALID_NUMBER:
            fprintf(stderr, "Error: '%s' is not a number\n", chislo_str);
            return 1;
        case OVERFLOW_ERROR:
            fprintf(stderr, "Error: number '%s' is too big\n", chislo_str);
            return 1;
        default:
            fprintf(stderr, "Error: can not read the number\n");
            return 1;
    }

    /* Выполняем действие по флагу; каждый case в {} - чтобы объявлять переменные.
       Все case устроены одинаково:
         1) завели переменную под ответ и передали ее адрес в функцию;
         2) проверили статус: если не OK - сообщение об ошибке в stderr и выход с кодом 1;
         3) если OK - печатаем ответ, который функция положила в переменную.
       "internal error" - запасная ветка для статусов, которые здесь появиться не могут:
       INVALID_ARGS (передали NULL) и BUFFER_TOO_SMALL (мал массив). main передает
       правильные адреса и массивы нужного размера, так что пользователь это сообщение
       не увидит. Если увидел - значит, ошибка в самом коде программы */
    switch (flag) {
        case 'h': {
            /* кратные x от 1 до 100. Ошибки пользователя: x = 0.
               NO_MULTIPLES - не ошибка: просто сообщаем, что кратных нет (|x| > 100) */
            long long kratnye[100];  /* максимум 100 кратных (при x = 1) */
            int kolvo = 0;
            sc = find_multiples(x, kratnye, 100, &kolvo);
            if (sc == OK) {
                printf("Multiples of %lld (1..100):\n", x);
                /* числа через пробел: пробел ставим перед каждым, кроме первого */
                for (int i = 0; i < kolvo; i++) {
                    if (i > 0) {
                        printf(" ");
                    }
                    printf("%lld", kratnye[i]);
                }
                printf("\n");
            } else if (sc == NO_MULTIPLES) {
                printf("No multiples of %lld in 1..100\n", x);
            } else if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: x can not be 0\n");
                return 1;
            } else {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            break;
        }

        case 'p': {
            /* простое ли x. Ошибок от пользователя тут не бывает: ответ есть для любого x */
            int rez = 0;
            sc = check_prime(x, &rez);
            if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            /* три ответа функции: 1 - простое, 0 - составное, -1 - ни то ни другое (0, 1, отрицательные) */
            if (rez == 1) {
                printf("%lld is prime\n", x);
            } else if (rez == 0) {
                printf("%lld is composite\n", x);
            } else {
                printf("%lld is not prime and not composite\n", x);
            }
            break;
        }

        case 's': {
            /* x в 16-ричной системе, цифры через пробел. Ошибок от пользователя тут не бывает */
            /* 16 символов - максимум hex-цифр у 8-байтного числа */
            char cifry[2 * sizeof(unsigned long long)];
            int dlina = 0;
            sc = hex_digits(x, cifry, (int)sizeof(cifry), &dlina);
            if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            /* минус печатаем сами, функция работает с модулем */
            if (x < 0) {
                printf("-");
            }
            /* cifry - не строка (в конце нет '\0'), поэтому печатаем по одному символу */
            for (int i = 0; i < dlina; i++) {
                if (i > 0) {
                    printf(" ");
                }
                printf("%c", cifry[i]);
            }
            printf("\n");
            break;
        }

        case 'e': {
            /* таблица степеней 1..10 в степенях 1..x. Ошибка пользователя: x не от 1 до 10 */
            long long tablica[10][10];
            sc = power_table(x, tablica);
            if (sc == VALUE_OUT_OF_RANGE) {
                fprintf(stderr, "Error: for -e x must be from 1 to 10\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }

            /* %6s и %12d - ширина столбца: число дополняется пробелами слева,
               чтобы столбцы стояли ровно. 12 - хватает на 10^10 */
            /* шапка: показатели степени */
            printf("%6s", "base");
            for (int pok = 1; pok <= x; pok++) {
                printf(" %12d", pok);
            }
            printf("\n");

            /* строки: основание, потом его степени */
            for (int osn = 0; osn < 10; osn++) {
                printf("%6d", osn + 1);
                for (int pok = 0; pok < x; pok++) {
                    printf(" %12lld", tablica[osn][pok]);
                }
                printf("\n");
            }
            break;
        }

        case 'a': {
            /* сумма 1 + 2 + ... + x. Ошибки пользователя: x < 1 (INVALID_NUMBER)
               и слишком большой x, сумма не влезает в unsigned long long (OVERFLOW_ERROR).
               %llu - печать unsigned long long, %lld - long long */
            unsigned long long summa = 0;
            sc = sum_naturals(x, &summa);
            if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: for -a x must be 1 or more\n");
                return 1;
            } else if (sc == OVERFLOW_ERROR) {
                fprintf(stderr, "Error: the sum is too big\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            printf("Sum 1..%lld = %llu\n", x, summa);
            break;
        }

        case 'f': {
            /* факториал x. Ошибки пользователя: x < 0 (INVALID_NUMBER)
               и x > 20 - уже 21! не влезает в unsigned long long (OVERFLOW_ERROR) */
            unsigned long long fact = 0;
            sc = calc_factorial(x, &fact);
            if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: for -f x must be 0 or more\n");
                return 1;
            } else if (sc == OVERFLOW_ERROR) {
                fprintf(stderr, "Error: %lld! is too big\n", x);
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: internal error\n");
                return 1;
            }
            printf("%lld! = %llu\n", x, fact);
            break;
        }

        default:
            /* сюда не попадем: parse_flag уже проверил букву */
            fprintf(stderr, "Error: wrong flag\n");
            return 1;
    }

    return 0;
}
