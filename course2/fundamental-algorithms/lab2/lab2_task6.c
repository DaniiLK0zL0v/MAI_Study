/*
 * Лаб 2, задание 6.
 * overfscanf / oversscanf — расширенные fscanf/sscanf.
 * Дополнительные флаги: %Ro, %Zr, %Cv, %CV.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    OK,
    INVALID_ARGS,
    ALLOC_ERROR,
    INVALID_DATA
} status_code;

/* ========================= вспомогательные ========================= */

/* Римские цифры -> int */
static int roman_char_value(char c) {
    switch (c) {
        case 'I': case 'i': return 1;
        case 'V': case 'v': return 5;
        case 'X': case 'x': return 10;
        case 'L': case 'l': return 50;
        case 'C': case 'c': return 100;
        case 'D': case 'd': return 500;
        case 'M': case 'm': return 1000;
        default: return 0;
    }
}

static int is_roman_char(char c) {
    return roman_char_value(c) != 0;
}

/* Читаем римское число из строки (указатель сдвигается) */
static int parse_roman(const char **sp, int *result) {
    const char *s = *sp;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }

    if (!is_roman_char(*s)) return -1;

    int total = 0;
    while (is_roman_char(*s)) {
        int cur = roman_char_value(*s);
        int next = roman_char_value(*(s + 1));
        if (next > cur) {
            total += next - cur;
            s += 2;
        } else {
            total += cur;
            s++;
        }
    }

    *result = neg ? -total : total;
    *sp = s;
    return 0;
}

/* Читаем Zeckendorf из строки: последовательность 0/1, заканчивающаяся '1' */
static int parse_zeckendorf(const char **sp, unsigned int *result) {
    const char *s = *sp;
    /* Считываем последовательность 0/1 */
    int coeffs[64];
    int count = 0;
    while (*s == '0' || *s == '1') {
        coeffs[count++] = *s - '0';
        s++;
        if (count >= 64) break;
    }

    if (count < 2) return -1;
    /* Последний символ должен быть 1 (завершающая единица) */
    if (coeffs[count - 1] != 1) return -1;

    /* Числа Фибоначчи */
    unsigned int fibs[64];
    fibs[0] = 1;
    fibs[1] = 2;
    for (int i = 2; i < count - 1; i++) {
        fibs[i] = fibs[i - 1] + fibs[i - 2];
    }

    unsigned int sum = 0;
    /* коэффициенты от младшего к старшему, без последней единицы */
    for (int i = 0; i < count - 1; i++) {
        if (coeffs[i]) sum += fibs[i];
    }

    *result = sum;
    *sp = s;
    return 0;
}

/* Считываем число в заданной системе счисления из строки.
 * lower = 1: буквы в нижнем регистре, lower = 0: верхний регистр */
static int parse_base_number(const char **sp, int base, int lower, int *result) {
    if (base < 2 || base > 36) base = 10;
    const char *s = *sp;

    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    int got_digit = 0;
    long long acc = 0;
    while (*s != '\0') {
        int digit = -1;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (lower && *s >= 'a' && *s <= 'z') {
            digit = 10 + (*s - 'a');
        } else if (!lower && *s >= 'A' && *s <= 'Z') {
            digit = 10 + (*s - 'A');
        }

        if (digit < 0 || digit >= base) break;
        acc = acc * base + digit;
        got_digit = 1;
        s++;
    }

    if (!got_digit) return -1;
    *result = (int)(neg ? -acc : acc);
    *sp = s;
    return 0;
}

/* ========================= overfscanf ========================= */

