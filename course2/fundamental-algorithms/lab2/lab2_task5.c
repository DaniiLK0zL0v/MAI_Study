/*
 * Лабораторная 2, задание 5. Функции overfprintf и oversprintf.
 *
 * Они работают как стандартные fprintf и sprintf (тот же прототип, те же флаги
 * %d, %5.2f, %-10s, %*d, %lld, %n, ...), но понимают еще 10 своих флагов:
 *   %Ro  - int римскими цифрами:                       42 -> XLII
 *   %Zr  - unsigned int в цекендорфовом представлении: 10 -> 010011
 *          (коэффициенты при числах Фибоначчи 1, 2, 3, 5, 8, ... от младшего к старшему,
 *          в конце лишняя 1 - признак конца записи)
 *   %Cv  - int в системе с основанием base (два аргумента: число, основание), буквы маленькие
 *   %CV  - то же, буквы большие:                       255, 16 -> ff / FF
 *   %to  - строка с числом в системе base (два аргумента: строка, основание) -> в десятичной,
 *          буквы в строке маленькие;  %TO - то же, буквы большие: "ff", 16 -> 255
 *   %mi, %mu, %md, %mf - дамп памяти int, unsigned int, double, float: байты в том порядке,
 *          как лежат в памяти, каждый - 8 двоичных цифр, через пробел.
 *   У %Cv, %CV, %to, %TO основание вне [2..36] заменяется на 10 (так в условии).
 *
 * Запуск:  lab2_task5                      - демонстрация (стандартные и новые флаги, ошибки)
 *          lab2_task5 <флаг> <значение> [основание]   - один новый флаг на своем значении
 *          например: lab2_task5 Ro 1994   ->  MCMXCIV;   lab2_task5 Cv -255 16  ->  -ff
 *
 * Устройство: у overfprintf и oversprintf одно общее ядро over_vprintf. Разница только
 * в том, куда пишутся символы: в файл (FILE *) или в строку (char *). Это описывает
 * структура vyvod. Стандартные флаги ядро разбирает само (флаги, ширина, точность,
 * модификатор длины), достает аргумент нужного типа и отдает один этот спецификатор
 * стандартной vsnprintf. Новые флаги ядро печатает само.
 *
 * Возвращаемое значение. Условие требует ТОТ ЖЕ прототип, что у fprintf и sprintf:
 *   int overfprintf(FILE *stream, const char *format, ...);
 *   int oversprintf(char *str, const char *format, ...);
 * Поэтому эти две функции (как и main) возвращают int: число выведенных символов или -1
 * при ошибке - как fprintf. Все остальные функции возвращают status_code.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free, strtoll, strtod */
#include <stdarg.h>   /* va_list, va_start, va_arg, va_copy, va_end */
#include <string.h>   /* memcpy, strlen, strcmp */
#include <stdint.h>   /* intmax_t, uintmax_t, SIZE_MAX */
#include <stddef.h>   /* size_t, ptrdiff_t */
#include <limits.h>   /* INT_MAX, INT_MIN, UINT_MAX */
#include <ctype.h>    /* isspace */
#include <wchar.h>    /* wint_t, wchar_t - для стандартных %lc и %ls */
#include <float.h>    /* FLT_MAX - самое большое float */
#include <math.h>     /* isfinite, fabs */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* передали NULL (поток, строку, формат, строку для %s / %to, указатель для %n) */
    INVALID_FORMAT,   /* непонятный спецификатор: "%y", "%" в конце строки, "%Lc", "%5n" */
    INVALID_NUMBER,   /* строка для %to/%TO - не число в этой системе (не та буква, цифра >= base) */
    OUT_OF_RANGE,     /* число для %Ro больше 3999 по модулю - римскими цифрами не записать */
    OVERFLOW_ERROR,   /* число для %to не помещается в int; выведено больше INT_MAX символов */
    WRITE_ERROR,      /* запись в файл не удалась */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* ===================== куда пишем: файл или строка ===================== */

/* "Вывод": куда ядро пишет символы. Ровно одно из двух не NULL:
   stream - пишем в файл (overfprintf), buf - в строку (oversprintf).
   count - сколько символов уже записано (это вернет функция, и это пишет %n) */
typedef struct {
    FILE *stream;
    char *buf;
    size_t count;
} vyvod;

/* Записать len символов из s. Длина передается отдельно (не strlen): стандартный %c
   может вывести символ с кодом 0, а strlen на нем остановилась бы */
