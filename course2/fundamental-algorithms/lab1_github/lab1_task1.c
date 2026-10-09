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

    /* модуль копим в unsigned long long: модуль LLONG_MIN на 1 больше LLONG_MAX */
    unsigned long long predel = otric ? (unsigned long long)LLONG_MAX + 1 : (unsigned long long)LLONG_MAX;
    unsigned long long modul = 0;
    for (; str[i] != '\0'; i++) {
        unsigned long long cifra = (unsigned long long)(str[i] - '0');
        /* modul * 10 + cifra > predel  <=>  modul > (predel - cifra) / 10 */
        if (modul > (predel - cifra) / 10) {
            return OVERFLOW_ERROR;
        }
        modul = modul * 10 + cifra;
    }

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
    /* ровно 2 символа: первый '-' или '/', второй - буква */
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
        if (*kolvo >= max_kolvo) {
            return BUFFER_TOO_SMALL;
        }
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

    /* перебираем нечетные делители до корня из x.
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

    /* hex_chars[ostatok] - символ цифры */
    const char hex_chars[] = "0123456789abcdef";

    /* модуль x. Для LLONG_MIN обычный -x переполнится, поэтому считаем в unsigned */
    unsigned long long val = (x < 0) ? 0ULL - (unsigned long long)x : (unsigned long long)x;

    /* у нуля одна цифра, цикл ниже его бы пропустил */
    if (val == 0) {
        cifry[0] = '0';
        *dlina = 1;
        return OK;
    }

    /* в одном байте ровно 2 hex-цифры */
    char tmp[2 * sizeof(unsigned long long)];
    int cnt = 0;
    /* делим на 16: остаток - очередная цифра с конца */
    while (val > 0) {
        tmp[cnt] = hex_chars[val % 16];
        val /= 16;
        cnt++;
    }

    if (cnt > razmer) {
        return BUFFER_TOO_SMALL;
    }
    /* в tmp цифры задом наперед - разворачиваем */
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

    unsigned long long a = (unsigned long long)x;
    unsigned long long b = a + 1;
    /* одно из двух соседних чисел четное - делим на 2 его, чтобы не переполниться раньше времени */
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
    /* нужно ровно 2 аргумента: флаг и число */
    if (argc != 3) {
        fprintf(stderr, "Usage: lab1_task1 <flag> <number>\n");
        fprintf(stderr, "Flags: -h, -p, -s, -e, -a, -f (or /h, /p, ...)\n");
        return 1;
    }

    /* флаг может стоять и первым, и вторым: "-p 17" и "17 -p" */
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

    /* выполняем действие по флагу; каждый case в {} - чтобы объявлять переменные */
    switch (flag) {
        case 'h': {
            long long kratnye[100];  /* максимум 100 кратных (при x = 1) */
            int kolvo = 0;
            sc = find_multiples(x, kratnye, 100, &kolvo);
            if (sc == OK) {
                printf("Multiples of %lld (1..100):\n", x);
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
                fprintf(stderr, "Error: something went wrong\n");
                return 1;
            }
            break;
        }

        case 'p': {
            int rez = 0;
            sc = check_prime(x, &rez);
            if (sc != OK) {
                fprintf(stderr, "Error: something went wrong\n");
                return 1;
            }
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
            char cifry[2 * sizeof(unsigned long long)];
            int dlina = 0;
            sc = hex_digits(x, cifry, (int)sizeof(cifry), &dlina);
            if (sc != OK) {
                fprintf(stderr, "Error: something went wrong\n");
                return 1;
            }
            /* минус печатаем сами, функция работает с модулем */
            if (x < 0) {
                printf("-");
            }
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
            long long tablica[10][10];
            sc = power_table(x, tablica);
            if (sc == VALUE_OUT_OF_RANGE) {
                fprintf(stderr, "Error: for -e x must be from 1 to 10\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: something went wrong\n");
                return 1;
            }

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
            unsigned long long summa = 0;
            sc = sum_naturals(x, &summa);
            if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: for -a x must be 1 or more\n");
                return 1;
            } else if (sc == OVERFLOW_ERROR) {
                fprintf(stderr, "Error: the sum is too big\n");
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: something went wrong\n");
                return 1;
            }
            printf("Sum 1..%lld = %llu\n", x, summa);
            break;
        }

        case 'f': {
            unsigned long long fact = 0;
            sc = calc_factorial(x, &fact);
            if (sc == INVALID_NUMBER) {
                fprintf(stderr, "Error: for -f x must be 0 or more\n");
                return 1;
            } else if (sc == OVERFLOW_ERROR) {
                fprintf(stderr, "Error: %lld! is too big\n", x);
                return 1;
            } else if (sc != OK) {
                fprintf(stderr, "Error: something went wrong\n");
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