int overfscanf(FILE *stream, const char *format, ...) {
    if (stream == NULL || format == NULL) return -1;

    va_list args;
    va_start(args, format);

    int matched = 0;
    size_t fi = 0;
    size_t fmtlen = strlen(format);

    while (fi < fmtlen) {
        if (format[fi] == '%') {
            /* Проверяем наши специальные флаги */
            char c1 = (fi + 1 < fmtlen) ? format[fi + 1] : '\0';
            char c2 = (fi + 2 < fmtlen) ? format[fi + 2] : '\0';

            if (c1 == 'R' && c2 == 'o') {
                /* Roman numeral */
                int *ptr = va_arg(args, int *);
                /* Читаем слово из потока */
                char word[256];
                if (fscanf(stream, "%255s", word) != 1) break;
                const char *wp = word;
                if (parse_roman(&wp, ptr) != 0) break;
                matched++;
                fi += 3;
                continue;
            }
            if (c1 == 'Z' && c2 == 'r') {
                /* Zeckendorf */
                unsigned int *ptr = va_arg(args, unsigned int *);
                char word[256];
                if (fscanf(stream, "%255s", word) != 1) break;
                const char *wp = word;
                if (parse_zeckendorf(&wp, ptr) != 0) break;
                matched++;
                fi += 3;
                continue;
            }
            if (c1 == 'C' && (c2 == 'v' || c2 == 'V')) {
                int *ptr = va_arg(args, int *);
                int base = va_arg(args, int);
                char word[256];
                if (fscanf(stream, "%255s", word) != 1) break;
                const char *wp = word;
                if (parse_base_number(&wp, base, c2 == 'v' ? 1 : 0, ptr) != 0) break;
                matched++;
                fi += 3;
                continue;
            }

            /* Стандартный спецификатор: собираем и передаём в fscanf */
            size_t spec_start = fi;
            fi++; /* '%' */
            /* Пропускаем модификаторы */
            while (fi < fmtlen && !isalpha((unsigned char)format[fi]) && format[fi] != '%' && format[fi] != '[') {
                fi++;
            }
            if (fi < fmtlen) fi++; /* терминатор */

            size_t spec_len = fi - spec_start;
            char spec[64];
            if (spec_len >= sizeof(spec)) spec_len = sizeof(spec) - 1;
            memcpy(spec, format + spec_start, spec_len);
            spec[spec_len] = '\0';

            char term = spec[spec_len - 1];
            if (term == '%') {
                /* литеральный % */
                int ch = fgetc(stream);
                while (ch != EOF && isspace(ch)) ch = fgetc(stream);
                if (ch != '%') { if (ch != EOF) ungetc(ch, stream); break; }
            } else {
                void *ptr = va_arg(args, void *);
                int r = fscanf(stream, spec, ptr);
                if (r < 1) break;
                matched++;
            }
        } else if (isspace((unsigned char)format[fi])) {
            /* Пробельный символ в формате — пропускаем пробелы в потоке */
            int ch;
            while ((ch = fgetc(stream)) != EOF && isspace(ch)) {}
            if (ch != EOF) ungetc(ch, stream);
            fi++;
        } else {
            /* Литеральный символ */
            int ch = fgetc(stream);
            if (ch == EOF || ch != (unsigned char)format[fi]) {
                if (ch != EOF) ungetc(ch, stream);
                break;
            }
            fi++;
        }
    }

    va_end(args);
    return matched;
}

/* ========================= oversscanf ========================= */