status_code put_chars(vyvod *w, const char *s, const size_t len) {
    if (w == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    if (w->stream != NULL) {
        /* fwrite возвращает, сколько записал; меньше len - ошибка записи (диск полон, ...) */
        if (fwrite(s, 1, len, w->stream) != len) {
            return WRITE_ERROR;
        }
    } else if (w->buf != NULL) {
        /* в строку: дописываем после уже записанного. Хватит ли места - отвечает
           вызывающий, как у sprintf (размер буфера в прототипе не передается) */
        memcpy(w->buf + w->count, s, len);
    } else {
        return INVALID_ARGS;
    }
    w->count += len;
    return OK;
}

/* Записать один символ */
status_code put_char(vyvod *w, const char c) {
    return put_chars(w, &c, 1);
}

/* ===================== новые флаги ===================== */

/* %Ro: число римскими цифрами. Римскими цифрами пишут числа от 1 до 3999
   (4000 уже нельзя: нет цифры больше M, а MMMM не пишут). Отрицательное - со знаком
   минус: -14 -> -XIV. Ноль римляне не писали; в средние века его обозначали N
   (nulla - "ничего"), так и печатаем. |x| > 3999 -> OUT_OF_RANGE.
   Как: идем по таблице от больших значений к меньшим и, пока число не меньше
   значения, пишем его символы и вычитаем. В таблице есть и "вычитательные" пары
   (900 = CM, 4 = IV), поэтому 1994 = 1000 + 900 + 90 + 4 = M CM XC IV.
   Таблицы - готовые константы (как таблица умножения), а не буферы */
status_code print_roman(vyvod *w, const int x) {
    if (w == NULL) {
        return INVALID_ARGS;
    }
    if (x == 0) {
        return put_char(w, 'N');
    }
    if (x < -3999 || x > 3999) {
        return OUT_OF_RANGE;
    }
    static const int znach[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char *simv[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX",
                                 "V", "IV", "I"};
    status_code sc = OK;
    int ostatok = x;
    if (x < 0) {
        sc = put_char(w, '-');
        ostatok = -x;   /* -x не переполняется: |x| <= 3999 */
    }
    for (int i = 0; i < 13 && sc == OK; i++) {
        while (ostatok >= znach[i] && sc == OK) {
            sc = put_chars(w, simv[i], strlen(simv[i]));
            ostatok -= znach[i];
        }
    }
    return sc;
}

/* %Zr: цекендорфово представление. Теорема Цекендорфа: любое натуральное число - сумма
   разных чисел Фибоначчи 1, 2, 3, 5, 8, 13, ..., среди которых нет двух соседних.
   Найти ее можно жадно: берем самое большое число Фибоначчи, не большее x, вычитаем,
   повторяем. Пример: 10 = 8 + 2. Коэффициенты при 1, 2, 3, 5, 8: 0 1 0 0 1.
   Печатаем от младшего к старшему и добавляем 1 в конце: "01001" + "1" = "010011".
   (Две единицы подряд в конце - признак конца: в самой записи соседних единиц не бывает.)
   0 - сумма без слагаемых; печатаем один коэффициент 0 и признак конца: "01".

   Без массивов. Числа Фибоначчи идут парами (a, b), следующее = a + b, а предыдущее
   можно получить вычитанием: b - a. Поэтому:
   1) идем вверх, пока следующее число не больше x: a - самое большое число <= x,
      k - его номер (1 -> 0, 2 -> 1, 3 -> 2, 5 -> 3, 8 -> 4, ...);
   2) идем вниз от номера k до 0, коэффициенты - биты переменной maska
      (бит i = 1 - число Фибоначчи номер i входит в сумму).
   Чисел Фибоначчи до UINT_MAX (~4.3e9) меньше 50, unsigned long long хватает на 64 бита.
   Считаем в unsigned long long, чтобы сумма a + b не переполнилась */
status_code print_zeckendorf(vyvod *w, const unsigned int x) {
    if (w == NULL) {
        return INVALID_ARGS;
    }
    if (x == 0) {
        return put_chars(w, "01", 2);
    }
    /* 1) вверх: a = 1, b = 2 (первые два числа) */
    unsigned long long a = 1;
    unsigned long long b = 2;
    int k = 0;
    while (b <= x) {
        unsigned long long sled = a + b;
        a = b;
        b = sled;
        k++;
    }
    /* 2) вниз: tek - число Fibonacci номер i, pred - номер i - 1 */
    unsigned long long tek = a;
    unsigned long long pred = b - a;
    unsigned long long ostatok = x;
    unsigned long long maska = 0;
    for (int i = k; i >= 0; i--) {
        if (tek <= ostatok) {
            maska |= 1ULL << i;
            ostatok -= tek;
        }
        /* шаг вниз: (tek, pred) -> (pred, tek - pred) */
        unsigned long long novoe_pred = tek - pred;
        tek = pred;
        pred = novoe_pred;
    }
    /* печать от младшего (i = 0) к старшему (i = k), потом признак конца */
    status_code sc = OK;
    for (int i = 0; i <= k && sc == OK; i++) {
        sc = put_char(w, ((maska >> i) & 1ULL) ? '1' : '0');
    }
    if (sc == OK) {
        sc = put_char(w, '1');
    }
    return sc;
}

/* Основание для %Cv, %CV, %to, %TO: вне [2..36] - 10 (так в условии) */
status_code fix_base(const int base, int *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    *rez = (base < 2 || base > 36) ? 10 : base;
    return OK;
}

/* %Cv / %CV: x в системе base. bolshie = 1 - буквы A..Z, 0 - a..z.
   Модуль числа берем в unsigned long long: -INT_MIN в int не помещается, а в
   long long - да. Цифры печатаем сразу от старшей, без буфера: находим stepen -
   самую большую степень base, не большую модуля, потом цифра = модуль / stepen,
   модуль = модуль % stepen, stepen = stepen / base.
   Пример 255, 16: stepen = 16;  255 / 16 = 15 -> 'f', 255 % 16 = 15;  15 / 1 = 15 -> 'f' */
status_code print_in_base(vyvod *w, const int x, const int base, const int bolshie) {
    if (w == NULL) {
        return INVALID_ARGS;
    }
    int osn = 10;
    fix_base(base, &osn);
    status_code sc = OK;
    unsigned long long modul = 0;
    if (x < 0) {
        sc = put_char(w, '-');
        modul = (unsigned long long)(-(long long)x);
    } else {
        modul = (unsigned long long)x;
    }
    /* stepen <= modul / osn  - то же, что stepen * osn <= modul, но без умножения */
    unsigned long long stepen = 1;
    while (stepen <= modul / (unsigned long long)osn) {
        stepen *= (unsigned long long)osn;
    }
    /* для x = 0: stepen = 1, одна цифра 0 */
    while (stepen > 0 && sc == OK) {
        int cifra = (int)(modul / stepen);
        modul = modul % stepen;
        stepen = stepen / (unsigned long long)osn;
        char c = 0;
        if (cifra < 10) {
            c = (char)('0' + cifra);
        } else {
            c = (char)((bolshie ? 'A' : 'a') + cifra - 10);
        }
        sc = put_char(w, c);
    }
    return sc;
}

/* Строка -> int для %to / %TO. Строка: необязательный '-', потом цифры системы base.
   bolshie = 1 - буквы-цифры только A..Z (%TO), 0 - только a..z (%to).
   Другой регистр, цифра >= base, пустая строка - INVALID_NUMBER.
   Не помещается в int - OVERFLOW_ERROR. Проверка до умножения:
     acc * osn + cifra > predel  <=>  acc > (predel - cifra) / osn,
   predel = INT_MAX для положительных и INT_MAX + 1 (модуль INT_MIN) для отрицательных */
status_code parse_in_base(const char *str, const int base, const int bolshie, int *rez) {
    if (str == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    int osn = 10;
    fix_base(base, &osn);
    size_t i = 0;
    int minus = 0;
    if (str[0] == '-') {
        minus = 1;
        i = 1;
    }
    if (str[i] == '\0') {
        return INVALID_NUMBER;
    }
    const long long predel = minus ? (long long)INT_MAX + 1 : (long long)INT_MAX;
    long long acc = 0;
    for (; str[i] != '\0'; i++) {
        char c = str[i];
        int cifra = 0;
        if (c >= '0' && c <= '9') {
            cifra = c - '0';
        } else if (bolshie && c >= 'A' && c <= 'Z') {
            cifra = c - 'A' + 10;
        } else if (!bolshie && c >= 'a' && c <= 'z') {
            cifra = c - 'a' + 10;
        } else {
            return INVALID_NUMBER;
        }
        if (cifra >= osn) {
            return INVALID_NUMBER;
        }
        if (acc > (predel - cifra) / osn) {
            return OVERFLOW_ERROR;
        }
        acc = acc * osn + cifra;
    }
    *rez = (int)(minus ? -acc : acc);
    return OK;
}

/* %mi, %mu, %md, %mf: дамп памяти. adres - начало значения, razmer - сколько байт.
   Байты идем по порядку адресов ("слева направо" в памяти); каждый байт - 8 двоичных
   цифр от старшего бита к младшему, байты через пробел.
   На x86 и ARM порядок байт little-endian: младший байт лежит первым, поэтому
   int 1 -> 00000001 00000000 00000000 00000000.
   unsigned char - потому что байт смотрим как число 0..255 (char может быть со знаком) */
status_code print_dump(vyvod *w, const void *adres, const size_t razmer) {
    if (w == NULL || adres == NULL) {
        return INVALID_ARGS;
    }
    const unsigned char *bayty = (const unsigned char *)adres;
    status_code sc = OK;
    for (size_t i = 0; i < razmer && sc == OK; i++) {
        if (i > 0) {
            sc = put_char(w, ' ');
        }
        for (int bit = 7; bit >= 0 && sc == OK; bit--) {
            sc = put_char(w, ((bayty[i] >> bit) & 1) ? '1' : '0');
        }
    }
    return sc;
}

/* Новый флаг? s указывает на символ сразу после '%'. Новые флаги - ровно две буквы
   из списка; *kakoy - сами эти две буквы (строка-константа), NULL - не новый флаг.
   Новые флаги проверяются раньше стандартных: например "%to" в C99 - это 'o' с
   модификатором t (ptrdiff_t), но по условию задания это наш флаг */
status_code custom_flag(const char *s, const char **kakoy) {
    if (s == NULL || kakoy == NULL) {
        return INVALID_ARGS;
    }
    static const char *spisok[] = {"Ro", "Zr", "Cv", "CV", "to", "TO", "mi", "mu", "md", "mf"};
    *kakoy = NULL;
    /* s[0] != '\0' проверяем первым: тогда s[1] читать можно (строка не кончилась) */
    if (s[0] == '\0') {
        return OK;
    }
    for (int i = 0; i < 10; i++) {
        if (s[0] == spisok[i][0] && s[1] == spisok[i][1]) {
            *kakoy = spisok[i];
        }
    }
    return OK;
}

/* Напечатать новый флаг kakoy, взяв его аргументы из списка args.
   args - указатель на va_list: va_arg(*args, ...) сдвигает ОБЩИЙ список, и ядро
   после этой функции продолжает с правильного места. Если бы передали va_list
   по значению, после va_arg внутри функции список у вызывающего стал бы
   неопределенным (так сказано в стандарте) */
status_code print_custom(vyvod *w, const char *kakoy, va_list *args) {
    if (w == NULL || kakoy == NULL || args == NULL) {
        return INVALID_ARGS;
    }
    if (strcmp(kakoy, "Ro") == 0) {
        int x = va_arg(*args, int);
        return print_roman(w, x);
    }
    if (strcmp(kakoy, "Zr") == 0) {
        unsigned int x = va_arg(*args, unsigned int);
        return print_zeckendorf(w, x);
    }
    if (strcmp(kakoy, "Cv") == 0 || strcmp(kakoy, "CV") == 0) {
        /* порядок как в условии: сначала число, потом основание */
        int x = va_arg(*args, int);
        int base = va_arg(*args, int);
        return print_in_base(w, x, base, kakoy[1] == 'V');
    }
    if (strcmp(kakoy, "to") == 0 || strcmp(kakoy, "TO") == 0) {
        const char *str = va_arg(*args, const char *);
        int base = va_arg(*args, int);
        int x = 0;
        status_code sc = parse_in_base(str, base, kakoy[0] == 'T', &x);
        if (sc != OK) {
            return sc;
        }
        /* обратно в десятичную - уже как обычное int: print_in_base с основанием 10 */
        return print_in_base(w, x, 10, 0);
    }
    if (strcmp(kakoy, "mi") == 0) {
        int x = va_arg(*args, int);
        return print_dump(w, &x, sizeof(x));
    }
    if (strcmp(kakoy, "mu") == 0) {
        unsigned int x = va_arg(*args, unsigned int);
        return print_dump(w, &x, sizeof(x));
    }
    if (strcmp(kakoy, "md") == 0) {
        double x = va_arg(*args, double);
        return print_dump(w, &x, sizeof(x));
    }
    if (strcmp(kakoy, "mf") == 0) {
        /* float в ... всегда превращается в double, поэтому va_arg(*args, float) нельзя.
           Достаем double и превращаем обратно во float - значение то же самое */
        float x = (float)va_arg(*args, double);
        return print_dump(w, &x, sizeof(x));
    }
    return INVALID_FORMAT;
}

/* ===================== стандартные флаги ===================== */

/* Модификатор длины: какого размера аргумент (%hhd - char, %ld - long, %Lf - long double) */
typedef enum {
    LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIG_L
} dlina_kind;

/* Разобранный стандартный спецификатор: %[флаги][ширина][.точность][длина]тип */
typedef struct {
    int minus, plus, probel, reshetka, nol;  /* флаги '-', '+', ' ', '#', '0' (1 - есть) */
    int shirina;          /* ширина; -1 - не задана */
    int shirina_zv;       /* 1 - ширина задана '*' (берется из аргументов) */
    int tochnost;         /* точность; -1 - не задана */
    int tochnost_zv;      /* 1 - точность задана ".*" */
    dlina_kind dlina;
    char tip;             /* d i o u x X f F e E g G a A c s p n % */
} spec_info;

/* Целое из цифр формата (ширина, точность): s - первая цифра; *prochitano - сколько
   символов заняло. Больше INT_MAX - INVALID_FORMAT (fprintf такое тоже не умеет) */
status_code parse_format_int(const char *s, int *rez, size_t *prochitano) {
    if (s == NULL || rez == NULL || prochitano == NULL) {
        return INVALID_ARGS;
    }
    int znach = 0;
    size_t i = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        int cifra = s[i] - '0';
        if (znach > (INT_MAX - cifra) / 10) {
            return INVALID_FORMAT;
        }
        znach = znach * 10 + cifra;
        i++;
    }
    *rez = znach;
    *prochitano = i;
    return OK;
}

/* Подходит ли модификатор длины к типу (по стандарту C99):
   целые d i o u x X и n - любые модификаторы, кроме L;
   вещественные f F e E g G a A - без модификатора, l (ничего не меняет) или L;
   c и s - без модификатора или l (широкие символы); p и % - без модификатора */
status_code check_dlina(const char tip, const dlina_kind dlina, int *podhodit) {
    if (podhodit == NULL) {
        return INVALID_ARGS;
    }
    const char *celye = "diouxXn";
    const char *veshch = "fFeEgGaA";
    if (strchr(celye, tip) != NULL) {
        *podhodit = (dlina != LEN_BIG_L);
    } else if (strchr(veshch, tip) != NULL) {
        *podhodit = (dlina == LEN_NONE || dlina == LEN_L || dlina == LEN_BIG_L);
    } else if (tip == 'c' || tip == 's') {
        *podhodit = (dlina == LEN_NONE || dlina == LEN_L);
    } else {
        *podhodit = (dlina == LEN_NONE);
    }
    return OK;
}

/* Разобрать стандартный спецификатор. s - символ сразу после '%'.
   *dlina_spec - сколько символов формата он занял (без '%').
   Пример: "-08.3lld" -> minus, nol, shirina 8, tochnost 3, LEN_LL, tip 'd' */
status_code parse_spec(const char *s, spec_info *sp, size_t *dlina_spec) {
    if (s == NULL || sp == NULL || dlina_spec == NULL) {
        return INVALID_ARGS;
    }
    sp->minus = sp->plus = sp->probel = sp->reshetka = sp->nol = 0;
    sp->shirina = -1;
    sp->shirina_zv = 0;
    sp->tochnost = -1;
    sp->tochnost_zv = 0;
    sp->dlina = LEN_NONE;
    sp->tip = 0;
    size_t i = 0;

    /* 1) флаги - в любом порядке, могут повторяться */
    while (s[i] == '-' || s[i] == '+' || s[i] == ' ' || s[i] == '#' || s[i] == '0') {
        if (s[i] == '-') sp->minus = 1;
        if (s[i] == '+') sp->plus = 1;
        if (s[i] == ' ') sp->probel = 1;
        if (s[i] == '#') sp->reshetka = 1;
        if (s[i] == '0') sp->nol = 1;
        i++;
    }
    /* 2) ширина: '*' или число */
    if (s[i] == '*') {
        sp->shirina_zv = 1;
        i++;
    } else if (s[i] >= '1' && s[i] <= '9') {
        size_t k = 0;
        if (parse_format_int(s + i, &sp->shirina, &k) != OK) {
            return INVALID_FORMAT;
        }
        i += k;
    }
    /* 3) точность: '.' и потом '*' или число (просто "." - точность 0) */
    if (s[i] == '.') {
        i++;
        if (s[i] == '*') {
            sp->tochnost_zv = 1;
            i++;
        } else {
            size_t k = 0;
            if (parse_format_int(s + i, &sp->tochnost, &k) != OK) {
                return INVALID_FORMAT;
            }
            i += k;
        }
    }
    /* 4) модификатор длины: hh h ll l j z t L (двухбуквенные проверяем первыми) */
    if (s[i] == 'h' && s[i + 1] == 'h') {
        sp->dlina = LEN_HH;
        i += 2;
    } else if (s[i] == 'l' && s[i + 1] == 'l') {
        sp->dlina = LEN_LL;
        i += 2;
    } else if (s[i] == 'h') {
        sp->dlina = LEN_H;
        i++;
    } else if (s[i] == 'l') {
        sp->dlina = LEN_L;
        i++;
    } else if (s[i] == 'j') {
        sp->dlina = LEN_J;
        i++;
    } else if (s[i] == 'z') {
        sp->dlina = LEN_Z;
        i++;
    } else if (s[i] == 't') {
        sp->dlina = LEN_T;
        i++;
    } else if (s[i] == 'L') {
        sp->dlina = LEN_BIG_L;
        i++;
    }
    /* 5) тип - одна буква из списка. '\0' (формат кончился) - тоже ошибка */
    if (s[i] == '\0' || strchr("diouxXfFeEgGaAcspn%", s[i]) == NULL) {
        return INVALID_FORMAT;
    }
    sp->tip = s[i];
    i++;

    int podhodit = 0;
    check_dlina(sp->tip, sp->dlina, &podhodit);
    if (!podhodit) {
        return INVALID_FORMAT;
    }
    /* "%%" - только сам по себе ("%5%" - ошибка); у %n флаги, ширина и точность
       не имеют смысла (в стандарте - неопределенное поведение), поэтому запрещаем.
       Модификатор длины у '%' уже запрещен в check_dlina */
    if (sp->tip == '%' || sp->tip == 'n') {
        if (sp->minus || sp->plus || sp->probel || sp->reshetka || sp->nol ||
            sp->shirina >= 0 || sp->shirina_zv || sp->tochnost >= 0 || sp->tochnost_zv) {
            return INVALID_FORMAT;
        }
    }
    *dlina_spec = i;
    return OK;
}

/* Собрать строку спецификатора для vsnprintf из разобранного: "%-08.3lld".
   Ширина и точность уже числа (звездочки заменены на значения из аргументов).
   Строка в динамической памяти (освобождает вызывающий). Длина: '%' + 5 флагов +
   2 числа по 10 цифр + '.' + 2 буквы длины + тип + '\0' - меньше 40 символов */
status_code build_spec(const spec_info *sp, char **rez) {
    if (sp == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    char *s = (char *)malloc(40);
    if (s == NULL) {
        return ALLOC_ERROR;
    }
    size_t i = 0;
    s[i++] = '%';
    if (sp->minus) s[i++] = '-';
    if (sp->plus) s[i++] = '+';
    if (sp->probel) s[i++] = ' ';
    if (sp->reshetka) s[i++] = '#';
    if (sp->nol) s[i++] = '0';
    /* числа дописываем через sprintf: он возвращает, сколько символов записал */
    if (sp->shirina >= 0) {
        i += (size_t)sprintf(s + i, "%d", sp->shirina);
    }
    if (sp->tochnost >= 0) {
        i += (size_t)sprintf(s + i, ".%d", sp->tochnost);
    }
    static const char *dliny[] = {"", "hh", "h", "l", "ll", "j", "z", "t", "L"};
    const char *d = dliny[sp->dlina];
    for (size_t k = 0; d[k] != '\0'; k++) {
        s[i++] = d[k];
    }
    s[i++] = sp->tip;
    s[i] = '\0';
    *rez = s;
    return OK;
}

/* Отформатировать ОДНО значение стандартной функцией и записать в вывод w.
   Сама функция тоже с переменным числом аргументов: ей передают спецификатор и одно
   значение уже правильного типа, например  format_value(w, "%-5lld", (long long)x).
   Внутри vsnprintf вызывается два раза:
     1) vsnprintf(NULL, 0, ...) - ничего не пишет, только возвращает длину результата;
     2) выделяем ровно столько памяти и пишем по-настоящему.
   Список аргументов можно пройти только один раз, поэтому перед первым вызовом
   делаем копию va_copy (C99) - для второго вызова */
status_code format_value(vyvod *w, const char *spec, ...) {
    if (w == NULL || spec == NULL) {
        return INVALID_ARGS;
    }
    va_list args;
    va_list kopiya;
    va_start(args, spec);
    va_copy(kopiya, args);
    int dlina = vsnprintf(NULL, 0, spec, args);
    va_end(args);
    /* отрицательная длина - стандартная функция не смогла (например, широкий символ
       не переводится в текущую кодировку) */
    if (dlina < 0) {
        va_end(kopiya);
        return INVALID_FORMAT;
    }
    char *str = (char *)malloc((size_t)dlina + 1);
    if (str == NULL) {
        va_end(kopiya);
        return ALLOC_ERROR;
    }
    vsnprintf(str, (size_t)dlina + 1, spec, kopiya);
    va_end(kopiya);
    status_code sc = put_chars(w, str, (size_t)dlina);
    free(str);
    return sc;
}

/* %n: записать по указателю из аргументов, сколько символов уже выведено.
   Тип указателя зависит от модификатора длины: %n - int *, %hhn - signed char *,
   %lln - long long *, ... Значение приводим к этому типу */
status_code store_count(const spec_info *sp, va_list *args, const size_t count) {
    if (sp == NULL || args == NULL) {
        return INVALID_ARGS;
    }
    if (sp->dlina == LEN_HH) {
        signed char *p = va_arg(*args, signed char *);
        if (p == NULL) return INVALID_ARGS;
        *p = (signed char)count;
    } else if (sp->dlina == LEN_H) {
        short *p = va_arg(*args, short *);
        if (p == NULL) return INVALID_ARGS;
        *p = (short)count;
    } else if (sp->dlina == LEN_L) {
        long *p = va_arg(*args, long *);
        if (p == NULL) return INVALID_ARGS;
        *p = (long)count;
    } else if (sp->dlina == LEN_LL) {
        long long *p = va_arg(*args, long long *);
        if (p == NULL) return INVALID_ARGS;
        *p = (long long)count;
    } else if (sp->dlina == LEN_J) {
        intmax_t *p = va_arg(*args, intmax_t *);
        if (p == NULL) return INVALID_ARGS;
        *p = (intmax_t)count;
    } else if (sp->dlina == LEN_Z) {
        size_t *p = va_arg(*args, size_t *);
        if (p == NULL) return INVALID_ARGS;
        *p = count;
    } else if (sp->dlina == LEN_T) {
        ptrdiff_t *p = va_arg(*args, ptrdiff_t *);
        if (p == NULL) return INVALID_ARGS;
        *p = (ptrdiff_t)count;
    } else {
        int *p = va_arg(*args, int *);
        if (p == NULL) return INVALID_ARGS;
        *p = (int)count;
    }
    return OK;
}

/* Достать из списка аргумент для %lc (широкий символ wint_t).
   Аргументы меньше int в ... превращаются в int ("продвижение"). На Windows wint_t -
   это 16-битный unsigned short, он приходит как int; на Linux wint_t - 32-битный и
   приходит как есть. va_arg с типом меньше int - неопределенное поведение, поэтому
   выбираем тип при компиляции: #if проверяет WINT_MAX (самое большое wint_t, stdint.h) */
status_code get_wint(va_list *args, wint_t *rez) {
    if (args == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
#if WINT_MAX <= INT_MAX
    *rez = (wint_t)va_arg(*args, int);
#else
    *rez = va_arg(*args, wint_t);
#endif
    return OK;
}

/* Напечатать стандартный спецификатор: достать аргумент нужного типа и отдать
   format_value. Тип аргумента зависит от типа спецификатора и модификатора длины.
   Почему нельзя просто отдать весь формат vfprintf: тогда стандартная функция сама
   возьмет аргументы, и мы не узнаем, где в списке начинаются аргументы следующего
   нашего флага. Поэтому каждый аргумент достаем сами, по одному */
status_code print_standard(vyvod *w, spec_info *sp, va_list *args) {
    if (w == NULL || sp == NULL || args == NULL) {
        return INVALID_ARGS;
    }
    if (sp->tip == '%') {
        return put_char(w, '%');
    }
    if (sp->tip == 'n') {
        return store_count(sp, args, w->count);
    }
    /* Ширина и точность из аргументов ('*'): сначала ширина, потом точность - оба int.
       Отрицательная ширина значит флаг '-' и ширину по модулю;
       отрицательная точность - "точность не задана" (так в стандарте) */
    if (sp->shirina_zv) {
        int shir = va_arg(*args, int);
        if (shir < 0) {
            sp->minus = 1;
            /* -INT_MIN в int не помещается - такую ширину все равно не напечатать */
            if (shir == INT_MIN) {
                return INVALID_FORMAT;
            }
            shir = -shir;
        }
        sp->shirina = shir;
    }
    if (sp->tochnost_zv) {
        int toch = va_arg(*args, int);
        sp->tochnost = (toch < 0) ? -1 : toch;
    }

    char *spec = NULL;
    status_code sc = build_spec(sp, &spec);
    if (sc != OK) {
        return sc;
    }
    const char t = sp->tip;
    const dlina_kind d = sp->dlina;

    if (t == 'd' || t == 'i') {
        /* целые со знаком. hh и h: char и short в ... превращаются в int */
        if (d == LEN_L) sc = format_value(w, spec, va_arg(*args, long));
        else if (d == LEN_LL) sc = format_value(w, spec, va_arg(*args, long long));
        else if (d == LEN_J) sc = format_value(w, spec, va_arg(*args, intmax_t));
        else if (d == LEN_Z) sc = format_value(w, spec, va_arg(*args, size_t));
        else if (d == LEN_T) sc = format_value(w, spec, va_arg(*args, ptrdiff_t));
        else sc = format_value(w, spec, va_arg(*args, int));
    } else if (t == 'o' || t == 'u' || t == 'x' || t == 'X') {
        /* целые без знака */
        if (d == LEN_L) sc = format_value(w, spec, va_arg(*args, unsigned long));
        else if (d == LEN_LL) sc = format_value(w, spec, va_arg(*args, unsigned long long));
        else if (d == LEN_J) sc = format_value(w, spec, va_arg(*args, uintmax_t));
        else if (d == LEN_Z) sc = format_value(w, spec, va_arg(*args, size_t));
        else if (d == LEN_T) sc = format_value(w, spec, va_arg(*args, ptrdiff_t));
        else sc = format_value(w, spec, va_arg(*args, unsigned int));
    } else if (t == 'c') {
        /* символ: char в ... превращается в int; %lc - широкий символ wint_t */
        if (d == LEN_L) {
            wint_t simvol = 0;
            get_wint(args, &simvol);
            sc = format_value(w, spec, simvol);
        } else {
            sc = format_value(w, spec, va_arg(*args, int));
        }
    } else if (t == 's') {
        /* строка: NULL печатать нельзя (у стандартной функции - неопределенное поведение) */
        if (d == LEN_L) {
            const wchar_t *str = va_arg(*args, const wchar_t *);
            sc = (str == NULL) ? INVALID_ARGS : format_value(w, spec, str);
        } else {
            const char *str = va_arg(*args, const char *);
            sc = (str == NULL) ? INVALID_ARGS : format_value(w, spec, str);
        }
    } else if (t == 'p') {
        sc = format_value(w, spec, va_arg(*args, void *));
    } else {
        /* вещественные f F e E g G a A: float в ... превращается в double;
           %Lf - long double */
        if (d == LEN_BIG_L) sc = format_value(w, spec, va_arg(*args, long double));
        else sc = format_value(w, spec, va_arg(*args, double));
    }
    free(spec);
    return sc;
}

/* ===================== общее ядро ===================== */

/* Общее ядро overfprintf и oversprintf: идет по формату, обычные символы пишет как
   есть, на '%' смотрит, новый это флаг или стандартный, и печатает его.
   Ошибка - сразу выход (то, что уже записано, остается - как у fprintf).
   args - указатель на va_list (почему указатель - см. print_custom) */
status_code over_vprintf(vyvod *w, const char *format, va_list *args) {
    if (w == NULL || format == NULL || args == NULL) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    status_code sc = OK;
    while (format[i] != '\0' && sc == OK) {
        if (format[i] != '%') {
            /* обычный символ: пишем подряд все до следующего '%' или конца строки */
            size_t nachalo = i;
            while (format[i] != '\0' && format[i] != '%') {
                i++;
            }
            sc = put_chars(w, format + nachalo, i - nachalo);
            continue;
        }
        /* '%': сначала проверяем новые флаги (две буквы сразу после '%') */
        const char *kakoy = NULL;
        custom_flag(format + i + 1, &kakoy);
        if (kakoy != NULL) {
            sc = print_custom(w, kakoy, args);
            i += 3;
            continue;
        }
        /* иначе стандартный спецификатор */
        spec_info sp;
        size_t dlina_spec = 0;
        sc = parse_spec(format + i + 1, &sp, &dlina_spec);
        if (sc == OK) {
            sc = print_standard(w, &sp, args);
            i += 1 + dlina_spec;
        }
    }
    /* fprintf возвращает int: если символов больше INT_MAX, вернуть их число нельзя */
    if (sc == OK && w->count > (size_t)INT_MAX) {
        sc = OVERFLOW_ERROR;
    }
    return sc;
}

/* ===================== overfprintf и oversprintf ===================== */

/* Как fprintf: печатает в поток stream по формату format.
   Возвращает число выведенных символов или -1 при ошибке (NULL, неверный формат,
   неверное значение нового флага, ошибка записи). Прототип int - по условию */
int overfprintf(FILE *stream, const char *format, ...) {
    if (stream == NULL || format == NULL) {
        return -1;
    }
    vyvod w = {stream, NULL, 0};
    va_list args;
    va_start(args, format);
    status_code sc = over_vprintf(&w, format, &args);
    va_end(args);
    if (sc != OK) {
        return -1;
    }
    return (int)w.count;
}

/* Как sprintf: пишет в строку str и ставит в конце '\0'. Размер str не передается
   (прототип как у sprintf), поэтому места должно хватать - это забота вызывающего.
   При ошибке строка все равно заканчивается '\0' после уже записанного.
   Возвращает число записанных символов (без '\0') или -1 */
int oversprintf(char *str, const char *format, ...) {
    if (str == NULL || format == NULL) {
        return -1;
    }
    vyvod w = {NULL, str, 0};
    va_list args;
    va_start(args, format);
    status_code sc = over_vprintf(&w, format, &args);
    va_end(args);
    str[w.count] = '\0';
    if (sc != OK) {
        return -1;
    }
    return (int)w.count;
}

/* ===================== демонстрация ===================== */

/* Демонстрация. Каждая строка - вызов overfprintf(stdout, ...), в конце строки
   печатаем, что функция вернула (число символов). Размер буфера для oversprintf -
   с запасом: у sprintf нет параметра "размер", следить за ним должен вызывающий.
   Буфер - через malloc (не массив на стеке) */
status_code run_demo(void) {
    int r = 0;

    printf("Standard flags:\n");
    r = overfprintf(stdout, "  %%d %%5d %%-5d|: %d %5d %-5d|", 42, 42, 42);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%+d %% d %%05d: %+d % d %05d", 7, 7, -7);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%x %%X %%#o %%#x: %x %X %#o %#x", 255u, 255u, 8u, 255u);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%.2f %%e %%g %%10.3f|: %.2f %e %g %10.3f|", 3.14159, 1234.5, 0.0001, 2.5);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%s %%.3s %%8s|: %s %.3s %8s|", "hello", "hello", "hi");
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%c%%c%%c: %c%c%c", 'a', 'b', 'c');
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%lld %%llu %%hhd %%hu: %lld %llu %hhd %hu", -9000000000LL,
                    18000000000000000000ULL, 300, 70000);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%zu %%td %%jd: %zu %td %jd", (size_t)123, (ptrdiff_t)-5, (intmax_t)77);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%*d %%-*d| %%.*f: %*d %-*d| %.*f", 6, 42, 4, 7, 3, 3.14159);
    printf("  (returned %d)\n", r);
    int skolko = 0;
    r = overfprintf(stdout, "  %%n: abc%n", &skolko);
    printf("  -> %%n stored %d  (returned %d)\n", skolko, r);
    r = overfprintf(stdout, "  100%%");
    printf("  (returned %d)\n", r);

    printf("\nNew flags:\n");
    r = overfprintf(stdout, "  %%Ro: 1 = %Ro, 4 = %Ro, 9 = %Ro, 14 = %Ro, 1994 = %Ro, 3999 = %Ro",
                    1, 4, 9, 14, 1994, 3999);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%Ro: -14 = %Ro, 0 = %Ro", -14, 0);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%Zr: 0 = %Zr, 1 = %Zr, 2 = %Zr, 3 = %Zr, 4 = %Zr, 10 = %Zr, 100 = %Zr",
                    0u, 1u, 2u, 3u, 4u, 10u, 100u);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%Zr: UINT_MAX = %Zr", UINT_MAX);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%Cv %%CV: 255 in 16 = %Cv / %CV, -10 in 2 = %Cv, 35 in 36 = %Cv",
                    255, 16, 255, 16, -10, 2, 35, 36);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%Cv: INT_MIN in 16 = %Cv, 0 in 7 = %Cv, base 1 -> 10: %Cv",
                    INT_MIN, 16, 0, 7, 123, 1);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%to %%TO: \"ff\" 16 = %to, \"FF\" 16 = %TO, \"-101\" 2 = %to, \"zz\" 36 = %to",
                    "ff", 16, "FF", 16, "-101", 2, "zz", 36);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%to: \"123\" base 99 -> 10: %to, \"-80000000\" 16 = %TO", "123", 99, "-80000000", 16);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%mi: 1 = %mi", 1);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%mi: -1 = %mi", -1);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%mu: 258 = %mu", 258u);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%md: 1.0 = %md", 1.0);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%mf: 1.0f = %mf", 1.0f);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  %%mf: -2.5f = %mf", -2.5f);
    printf("  (returned %d)\n", r);
    r = overfprintf(stdout, "  mixed: %d is %Ro, in 2 = %Cv, %s", 12, 12, 12, 2, "done");
    printf("  (returned %d)\n", r);

    printf("\noversprintf (the same core, output goes to a string):\n");
    char *buf = (char *)malloc(512);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    r = oversprintf(buf, "year %Ro, %Zr, %CV, %TO, [%5.1f]", 2024, 10u, 48879, 16, "7FFFFFFF", 16, 2.3);
    printf("  \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "%mi | %d%%", 256, 50);
    printf("  \"%s\"  (returned %d)\n", buf, r);

    printf("\nErrors (the function returns -1, the text before the error is already written):\n");
    r = oversprintf(buf, "x = %Ro", 4000);
    printf("  %%Ro with 4000: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %to", "FF", 16);
    printf("  %%to with \"FF\" (big letters): \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %to", "19", 8);
    printf("  %%to with \"19\" in base 8: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %to", "80000000", 16);
    printf("  %%to with \"80000000\" in 16 (more than INT_MAX): \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %to", "", 10);
    printf("  %%to with empty string: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %y", 1);
    printf("  unknown %%y: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %Lc", 'a');
    printf("  wrong length %%Lc: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %");
    printf("  %% at the end: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %5%");
    printf("  %%5%%: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %5n", &skolko);
    printf("  %%5n: \"%s\"  (returned %d)\n", buf, r);
    r = oversprintf(buf, "x = %s", (const char *)NULL);
    printf("  %%s with NULL: \"%s\"  (returned %d)\n", buf, r);
    r = overfprintf(NULL, "x");
    printf("  overfprintf(NULL, ...): returned %d\n", r);
    r = oversprintf(NULL, "x");
    printf("  oversprintf(NULL, ...): returned %d\n", r);
    free(buf);
    return OK;
}

