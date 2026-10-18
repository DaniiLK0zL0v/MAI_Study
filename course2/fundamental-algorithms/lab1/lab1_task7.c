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
    INVALID_FLAG,
    READ_ERROR,
    WRITE_ERROR
} status_code;

/* Лексема - кусок текста без пробелов, табуляций и переводов строк ("слово").
   Все функции читают файл по одному символу через fgetc.
   ch везде int, а не char: fgetc возвращает байт 0..255 или EOF (-1), в char их не различить */

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
    /* ровно 2 символа: arg[1] == конец строки - один символ, arg[2] не конец - больше двух */
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
    /* читаем символы, пока они разделители; первый не-разделитель остается в *ch.
       razd = 1 в начале - чтобы цикл выполнился хотя бы раз */
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
    /* Перевод в систему base: остаток от деления на base - очередная цифра с конца.
       Пример: 65 ('A') в base 8: 65 % 8 = 1, 65 / 8 = 8; 8 % 8 = 0, 8 / 8 = 1; 1 % 8 = 1 -> "101".
       "0123456789ABCDEF"[ostatok] - символ цифры (строку можно индексировать, как массив).
       do-while, а не while: у кода 0 тоже должна быть одна цифра "0" */
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
    /* pervaya - общий флаг для всего выходного файла (передается адресом и
       меняется здесь), так пробелы стоят только МЕЖДУ лексемами. Пробел
       пишется, только если лексема не первая: при ложной левой части && правая не выполняется */
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

    /* Пока хоть в одном файле есть лексемы: берем по одной из каждого по очереди.
       Когда copy_token дошел до конца файла, он ставит est = 0, и этот файл
       дальше пропускается - так остаток другого файла дописывается подряд */
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
    /* nomer - номер текущей лексемы. ch - первый символ текущей лексемы:
       перед циклом пропускаем разделители в начале файла */
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
        /* по номеру решаем, что делать с лексемой: strochnye - переводить ли
           заглавные в строчные, base - в какой системе писать коды символов.
           10-я делится и на 2, и на 5: ее проверяем первой */
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
            /* заглавная латинская -> строчная: сдвиг на одинаковое расстояние в таблице кодов */
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
        /* лексема кончилась - пропускаем разделители до начала следующей (или до EOF) */
        sc = skip_razdeliteli(in, &ch);
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

/* Ведут ли два пути к одному и тому же файлу на диске: *odinakovye = 1 - да, 0 - нет.
   Сравнивать строки (strcmp) мало: "in.txt", "./in.txt", "../lab1/in.txt" и полный путь -
   разные строки, но один файл. Если выходной файл совпадет с входным, fopen(..., "w")
   сотрет его раньше, чем мы успеем прочитать.
   В стандартном C способа узнать "это один файл" нет, поэтому спрашиваем операционную систему:
   - Linux: stat заполняет struct stat сведениями о файле. st_dev - номер диска (устройства),
     st_ino - номер файла на этом диске (inode). Пара (st_dev, st_ino) у каждого файла своя,
     и неважно, каким путем к нему пришли (в том числе через ссылки ln и ln -s).
   - Windows (сборка для себя): _fullpath превращает путь в полный ("C:\lab\in.txt"),
     сравниваем без учета больших и маленьких букв (в Windows IN.TXT и in.txt - один файл).
   Если какой-то путь не существует (stat не смог), это не один файл: например, выходного
   файла еще нет - его создаст fopen. Если не существует входной, fopen потом сообщит об ошибке */
status_code same_file(const char *put1, const char *put2, int *odinakovye) {
    if (put1 == NULL || put2 == NULL || odinakovye == NULL) {
        return INVALID_ARGS;
    }
    *odinakovye = 0;
#ifdef _WIN32
    /* _fullpath(NULL, ...) сам выделяет память под полный путь через malloc - потом free.
       NULL в ответе - путь неправильный или нет памяти: тогда считаем, что файлы разные
       (fopen дальше сам сообщит об ошибке, если путь плохой) */
    char *polny1 = _fullpath(NULL, put1, 0);
    char *polny2 = _fullpath(NULL, put2, 0);
    if (polny1 != NULL && polny2 != NULL && _stricmp(polny1, polny2) == 0) {
        *odinakovye = 1;
    }
    free(polny1);   /* free(NULL) ничего не делает - можно не проверять */
    free(polny2);
#else
    /* stat возвращает 0, если сведения о файле получены; иначе (файла нет) - не 0 */
    struct stat sved1;
    struct stat sved2;
    if (stat(put1, &sved1) == 0 && stat(put2, &sved2) == 0 &&
        sved1.st_dev == sved2.st_dev && sved1.st_ino == sved2.st_ino) {
        *odinakovye = 1;
    }
#endif
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

    /* Выходной файл - последний; он не должен совпадать ни с одним входным
       (fopen с "w" стер бы входной файл до чтения). same_file ловит и разные
       записи одного пути: "f1.txt" и "./f1.txt". Два одинаковых входных файла
       при -r разрешены: оба только читаются */
    const char *out_name = argv[argc - 1];
    for (int i = 2; i < argc - 1; i++) {
        int odinakovye = 0;
        if (same_file(argv[i], out_name, &odinakovye) != OK) {
            fprintf(stderr, "Error: internal error\n");
            return 1;
        }
        if (odinakovye) {
            fprintf(stderr, "Error: output file must be different from input files\n");
            return 1;
        }
    }

    /* Открываем файлы. f2 нужен только для -r, при -a он остается NULL.
       Если очередной файл не открылся - закрываем уже открытые и выходим */
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

    /* -r - слияние двух файлов, -a - преобразование одного */
    status_code sc = (flag == 'r') ? merge_tokens(f1, f2, out) : transform_tokens(f1, out);

    /* закрываем все файлы в любом случае */
    fclose(f1);
    if (f2 != NULL) {
        fclose(f2);
    }
    if (fclose(out) != 0 && sc == OK) {
        sc = WRITE_ERROR;
    }

    /* по статусу печатаем итог. default ("internal error") - запасная ветка для статусов,
       которые здесь появиться не могут: INVALID_ARGS (main передает открытые файлы).
       Пользователь это сообщение не увидит; если увидел - ошибка в самом коде программы */
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
            fprintf(stderr, "Error: internal error\n");
            return 1;
    }
}
