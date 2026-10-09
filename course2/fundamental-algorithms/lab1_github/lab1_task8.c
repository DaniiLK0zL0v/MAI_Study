#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL вместо указателя */
    INVALID_NUMBER,    /* в лексеме есть символ, который не цифра ни в какой системе */
    OVERFLOW_ERROR,    /* значение не помещается в long long */
    NO_MEMORY,
    READ_ERROR,
    WRITE_ERROR,
    END_OF_FILE        /* лексем больше нет */
} status_code;

/* Разделители: пробел, табуляция, перевод строки (и '\r' из Windows-файлов).
   ch - то, что вернул fgetc: байт от 0 до 255 или EOF. rez = 1 - разделитель */
status_code is_razdelitel(const int ch, int *rez) {
    if (rez == NULL || (ch < 0 && ch != EOF) || ch > UCHAR_MAX) {
        return INVALID_ARGS;
    }
    *rez = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
    return OK;
}

/* Цифра -> значение: '0'..'9' -> 0..9, 'a'/'A'..'z'/'Z' -> 10..35.
   Другой символ - INVALID_NUMBER.
   Большие и маленькие буквы одинаковые - так сказано в задании */
status_code digit_value(const char ch, int *val) {
    if (val == NULL) {
        return INVALID_ARGS;
    }
    if (ch >= '0' && ch <= '9') {
        *val = ch - '0';
        return OK;
    }
    char up = (char)toupper((unsigned char)ch);
    if (up >= 'A' && up <= 'Z') {
        *val = up - 'A' + 10;
        return OK;
    }
    return INVALID_NUMBER;
}

/* Прочитать одну лексему в динамический буфер.
   buf и razmer - буфер и его размер; если лексема длиннее - буфер увеличиваем (realloc).
   Буфер живет между вызовами, освобождает его main */
status_code read_token(FILE *in, char **buf, size_t *razmer) {
    if (in == NULL || buf == NULL || razmer == NULL) {
        return INVALID_ARGS;
    }
    /* пропускаем разделители (EOF - не разделитель, на нем цикл тоже кончится) */
    int ch = EOF;
    int razd = 1;
    while (razd) {
        ch = fgetc(in);
        if (is_razdelitel(ch, &razd) != OK) {
            return INVALID_ARGS;
        }
    }
    if (ch == EOF) {
        return ferror(in) ? READ_ERROR : END_OF_FILE;
    }

    /* копируем символы до разделителя или конца файла */
    size_t dlina = 0;
    while (ch != EOF) {
        if (is_razdelitel(ch, &razd) != OK) {
            return INVALID_ARGS;
        }
        if (razd) {
            break;
        }
        /* +1 - место под '\0' */
        if (dlina + 1 >= *razmer) {
            size_t new_razmer = (*razmer == 0) ? 16 : *razmer * 2;
            char *tmp = realloc(*buf, new_razmer);
            if (tmp == NULL) {
                return NO_MEMORY;   /* старый буфер не потерян - его освободит main */
            }
            *buf = tmp;
            *razmer = new_razmer;
        }
        (*buf)[dlina] = (char)ch;
        dlina++;
        ch = fgetc(in);
    }
    (*buf)[dlina] = '\0';
    return ferror(in) ? READ_ERROR : OK;
}

/* Разобрать число: знак, минимальное основание, значение в 10-й системе.
   nachalo - с какого символа идут значащие цифры (после знака и ведущих нулей) */