/* ===================== один флаг из командной строки ===================== */

/* Строка -> целое в диапазоне [min; max]. strtoll с проверкой конца строки;
   если число огромное, strtoll вернет LLONG_MAX или LLONG_MIN - они все равно вне
   диапазона int, поэтому errno не нужен */
status_code parse_ll(const char *str, const long long min, const long long max, long long *rez) {
    if (str == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }
    char *end = NULL;
    long long znach = strtoll(str, &end, 10);
    if (end == str || *end != '\0') {
        return INVALID_NUMBER;
    }
    if (znach < min || znach > max) {
        return OVERFLOW_ERROR;
    }
    *rez = znach;
    return OK;
}

/* Строка -> double (для %md и %mf). inf и nan разрешены: их дамп тоже интересен */
status_code parse_dbl(const char *str, double *rez) {
    if (str == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0' || isspace((unsigned char)str[0])) {
        return INVALID_NUMBER;
    }
    char *end = NULL;
    double znach = strtod(str, &end);
    if (end == str || *end != '\0') {
        return INVALID_NUMBER;
    }
    *rez = znach;
    return OK;
}

/* Режим командной строки: один новый флag на одном значении.
   argv[1] - флаг без '%' (Ro, Zr, Cv, CV, to, TO, mi, mu, md, mf), дальше значение
   и для Cv/CV/to/TO - основание. Печатаем результат через overfprintf и через
   oversprintf (одно ядро - результат должен совпасть).
   *kod - код возврата программы */
