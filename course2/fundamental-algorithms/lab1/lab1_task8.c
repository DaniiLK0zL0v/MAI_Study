/* _POSIX_C_SOURCE нужно написать ДО всех #include. В режиме -std=c99 заголовки
   показывают только функции стандарта C, а stat (узнать номер файла на диске) -
   функция стандарта POSIX (Linux). Эта строка просит заголовки показать и ее */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
    INVALID_NUMBER,    /* в лексеме есть символ, который не цифра ни в какой системе */
    OVERFLOW_ERROR,    /* значение не помещается в long long */
    NO_MEMORY,
    READ_ERROR,
    WRITE_ERROR,
    END_OF_FILE        /* лексем больше нет */
} status_code;

/* Задача: для каждой лексемы ("слова") файла найти минимальное основание системы
   счисления, в которой она может быть числом, и ее значение в десятичной системе.
   Пример: "1F" - самая большая цифра F = 15, основание 16, значение 31 */

/* Разделители: пробел, табуляция, перевод строки (и '\r' из Windows-файлов).
   ch - то, что вернул fgetc: байт от 0 до 255 или EOF. result = 1 - разделитель */
status_code is_separator(const int ch, int *result) {
    if (result == NULL || (ch < 0 && ch != EOF) || ch > UCHAR_MAX) {
        return INVALID_ARGS;
    }
    *result = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
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
    /* буквы переводим в заглавные: 'A' - 'A' + 10 = 10, 'B' -> 11, ... 'Z' -> 35 */
    char up = (char)toupper((unsigned char)ch);
    if (up >= 'A' && up <= 'Z') {
        *val = up - 'A' + 10;
        return OK;
    }
    return INVALID_NUMBER;
}

/* Прочитать одну лексему в динамический буфер.
   buf и size - буфер и его размер; если лексема длиннее - буфер увеличиваем (realloc).
   Буфер живет между вызовами, освобождает его main */
status_code read_token(FILE *in, char **buf, size_t *size) {
    if (in == NULL || buf == NULL || size == NULL) {
        return INVALID_ARGS;
    }
    /* пропускаем разделители (EOF - не разделитель, на нем цикл тоже кончится) */
    int ch = EOF;
    int sep = 1;
    while (sep) {
        ch = fgetc(in);
        if (is_separator(ch, &sep) != OK) {
            return INVALID_ARGS;
        }
    }
    if (ch == EOF) {
        return ferror(in) ? READ_ERROR : END_OF_FILE;
    }

    /* *buf и *size меняем через указатели: если буфер увеличится,
       новый адрес и размер должны попасть обратно в вызывающий код */
    /* копируем символы до разделителя или конца файла */
    size_t length = 0;
    while (ch != EOF) {
        if (is_separator(ch, &sep) != OK) {
            return INVALID_ARGS;
        }
        if (sep) {
            break;
        }
        /* +1 - место под '\0' */
        /* места не хватает - увеличиваем буфер вдвое (в первый раз - 16 байт).
           realloc выделяет новый кусок, копирует туда старое содержимое и
           возвращает новый адрес. Пишем результат во временный tmp: если realloc
           вернет NULL, в *buf останется старый адрес и память не потеряется */
        if (length + 1 >= *size) {
            size_t new_size = (*size == 0) ? 16 : *size * 2;
            char *tmp = realloc(*buf, new_size);
            if (tmp == NULL) {
                return NO_MEMORY;   /* старый буфер не потерян - его освободит main */
            }
            *buf = tmp;
            *size = new_size;
        }
        /* (*buf)[length] - символ номер length в буфере; скобки нужны,
           потому что buf - адрес указателя, а не сам буфер */
        (*buf)[length] = (char)ch;
        length++;
        ch = fgetc(in);
    }
    (*buf)[length] = '\0';
    return ferror(in) ? READ_ERROR : OK;
}

/* Разобрать число: знак, минимальное основание, значение в 10-й системе.
   start - с какого символа идут значащие цифры (после знака и ведущих нулей) */
