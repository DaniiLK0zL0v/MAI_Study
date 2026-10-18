/*
 * Лаб 2, задание 5.
 * overfprintf / oversprintf — расширенные fprintf/sprintf.
 * Дополнительные флаги: %Ro, %Zr, %Cv, %CV, %to, %TO, %mi, %mu, %md, %mf.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef enum {
    OK,
    INVALID_ARGS,
    ALLOC_ERROR,
    INVALID_DATA
} status_code;

/* ========================= вспомогательные ========================= */

/* int -> римские цифры. Записывает в buf (буфер должен быть достаточного размера).
 * Работает для чисел 1..3999, для отрицательных ставит минус. Для 0 — пустая строка. */
static int int_to_roman(int value, char *buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) return -1;
    buf[0] = '\0';
    if (value == 0) return 0;

    size_t pos = 0;
    if (value < 0) {
        if (pos + 1 >= buf_size) return -1;
        buf[pos++] = '-';
        buf[pos] = '\0';
        value = -value;
    }

    static const int vals[] =    {1000, 900, 500, 400, 100,  90,  50,  40,  10,   9,   5,   4,   1};
    static const char *syms[] =  {"M", "CM","D", "CD","C", "XC","L", "XL","X", "IX","V", "IV","I"};
    int n = 13;

    for (int i = 0; i < n; i++) {
        while (value >= vals[i]) {
            size_t slen = strlen(syms[i]);
            if (pos + slen >= buf_size) return -1;
            for (size_t j = 0; j < slen; j++) {
                buf[pos++] = syms[i][j];
            }
            buf[pos] = '\0';
            value -= vals[i];
        }
    }
    return 0;
}

/* unsigned int -> Zeckendorf representation.
 * Коэффициенты при числах Фибоначчи от младшего к старшему + 1 в конце. */
static int uint_to_zeckendorf(unsigned int value, char *buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) return -1;

    if (value == 0) {
        if (buf_size < 3) return -1;
        buf[0] = '0';
        buf[1] = '1'; /* завершающая единица */
        buf[2] = '\0';
        return 0;
    }

    /* Находим числа Фибоначчи до value */
    unsigned int fibs[48]; /* достаточно для unsigned int */
    int fib_count = 0;
    fibs[0] = 1;
    fibs[1] = 2;
    fib_count = 2;
    while (1) {
        unsigned int next = fibs[fib_count - 1] + fibs[fib_count - 2];
        if (next > value) break;
        fibs[fib_count++] = next;
        if (fib_count >= 48) break;
    }

    /* Жадный алгоритм: от старшего Фибоначчи к младшему */
    int *coeffs = (int *)calloc((size_t)fib_count, sizeof(int));
    if (coeffs == NULL) return -1;

    unsigned int rem = value;
    for (int i = fib_count - 1; i >= 0 && rem > 0; i--) {
        if (fibs[i] <= rem) {
            coeffs[i] = 1;
            rem -= fibs[i];
        }
    }

    /* Записываем от младшего к старшему + 1 в конце */
    size_t pos = 0;
    for (int i = 0; i < fib_count; i++) {
        if (pos + 1 >= buf_size) { free(coeffs); return -1; }
        buf[pos++] = (char)('0' + coeffs[i]);
    }
    if (pos + 1 >= buf_size) { free(coeffs); return -1; }
    buf[pos++] = '1'; /* завершающая единица */
    buf[pos] = '\0';

    free(coeffs);
    return 0;
}