status_code analyze_number(const char *str, int *otric, size_t *nachalo, int *base,
                           long long *znachenie) {
    if (str == NULL || otric == NULL || nachalo == NULL || base == NULL || znachenie == NULL) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    *otric = 0;
    if (str[0] == '-' || str[0] == '+') {
        *otric = (str[0] == '-');
        i = 1;
    }
    if (str[i] == '\0') {
        return INVALID_NUMBER;   /* один знак без цифр */
    }

    /* 1) проверяем символы и ищем самую большую цифру */
    int max_cifra = 0;
    for (size_t j = i; str[j] != '\0'; j++) {
        int d = 0;
        if (digit_value(str[j], &d) != OK) {
            return INVALID_NUMBER;
        }
        if (d > max_cifra) {
            max_cifra = d;
        }
    }
    /* минимальное основание = самая большая цифра + 1, но не меньше 2 */
    *base = (max_cifra + 1 < 2) ? 2 : max_cifra + 1;

    /* 2) пропускаем ведущие нули (но число "000" - это "0") */
    while (str[i] == '0' && str[i + 1] != '\0') {
        i++;
    }
    *nachalo = i;
    if (str[i] == '0') {
        *otric = 0;   /* "-0" это просто 0 */
    }

    /* 3) значение по схеме Горнера с проверкой переполнения */
    long long val = 0;
    for (size_t j = i; str[j] != '\0'; j++) {
        int d = 0;
        if (digit_value(str[j], &d) != OK) {
            return INVALID_NUMBER;   /* сюда не попадем: символы уже проверены выше */
        }
        /* val * base + d > LLONG_MAX  <=>  val > (LLONG_MAX - d) / base */
        if (val > (LLONG_MAX - d) / *base) {
            return OVERFLOW_ERROR;
        }
        val = val * *base + d;
    }
    *znachenie = *otric ? -val : val;
    return OK;
}

/* Обработать весь файл: для каждой лексемы строка "число основание значение" */
status_code process_file(FILE *in, FILE *out, long *propuscheno) {
    if (in == NULL || out == NULL || propuscheno == NULL) {
        return INVALID_ARGS;
    }
    char *buf = NULL;
    size_t razmer = 0;
    status_code sc;
    *propuscheno = 0;

    while ((sc = read_token(in, &buf, &razmer)) == OK) {
        int otric = 0, base = 0;
        size_t nachalo = 0;
        long long val = 0;
        status_code res = analyze_number(buf, &otric, &nachalo, &base, &val);

        int zapisano = 0;
        if (res == OK) {
            zapisano = fprintf(out, "%s%s %d %lld\n", otric ? "-" : "", buf + nachalo, base, val);
        } else if (res == OVERFLOW_ERROR) {
            zapisano = fprintf(out, "%s%s %d too_big\n", otric ? "-" : "", buf + nachalo, base);
        } else {
            (*propuscheno)++;   /* не число - пропускаем */
            continue;
        }
        if (zapisano < 0) {
            sc = WRITE_ERROR;
            break;
        }
    }
    free(buf);   /* free(NULL) ничего не делает - это безопасно */
    return (sc == END_OF_FILE) ? OK : sc;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: lab1_task8 <input file> <output file>\n");
        return 1;
    }
    if (strcmp(argv[1], argv[2]) == 0) {
        fprintf(stderr, "Error: input and output must be different files\n");
        return 1;
    }

    FILE *in = fopen(argv[1], "r");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        return 1;
    }
    FILE *out = fopen(argv[2], "w");
    if (out == NULL) {
        fprintf(stderr, "Error: can not create file '%s'\n", argv[2]);
        fclose(in);
        return 1;
    }

    long propuscheno = 0;
    status_code sc = process_file(in, out, &propuscheno);
    fclose(in);
    if (fclose(out) != 0 && sc == OK) {
        sc = WRITE_ERROR;
    }

    switch (sc) {
        case OK:
            printf("Done. Result is in '%s'\n", argv[2]);
            if (propuscheno > 0) {
                printf("Skipped %ld words that are not numbers\n", propuscheno);
            }
            return 0;
        case NO_MEMORY:
            fprintf(stderr, "Error: not enough memory\n");
            return 1;
        case READ_ERROR:
            fprintf(stderr, "Error: can not read file '%s'\n", argv[1]);
            return 1;
        case WRITE_ERROR:
            fprintf(stderr, "Error: can not write file '%s'\n", argv[2]);
            return 1;
        default:
            fprintf(stderr, "Error: something went wrong\n");
            return 1;
    }
}
