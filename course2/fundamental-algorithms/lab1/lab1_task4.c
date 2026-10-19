/* _POSIX_C_SOURCE нужно написать ДО всех #include. В режиме -std=c99 заголовки
   показывают только функции стандарта C, а stat (узнать номер файла на диске) -
   функция стандарта POSIX (Linux). Эта строка просит заголовки показать и ее */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
/* stat и struct stat - сведения о файле (Linux). На Windows вместо них _fullpath из stdlib.h */
#ifndef _WIN32
#include <sys/stat.h>
#endif

/* Статус-коды */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL вместо указателя */
    BAD_PATH,          /* (Windows) из пути не получился полный путь - путь неправильный */
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
   action - буква действия, s_n - была ли 'n' (1 - да) */
status_code parse_flag(const char *arg, char *action, int *s_n) {
    if (arg == NULL || action == NULL || s_n == NULL) {
        return INVALID_ARGS;
    }
    if (arg[0] != '-' && arg[0] != '/') {
        return INVALID_FLAG;
    }

    /* letter - адрес символа после '-' или '/'. Если там 'n' - запоминаем это
       и сдвигаем letter на следующий символ (letter++ - адрес следующего символа) */
    const char *letter = &arg[1];
    *s_n = 0;
    if (letter[0] == 'n') {
        *s_n = 1;
        letter++;   /* пропускаем 'n' */
    }
    /* после буквы действия строка должна закончиться */
    if (letter[0] == '\0' || letter[1] != '\0') {
        return INVALID_FLAG;
    }
    if (letter[0] != 'd' && letter[0] != 'i' && letter[0] != 's' && letter[0] != 'a') {
        return INVALID_FLAG;
    }
    *action = letter[0];
    return OK;
}

/* Имя выходного файла: к ИМЕНИ входного файла (не к пути!) приписываем "out_".
   "dir/in.txt" -> "dir/out_in.txt". Память выделяется здесь, освобождает вызывающий */
status_code make_out_name(const char *in_path, char **out_path) {
    if (in_path == NULL || out_path == NULL) {
        return INVALID_ARGS;
    }

    /* ищем, где начинается имя файла: после последнего '/' или '\' */
    size_t length = strlen(in_path);
    size_t name_start = 0;
    for (size_t i = 0; i < length; i++) {
        if (in_path[i] == '/' || in_path[i] == '\\') {
            name_start = i + 1;
        }
    }

    /* +4 на "out_", +1 на '\0' */
    /* Пример: "dir/in.txt" - последний '/' под индексом 3, имя начинается с индекса 4.
       Новое имя собираем из трех кусков: папка (первые name_start символов),
       "out_" и имя файла. memcpy копирует ровно n байт, strcpy - строку вместе с '\0' */
    char *result = malloc(length + 4 + 1);
    if (result == NULL) {
        return NO_MEMORY;
    }
    memcpy(result, in_path, name_start);                     /* папка */
    memcpy(result + name_start, "out_", 4);                  /* префикс */
    strcpy(result + name_start + 4, in_path + name_start); /* имя с '\0' */

    *out_path = result;
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
        /* цифру просто не пишем, остальные символы копируем как есть.
           fputc вызовется только для не-цифр: если левая часть && ложна, правая не выполняется */
        if (kind != KIND_DIGIT && fputc(ch, out) == EOF) {
            return WRITE_ERROR;
        }
    }
    /* цикл кончается на EOF, а EOF бывает и в конце файла, и при ошибке чтения.
       ferror(in) отличает одно от другого: не 0 - была ошибка */
    return ferror(in) ? READ_ERROR : OK;
}

/* -i и -s: для каждой строки записать количество символов вида which_kind.
   -i: which_kind = KIND_LATIN (латинские буквы),
   -s: which_kind = KIND_OTHER (не буква, не цифра и не пробел) */