/* int -> строка в системе счисления base, нижний/верхний регистр букв */
static int int_to_base(int value, int base, int upper, char *buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) return -1;
    if (base < 2 || base > 36) base = 10;

    if (value == 0) {
        if (buf_size < 2) return -1;
        buf[0] = '0';
        buf[1] = '\0';
        return 0;
    }

    char tmp[68]; /* достаточно для любого int в base 2 */
    int pos = 0;
    int neg = 0;
    unsigned int uval;

    if (value < 0) {
        neg = 1;
        /* Безопасное получение модуля: для INT_MIN */
        uval = (unsigned int)(-(value + 1)) + 1u;
    } else {
        uval = (unsigned int)value;
    }

    while (uval > 0) {
        unsigned int digit = uval % (unsigned int)base;
        if (digit < 10) {
            tmp[pos++] = (char)('0' + digit);
        } else {
            tmp[pos++] = (char)((upper ? 'A' : 'a') + (int)(digit - 10));
        }
        uval /= (unsigned int)base;
    }

    size_t need = (size_t)pos + (neg ? 1u : 0u) + 1u;
    if (need > buf_size) return -1;

    size_t bpos = 0;
    if (neg) buf[bpos++] = '-';
    for (int i = pos - 1; i >= 0; i--) {
        buf[bpos++] = tmp[i];
    }
    buf[bpos] = '\0';
    return 0;
}

/* Строка (base) -> int (decimal).  Буквы в нижнем/верхнем регистре. */
static int base_str_to_int(const char *str, int base, int upper, int *result) {
    if (str == NULL || result == NULL) return -1;
    if (base < 2 || base > 36) base = 10;

    int neg = 0;
    size_t i = 0;
    if (str[0] == '-') { neg = 1; i = 1; }
    else if (str[0] == '+') { i = 1; }

    long long acc = 0;
    for (; str[i] != '\0'; i++) {
        int digit;
        char c = str[i];
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (upper && c >= 'A' && c <= 'Z') {
            digit = 10 + (c - 'A');
        } else if (!upper && c >= 'a' && c <= 'z') {
            digit = 10 + (c - 'a');
        } else {
            return -1;
        }
        if (digit >= base) return -1;
        acc = acc * base + digit;
    }

    *result = (int)(neg ? -acc : acc);
    return 0;
}

/* Memory dump: байты -> двоичное представление */
static int memory_dump(const void *ptr, size_t size, char *buf, size_t buf_size) {
    if (ptr == NULL || buf == NULL || size == 0) return -1;
    /* Каждый байт = 8 бит + пробел-разделитель, кроме последнего */
    size_t need = size * 9; /* 8 бит + пробел или '\0' */
    if (need > buf_size) return -1;

    const unsigned char *bytes = (const unsigned char *)ptr;
    size_t pos = 0;
    for (size_t i = 0; i < size; i++) {
        if (i > 0) buf[pos++] = ' ';
        for (int bit = 7; bit >= 0; bit--) {
            buf[pos++] = (bytes[i] >> bit) & 1 ? '1' : '0';
        }
    }
    buf[pos] = '\0';
    return 0;
}

/* ========================= overfprintf ========================= */

