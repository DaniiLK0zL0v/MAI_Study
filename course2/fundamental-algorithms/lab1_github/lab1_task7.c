#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL вместо указателя */
    INVALID_FLAG,
    READ_ERROR,
    WRITE_ERROR
} status_code;

/* Разделители лексем: пробел, табуляция, перевод строки (и '\r' из Windows-файлов).
   ch - то, что вернул fgetc: байт от 0 до 255 или EOF. rez = 1 - разделитель */
status_code is_razdelitel(const int ch, int *rez) {
    if (rez == NULL || (ch < 0 && ch != EOF) || ch > UCHAR_MAX) {
        return INVALID_ARGS;
    }
    *rez = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
    return OK;
}

/* Флаг: '-' или '/' и буква r или a */
status_code parse_flag(const char *arg, char *flag) {
    if (arg == NULL || flag == NULL) {
        return INVALID_ARGS;
    }
    if ((arg[0] != '-' && arg[0] != '/') || arg[1] == '\0' || arg[2] != '\0') {
        return INVALID_FLAG;
    }
    if (arg[1] != 'r' && arg[1] != 'a') {
        return INVALID_FLAG;
    }
    *flag = arg[1];
    return OK;
}

/* Пропустить разделители. В ch кладем первый символ лексемы или EOF */
status_code skip_razdeliteli(FILE *in, int *ch) {
    if (in == NULL || ch == NULL) {
        return INVALID_ARGS;
    }
    int razd = 1;
    while (razd) {   /* EOF - не разделитель, на нем цикл тоже кончится */
        *ch = fgetc(in);
        status_code sc = is_razdelitel(*ch, &razd);
        if (sc != OK) {
            return sc;
        }
    }
    return ferror(in) ? READ_ERROR : OK;
}

/* Записать символ в системе счисления base (код символа), например 'A' в base 8 -> "101" */
status_code write_code(FILE *out, const unsigned int kod, const unsigned int base) {
    if (out == NULL || base < 2 || base > 16) {
        return INVALID_ARGS;
    }
    char cifry[16];   /* код <= 255, в base 2 это максимум 8 цифр */
    int cnt = 0;
    unsigned int val = kod;
    do {
        cifry[cnt] = "0123456789ABCDEF"[val % base];
        val /= base;
        cnt++;
    } while (val > 0);
    /* цифры получились с конца - пишем задом наперед */
    for (int i = cnt - 1; i >= 0; i--) {
        if (fputc(cifry[i], out) == EOF) {
            return WRITE_ERROR;
        }
    }
    return OK;
}

/* Скопировать одну лексему из in в out.
   Перед лексемой (кроме самой первой) пишем один пробел.
   est - была ли лексема (0 - файл закончился) */
status_code copy_token(FILE *in, FILE *out, int *pervaya, int *est) {
    if (in == NULL || out == NULL || pervaya == NULL || est == NULL) {
        return INVALID_ARGS;
    }
    int ch = EOF;
    status_code sc = skip_razdeliteli(in, &ch);
    if (sc != OK) {
        return sc;
    }
    if (ch == EOF) {
        *est = 0;
        return OK;
    }
    *est = 1;
    if (!*pervaya && fputc(' ', out) == EOF) {
        return WRITE_ERROR;
    }
    *pervaya = 0;

    /* пишем символы, пока не встретим разделитель или конец файла */
    while (ch != EOF) {
        int razd = 0;
        sc = is_razdelitel(ch, &razd);
        if (sc != OK) {
            return sc;
        }
        if (razd) {
            break;
        }
        if (fputc(ch, out) == EOF) {
            return WRITE_ERROR;
        }
        ch = fgetc(in);
    }
    return ferror(in) ? READ_ERROR : OK;
}

/* -r: лексемы по очереди: 1-я из file1, 2-я из file2, 3-я из file1 ...
   Когда один файл кончился - дописываем остаток другого */
