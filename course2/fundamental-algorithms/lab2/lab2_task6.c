/*
 * Лабораторная 2, задание 6. Функции overfscanf и oversscanf.
 *
 * Они работают как стандартные fscanf и sscanf (тот же прототип, те же флаги
 * %d, %5s, %*d, %lf, %[a-z], %n, ...), но понимают еще 4 своих флага:
 *   %Ro  - прочитать int, записанный римскими цифрами:   "MCMXCIV" -> 1994
 *   %Zr  - прочитать unsigned int в цекендорфовом представлении (коэффициенты при
 *          числах Фибоначчи 1, 2, 3, 5, 8, ... от младшего к старшему и 1 в конце):
 *          "010011" -> 10
 *   %Cv  - прочитать int в системе с основанием base, буквы маленькие (аргументы:
 *          адрес int, куда записать, и основание; основание вне [2..36] -> 10)
 *   %CV  - то же, буквы большие:  "ff" (%Cv, 16) -> 255,  "FF" (%CV, 16) -> 255
 * Новые флаги, как %d, сначала пропускают пробелы и переводы строк.
 * Пишутся они так же, как печатает задание 5: отрицательные с '-', ноль римскими - N,
 * ноль в цекендорфовом - 01.
 *
 * Запуск:  lab2_task6                    - демонстрация (oversscanf на строках, overfscanf
 *                                           на файле tests/data/task6_input.txt)
 *          lab2_task6 <флаг> [основание]  - читать из стандартного ввода значения
 *                                           флага Ro, Zr, Cv или CV, пока читаются
 *          например: echo "XIV MMXXIV" | lab2_task6 Ro   ->  14, 2024
 *
 * Устройство (как в задании 5): у overfscanf и oversscanf одно общее ядро over_vscanf.
 * Разница только в том, откуда берутся символы: из файла (FILE *) или из строки.
 * Это описывает структура source. Новые флаги ядро читает само, по одному символу.
 * Стандартные - разбирает (подавление *, ширина, модификатор длины, набор [...]),
 * достает указатель нужного типа и отдает ОДИН спецификатор стандартной vfscanf или
 * vsscanf, дописав к нему %n - чтобы узнать, сколько символов она прочитала.
 *
 * Возвращаемое значение. Условие требует ТОТ ЖЕ прототип, что у fscanf и sscanf:
 *   int overfscanf(FILE *stream, const char *format, ...);
 *   int oversscanf(const char *str, const char *format, ...);
 * Поэтому они (как и main) возвращают int: сколько значений прочитано и записано,
 * или EOF (-1), если ввод кончился раньше первого преобразования, - как fscanf.
 * EOF возвращается и при ошибке в параметрах (NULL, неверный формат).
 * Все остальные функции возвращают status_code.
 *
 * Проверка данных во входном потоке по условию не требуется. Но и неправильный ввод
 * не должен ломать программу: если символы не подходят, чтение останавливается
 * (как у fscanf), слишком большие числа тоже останавливают чтение (без переполнения).
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, free */
#include <stdarg.h>   /* va_list, va_start, va_arg, va_end */
#include <string.h>   /* strchr, strcmp */
#include <stdint.h>   /* intmax_t, uintmax_t */
#include <stddef.h>   /* size_t, ptrdiff_t */
#include <limits.h>   /* INT_MAX, UINT_MAX */
#include <ctype.h>    /* isspace */
#include <wchar.h>    /* wchar_t - для стандартных %lc, %ls, %l[ */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,     /* передали NULL (поток, строку, формат, указатель для значения) */
    INVALID_FORMAT,   /* непонятный спецификатор: "%y", "%" в конце, "%Lc", "%0d", "%[" без ']' */
    MATCH_FAILURE,    /* символы во вводе не подходят к формату (или число слишком большое) */
    INPUT_END,        /* ввод кончился (конец файла или строки) */
    ALLOC_ERROR       /* malloc не смог выделить память */
} status_code;

/* ===================== откуда читаем: файл или строка ===================== */

/* "Источник": откуда ядро берет символы. Ровно одно из двух не NULL:
   stream - читаем из файла (overfscanf), str - из строки (oversscanf), pos - где мы в строке.
   count - сколько символов уже прочитано (это записывает %n) */
typedef struct {
    FILE *stream;
    const char *str;
    size_t pos;
    size_t count;
} source;

/* Взять следующий символ. *c - код символа (0..255) или EOF, если ввод кончился.
   unsigned char: русская буква в char отрицательна, а нам нужен код 0..255, как у fgetc */
status_code get_char(source *src, int *c) {
    if (src == NULL || c == NULL) {
        return INVALID_ARGS;
    }
    if (src->stream != NULL) {
        *c = fgetc(src->stream);
    } else if (src->str != NULL) {
        *c = (src->str[src->pos] == '\0') ? EOF : (unsigned char)src->str[src->pos];
        if (*c != EOF) {
            src->pos++;
        }
    } else {
        return INVALID_ARGS;
    }
    if (*c != EOF) {
        src->count++;
    }
    return OK;
}

/* Вернуть прочитанный символ обратно ("подсмотрели" и он не подошел).
   Для файла - ungetc: стандарт гарантирует возврат одного символа, больше нам и
   не нужно (все наши чтения смотрят только на один символ вперед). EOF не возвращаем */
status_code unget_char(source *src, const int c) {
    if (src == NULL) {
        return INVALID_ARGS;
    }
    if (c == EOF) {
        return OK;
    }
    if (src->stream != NULL) {
        ungetc(c, src->stream);
    } else {
        src->pos--;
    }
    src->count--;
    return OK;
}

/* Пропустить пробелы, табуляции, переводы строк. *c - первый не пробельный символ
   (он уже прочитан!) или EOF */