int overfprintf(FILE *stream, const char *format, ...) {
    if (stream == NULL || format == NULL) return -1;

    va_list args;
    va_start(args, format);

    int total = 0;
    size_t i = 0;
    size_t fmtlen = strlen(format);

    while (i < fmtlen) {
        if (format[i] == '%' && i + 1 < fmtlen) {
            /* Проверяем наши специальные флаги (2-символьные) */
            if (i + 2 <= fmtlen) {
                char c1 = format[i + 1];
                char c2 = (i + 2 < fmtlen) ? format[i + 2] : '\0';

                if (c1 == 'R' && c2 == 'o') {
                    /* Roman numeral */
                    int val = va_arg(args, int);
                    char roman[64];
                    int_to_roman(val, roman, sizeof(roman));
                    int w = fprintf(stream, "%s", roman);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'Z' && c2 == 'r') {
                    /* Zeckendorf */
                    unsigned int val = va_arg(args, unsigned int);
                    char zeck[128];
                    uint_to_zeckendorf(val, zeck, sizeof(zeck));
                    int w = fprintf(stream, "%s", zeck);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'C' && (c2 == 'v' || c2 == 'V')) {
                    /* int in base, lowercase/uppercase */
                    int val = va_arg(args, int);
                    int base = va_arg(args, int);
                    char conv[68];
                    int_to_base(val, base, c2 == 'V' ? 1 : 0, conv, sizeof(conv));
                    int w = fprintf(stream, "%s", conv);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 't' && c2 == 'o') {
                    /* string in base -> decimal, lowercase */
                    char *str = va_arg(args, char *);
                    int base = va_arg(args, int);
                    int result;
                    base_str_to_int(str, base, 0, &result);
                    int w = fprintf(stream, "%d", result);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'T' && c2 == 'O') {
                    /* string in base -> decimal, uppercase */
                    char *str = va_arg(args, char *);
                    int base = va_arg(args, int);
                    int result;
                    base_str_to_int(str, base, 1, &result);
                    int w = fprintf(stream, "%d", result);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'm' && (c2 == 'i' || c2 == 'u' || c2 == 'd' || c2 == 'f')) {
                    char dump[256];
                    if (c2 == 'i') {
                        int val = va_arg(args, int);
                        memory_dump(&val, sizeof(int), dump, sizeof(dump));
                    } else if (c2 == 'u') {
                        unsigned int val = va_arg(args, unsigned int);
                        memory_dump(&val, sizeof(unsigned int), dump, sizeof(dump));
                    } else if (c2 == 'd') {
                        double val = va_arg(args, double);
                        memory_dump(&val, sizeof(double), dump, sizeof(dump));
                    } else { /* 'f' */
                        /* float promoted to double in varargs */
                        double dval = va_arg(args, double);
                        float val = (float)dval;
                        memory_dump(&val, sizeof(float), dump, sizeof(dump));
                    }
                    int w = fprintf(stream, "%s", dump);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
            }

            /* Стандартный флаг: передаём как есть через vfprintf для одного спецификатора */
            /* Собираем спецификатор: от '%' до терминатора (d,i,u,x,X,o,s,c,f,e,g,p,n,%) */
            size_t spec_start = i;
            i++; /* пропускаем '%' */
            /* Пропускаем флаги, ширину, точность */
            while (i < fmtlen && !isalpha((unsigned char)format[i]) && format[i] != '%') {
                i++;
            }
            if (i < fmtlen) {
                i++; /* терминатор */
            }
            /* Копируем спецификатор */
            size_t spec_len = i - spec_start;
            char spec[64];
            if (spec_len >= sizeof(spec)) spec_len = sizeof(spec) - 1;
            memcpy(spec, format + spec_start, spec_len);
            spec[spec_len] = '\0';

            char term = spec[spec_len - 1];
            /* Переключаемся по терминатору */
            if (term == 'd' || term == 'i') {
                int val = va_arg(args, int);
                int w = fprintf(stream, spec, val);
                if (w > 0) total += w;
            } else if (term == 'u' || term == 'x' || term == 'X' || term == 'o') {
                unsigned int val = va_arg(args, unsigned int);
                int w = fprintf(stream, spec, val);
                if (w > 0) total += w;
            } else if (term == 'f' || term == 'e' || term == 'E' || term == 'g' || term == 'G') {
                double val = va_arg(args, double);
                int w = fprintf(stream, spec, val);
                if (w > 0) total += w;
            } else if (term == 's') {
                char *val = va_arg(args, char *);
                int w = fprintf(stream, spec, val);
                if (w > 0) total += w;
            } else if (term == 'c') {
                int val = va_arg(args, int);
                int w = fprintf(stream, spec, val);
                if (w > 0) total += w;
            } else if (term == 'p') {
                void *val = va_arg(args, void *);
                int w = fprintf(stream, spec, val);
                if (w > 0) total += w;
            } else if (term == '%') {
                int w = fprintf(stream, "%%");
                if (w > 0) total += w;
            } else if (term == 'l') {
                /* длинные типы: %ld, %lu, %lf, %lld и т.п. */
                /* нужно дочитать ещё один символ */
                if (i < fmtlen) {
                    char next = format[i];
                    i++;
                    spec[spec_len] = next;
                    spec[spec_len + 1] = '\0';
                    if (next == 'd' || next == 'i') {
                        long val = va_arg(args, long);
                        int w = fprintf(stream, spec, val);
                        if (w > 0) total += w;
                    } else if (next == 'u' || next == 'x' || next == 'X') {
                        unsigned long val = va_arg(args, unsigned long);
                        int w = fprintf(stream, spec, val);
                        if (w > 0) total += w;
                    } else if (next == 'f') {
                        double val = va_arg(args, double);
                        int w = fprintf(stream, spec, val);
                        if (w > 0) total += w;
                    }
                }
            } else {
                /* Неизвестный — просто печатаем как есть */
                int w = fprintf(stream, "%s", spec);
                if (w > 0) total += w;
            }
        } else {
            fputc(format[i], stream);
            total++;
            i++;
        }
    }

    va_end(args);
    return total;
}

/* ========================= oversprintf ========================= */

/* oversprintf — аналогично overfprintf, но пишет в строку buf.
 * Используем промежуточный файл tmpfile(). */
int oversprintf(char *buf, const char *format, ...) {
    if (buf == NULL || format == NULL) return -1;

    /* Собираем аргументы и перенаправляем через overfprintf в tmpfile */
    FILE *tmp = tmpfile();
    if (tmp == NULL) return -1;

    /* Нужно передать va_list — для этого дублируем логику.
     * Вместо дублирования используем sprintf + наши обработчики. */

    va_list args;
    va_start(args, format);

    int total = 0;
    size_t i = 0;
    size_t fmtlen = strlen(format);

    while (i < fmtlen) {
        if (format[i] == '%' && i + 1 < fmtlen) {
            if (i + 2 <= fmtlen) {
                char c1 = format[i + 1];
                char c2 = (i + 2 < fmtlen) ? format[i + 2] : '\0';

                if (c1 == 'R' && c2 == 'o') {
                    int val = va_arg(args, int);
                    char roman[64];
                    int_to_roman(val, roman, sizeof(roman));
                    int w = sprintf(buf + total, "%s", roman);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'Z' && c2 == 'r') {
                    unsigned int val = va_arg(args, unsigned int);
                    char zeck[128];
                    uint_to_zeckendorf(val, zeck, sizeof(zeck));
                    int w = sprintf(buf + total, "%s", zeck);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'C' && (c2 == 'v' || c2 == 'V')) {
                    int val = va_arg(args, int);
                    int base = va_arg(args, int);
                    char conv[68];
                    int_to_base(val, base, c2 == 'V' ? 1 : 0, conv, sizeof(conv));
                    int w = sprintf(buf + total, "%s", conv);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 't' && c2 == 'o') {
                    char *str = va_arg(args, char *);
                    int base = va_arg(args, int);
                    int result;
                    base_str_to_int(str, base, 0, &result);
                    int w = sprintf(buf + total, "%d", result);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'T' && c2 == 'O') {
                    char *str = va_arg(args, char *);
                    int base = va_arg(args, int);
                    int result;
                    base_str_to_int(str, base, 1, &result);
                    int w = sprintf(buf + total, "%d", result);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
                if (c1 == 'm' && (c2 == 'i' || c2 == 'u' || c2 == 'd' || c2 == 'f')) {
                    char dump[256];
                    if (c2 == 'i') {
                        int val = va_arg(args, int);
                        memory_dump(&val, sizeof(int), dump, sizeof(dump));
                    } else if (c2 == 'u') {
                        unsigned int val = va_arg(args, unsigned int);
                        memory_dump(&val, sizeof(unsigned int), dump, sizeof(dump));
                    } else if (c2 == 'd') {
                        double val = va_arg(args, double);
                        memory_dump(&val, sizeof(double), dump, sizeof(dump));
                    } else {
                        double dval = va_arg(args, double);
                        float val = (float)dval;
                        memory_dump(&val, sizeof(float), dump, sizeof(dump));
                    }
                    int w = sprintf(buf + total, "%s", dump);
                    if (w > 0) total += w;
                    i += 3;
                    continue;
                }
            }

            size_t spec_start = i;
            i++;
            while (i < fmtlen && !isalpha((unsigned char)format[i]) && format[i] != '%') {
                i++;
            }
            if (i < fmtlen) i++;

            size_t spec_len = i - spec_start;
            char spec[64];
            if (spec_len >= sizeof(spec)) spec_len = sizeof(spec) - 1;
            memcpy(spec, format + spec_start, spec_len);
            spec[spec_len] = '\0';

            char term = spec[spec_len - 1];
            if (term == 'd' || term == 'i') {
                int val = va_arg(args, int);
                int w = sprintf(buf + total, spec, val);
                if (w > 0) total += w;
            } else if (term == 'u' || term == 'x' || term == 'X' || term == 'o') {
                unsigned int val = va_arg(args, unsigned int);
                int w = sprintf(buf + total, spec, val);
                if (w > 0) total += w;
            } else if (term == 'f' || term == 'e' || term == 'E' || term == 'g' || term == 'G') {
                double val = va_arg(args, double);
                int w = sprintf(buf + total, spec, val);
                if (w > 0) total += w;
            } else if (term == 's') {
                char *val = va_arg(args, char *);
                int w = sprintf(buf + total, spec, val);
                if (w > 0) total += w;
            } else if (term == 'c') {
                int val = va_arg(args, int);
                int w = sprintf(buf + total, spec, val);
                if (w > 0) total += w;
            } else if (term == '%') {
                buf[total++] = '%';
                buf[total] = '\0';
            } else {
                int w = sprintf(buf + total, "%s", spec);
                if (w > 0) total += w;
            }
        } else {
            buf[total++] = format[i];
            buf[total] = '\0';
            i++;
        }
    }

    va_end(args);
    fclose(tmp);
    buf[total] = '\0';
    return total;
}

/* ========================= main (демонстрация) ========================= */

int main(void) {
    printf("=== overfprintf demo ===\n");

    /* %Ro: Roman numerals */
    overfprintf(stdout, "Roman 42 = %Ro\n", 42);
    overfprintf(stdout, "Roman 2024 = %Ro\n", 2024);
    overfprintf(stdout, "Roman -7 = %Ro\n", -7);

    /* %Zr: Zeckendorf */
    overfprintf(stdout, "Zeckendorf 10 = %Zr\n", 10u);
    overfprintf(stdout, "Zeckendorf 1 = %Zr\n", 1u);
    overfprintf(stdout, "Zeckendorf 0 = %Zr\n", 0u);

    /* %Cv: int in base, lowercase */
    overfprintf(stdout, "255 in base 16 (lower) = %Cv\n", 255, 16);
    overfprintf(stdout, "-255 in base 16 (lower) = %Cv\n", -255, 16);

    /* %CV: int in base, uppercase */
    overfprintf(stdout, "255 in base 16 (upper) = %CV\n", 255, 16);

    /* %to: string base -> decimal, lowercase */
    overfprintf(stdout, "ff (base 16, lower) = %to\n", "ff", 16);

    /* %TO: string base -> decimal, uppercase */
    overfprintf(stdout, "FF (base 16, upper) = %TO\n", "FF", 16);

    /* %mi: memory dump int */
    overfprintf(stdout, "Memory dump of int 1: %mi\n", 1);

    /* %mu: memory dump unsigned int */
    overfprintf(stdout, "Memory dump of uint 255: %mu\n", 255u);

    /* %md: memory dump double */
    overfprintf(stdout, "Memory dump of double 1.0: %md\n", 1.0);

    /* %mf: memory dump float */
    overfprintf(stdout, "Memory dump of float 1.0f: %mf\n", 1.0f);

    /* Стандартные флаги тоже работают */
    overfprintf(stdout, "Standard: %d, %s, %.2f\n", 42, "hello", 3.14);

    printf("\n=== oversprintf demo ===\n");
    char buf[1024];
    oversprintf(buf, "Roman %Ro, Zeck %Zr, Base16 %Cv, dec %d", 2024, 10u, 255, 16, 99);
    printf("Result: %s\n", buf);

    return 0;
}