status_code merge_tokens(FILE *f1, FILE *f2, FILE *out) {
    if (f1 == NULL || f2 == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    int pervaya = 1;
    int est1 = 1, est2 = 1;   /* остались ли лексемы в файлах */
    status_code sc;

    while (est1 || est2) {
        if (est1) {
            sc = copy_token(f1, out, &pervaya, &est1);
            if (sc != OK) {
                return sc;
            }
        }
        if (est2) {
            sc = copy_token(f2, out, &pervaya, &est2);
            if (sc != OK) {
                return sc;
            }
        }
    }
    return OK;
}

/* -a: преобразовать лексемы по их номеру (нумерация с 1):
   каждая 10-я: латиница -> строчные, потом каждый символ -> ASCII-код в base 4;
   каждая 2-я (не 10-я): латиница -> строчные;
   каждая 5-я (не 10-я): каждый символ -> ASCII-код в base 8;
   остальные без изменений */
status_code transform_tokens(FILE *in, FILE *out) {
    if (in == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    long nomer = 0;
    int ch = EOF;
    status_code sc = skip_razdeliteli(in, &ch);
    if (sc != OK) {
        return sc;
    }

    while (ch != EOF) {
        nomer++;
        if (nomer > 1 && fputc(' ', out) == EOF) {
            return WRITE_ERROR;
        }
        int strochnye = (nomer % 2 == 0);                    /* и для 10-й тоже */
        unsigned int base = 0;                               /* 0 - писать сам символ */
        if (nomer % 10 == 0) {
            base = 4;
        } else if (nomer % 5 == 0) {
            base = 8;
        }

        /* обрабатываем символы лексемы по одному, до разделителя или конца файла */
        while (ch != EOF) {
            int razd = 0;
            sc = is_razdelitel(ch, &razd);
            if (sc != OK) {
                return sc;
            }
            if (razd) {
                break;
            }
            if (strochnye && ch >= 'A' && ch <= 'Z') {
                ch = ch - 'A' + 'a';
            }
            if (base != 0) {
                sc = write_code(out, (unsigned int)ch, base);
            } else if (fputc(ch, out) == EOF) {
                sc = WRITE_ERROR;
            }
            if (sc != OK) {
                return sc;
            }
            ch = fgetc(in);
        }
        if (ferror(in)) {
            return READ_ERROR;
        }
        sc = skip_razdeliteli(in, &ch);
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: lab1_task7 -r <file1> <file2> <output>\n");
        fprintf(stderr, "       lab1_task7 -a <input> <output>\n");
        return 1;
    }
    char flag = '\0';
    if (parse_flag(argv[1], &flag) != OK) {
        fprintf(stderr, "Error: wrong flag '%s'\n", argv[1]);
        return 1;
    }
    /* -r: флаг + 3 файла, -a: флаг + 2 файла */
    int nuzhno = (flag == 'r') ? 5 : 4;
    if (argc != nuzhno) {
        fprintf(stderr, "Error: flag -%c needs %d file names\n", flag, nuzhno - 2);
        return 1;
    }

    /* выходной файл - последний; он не должен совпадать с входными */
    const char *out_name = argv[argc - 1];
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], out_name) == 0) {
            fprintf(stderr, "Error: output file must be different from input files\n");
            return 1;
        }
    }

    FILE *f1 = fopen(argv[2], "r");
    if (f1 == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[2]);
        return 1;
    }
    FILE *f2 = NULL;
    if (flag == 'r') {
        f2 = fopen(argv[3], "r");
        if (f2 == NULL) {
            fprintf(stderr, "Error: can not open file '%s'\n", argv[3]);
            fclose(f1);
            return 1;
        }
    }
    FILE *out = fopen(out_name, "w");
    if (out == NULL) {
        fprintf(stderr, "Error: can not create file '%s'\n", out_name);
        fclose(f1);
        if (f2 != NULL) {
            fclose(f2);
        }
        return 1;
    }

    status_code sc = (flag == 'r') ? merge_tokens(f1, f2, out) : transform_tokens(f1, out);

    /* закрываем все файлы в любом случае */
    fclose(f1);
    if (f2 != NULL) {
        fclose(f2);
    }
    if (fclose(out) != 0 && sc == OK) {
        sc = WRITE_ERROR;
    }

    switch (sc) {
        case OK:
            printf("Done. Result is in '%s'\n", out_name);
            return 0;
        case READ_ERROR:
            fprintf(stderr, "Error: can not read input file\n");
            return 1;
        case WRITE_ERROR:
            fprintf(stderr, "Error: can not write file '%s'\n", out_name);
            return 1;
        default:
            fprintf(stderr, "Error: something went wrong\n");
            return 1;
    }
}
