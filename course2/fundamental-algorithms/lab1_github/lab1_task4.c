#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL вместо указателя */
    INVALID_FLAG,
    NO_MEMORY,         /* malloc вернул NULL */
    READ_ERROR,        /* ошибка чтения файла */
    WRITE_ERROR        /* ошибка записи файла */
} status_code;

/* Вид символа */
typedef enum {
    KIND_LATIN,        /* буква латинского алфавита */
    KIND_DIGIT,        /* арабская цифра */
    KIND_SPACE,        /* пробел ' ' */
    KIND_OTHER         /* все остальное */
} char_kind_t;

/* Определить вид символа ch. ch - байт, как его возвращает fgetc (от 0 до 255).
   Свои проверки, а не isalpha: только латиница и цифры ASCII */
status_code char_kind(const int ch, char_kind_t *kind) {
    if (kind == NULL || ch < 0 || ch > UCHAR_MAX) {
        return INVALID_ARGS;
    }
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
        *kind = KIND_LATIN;
    } else if (ch >= '0' && ch <= '9') {
        *kind = KIND_DIGIT;
    } else if (ch == ' ') {
        *kind = KIND_SPACE;
    } else {
        *kind = KIND_OTHER;
    }
    return OK;
}

/* Флаг: '-' или '/', потом необязательная 'n', потом буква d, i, s или a.
   deystvie - буква действия, s_n - была ли 'n' (1 - да) */
status_code parse_flag(const char *arg, char *deystvie, int *s_n) {
    if (arg == NULL || deystvie == NULL || s_n == NULL) {
        return INVALID_ARGS;
    }
    if (arg[0] != '-' && arg[0] != '/') {
        return INVALID_FLAG;
    }

    const char *bukva = &arg[1];
    *s_n = 0;
    if (bukva[0] == 'n') {
        *s_n = 1;
        bukva++;   /* пропускаем 'n' */
    }
    /* после буквы действия строка должна закончиться */
    if (bukva[0] == '\0' || bukva[1] != '\0') {
        return INVALID_FLAG;
    }
    if (bukva[0] != 'd' && bukva[0] != 'i' && bukva[0] != 's' && bukva[0] != 'a') {
        return INVALID_FLAG;
    }
    *deystvie = bukva[0];
    return OK;
}

/* Имя выходного файла: к ИМЕНИ входного файла (не к пути!) приписываем "out_".
   "dir/in.txt" -> "dir/out_in.txt". Память выделяется здесь, освобождает вызывающий */
status_code make_out_name(const char *in_path, char **out_path) {
    if (in_path == NULL || out_path == NULL) {
        return INVALID_ARGS;
    }

    /* ищем, где начинается имя файла: после последнего '/' или '\' */
    size_t dlina = strlen(in_path);
    size_t nachalo_imeni = 0;
    for (size_t i = 0; i < dlina; i++) {
        if (in_path[i] == '/' || in_path[i] == '\\') {
            nachalo_imeni = i + 1;
        }
    }

    /* +4 на "out_", +1 на '\0' */
    char *rez = malloc(dlina + 4 + 1);
    if (rez == NULL) {
        return NO_MEMORY;
    }
    memcpy(rez, in_path, nachalo_imeni);                     /* папка */
    memcpy(rez + nachalo_imeni, "out_", 4);                  /* префикс */
    strcpy(rez + nachalo_imeni + 4, in_path + nachalo_imeni); /* имя с '\0' */

    *out_path = rez;
    return OK;
}