status_code skip_spaces(source *src, int *c) {
    if (src == NULL || c == NULL) {
        return INVALID_ARGS;
    }
    status_code sc = get_char(src, c);
    while (sc == OK && *c != EOF && isspace(*c)) {
        sc = get_char(src, c);
    }
    return sc;
}

/* ===================== новые флаги ===================== */

/* Значение римской цифры; 0 - это не римская цифра (только большие буквы, как печатает
   задание 5) */
status_code roman_digit(const int c, int *value) {
    if (value == NULL) {
        return INVALID_ARGS;
    }
    switch (c) {
        case 'I': *value = 1; break;
        case 'V': *value = 5; break;
        case 'X': *value = 10; break;
        case 'L': *value = 50; break;
        case 'C': *value = 100; break;
        case 'D': *value = 500; break;
        case 'M': *value = 1000; break;
        default: *value = 0; break;
    }
    return OK;
}

/* %Ro: прочитать римское число. Сначала пробелы, потом необязательный '-', потом
   римские цифры, пока они идут. Одна "N" - ноль (так его печатает задание 5).
   Как считать за один проход: прибавляем значение каждой цифры, но если цифра
   больше предыдущей, то предыдущую надо было вычесть, а мы ее прибавили - поэтому
   вычитаем ее дважды. Пример "XIV": X: 10;  I: 11;  V > I: 11 + 5 - 2*1 = 14.
   Сумма больше INT_MAX (очень длинная строка из M) - MATCH_FAILURE: чтение
   останавливается, переполнения нет. Первый неподходящий символ возвращаем назад */