status_code run_one(const int argc, char *argv[], int *kod) {
    if (argv == NULL || kod == NULL) {
        return INVALID_ARGS;
    }
    *kod = 1;
    const char *kakoy = NULL;
    custom_flag(argv[1], &kakoy);
    if (kakoy == NULL || argv[1][2] != '\0') {
        fprintf(stderr, "Error: unknown flag '%s' (use Ro Zr Cv CV to TO mi mu md mf)\n", argv[1]);
        return OK;
    }
    /* у Cv, CV, to, TO - два аргумента (значение и основание), у остальных - один */
    const int dva = (kakoy[0] == 'C' || kakoy[0] == 't' || kakoy[0] == 'T');
    if (argc != (dva ? 4 : 3)) {
        fprintf(stderr, dva ? "Error: flag %s needs a value and a base\n"
                            : "Error: flag %s needs one value\n", kakoy);
        return OK;
    }
    /* формат "%Ro" и т. п. */
    char format[4] = {'%', kakoy[0], kakoy[1], '\0'};
    /* для oversprintf - буфер; самый длинный результат - дамп double: 8 * 9 = 72 символа,
       а римское число и запись в двоичной системе короче. Берем с запасом */
    char *buf = (char *)malloc(256);
    if (buf == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        return OK;
    }
    long long x = 0;
    long long base = 10;
    double d = 0.0;
    int r1 = -1;
    int r2 = -1;
    status_code sc = OK;

    if (dva) {
        /* основание - любое int: вне [2..36] функция сама заменит его на 10 */
        sc = parse_ll(argv[3], INT_MIN, INT_MAX, &base);
        if (sc != OK) {
            fprintf(stderr, "Error: base '%s' must be a whole number (int)\n", argv[3]);
            free(buf);
            return OK;
        }
    }
    if (kakoy[0] == 't' || kakoy[0] == 'T') {
        /* %to / %TO: значение - сама строка */
        printf("overfprintf: ");
        r1 = overfprintf(stdout, format, argv[2], (int)base);
        printf("\n");
        r2 = oversprintf(buf, format, argv[2], (int)base);
    } else if (kakoy[0] == 'm' && (kakoy[1] == 'd' || kakoy[1] == 'f')) {
        /* %md, %mf: вещественное. Для %mf передаем float (в ... он станет double) */
        if (parse_dbl(argv[2], &d) != OK) {
            fprintf(stderr, "Error: '%s' is not a number\n", argv[2]);
            free(buf);
            return OK;
        }
        /* double -> float: если число больше самого большого float (FLT_MAX ~3.4e38),
           по стандарту C99 результат не определен - не переводим. inf и nan можно:
           во float они тоже есть. Сравнение с FLT_MAX - граница диапазона, не "равенство" */
        if (kakoy[1] == 'f' && isfinite(d) && fabs(d) > FLT_MAX) {
            fprintf(stderr, "Error: '%s' is too big for float\n", argv[2]);
            free(buf);
            return OK;
        }
        printf("overfprintf: ");
        if (kakoy[1] == 'd') {
            r1 = overfprintf(stdout, format, d);
            printf("\n");
            r2 = oversprintf(buf, format, d);
        } else {
            r1 = overfprintf(stdout, format, (float)d);
            printf("\n");
            r2 = oversprintf(buf, format, (float)d);
        }
    } else {
        /* Ro, Cv, CV, mi - int;  Zr, mu - unsigned int (от 0 до UINT_MAX) */
        const int bez_znaka = (kakoy[0] == 'Z' || kakoy[1] == 'u');
        sc = bez_znaka ? parse_ll(argv[2], 0, UINT_MAX, &x) : parse_ll(argv[2], INT_MIN, INT_MAX, &x);
        if (sc != OK) {
            fprintf(stderr, bez_znaka ? "Error: '%s' must be a whole number from 0 to 4294967295\n"
                                      : "Error: '%s' must be a whole number (int)\n", argv[2]);
            free(buf);
            return OK;
        }
        printf("overfprintf: ");
        if (bez_znaka) {
            r1 = overfprintf(stdout, format, (unsigned int)x);
            printf("\n");
            r2 = oversprintf(buf, format, (unsigned int)x);
        } else if (dva) {
            r1 = overfprintf(stdout, format, (int)x, (int)base);
            printf("\n");
            r2 = oversprintf(buf, format, (int)x, (int)base);
        } else {
            r1 = overfprintf(stdout, format, (int)x);
            printf("\n");
            r2 = oversprintf(buf, format, (int)x);
        }
    }
    printf("oversprintf: %s\n", buf);
    printf("returned: %d and %d\n", r1, r2);
    if (r1 < 0 || r2 < 0) {
        fprintf(stderr, "Error: the value can not be printed with %s\n", format);
    } else {
        *kod = 0;
    }
    free(buf);
    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    /* Без аргументов - демонстрация */
    if (argc == 1) {
        if (run_demo() != OK) {
            fprintf(stderr, "Error: not enough memory\n");
            return 1;
        }
        return 0;
    }
    /* С аргументами: флаг, значение и, для Cv/CV/to/TO, основание */
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: lab2_task5                        (demo)\n");
        fprintf(stderr, "       lab2_task5 <flag> <value> [base]\n");
        fprintf(stderr, "flags: Ro Zr Cv CV to TO mi mu md mf\n");
        return 1;
    }
    int kod = 1;
    if (run_one(argc, argv, &kod) != OK) {
        /* INVALID_ARGS - argv и kod не NULL, быть не может: ошибка в программе */
        fprintf(stderr, "Error: internal error\n");
        return 1;
    }
    return kod;
}