/* -d: переписать файл без арабских цифр */
status_code remove_digits(FILE *in, FILE *out) {
    if (in == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    int ch;
    while ((ch = fgetc(in)) != EOF) {
        char_kind_t kind;
        if (char_kind(ch, &kind) != OK) {
            return INVALID_ARGS;
        }
        if (kind != KIND_DIGIT && fputc(ch, out) == EOF) {
            return WRITE_ERROR;
        }
    }
    return ferror(in) ? READ_ERROR : OK;
}

/* -i и -s: для каждой строки записать количество символов вида kakie.
   -i: kakie = KIND_LATIN (латинские буквы),
   -s: kakie = KIND_OTHER (не буква, не цифра и не пробел) */
status_code count_in_lines(FILE *in, FILE *out, const char_kind_t kakie) {
    if (in == NULL || out == NULL || (kakie != KIND_LATIN && kakie != KIND_OTHER)) {
        return INVALID_ARGS;
    }
    int ch;
    long kolvo = 0;
    int est_simvoly = 0;   /* были ли символы после последнего '\n' */

    while ((ch = fgetc(in)) != EOF) {
        if (ch == '\n') {
            /* строка закончилась - пишем результат */
            if (fprintf(out, "%ld\n", kolvo) < 0) {
                return WRITE_ERROR;
            }
            kolvo = 0;
            est_simvoly = 0;
            continue;
        }
        est_simvoly = 1;
        char_kind_t kind;
        if (char_kind(ch, &kind) != OK) {
            return INVALID_ARGS;
        }
        if (kind == kakie) {
            kolvo++;
        }
    }
    if (ferror(in)) {
        return READ_ERROR;
    }
    /* последняя строка без '\n' в конце файла */
    if (est_simvoly) {
        if (fprintf(out, "%ld\n", kolvo) < 0) {
            return WRITE_ERROR;
        }
    }
    return OK;
}

/* -a: каждый символ, кроме цифр, заменить его кодом в 16-ричной системе.
   Перевод строки оставляем, чтобы сохранить деление на строки */
status_code replace_with_hex(FILE *in, FILE *out) {
    if (in == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    int ch;
    while ((ch = fgetc(in)) != EOF) {
        char_kind_t kind;
        if (char_kind(ch, &kind) != OK) {
            return INVALID_ARGS;
        }
        int zapisano;
        if (kind == KIND_DIGIT || ch == '\n') {
            zapisano = fputc(ch, out);
        } else {
            /* fgetc возвращает байт как unsigned char, поэтому ch от 0 до 255.
               %02X - всегда 2 цифры: табуляция (код 9) -> "09", а не "9" */
            zapisano = fprintf(out, "%02X", (unsigned int)ch);
        }
        if (zapisano < 0) {
            return WRITE_ERROR;
        }
    }
    return ferror(in) ? READ_ERROR : OK;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: lab1_task4 <flag> <input file> [output file]\n");
        fprintf(stderr, "Flags: -d -i -s -a, with output file: -nd -ni -ns -na\n");
        return 1;
    }

    char deystvie = '\0';
    int s_n = 0;
    if (parse_flag(argv[1], &deystvie, &s_n) != OK) {
        fprintf(stderr, "Error: wrong flag '%s'\n", argv[1]);
        return 1;
    }

    /* с 'n' нужен еще путь к выходному файлу */
    if ((s_n && argc != 4) || (!s_n && argc != 3)) {
        fprintf(stderr, "Error: wrong number of arguments for flag '%s'\n", argv[1]);
        return 1;
    }

    /* имя выходного файла: из аргументов или "out_" + имя входного */
    char *out_path = NULL;   /* если выделим память - потом освободим */
    const char *out_name = NULL;
    if (s_n) {
        out_name = argv[3];
    } else {
        if (make_out_name(argv[2], &out_path) != OK) {
            fprintf(stderr, "Error: not enough memory\n");
            return 1;
        }
        out_name = out_path;
    }

    /* если писать в тот же файл, он сотрется раньше, чем мы его прочитаем */
    if (strcmp(argv[2], out_name) == 0) {
        fprintf(stderr, "Error: input and output must be different files\n");
        free(out_path);
        return 1;
    }

    FILE *in = fopen(argv[2], "r");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[2]);
        free(out_path);
        return 1;
    }
    FILE *out = fopen(out_name, "w");
    if (out == NULL) {
        fprintf(stderr, "Error: can not create file '%s'\n", out_name);
        fclose(in);
        free(out_path);
        return 1;
    }

    status_code sc = OK;
    switch (deystvie) {
        case 'd':
            sc = remove_digits(in, out);
            break;
        case 'i':
            sc = count_in_lines(in, out, KIND_LATIN);
            break;
        case 's':
            sc = count_in_lines(in, out, KIND_OTHER);
            break;
        case 'a':
            sc = replace_with_hex(in, out);
            break;
        default:
            sc = INVALID_FLAG;
            break;
    }

    /* закрываем все в любом случае; fclose тоже может сообщить об ошибке записи */
    fclose(in);
    if (fclose(out) != 0 && sc == OK) {
        sc = WRITE_ERROR;
    }

    int kod = 0;
    switch (sc) {
        case OK:
            printf("Done. Result is in '%s'\n", out_name);
            break;
        case READ_ERROR:
            fprintf(stderr, "Error: can not read file '%s'\n", argv[2]);
            kod = 1;
            break;
        case WRITE_ERROR:
            fprintf(stderr, "Error: can not write file '%s'\n", out_name);
            kod = 1;
            break;
        default:
            fprintf(stderr, "Error: something went wrong\n");
            kod = 1;
            break;
    }
    free(out_path);
    return kod;
}