/* Для oversscanf оборачиваем строку через fmemopen / tmpfile */
int oversscanf(const char *str, const char *format, ...) {
    if (str == NULL || format == NULL) return -1;

    /* Создаём временный файл и записываем в него строку */
    FILE *tmp = tmpfile();
    if (tmp == NULL) return -1;
    fputs(str, tmp);
    rewind(tmp);

    va_list args;
    va_start(args, format);

    int matched = 0;
    size_t fi = 0;
    size_t fmtlen = strlen(format);

    while (fi < fmtlen) {
        if (format[fi] == '%') {
            char c1 = (fi + 1 < fmtlen) ? format[fi + 1] : '\0';
            char c2 = (fi + 2 < fmtlen) ? format[fi + 2] : '\0';

            if (c1 == 'R' && c2 == 'o') {
                int *ptr = va_arg(args, int *);
                char word[256];
                if (fscanf(tmp, "%255s", word) != 1) break;
                const char *wp = word;
                if (parse_roman(&wp, ptr) != 0) break;
                matched++;
                fi += 3;
                continue;
            }
            if (c1 == 'Z' && c2 == 'r') {
                unsigned int *ptr = va_arg(args, unsigned int *);
                char word[256];
                if (fscanf(tmp, "%255s", word) != 1) break;
                const char *wp = word;
                if (parse_zeckendorf(&wp, ptr) != 0) break;
                matched++;
                fi += 3;
                continue;
            }
            if (c1 == 'C' && (c2 == 'v' || c2 == 'V')) {
                int *ptr = va_arg(args, int *);
                int base = va_arg(args, int);
                char word[256];
                if (fscanf(tmp, "%255s", word) != 1) break;
                const char *wp = word;
                if (parse_base_number(&wp, base, c2 == 'v' ? 1 : 0, ptr) != 0) break;
                matched++;
                fi += 3;
                continue;
            }

            size_t spec_start = fi;
            fi++;
            while (fi < fmtlen && !isalpha((unsigned char)format[fi]) && format[fi] != '%' && format[fi] != '[') {
                fi++;
            }
            if (fi < fmtlen) fi++;

            size_t spec_len = fi - spec_start;
            char spec[64];
            if (spec_len >= sizeof(spec)) spec_len = sizeof(spec) - 1;
            memcpy(spec, format + spec_start, spec_len);
            spec[spec_len] = '\0';

            char term = spec[spec_len - 1];
            if (term == '%') {
                int ch = fgetc(tmp);
                while (ch != EOF && isspace(ch)) ch = fgetc(tmp);
                if (ch != '%') { if (ch != EOF) ungetc(ch, tmp); break; }
            } else {
                void *ptr = va_arg(args, void *);
                int r = fscanf(tmp, spec, ptr);
                if (r < 1) break;
                matched++;
            }
        } else if (isspace((unsigned char)format[fi])) {
            int ch;
            while ((ch = fgetc(tmp)) != EOF && isspace(ch)) {}
            if (ch != EOF) ungetc(ch, tmp);
            fi++;
        } else {
            int ch = fgetc(tmp);
            if (ch == EOF || ch != (unsigned char)format[fi]) {
                if (ch != EOF) ungetc(ch, tmp);
                break;
            }
            fi++;
        }
    }

    va_end(args);
    fclose(tmp);
    return matched;
}

/* ========================= main (демонстрация) ========================= */

int main(void) {
    printf("=== oversscanf demo ===\n");

    /* %Ro: Roman */
    int roman_val;
    int r = oversscanf("XLII", "%Ro", &roman_val);
    printf("Read Roman 'XLII': %d (matched=%d)\n", roman_val, r);

    r = oversscanf("MMXXIV", "%Ro", &roman_val);
    printf("Read Roman 'MMXXIV': %d (matched=%d)\n", roman_val, r);

    /* %Zr: Zeckendorf */
    unsigned int zeck_val;
    r = oversscanf("01001001", "%Zr", &zeck_val);
    /* 01001001 -> коэфф.: 0,1,0,0,1,0,0 при Fib 1,2,3,5,8,13,21 -> 2+8=10, последняя 1 - маркер */
    printf("Read Zeckendorf '01001001': %u (matched=%d)\n", zeck_val, r);

    /* %Cv: число в системе счисления, нижний регистр */
    int base_val;
    r = oversscanf("ff", "%Cv", &base_val, 16);
    printf("Read 'ff' base 16 (lower): %d (matched=%d)\n", base_val, r);

    /* %CV: верхний регистр */
    r = oversscanf("FF", "%CV", &base_val, 16);
    printf("Read 'FF' base 16 (upper): %d (matched=%d)\n", base_val, r);

    /* Комбинация стандартных и пользовательских */
    int x;
    r = oversscanf("42 XIV", "%d %Ro", &x, &roman_val);
    printf("Read '42 XIV': x=%d, roman=%d (matched=%d)\n", x, roman_val, r);

    printf("\n=== overfscanf demo (stdin) ===\n");
    printf("Enter a Roman numeral: ");
    fflush(stdout);
    r = overfscanf(stdin, "%Ro", &roman_val);
    if (r >= 1) {
        printf("Parsed: %d\n", roman_val);
    } else {
        printf("Could not parse.\n");
    }

    return 0;
}