status_code count_in_lines(FILE *in, FILE *out, const char_kind_t which_kind) {
    if (in == NULL || out == NULL || (which_kind != KIND_LATIN && which_kind != KIND_OTHER)) {
        return INVALID_ARGS;
    }
    int ch;
    long long count = 0;
    int has_chars = 0;   /* были ли символы после последнего '\n' */

    while ((ch = fgetc(in)) != EOF) {
        if (ch == '\n') {
            /* строка закончилась - пишем результат */
            if (fprintf(out, "%lld\n", count) < 0) {
                return WRITE_ERROR;
            }
            count = 0;
            has_chars = 0;
            continue;
        }
        has_chars = 1;
        char_kind_t kind;
        if (char_kind(ch, &kind) != OK) {
            return INVALID_ARGS;
        }
        if (kind == which_kind) {
            count++;
        }
    }
    if (ferror(in)) {
        return READ_ERROR;
    }
    /* последняя строка без '\n' в конце файла */
    if (has_chars) {
        if (fprintf(out, "%lld\n", count) < 0) {
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
        /* written - что вернула функция записи; меньше 0 - ошибка.
           fputc при ошибке вернет EOF (-1), fprintf - отрицательное число */
        int written;
        if (kind == KIND_DIGIT || ch == '\n') {
            written = fputc(ch, out);
        } else {
            /* fgetc возвращает байт как unsigned char, поэтому ch от 0 до 255.
               %02X - всегда 2 цифры: табуляция (код 9) -> "09", а не "9" */
            written = fprintf(out, "%02X", (unsigned int)ch);
        }
        if (written < 0) {
            return WRITE_ERROR;
        }
    }
    return ferror(in) ? READ_ERROR : OK;
}

#ifdef _WIN32
/* Только для Windows: полный путь файла ("in.txt" -> "C:\...\lab1\in.txt") в динамической памяти.
   _fullpath(буфер, путь, размер буфера) пишет полный путь в наш буфер; если буфер мал
   или путь неправильный - возвращает NULL. Буфер начинаем с 260 байт (обычный предел
   длины пути в Windows) и удваиваем; путь в Windows не длиннее 32767 символов, поэтому
   на 65536 байтах останавливаемся: если и там NULL - путь неправильный (BAD_PATH).
   _fullpath(NULL, ...) умеет выделить память сам, но так не делаем: эту память выделяет
   библиотека Windows внутри себя, и программа Dr. Memory (проверка утечек) ее не видит -
   считает ошибкой наш free. Со своим malloc все выделения видны и понятны */
status_code full_path(const char *path, char **full) {
    if (path == NULL || full == NULL) {
        return INVALID_ARGS;
    }
    for (size_t size = 260; size <= 65536; size *= 2) {
        char *buf = (char *)malloc(size);
        if (buf == NULL) {
            return NO_MEMORY;
        }
        if (_fullpath(buf, path, size) != NULL) {
            *full = buf;   /* освобождает вызывающий */
            return OK;
        }
        free(buf);   /* не поместилось (или путь плохой) - пробуем буфер вдвое больше */
    }
    return BAD_PATH;
}
#endif

/* Ведут ли два пути к одному и тому же файлу на диске: *equal = 1 - да, 0 - нет.
   Сравнивать строки (strcmp) мало: "in.txt", "./in.txt", "../lab1/in.txt" и полный путь -
   разные строки, но один файл. Если выходной файл совпадет с входным, fopen(..., "w")
   сотрет его раньше, чем мы успеем прочитать.
   В стандартном C способа узнать "это один файл" нет, поэтому спрашиваем операционную систему:
   - Linux: stat заполняет struct stat сведениями о файле. st_dev - номер диска (устройства),
     st_ino - номер файла на этом диске (inode). Пара (st_dev, st_ino) у каждого файла своя,
     и неважно, каким путем к нему пришли (в том числе через ссылки ln и ln -s).
   - Windows (сборка для себя): full_path превращает путь в полный ("C:\lab\in.txt"),
     сравниваем без учета больших и маленьких букв (в Windows IN.TXT и in.txt - один файл).
   Если какой-то путь не существует (stat не смог), это не один файл: например, выходного
   файла еще нет - его создаст fopen. Если не существует входной, fopen потом сообщит об ошибке */
status_code same_file(const char *path1, const char *path2, int *equal) {
    if (path1 == NULL || path2 == NULL || equal == NULL) {
        return INVALID_ARGS;
    }
    *equal = 0;
#ifdef _WIN32
    /* full_path - полные пути в нашей памяти (потом free). Путь неправильный (BAD_PATH) -
       считаем, что файлы разные: fopen дальше сам сообщит об ошибке */
    char *full1 = NULL;
    char *full2 = NULL;
    status_code sc1 = full_path(path1, &full1);
    status_code sc2 = full_path(path2, &full2);
    if (sc1 == NO_MEMORY || sc2 == NO_MEMORY) {
        free(full1);   /* free(NULL) ничего не делает - можно не проверять */
        free(full2);
        return NO_MEMORY;
    }
    if (sc1 == OK && sc2 == OK && _stricmp(full1, full2) == 0) {
        *equal = 1;
    }
    free(full1);
    free(full2);
#else
    /* stat возвращает 0, если сведения о файле получены; иначе (файла нет) - не 0 */
    struct stat info1;
    struct stat info2;
    if (stat(path1, &info1) == 0 && stat(path2, &info2) == 0 &&
        info1.st_dev == info2.st_dev && info1.st_ino == info2.st_ino) {
        *equal = 1;
    }
#endif
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: lab1_task4 <flag> <input file> [output file]\n");
        fprintf(stderr, "Flags: -d -i -s -a, with output file: -nd -ni -ns -na\n");
        return 1;
    }

    /* action - буква действия (d, i, s, a), s_n - была ли 'n' во флаге */
    char action = '\0';
    int s_n = 0;
    if (parse_flag(argv[1], &action, &s_n) != OK) {
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
    /* out_path - память от make_out_name, ее потом освобождаем.
       out_name - имя, которым пользуемся дальше: argv[3] или out_path.
       При флаге с 'n' out_path остается NULL, а free(NULL) ничего не делает,
       поэтому free(out_path) ниже безопасен в обоих случаях */
    if (s_n) {
        out_name = argv[3];
    } else {
        if (make_out_name(argv[2], &out_path) != OK) {
            fprintf(stderr, "Error: not enough memory\n");
            return 1;
        }
        out_name = out_path;
    }

    /* если писать в тот же файл, он сотрется раньше, чем мы его прочитаем.
       same_file ловит и разные записи одного пути: "in.txt" и "./in.txt" */
    int equal = 0;
    status_code sc_path = same_file(argv[2], out_name, &equal);
    if (sc_path == NO_MEMORY) {
        /* на Windows same_file выделяет память под полные пути */
        fprintf(stderr, "Error: not enough memory\n");
        free(out_path);
        return 1;
    }
    if (sc_path != OK) {
        fprintf(stderr, "Error: internal error\n");   /* INVALID_ARGS: NULL-ов здесь не бывает */
        free(out_path);
        return 1;
    }
    if (equal) {
        fprintf(stderr, "Error: input and output must be different files\n");
        free(out_path);
        return 1;
    }

    /* "r" - открыть на чтение, "w" - на запись (файл создается, старое содержимое стирается).
       fopen вернул NULL - файл открыть не удалось. Перед каждым выходом из main
       закрываем уже открытые файлы и освобождаем память */
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
    /* по букве действия вызываем нужную функцию; -i и -s - одна и та же функция,
       отличается только вид символов, которые считаем */
    switch (action) {
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

    /* по статусу печатаем итог; exit_code - код возврата программы (0 - успех, 1 - ошибка).
       default ("internal error") - запасная ветка для статусов, которые здесь появиться
       не могут: INVALID_ARGS и INVALID_FLAG (main передает открытые файлы, флаг уже проверен).
       Пользователь это сообщение не увидит; если увидел - ошибка в самом коде программы */
    int exit_code = 0;
    switch (sc) {
        case OK:
            printf("Done. Result is in '%s'\n", out_name);
            break;
        case READ_ERROR:
            fprintf(stderr, "Error: can not read file '%s'\n", argv[2]);
            exit_code = 1;
            break;
        case WRITE_ERROR:
            fprintf(stderr, "Error: can not write file '%s'\n", out_name);
            exit_code = 1;
            break;
        default:
            fprintf(stderr, "Error: internal error\n");
            exit_code = 1;
            break;
    }
    free(out_path);
    return exit_code;
}