status_code scan_roman(source *src, int *result) {
    if (src == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    int c = EOF;
    skip_spaces(src, &c);
    if (c == EOF) {
        return INPUT_END;
    }
    int minus = 0;
    if (c == '-') {
        minus = 1;
        get_char(src, &c);
    }
    /* ноль: одна буква N */
    if (c == 'N' && !minus) {
        *result = 0;
        return OK;
    }
    long long sum = 0;
    int prev = 0;            /* значение предыдущей цифры (0 - еще не было) */
    int item_count = 0;           /* сколько римских цифр прочитано */
    int digit = 0;
    roman_digit(c, &digit);
    while (digit != 0) {
        if (prev != 0 && prev < digit) {
            sum += digit - 2LL * prev;
        } else {
            sum += digit;
        }
        if (sum > INT_MAX) {
            return MATCH_FAILURE;
        }
        prev = digit;
        item_count++;
        get_char(src, &c);
        roman_digit(c, &digit);
    }
    /* c - первый не римский символ: он не наш, возвращаем */
    unget_char(src, c);
    if (item_count == 0) {
        return MATCH_FAILURE;
    }
    *result = (int)(minus ? -sum : sum);
    return OK;
}

/* %Zr: прочитать цекендорфово представление. Символы '0' и '1' - коэффициенты при
   числах Фибоначчи 1, 2, 3, 5, 8, ... (от младшего к старшему), в конце лишняя '1'.
   В самой записи двух единиц подряд не бывает, поэтому "11" - конец: вторая единица
   - это признак конца, ее не считаем. "010011" -> 0*1 + 1*2 + 0*3 + 0*5 + 1*8 = 10.
   Ноль задание 5 печатает как "01" - там нет "11". Поэтому, если цифры 0/1 кончились
   раньше "11", последняя прочитанная '1' - признак конца, и ее вклад вычитаем.
   Если последняя была '0' - признака конца нет, это MATCH_FAILURE.
   Числа Фибоначчи считаем на ходу (cur, next) в unsigned long long. Когда они
   становятся больше UINT_MAX, дальше их не растим (иначе переполнились бы на 93-м
   шаге): любая единица там дает сумму больше UINT_MAX, а это MATCH_FAILURE.
   Сумму тоже ограничиваем, чтобы она не переполнилась на очень длинном вводе */
status_code scan_zeckendorf(source *src, unsigned int *result) {
    if (src == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    int c = EOF;
    skip_spaces(src, &c);
    if (c == EOF) {
        return INPUT_END;
    }
    const unsigned long long too_big = (unsigned long long)UINT_MAX + 1;   /* "слишком много" */
    unsigned long long cur = 1;     /* число Фибоначчи для текущего коэффициента */
    unsigned long long next = 2;    /* следующее */
    unsigned long long sum = 0;
    unsigned long long last_value = 0;   /* вклад последней прочитанной '1' */
    int prev = 0;                   /* предыдущий символ ('0', '1' или 0 - не было) */
    int finished = 0;                  /* 1 - встретили "11" */
    int item_count = 0;
    while ((c == '0' || c == '1') && !finished) {
        item_count++;
        if (c == '1' && prev == '1') {
            /* вторая единица подряд - признак конца, в сумму не идет */
            finished = 1;
        } else {
            last_value = 0;
            if (c == '1') {
                last_value = cur;
                sum += cur;
                /* сумму держим не больше 2 * too_big: этого хватает, чтобы понять
                   "слишком много", и переполнения нет при любой длине ввода */
                if (sum > 2 * too_big) {
                    sum = 2 * too_big;
                }
            }
            prev = c;
            /* следующее число Фибоначчи; больше UINT_MAX - не растим дальше */
            if (cur < too_big) {
                unsigned long long next_fib = cur + next;
                cur = next;
                next = next_fib;
            }
            get_char(src, &c);
        }
    }
    if (!finished) {
        /* цифры кончились раньше "11": c - чужой символ, возвращаем его */
        unget_char(src, c);
        if (item_count == 0 || prev != '1') {
            return MATCH_FAILURE;
        }
        /* последняя '1' была признаком конца ("01" - это 0) */
        sum -= last_value;
    }
    if (sum > UINT_MAX) {
        return MATCH_FAILURE;
    }
    *result = (unsigned int)sum;
    return OK;
}

/* Основание вне [2..36] -> 10 (так в условии) */
status_code fix_base(const int base, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    *result = (base < 2 || base > 36) ? 10 : base;
    return OK;
}

/* Значение символа как цифры системы radix: 0..radix-1 или -1 (не цифра).
   upper = 1 - буквы A..Z (%CV), 0 - a..z (%Cv); буква другого регистра - не цифра */
status_code digit_value(const int c, const int radix, const int upper, int *value) {
    if (value == NULL) {
        return INVALID_ARGS;
    }
    int d = -1;
    if (c >= '0' && c <= '9') {
        d = c - '0';
    } else if (upper && c >= 'A' && c <= 'Z') {
        d = c - 'A' + 10;
    } else if (!upper && c >= 'a' && c <= 'z') {
        d = c - 'a' + 10;
    }
    *value = (d >= 0 && d < radix) ? d : -1;
    return OK;
}

/* %Cv / %CV: прочитать int в системе base. Пробелы, необязательный '-', цифры, пока
   они цифры этой системы (первая не цифра возвращается назад). Нет ни одной цифры -
   MATCH_FAILURE. Число не помещается в int - MATCH_FAILURE (проверка до умножения:
   acc * radix + d > limit  <=>  acc > (limit - d) / radix) */
status_code scan_in_base(source *src, const int base, const int upper, int *result) {
    if (src == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    int radix = 10;
    fix_base(base, &radix);
    int c = EOF;
    skip_spaces(src, &c);
    if (c == EOF) {
        return INPUT_END;
    }
    int minus = 0;
    if (c == '-') {
        minus = 1;
        get_char(src, &c);
    }
    const long long limit = minus ? (long long)INT_MAX + 1 : (long long)INT_MAX;
    long long acc = 0;
    int item_count = 0;
    int d = -1;
    digit_value(c, radix, upper, &d);
    while (d >= 0) {
        if (acc > (limit - d) / radix) {
            return MATCH_FAILURE;
        }
        acc = acc * radix + d;
        item_count++;
        get_char(src, &c);
        digit_value(c, radix, upper, &d);
    }
    unget_char(src, c);
    if (item_count == 0) {
        return MATCH_FAILURE;
    }
    *result = (int)(minus ? -acc : acc);
    return OK;
}

/* Новый флаг? s - символ сразу после '%'. *which - "Ro", "Zr", "Cv", "CV" или NULL */
status_code custom_flag(const char *s, const char **which) {
    if (s == NULL || which == NULL) {
        return INVALID_ARGS;
    }
    static const char *list[] = {"Ro", "Zr", "Cv", "CV"};
    *which = NULL;
    /* s[0] проверяем первым: если строка кончилась, s[1] читать нельзя */
    if (s[0] == '\0') {
        return OK;
    }
    for (int i = 0; i < 4; i++) {
        if (s[0] == list[i][0] && s[1] == list[i][1]) {
            *which = list[i];
        }
    }
    return OK;
}

/* Прочитать новый флаг which и записать значение по адресу из аргументов.
   args - указатель на va_list: va_arg(*args, ...) сдвигает общий список (если передать
   va_list по значению, после va_arg здесь список у вызывающего станет неопределенным).
   no_assign = 1 - было "%*Ro": прочитать и никуда не записывать (как "%*d" у scanf),
   тогда адреса в аргументах нет (у %*Cv основание все равно есть - без него не прочитать).
   Значение записываем, только если оно прочиталось: при ошибке переменная не меняется */
status_code scan_custom(source *src, const char *which, const int no_assign, va_list *args) {
    if (src == NULL || which == NULL || args == NULL) {
        return INVALID_ARGS;
    }
    if (strcmp(which, "Ro") == 0) {
        int *p = no_assign ? NULL : va_arg(*args, int *);
        int x = 0;
        status_code sc = scan_roman(src, &x);
        if (sc == OK && !no_assign) {
            if (p == NULL) return INVALID_ARGS;
            *p = x;
        }
        return sc;
    }
    if (strcmp(which, "Zr") == 0) {
        unsigned int *p = no_assign ? NULL : va_arg(*args, unsigned int *);
        unsigned int x = 0;
        status_code sc = scan_zeckendorf(src, &x);
        if (sc == OK && !no_assign) {
            if (p == NULL) return INVALID_ARGS;
            *p = x;
        }
        return sc;
    }
    /* Cv / CV: сначала адрес, потом основание (так в условии) */
    int *p = no_assign ? NULL : va_arg(*args, int *);
    int base = va_arg(*args, int);
    int x = 0;
    status_code sc = scan_in_base(src, base, which[1] == 'V', &x);
    if (sc == OK && !no_assign) {
        if (p == NULL) return INVALID_ARGS;
        *p = x;
    }
    return sc;
}

/* ===================== стандартные флаги ===================== */

/* Модификатор длины: какого типа переменная (%hhd - signed char, %ld - long, %Lf - long double) */
typedef enum {
    LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIG_L
} length_kind;

/* Разобранный стандартный спецификатор:  %[*][ширина][длина]тип  или  %[*][ширина][l][набор] */
typedef struct {
    int no_assign;        /* 1 - есть '*': прочитать, но никуда не записывать (аргумента нет) */
    int width;           /* ширина - сколько символов читать самое большее; -1 - не задана */
    length_kind length;
    char conv;              /* d i o u x X a A e E f F g G c s [ p n % */
    const char *scanset;     /* для '[': где в формате начинается набор (сразу после '[') */
    size_t scanset_length;    /* длина набора вместе с закрывающей ']' */
} spec_info;

/* Подходит ли модификатор длины к типу (C99):
   целые d i o u x X и n - любые, кроме L;  вещественные - без модификатора, l (double) или L;
   c s [ - без модификатора или l (широкие символы);  p и % - без модификатора */
status_code check_length(const char conv, const length_kind length, int *fits) {
    if (fits == NULL) {
        return INVALID_ARGS;
    }
    if (strchr("diouxXn", conv) != NULL) {
        *fits = (length != LEN_BIG_L);
    } else if (strchr("aAeEfFgG", conv) != NULL) {
        *fits = (length == LEN_NONE || length == LEN_L || length == LEN_BIG_L);
    } else if (conv == 'c' || conv == 's' || conv == '[') {
        *fits = (length == LEN_NONE || length == LEN_L);
    } else {
        *fits = (length == LEN_NONE);
    }
    return OK;
}

/* Разобрать стандартный спецификатор. s - символ сразу после '%'.
   *spec_length - сколько символов формата он занял (без '%').
   Набор [...]: после '[' может идти '^' (все, КРОМЕ перечисленных), и ']' сразу в
   начале набора - это обычный символ, а не конец: "[]abc]" - набор из ], a, b, c */
status_code parse_spec(const char *s, spec_info *sp, size_t *spec_length) {
    if (s == NULL || sp == NULL || spec_length == NULL) {
        return INVALID_ARGS;
    }
    sp->no_assign = 0;
    sp->width = -1;
    sp->length = LEN_NONE;
    sp->conv = 0;
    sp->scanset = NULL;
    sp->scanset_length = 0;
    size_t i = 0;

    /* 1) '*' - не записывать */
    if (s[i] == '*') {
        sp->no_assign = 1;
        i++;
    }
    /* 2) ширина: число > 0. Проверка переполнения до умножения */
    if (s[i] >= '0' && s[i] <= '9') {
        int value = 0;
        while (s[i] >= '0' && s[i] <= '9') {
            int digit = s[i] - '0';
            if (value > (INT_MAX - digit) / 10) {
                return INVALID_FORMAT;
            }
            value = value * 10 + digit;
            i++;
        }
        /* ширина 0 стандартом запрещена */
        if (value == 0) {
            return INVALID_FORMAT;
        }
        sp->width = value;
    }
    /* 3) модификатор длины (двухбуквенные - первыми) */
    if (s[i] == 'h' && s[i + 1] == 'h') {
        sp->length = LEN_HH;
        i += 2;
    } else if (s[i] == 'l' && s[i + 1] == 'l') {
        sp->length = LEN_LL;
        i += 2;
    } else if (s[i] == 'h') {
        sp->length = LEN_H;
        i++;
    } else if (s[i] == 'l') {
        sp->length = LEN_L;
        i++;
    } else if (s[i] == 'j') {
        sp->length = LEN_J;
        i++;
    } else if (s[i] == 'z') {
        sp->length = LEN_Z;
        i++;
    } else if (s[i] == 't') {
        sp->length = LEN_T;
        i++;
    } else if (s[i] == 'L') {
        sp->length = LEN_BIG_L;
        i++;
    }
    /* 4) тип */
    if (s[i] == '\0' || strchr("diouxXaAeEfFgGcs[pn%", s[i]) == NULL) {
        return INVALID_FORMAT;
    }
    sp->conv = s[i];
    i++;
    /* 5) набор [...]: ищем закрывающую ']' */
    if (sp->conv == '[') {
        sp->scanset = s + i;
        size_t k = i;
        if (s[k] == '^') {
            k++;
        }
        if (s[k] == ']') {
            k++;   /* ']' в самом начале - обычный символ набора */
        }
        while (s[k] != '\0' && s[k] != ']') {
            k++;
        }
        if (s[k] != ']') {
            return INVALID_FORMAT;   /* нет закрывающей скобки */
        }
        sp->scanset_length = k + 1 - i;
        i = k + 1;
    }
    int fits = 0;
    check_length(sp->conv, sp->length, &fits);
    if (!fits) {
        return INVALID_FORMAT;
    }
    /* "%%" - только сам по себе; у %n нельзя '*' и ширину (неопределенное поведение) */
    if ((sp->conv == '%' || sp->conv == 'n') && (sp->no_assign || sp->width >= 0)) {
        return INVALID_FORMAT;
    }
    *spec_length = i;
    return OK;
}

/* Собрать строку спецификатора для стандартной функции и дописать к ней "%n":
   "%*5lld" + набор + "%n". Строка в динамической памяти (освобождает вызывающий).
   Длина: '%' + '*' + 10 цифр ширины + 2 буквы длины + тип + набор + "%n" + '\0' */
status_code build_spec(const spec_info *sp, char **result) {
    if (sp == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    char *s = (char *)malloc(20 + sp->scanset_length);
    if (s == NULL) {
        return ALLOC_ERROR;
    }
    size_t i = 0;
    s[i++] = '%';
    if (sp->no_assign) {
        s[i++] = '*';
    }
    if (sp->width > 0) {
        i += (size_t)sprintf(s + i, "%d", sp->width);
    }
    static const char *lengths[] = {"", "hh", "h", "l", "ll", "j", "z", "t", "L"};
    const char *d = lengths[sp->length];
    for (size_t k = 0; d[k] != '\0'; k++) {
        s[i++] = d[k];
    }
    s[i++] = sp->conv;
    for (size_t k = 0; k < sp->scanset_length; k++) {
        s[i++] = sp->scanset[k];
    }
    s[i++] = '%';
    s[i++] = 'n';
    s[i] = '\0';
    *result = s;
    return OK;
}

/* Прочитать ОДИН стандартный спецификатор стандартной функцией.
   Сама функция с переменным числом аргументов: ей передают спецификатор (уже с "%n"
   в конце) и указатели: на переменную для значения (если нет '*') и на int для %n.
   Например  scan_value(src, "%lld%n", &returned, (long long *)p, &consumed).
   Из файла читает vfscanf, из строки - vsscanf с текущей позиции.
   *returned - что вернула стандартная функция (1, 0 или EOF) */
status_code scan_value(source *src, const char *spec, int *returned, ...) {
    if (src == NULL || spec == NULL || returned == NULL) {
        return INVALID_ARGS;
    }
    va_list args;
    va_start(args, returned);
    if (src->stream != NULL) {
        *returned = vfscanf(src->stream, spec, args);
    } else {
        *returned = vsscanf(src->str + src->pos, spec, args);
    }
    va_end(args);
    return OK;
}

/* %n: записать по указателю из аргументов, сколько символов уже прочитано.
   Тип указателя зависит от модификатора длины */
status_code store_count(const spec_info *sp, va_list *args, const size_t count) {
    if (sp == NULL || args == NULL) {
        return INVALID_ARGS;
    }
    if (sp->length == LEN_HH) {
        signed char *p = va_arg(*args, signed char *);
        if (p == NULL) return INVALID_ARGS;
        *p = (signed char)count;
    } else if (sp->length == LEN_H) {
        short *p = va_arg(*args, short *);
        if (p == NULL) return INVALID_ARGS;
        *p = (short)count;
    } else if (sp->length == LEN_L) {
        long *p = va_arg(*args, long *);
        if (p == NULL) return INVALID_ARGS;
        *p = (long)count;
    } else if (sp->length == LEN_LL) {
        long long *p = va_arg(*args, long long *);
        if (p == NULL) return INVALID_ARGS;
        *p = (long long)count;
    } else if (sp->length == LEN_J) {
        intmax_t *p = va_arg(*args, intmax_t *);
        if (p == NULL) return INVALID_ARGS;
        *p = (intmax_t)count;
    } else if (sp->length == LEN_Z) {
        size_t *p = va_arg(*args, size_t *);
        if (p == NULL) return INVALID_ARGS;
        *p = count;
    } else if (sp->length == LEN_T) {
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

/* %%: как и числа, сначала пропускает пробелы, потом должен быть символ '%' */
status_code scan_percent(source *src) {
    int c = EOF;
    status_code sc = skip_spaces(src, &c);
    if (sc != OK) {
        return sc;
    }
    if (c == EOF) {
        return INPUT_END;
    }
    if (c != '%') {
        unget_char(src, c);
        return MATCH_FAILURE;
    }
    return OK;
}

/* Прочитать стандартный спецификатор. Достаем указатель нужного типа (тип зависит от
   типа спецификатора и модификатора длины) и отдаем scan_value вместе с &consumed.
   consumed = -1 до вызова: если стандартная функция дошла до %n, там будет число
   прочитанных символов (>= 0), значит спецификатор прочитан успешно. Иначе - ошибка:
   EOF от стандартной функции - ввод кончился, 0 - символы не подошли.
   *assigned = 1, если значение записано в переменную (для возвращаемого числа) */
status_code scan_standard(source *src, spec_info *sp, va_list *args, int *assigned) {
    if (src == NULL || sp == NULL || args == NULL || assigned == NULL) {
        return INVALID_ARGS;
    }
    *assigned = 0;
    if (sp->conv == '%') {
        return scan_percent(src);
    }
    if (sp->conv == 'n') {
        return store_count(sp, args, src->count);
    }
    char *spec = NULL;
    status_code sc = build_spec(sp, &spec);
    if (sc != OK) {
        return sc;
    }
    int consumed = -1;
    int returned = 0;
    const char t = sp->conv;
    const length_kind d = sp->length;

    if (sp->no_assign) {
        /* '*': аргумента для значения нет, только &consumed */
        sc = scan_value(src, spec, &returned, &consumed);
    } else if (t == 'd' || t == 'i') {
        if (d == LEN_HH) sc = scan_value(src, spec, &returned, va_arg(*args, signed char *), &consumed);
        else if (d == LEN_H) sc = scan_value(src, spec, &returned, va_arg(*args, short *), &consumed);
        else if (d == LEN_L) sc = scan_value(src, spec, &returned, va_arg(*args, long *), &consumed);
        else if (d == LEN_LL) sc = scan_value(src, spec, &returned, va_arg(*args, long long *), &consumed);
        else if (d == LEN_J) sc = scan_value(src, spec, &returned, va_arg(*args, intmax_t *), &consumed);
        else if (d == LEN_Z) sc = scan_value(src, spec, &returned, va_arg(*args, size_t *), &consumed);
        else if (d == LEN_T) sc = scan_value(src, spec, &returned, va_arg(*args, ptrdiff_t *), &consumed);
        else sc = scan_value(src, spec, &returned, va_arg(*args, int *), &consumed);
    } else if (t == 'o' || t == 'u' || t == 'x' || t == 'X') {
        if (d == LEN_HH) sc = scan_value(src, spec, &returned, va_arg(*args, unsigned char *), &consumed);
        else if (d == LEN_H) sc = scan_value(src, spec, &returned, va_arg(*args, unsigned short *), &consumed);
        else if (d == LEN_L) sc = scan_value(src, spec, &returned, va_arg(*args, unsigned long *), &consumed);
        else if (d == LEN_LL) sc = scan_value(src, spec, &returned, va_arg(*args, unsigned long long *), &consumed);
        else if (d == LEN_J) sc = scan_value(src, spec, &returned, va_arg(*args, uintmax_t *), &consumed);
        else if (d == LEN_Z) sc = scan_value(src, spec, &returned, va_arg(*args, size_t *), &consumed);
        else if (d == LEN_T) sc = scan_value(src, spec, &returned, va_arg(*args, ptrdiff_t *), &consumed);
        else sc = scan_value(src, spec, &returned, va_arg(*args, unsigned int *), &consumed);
    } else if (t == 'c' || t == 's' || t == '[') {
        /* строка или символы: char * (с модификатором l - wchar_t *).
           Места в массиве должно хватать - как у стандартной функции, поэтому %s без
           ширины опасен: пишите %99s для массива из 100 символов */
        if (d == LEN_L) sc = scan_value(src, spec, &returned, va_arg(*args, wchar_t *), &consumed);
        else sc = scan_value(src, spec, &returned, va_arg(*args, char *), &consumed);
    } else if (t == 'p') {
        sc = scan_value(src, spec, &returned, va_arg(*args, void **), &consumed);
    } else {
        /* вещественные: float * (без модификатора), double * (l), long double * (L) */
        if (d == LEN_L) sc = scan_value(src, spec, &returned, va_arg(*args, double *), &consumed);
        else if (d == LEN_BIG_L) sc = scan_value(src, spec, &returned, va_arg(*args, long double *), &consumed);
        else sc = scan_value(src, spec, &returned, va_arg(*args, float *), &consumed);
    }
    free(spec);
    if (sc != OK) {
        return sc;
    }
    if (consumed < 0) {
        /* до %n стандартная функция не дошла */
        return (returned == EOF) ? INPUT_END : MATCH_FAILURE;
    }
    /* сдвигаем позицию: из строки - на прочитанное (из файла стандартная функция
       уже сдвинула поток сама), и считаем символы для нашего %n */
    if (src->stream == NULL) {
        src->pos += (size_t)consumed;
    }
    src->count += (size_t)consumed;
    *assigned = !sp->no_assign;
    return OK;
}

/* ===================== общее ядро ===================== */

/* Общее ядро overfscanf и oversscanf. Идет по формату:
   - пробельный символ формата - пропустить во вводе любые пробелы (и ни одного тоже можно);
   - обычный символ - во вводе должен быть ровно он, иначе стоп;
   - '%' и новый флаг - scan_custom; иначе стандартный - parse_spec + scan_standard.
   *item_count - сколько значений записано в переменные.
   Возвращает OK, если дошли до конца формата, INPUT_END / MATCH_FAILURE - где-то
   остановились, INVALID_FORMAT / INVALID_ARGS / ALLOC_ERROR - ошибка */
status_code over_vscanf(source *src, const char *format, va_list *args, int *item_count) {
    if (src == NULL || format == NULL || args == NULL || item_count == NULL) {
        return INVALID_ARGS;
    }
    *item_count = 0;
    size_t i = 0;
    status_code sc = OK;
    while (format[i] != '\0' && sc == OK) {
        unsigned char f = (unsigned char)format[i];
        if (isspace(f)) {
            /* пробел в формате: пропустить пробелы во вводе. Первый не пробел -
               вернуть (он для следующего шага) */
            int c = EOF;
            sc = skip_spaces(src, &c);
            unget_char(src, c);
            i++;
            continue;
        }
        if (f != '%') {
            /* обычный символ формата должен совпасть с символом ввода */
            int c = EOF;
            sc = get_char(src, &c);
            if (sc == OK && c == EOF) {
                sc = INPUT_END;
            } else if (sc == OK && c != f) {
                unget_char(src, c);
                sc = MATCH_FAILURE;
            }
            i++;
            continue;
        }
        /* '%': сначала новые флаги - две буквы сразу после '%' или после "%*" */
        const int star = (format[i + 1] == '*');
        const char *which = NULL;
        custom_flag(format + i + 1 + star, &which);
        if (which != NULL) {
            sc = scan_custom(src, which, star, args);
            if (sc == OK && !star) {
                (*item_count)++;
            }
            i += 3 + star;
            continue;
        }
        /* стандартный спецификатор */
        spec_info sp;
        size_t spec_length = 0;
        sc = parse_spec(format + i + 1, &sp, &spec_length);
        if (sc == OK) {
            int assigned = 0;
            sc = scan_standard(src, &sp, args, &assigned);
            if (sc == OK) {
                *item_count += assigned;
            }
            i += 1 + spec_length;
        }
    }
    return sc;
}

/* Что вернуть из overfscanf/oversscanf по итогам ядра (как fscanf):
   - ошибка параметров или формата, нехватка памяти - EOF;
   - ввод кончился, а ни одного значения не записано - EOF (так делают стандартные
     библиотеки и Linux (glibc), и Windows: даже если перед этим было "%*d");
   - иначе - сколько значений записано (0 - символы не подошли сразу) */
status_code final_result(const status_code sc, const int item_count, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    if (sc == INVALID_ARGS || sc == INVALID_FORMAT || sc == ALLOC_ERROR) {
        *result = EOF;
    } else if (sc == INPUT_END && item_count == 0) {
        *result = EOF;
    } else {
        *result = item_count;
    }
    return OK;
}

/* ===================== overfscanf и oversscanf ===================== */

/* Как fscanf: читает из потока stream по формату format.
   Возвращает, сколько значений прочитано и записано, или EOF (-1). Прототип int - по условию */
int overfscanf(FILE *stream, const char *format, ...) {
    if (stream == NULL || format == NULL) {
        return EOF;
    }
    source src = {stream, NULL, 0, 0};
    int item_count = 0;
    va_list args;
    va_start(args, format);
    status_code sc = over_vscanf(&src, format, &args, &item_count);
    va_end(args);
    int result = EOF;
    final_result(sc, item_count, &result);
    return result;
}

/* Как sscanf: читает из строки str по формату format. Конец строки - как конец файла */
int oversscanf(const char *str, const char *format, ...) {
    if (str == NULL || format == NULL) {
        return EOF;
    }
    source src = {NULL, str, 0, 0};
    int item_count = 0;
    va_list args;
    va_start(args, format);
    status_code sc = over_vscanf(&src, format, &args, &item_count);
    va_end(args);
    int result = EOF;
    final_result(sc, item_count, &result);
    return result;
}

/* ===================== демонстрация ===================== */

/* Демонстрация: строки разбираются oversscanf, потом файл - overfscanf.
   Перед каждым вызовом переменные заполняем "мусором" (-1, 777): так видно, что
   функция записала, а что нет. Строки здесь - готовые данные примеров */
status_code run_demo(void) {
    int a = -1;
    int b = -1;
    int c = -1;
    unsigned int z = 777;
    int r = 0;

    printf("oversscanf, new flags:\n");
    r = oversscanf("MCMXCIV", "%Ro", &a);
    printf("  \"MCMXCIV\" %%Ro -> %d  (returned %d)\n", a, r);
    r = oversscanf("  XIV -XL N", "%Ro %Ro %Ro", &a, &b, &c);
    printf("  \"  XIV -XL N\" %%Ro %%Ro %%Ro -> %d %d %d  (returned %d)\n", a, b, c, r);
    r = oversscanf("MMMCMXCIX", "%Ro", &a);
    printf("  \"MMMCMXCIX\" %%Ro -> %d  (returned %d)\n", a, r);
    r = oversscanf("010011", "%Zr", &z);
    printf("  \"010011\" %%Zr -> %u  (returned %d)\n", z, r);
    unsigned int z1 = 777;
    unsigned int z2 = 777;
    unsigned int z3 = 777;
    r = oversscanf("11 011 0011 1011", "%Zr %Zr %Zr", &z1, &z2, &z3);
    printf("  \"11 011 0011 1011\" %%Zr %%Zr %%Zr -> %u %u %u  (returned %d)\n", z1, z2, z3, r);
    r = oversscanf("11 011 0011 1011", "%*Zr %*Zr %*Zr %Zr", &z);
    printf("  same with %%*Zr %%*Zr %%*Zr %%Zr -> %u  (returned %d)\n", z, r);
    r = oversscanf("01", "%Zr", &z);
    printf("  \"01\" %%Zr -> %u  (returned %d)\n", z, r);
    r = oversscanf("00100100100010000000100010100010101000010001011", "%Zr", &z);
    printf("  UINT_MAX in Zeckendorf %%Zr -> %u  (returned %d)\n", z, r);
    r = oversscanf("ff", "%Cv", &a, 16);
    printf("  \"ff\" %%Cv 16 -> %d  (returned %d)\n", a, r);
    r = oversscanf("FF", "%CV", &a, 16);
    printf("  \"FF\" %%CV 16 -> %d  (returned %d)\n", a, r);
    r = oversscanf("-1010 zz", "%Cv %Cv", &a, 2, &b, 36);
    printf("  \"-1010 zz\" %%Cv 2, %%Cv 36 -> %d %d  (returned %d)\n", a, b, r);
    r = oversscanf("123", "%Cv", &a, 99);
    printf("  \"123\" %%Cv base 99 -> 10: %d  (returned %d)\n", a, r);
    r = oversscanf("ffz", "%Cv", &a, 16);
    printf("  \"ffz\" %%Cv 16 -> %d (stops before z)  (returned %d)\n", a, r);

    printf("\noversscanf, new and standard flags together:\n");
    char word[16] = {0};
    double d = 0.0;
    a = b = c = -1;
    r = oversscanf("year MMXXIV: 42 3.5 hello", "year %Ro: %d %lf %15s", &a, &b, &d, word);
    printf("  year %%Ro: %%d %%lf %%15s -> %d %d %g \"%s\"  (returned %d)\n", a, b, d, word, r);
    int n1 = -1;
    a = b = -1;
    r = oversscanf("XII,0x1F rest", "%Ro,%i%n", &a, &b, &n1);
    printf("  %%Ro,%%i%%n on \"XII,0x1F rest\" -> %d %d, %%n = %d  (returned %d)\n", a, b, n1, r);
    char scanset[16] = {0};
    a = -1;
    r = oversscanf("abc123 XX", "%15[a-z]%*d %Ro", scanset, &a);
    printf("  %%15[a-z]%%*d %%Ro on \"abc123 XX\" -> \"%s\" %d  (returned %d)\n", scanset, a, r);
    char chars[4] = {0};
    a = -1;
    r = oversscanf("  xyMCM", " %2c%Ro", chars, &a);
    printf("  \" %%2c%%Ro\" on \"  xyMCM\" -> \"%s\" %d  (returned %d)\n", chars, a, r);
    long long big = 0;
    unsigned short us = 0;
    r = oversscanf("-9000000000 65535 50%", "%lld %hu %d%%", &big, &us, &b);
    printf("  %%lld %%hu %%d%%%% -> %lld %u %d  (returned %d)\n", big, (unsigned)us, b, r);

    printf("\nStops and EOF:\n");
    a = b = -1;
    r = oversscanf("XIV abc", "%Ro %Ro", &a, &b);
    printf("  \"XIV abc\" %%Ro %%Ro -> %d %d  (returned %d: abc is not Roman)\n", a, b, r);
    a = -1;
    r = oversscanf("xiv", "%Ro", &a);
    printf("  \"xiv\" %%Ro -> %d  (returned %d: small letters are not Roman)\n", a, r);
    a = -1;
    r = oversscanf("FF", "%Cv", &a, 16);
    printf("  \"FF\" %%Cv 16 -> %d  (returned %d: %%Cv needs small letters)\n", a, r);
    a = -1;
    r = oversscanf("80000000", "%Cv", &a, 16);
    printf("  \"80000000\" %%Cv 16 -> %d  (returned %d: more than INT_MAX)\n", a, r);
    z = 777;
    r = oversscanf("0100", "%Zr", &z);
    printf("  \"0100\" %%Zr -> %u  (returned %d: no 1 at the end)\n", z, r);
    a = -1;
    r = oversscanf("", "%Ro", &a);
    printf("  \"\" %%Ro -> %d  (returned %d = EOF)\n", a, r);
    r = oversscanf("   ", "%d", &a);
    printf("  \"   \" %%d -> returned %d = EOF\n", r);
    r = oversscanf("12", "%d %Ro", &a, &b);
    printf("  \"12\" %%d %%Ro -> %d  (returned %d: input ended after one value)\n", a, r);
    r = oversscanf("12", "%y", &a);
    printf("  wrong format %%y -> returned %d\n", r);
    r = oversscanf(NULL, "%d", &a);
    printf("  oversscanf(NULL, ...) -> returned %d\n", r);

    /* overfscanf - из файла. Программу запускают из папки лабораторной (как задание 3) */
    printf("\noverfscanf from tests/data/task6_input.txt:\n");
    FILE *f = fopen("tests/data/task6_input.txt", "r");
    if (f == NULL) {
        printf("  Error: can not open tests/data/task6_input.txt\n");
        return OK;
    }
    char name[32] = {0};
    a = b = c = -1;
    z = 777;
    r = overfscanf(f, "%31s %Ro %Zr %Cv %CV", name, &a, &z, &b, 16, &c, 36);
    printf("  %%31s %%Ro %%Zr %%Cv 16 %%CV 36 -> \"%s\" %d %u %d %d  (returned %d)\n", name, a, z, b, c, r);
    /* дальше по файлу: строки "name roman" до конца */
    while ((r = overfscanf(f, "%31s %Ro", name, &a)) == 2) {
        printf("  %%31s %%Ro -> \"%s\" %d\n", name, a);
    }
    printf("  last call returned %d (EOF = end of file)\n", r);
    fclose(f);
    return OK;
}

/* ===================== один флаг из стандартного ввода ===================== */

/* Основание из командной строки: целое (int), разбираем вручную. Любое int можно:
   вне [2..36] функция сама возьмет 10 */
status_code parse_int(const char *str, int *result) {
    if (str == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    int minus = 0;
    if (str[0] == '-') {
        minus = 1;
        i = 1;
    }
    if (str[i] == '\0') {
        return MATCH_FAILURE;
    }
    const long long limit = minus ? (long long)INT_MAX + 1 : (long long)INT_MAX;
    long long acc = 0;
    for (; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return MATCH_FAILURE;
        }
        int d = str[i] - '0';
        if (acc > (limit - d) / 10) {
            return MATCH_FAILURE;
        }
        acc = acc * 10 + d;
    }
    *result = (int)(minus ? -acc : acc);
    return OK;
}

/* Режим с аргументами: читать из stdin значения одного флага, пока читаются.
   Каждое значение печатается отдельной строкой. Конец ввода (EOF) - нормальный
   конец, код 0; неподходящие символы - сообщение и код 1 */
status_code run_stdin(const char *flag, const int base, int *exit_code) {
    if (flag == NULL || exit_code == NULL) {
        return INVALID_ARGS;
    }
    char format[4] = {'%', flag[0], flag[1], '\0'};
    int r = 0;
    int item_count = 0;
    *exit_code = 0;
    while (1) {
        if (flag[0] == 'Z') {
            unsigned int z = 0;
            r = overfscanf(stdin, format, &z);
            if (r == 1) {
                printf("%s: %u\n", format, z);
            }
        } else if (flag[0] == 'C') {
            int x = 0;
            r = overfscanf(stdin, format, &x, base);
            if (r == 1) {
                printf("%s: %d\n", format, x);
            }
        } else {
            int x = 0;
            r = overfscanf(stdin, format, &x);
            if (r == 1) {
                printf("%s: %d\n", format, x);
            }
        }
        if (r != 1) {
            break;
        }
        item_count++;
    }
    printf("values read: %d\n", item_count);
    if (r != EOF) {
        /* остановились не на конце ввода: дальше символы, которые не подходят */
        fprintf(stderr, "Error: the input does not match %s\n", format);
        *exit_code = 1;
    }
    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    /* Без аргументов - демонстрация */
    if (argc == 1) {
        run_demo();
        return 0;
    }
    /* С аргументами: флаг (Ro, Zr, Cv, CV) и для Cv/CV - основание */
    const char *which = NULL;
    custom_flag(argv[1], &which);
    if (argc > 3 || which == NULL || argv[1][2] != '\0') {
        fprintf(stderr, "Usage: lab2_task6                 (demo)\n");
        fprintf(stderr, "       lab2_task6 Ro | Zr         (values from standard input)\n");
        fprintf(stderr, "       lab2_task6 Cv | CV <base>\n");
        return 1;
    }
    const int needs_base = (which[0] == 'C');
    if (needs_base != (argc == 3)) {
        fprintf(stderr, needs_base ? "Error: flag %s needs a base\n"
                                   : "Error: flag %s does not need a base\n", which);
        return 1;
    }
    int base = 10;
    if (needs_base && parse_int(argv[2], &base) != OK) {
        fprintf(stderr, "Error: base '%s' must be a whole number (int)\n", argv[2]);
        return 1;
    }
    int exit_code = 0;
    if (run_stdin(which, base, &exit_code) != OK) {
        /* INVALID_ARGS быть не может: which и exit_code не NULL - ошибка в программе */
        fprintf(stderr, "Error: internal error\n");
        return 1;
    }
    return exit_code;
}