status_code analyze_number(const char *str, int *negative, size_t *start, int *base,
                           long long *value) {
    if (str == NULL || negative == NULL || start == NULL || base == NULL || value == NULL) {
        return INVALID_ARGS;
    }
    /* i - откуда начинаются цифры: после знака, если он есть */
    size_t i = 0;
    *negative = 0;
    if (str[0] == '-' || str[0] == '+') {
        *negative = (str[0] == '-');
        i = 1;
    }
    if (str[i] == '\0') {
        return INVALID_NUMBER;   /* один знак без цифр */
    }

    /* 1) проверяем символы и ищем самую большую цифру */
    int max_digit = 0;
    for (size_t j = i; str[j] != '\0'; j++) {
        int d = 0;
        if (digit_value(str[j], &d) != OK) {
            return INVALID_NUMBER;
        }
        if (d > max_digit) {
            max_digit = d;
        }
    }
    /* минимальное основание = самая большая цифра + 1, но не меньше 2 */
    *base = (max_digit + 1 < 2) ? 2 : max_digit + 1;

    /* 2) пропускаем ведущие нули (но число "000" - это "0") */
    while (str[i] == '0' && str[i + 1] != '\0') {
        i++;
    }
    *start = i;
    if (str[i] == '0') {
        *negative = 0;   /* "-0" это просто 0 */
    }

    /* 3) значение по схеме Горнера с проверкой переполнения */
    /* Число собираем слева направо: val = val * base + цифра.
       Пример "1F" в base 16: 0 * 16 + 1 = 1, 1 * 16 + 15 = 31.
       Проверка переполнения ДО умножения, иначе его уже не заметить */
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
    /* -val не переполнится: val <= LLONG_MAX, а -LLONG_MAX в long long помещается */
    *value = *negative ? -val : val;
    return OK;
}

/* Обработать весь файл: для каждой лексемы строка "число основание значение" */
status_code process_file(FILE *in, FILE *out, long long *skipped) {
    if (in == NULL || out == NULL || skipped == NULL) {
        return INVALID_ARGS;
    }
    /* buf - буфер под лексему, один на весь файл: read_token сам увеличивает его
       при необходимости. Начинаем с пустого (NULL, размер 0) */
    char *buf = NULL;
    size_t size = 0;
    status_code sc;
    *skipped = 0;

    /* читаем лексемы, пока read_token возвращает OK. Цикл кончится на END_OF_FILE
       (лексем больше нет - это нормально) или на ошибке */
    while ((sc = read_token(in, &buf, &size)) == OK) {
        int negative = 0, base = 0;
        size_t start = 0;
        long long val = 0;
        status_code res = analyze_number(buf, &negative, &start, &base, &val);

        /* Печатаем: знак, число без ведущих нулей (buf + start - адрес символа
           с номером start, то есть строка с этого места), основание, значение.
           Слишком большое - вместо значения too_big. written < 0 - ошибка записи */
        int written = 0;
        if (res == OK) {
            written = fprintf(out, "%s%s %d %lld\n", negative ? "-" : "", buf + start, base, val);
        } else if (res == OVERFLOW_ERROR) {
            written = fprintf(out, "%s%s %d too_big\n", negative ? "-" : "", buf + start, base);
        } else {
            (*skipped)++;   /* не число - пропускаем */
            continue;
        }
        if (written < 0) {
            sc = WRITE_ERROR;
            break;
        }
    }
    free(buf);   /* free(NULL) ничего не делает - это безопасно */
    /* END_OF_FILE - нормальный конец файла, для main это OK */
    return (sc == END_OF_FILE) ? OK : sc;
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
    if (argc != 3) {
        fprintf(stderr, "Usage: lab1_task8 <input file> <output file>\n");
        return 1;
    }
    /* Входной и выходной файл должны быть разными: fopen с "w" стер бы входной
       до чтения. same_file ловит и разные записи одного пути: "in.txt" и "./in.txt" */
    int equal = 0;
    status_code sc_path = same_file(argv[1], argv[2], &equal);
    if (sc_path == NO_MEMORY) {
        /* на Windows same_file выделяет память под полные пути */
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    if (sc_path != OK) {
        fprintf(stderr, "Error: internal error\n");   /* INVALID_ARGS: NULL-ов здесь не бывает */
        return 1;
    }
    if (equal) {
        fprintf(stderr, "Error: input and output must be different files\n");
        return 1;
    }

    /* "r" - чтение, "w" - запись (файл создается, старое содержимое стирается).
       Если второй файл не открылся - закрываем первый и выходим */
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

    long long skipped = 0;
    status_code sc = process_file(in, out, &skipped);
    fclose(in);
    if (fclose(out) != 0 && sc == OK) {
        sc = WRITE_ERROR;
    }

    /* по статусу печатаем итог. default ("internal error") - запасная ветка для статусов,
       которые здесь появиться не могут: INVALID_ARGS (main передает открытые файлы).
       Пользователь это сообщение не увидит; если увидел - ошибка в самом коде программы */
    switch (sc) {
        case OK:
            printf("Done. Result is in '%s'\n", argv[2]);
            if (skipped > 0) {
                printf("Skipped %lld words that are not numbers\n", skipped);
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
            fprintf(stderr, "Error: internal error\n");
            return 1;
    }
}
