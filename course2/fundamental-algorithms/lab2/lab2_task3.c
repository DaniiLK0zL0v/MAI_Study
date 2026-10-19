/*
 * Лабораторная 2, задание 3. Поиск подстроки в файлах.
 *
 * Главная функция задания - с переменным числом аргументов:
 *     find_substring(&rez, podstroka, kolvo_faylov, put1, put2, ...)
 * Для каждого файла она находит ВСЕ вхождения подстроки: номер строки и номер позиции
 * в строке (оба считаются с 1), где начинается вхождение. Вхождения могут перекрываться:
 * "aa" в "aaaa" - три вхождения (позиции 1, 2, 3). Подстрока может содержать '\n' -
 * тогда вхождение занимает несколько строк, а в ответе - строка и позиция его начала.
 * Функция только ищет и возвращает результат; печатает его main (print_result).
 * Если какой-то файл не открылся - это записывается в результат этого файла,
 * а поиск в остальных файлах продолжается.
 *
 * Запуск:
 *     lab2_task3                               - демонстрация: несколько вызовов
 *                                                find_substring с готовым списком файлов
 *     lab2_task3 <подстрока> <файл1> [файл2 ...] - поиск подстроки в своих файлах
 * В подстроке из командной строки можно писать \n (перевод строки), \t (табуляция)
 * и \\ (сама обратная косая черта): так в аргумент можно передать перевод строки.
 * Пример:  lab2_task3 cat a.txt   ->   File 'a.txt': 2 matches / line 1, position 5 / ...
 *
 * strstr и strncmp запрещены условием - сравнение символов написано вручную.
 * Файл читается по одному символу, а в памяти хранятся только последние m символов
 * (m - длина подстроки), поэтому размер файла не ограничен памятью.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* strlen, memcpy (strstr и strncmp не используются - запрещены) */
#include <stdarg.h>   /* va_list, va_start, va_arg, va_end - переменное число аргументов */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* в функцию передали NULL или 0 файлов */
    EMPTY_SUBSTRING,   /* подстрока пустая - искать нечего */
    BAD_ESCAPE,        /* в подстроке из командной строки "\" с непонятной буквой после */
    FILE_OPEN_ERROR,   /* файл не открылся (нет такого файла, нет прав) */
    FILE_READ_ERROR,   /* файл открылся, но прочитать его не получилось */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Одно вхождение: где оно начинается. Строки и позиции считаются с 1 */
typedef struct {
    size_t stroka;
    size_t pozitsiya;
} vhozhdenie;

/* Результат поиска в одном файле */
typedef struct {
    char *put;                 /* путь к файлу (своя копия в динамической памяти) */
    status_code status;        /* OK, FILE_OPEN_ERROR или FILE_READ_ERROR */
    vhozhdenie *spisok;        /* найденные вхождения по порядку (NULL, если нет ни одного) */
    size_t kolvo;              /* сколько вхождений найдено */
    size_t razmer;             /* сколько элементов выделено в spisok */
} file_result;

/* Результат всего поиска: по одному file_result на каждый файл, в порядке аргументов */
typedef struct {
    file_result *faily;
    size_t kolvo;
} search_result;

/* ===================== память ===================== */

/* Копия строки в динамической памяти (освобождает вызывающий) */
status_code copy_string(const char *str, char **kopiya) {
    if (str == NULL || kopiya == NULL) {
        return INVALID_ARGS;
    }
    size_t dlina = strlen(str);
    char *novaya = (char *)malloc(dlina + 1);   /* + 1 байт на '\0' */
    if (novaya == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(novaya, str, dlina + 1);   /* копируем вместе с '\0' */
    *kopiya = novaya;
    return OK;
}

/* Освободить результат поиска: у каждого файла - путь и список вхождений, потом сам массив.
   free(NULL) ничего не делает, поэтому недозаполненные файлы (NULL-указатели) тоже можно */
status_code free_search_result(search_result *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    if (rez->faily != NULL) {
        for (size_t i = 0; i < rez->kolvo; i++) {
            free(rez->faily[i].put);
            free(rez->faily[i].spisok);
        }
        free(rez->faily);
    }
    rez->faily = NULL;
    rez->kolvo = 0;
    return OK;
}

/* Добавить вхождение в конец списка файла. Список растет удвоением: 8, 16, 32, ... */
status_code add_vhozhdenie(file_result *fr, const size_t stroka, const size_t pozitsiya) {
    if (fr == NULL) {
        return INVALID_ARGS;
    }
    if (fr->kolvo == fr->razmer) {
        /* новый размер: 8 для начала, потом вдвое больше; проверяем, что
           (новый размер * размер элемента) помещается в size_t */
        size_t novyy_razmer = (fr->razmer == 0) ? 8 : fr->razmer * 2;
        if (fr->razmer > SIZE_MAX / 2 || novyy_razmer > SIZE_MAX / sizeof(vhozhdenie)) {
            return ALLOC_ERROR;
        }
        /* realloc - во временную переменную: если он вернет NULL, старый список
           останется на месте, и его освободит free_search_result.
           realloc(NULL, ...) работает как malloc - так создается первый список */
        vhozhdenie *novyy = (vhozhdenie *)realloc(fr->spisok, novyy_razmer * sizeof(vhozhdenie));
        if (novyy == NULL) {
            return ALLOC_ERROR;
        }
        fr->spisok = novyy;
        fr->razmer = novyy_razmer;
    }
    fr->spisok[fr->kolvo].stroka = stroka;
    fr->spisok[fr->kolvo].pozitsiya = pozitsiya;
    fr->kolvo++;
    return OK;
}

/* ===================== поиск ===================== */

/* Поиск всех вхождений подстроки (длина m >= 1) в открытом файле.
   Файл читаем по одному символу. В памяти держим "окно" - последние m прочитанных
   символов (массив okno) и для каждого из них его строку и позицию (массив mesta).
   Окно - кольцевое: символ номер k (считая с 0 от начала файла) лежит в клетке k % m,
   новый символ записывается на место самого старого. Когда прочитано уже m символов,
   самый старый символ окна лежит в клетке prochitano % m - с него и сравниваем
   окно с подстрокой посимвольно (сами, без strncmp). Совпало все - вхождение,
   его начало - строка и позиция самого старого символа окна.
   Пример, подстрока "aba" (m = 3), файл "xabab":
       прочитали x a b  -> окно "xab" - не совпало
       прочитали a      -> окно "aba" (x вытеснен) - совпало, начало: строка 1, позиция 2
       прочитали b      -> окно "bab" - не совпало
   Строка и позиция: после '\n' начинается новая строка (stroka + 1, pozitsiya = 1),
   после любого другого символа - pozitsiya + 1. Позиция считается в байтах:
   русская буква в UTF-8 - 2 байта, то есть 2 позиции.
   Результат дописывается в fr; файл не закрывает - его закроет вызывающий */
status_code search_in_stream(FILE *in, const char *podstroka, const size_t m, file_result *fr) {
    if (in == NULL || podstroka == NULL || fr == NULL || m == 0) {
        return INVALID_ARGS;
    }
    /* m <= длина строки podstroka, которая уже лежит в памяти, так что
       m * sizeof(vhozhdenie) переполниться не может только если проверим */
    if (m > SIZE_MAX / sizeof(vhozhdenie)) {
        return ALLOC_ERROR;
    }
    char *okno = (char *)malloc(m);
    if (okno == NULL) {
        return ALLOC_ERROR;
    }
    vhozhdenie *mesta = (vhozhdenie *)malloc(m * sizeof(vhozhdenie));
    if (mesta == NULL) {
        free(okno);
        return ALLOC_ERROR;
    }

    size_t stroka = 1;         /* где стоит символ, который сейчас прочитаем */
    size_t pozitsiya = 1;
    size_t prochitano = 0;     /* сколько символов прочитано (до m - пока окно не заполнено) */
    size_t kletka = 0;         /* клетка окна, куда пишем новый символ (= номер символа % m) */
    status_code sc = OK;
    int c;
    /* c - int, а не char: fgetc возвращает байт 0..255 или EOF (-1), в char их не различить */
    while ((c = fgetc(in)) != EOF) {
        /* 1) кладем символ в окно (на место самого старого) вместе с его местом в файле */
        okno[kletka] = (char)c;
        mesta[kletka].stroka = stroka;
        mesta[kletka].pozitsiya = pozitsiya;
        kletka++;
        if (kletka == m) {
            kletka = 0;        /* дошли до конца массива - по кругу в начало */
        }
        if (prochitano < m) {
            prochitano++;
        }

        /* 2) окно заполнено - сравниваем его с подстрокой. Самый старый символ теперь
           в клетке kletka (туда запишется следующий символ) */
        if (prochitano == m) {
            int sovpalo = 1;
            size_t k = kletka;
            for (size_t j = 0; j < m; j++) {
                if (okno[k] != podstroka[j]) {
                    sovpalo = 0;
                    break;   /* первое несовпадение - дальше сравнивать незачем */
                }
                k++;
                if (k == m) {
                    k = 0;
                }
            }
            if (sovpalo) {
                sc = add_vhozhdenie(fr, mesta[kletka].stroka, mesta[kletka].pozitsiya);
                if (sc != OK) {
                    break;   /* ALLOC_ERROR: память освободим ниже, после цикла */
                }
            }
        }

        /* 3) место следующего символа */
        if (c == '\n') {
            stroka++;
            pozitsiya = 1;
        } else {
            pozitsiya++;
        }
    }
    /* EOF бывает и в конце файла, и при ошибке чтения - различает их ferror */
    if (sc == OK && ferror(in)) {
        sc = FILE_READ_ERROR;
    }
    free(mesta);
    free(okno);
    return sc;
}

/* Поиск в одном файле по пути. Ошибки файла (не открылся, не читается) - это не ошибка
   функции: они записываются в fr->status, и функция возвращает OK, чтобы поиск
   в остальных файлах продолжился. Ошибкой функции считается только нехватка памяти.
   Файл открываем в режиме "rb" (двоичный): тогда на Windows "\r\n" не превращается
   в "\n", и позиции считаются одинаково на Windows и Linux ('\r' - обычный символ) */
status_code search_in_file(const char *put, const char *podstroka, const size_t m,
                           file_result *fr) {
    if (put == NULL || podstroka == NULL || fr == NULL || m == 0) {
        return INVALID_ARGS;
    }
    FILE *in = fopen(put, "rb");
    if (in == NULL) {
        fr->status = FILE_OPEN_ERROR;
        return OK;
    }
    status_code sc = search_in_stream(in, podstroka, m, fr);
    fclose(in);
    if (sc == FILE_READ_ERROR) {
        /* найденное до ошибки не показываем - ответ по этому файлу неполный */
        fr->status = FILE_READ_ERROR;
        free(fr->spisok);
        fr->spisok = NULL;
        fr->kolvo = 0;
        fr->razmer = 0;
        return OK;
    }
    if (sc != OK) {
        return sc;   /* ALLOC_ERROR */
    }
    fr->status = OK;
    return OK;
}

/* Главная функция задания: поиск подстроки podstroka в kolvo_faylov файлах,
   пути к которым переданы после kolvo_faylov (каждый - строка char *).
   Как работает переменное число аргументов:
     va_list args          - "указатель" на безымянные аргументы;
     va_start(args, kolvo_faylov) - встать сразу после последнего именованного параметра;
     va_arg(args, const char *)   - взять следующий аргумент этого типа;
     va_end(args)          - закончить (вызывать обязательно, на любом выходе).
   Сколько аргументов передали, функция сама узнать не может - поэтому их число
   передается параметром kolvo_faylov. Передать меньше путей, чем kolvo_faylov, нельзя:
   va_arg прочитает мусор (неопределенное поведение), это ошибка вызывающего.
   Результат - в *rez (освобождать free_search_result); при ошибке функции *rez пустой */
status_code find_substring(search_result *rez, const char *podstroka, const size_t kolvo_faylov,
                           ...) {
    if (rez == NULL || podstroka == NULL || kolvo_faylov == 0) {
        return INVALID_ARGS;
    }
    rez->faily = NULL;
    rez->kolvo = 0;
    size_t m = strlen(podstroka);
    if (m == 0) {
        return EMPTY_SUBSTRING;   /* пустая подстрока "входит" везде - такой поиск бессмыслен */
    }
    if (kolvo_faylov > SIZE_MAX / sizeof(file_result)) {
        return ALLOC_ERROR;
    }
    file_result *faily = (file_result *)malloc(kolvo_faylov * sizeof(file_result));
    if (faily == NULL) {
        return ALLOC_ERROR;
    }
    /* сначала все файлы "пустые": тогда free_search_result при ошибке посередине
       может освобождать все подряд (free(NULL) ничего не делает) */
    for (size_t i = 0; i < kolvo_faylov; i++) {
        faily[i].put = NULL;
        faily[i].status = OK;
        faily[i].spisok = NULL;
        faily[i].kolvo = 0;
        faily[i].razmer = 0;
    }
    rez->faily = faily;
    rez->kolvo = kolvo_faylov;

    va_list args;
    va_start(args, kolvo_faylov);
    status_code sc = OK;
    for (size_t i = 0; i < kolvo_faylov; i++) {
        const char *put = va_arg(args, const char *);
        if (put == NULL) {
            sc = INVALID_ARGS;
            break;
        }
        sc = copy_string(put, &faily[i].put);
        if (sc != OK) {
            break;
        }
        sc = search_in_file(put, podstroka, m, &faily[i]);
        if (sc != OK) {
            break;
        }
    }
    va_end(args);
    if (sc != OK) {
        free_search_result(rez);   /* освобождает все, что успели выделить, и обнуляет rez */
        return sc;
    }
    return OK;
}

/* ===================== ввод и вывод (только для main) ===================== */

/* Подстрока из командной строки -> настоящая строка: "\n" (два символа: '\' и 'n')
   превращается в один символ перевода строки, "\t" - в табуляцию, "\\" - в одну '\'.
   Иначе перевод строки в аргумент программы не передать. Другая буква после '\'
   (или '\' в самом конце) - BAD_ESCAPE. Результат в динамической памяти (free) */
status_code parse_escapes(const char *str, char **rez) {
    if (str == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    /* результат не длиннее исходной строки: каждая пара "\x" дает один символ */
    char *buf = (char *)malloc(strlen(str) + 1);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    size_t k = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] != '\\') {
            buf[k] = str[i];
            k++;
            continue;
        }
        /* str[i] == '\': смотрим следующую букву. Если строка кончилась (str[i + 1] - '\0'),
           ни один case не подойдет - default */
        switch (str[i + 1]) {
            case 'n':
                buf[k] = '\n';
                break;
            case 't':
                buf[k] = '\t';
                break;
            case '\\':
                buf[k] = '\\';
                break;
            default:
                free(buf);
                return BAD_ESCAPE;
        }
        k++;
        i++;   /* букву после '\' уже обработали - пропускаем ее */
    }
    buf[k] = '\0';
    *rez = buf;
    return OK;
}

/* Напечатать строку так, чтобы были видны невидимые символы: перевод строки - как \n,
   табуляция - как \t, '\' - как \\ (обратное действие к parse_escapes) */
status_code print_escaped(FILE *out, const char *str) {
    if (out == NULL || str == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            fputs("\\n", out);
        } else if (str[i] == '\t') {
            fputs("\\t", out);
        } else if (str[i] == '\\') {
            fputs("\\\\", out);
        } else {
            fputc(str[i], out);
        }
    }
    return OK;
}

/* Напечатать результат поиска: для каждого файла - сколько вхождений и где они.
   Найденное - в out (stdout), ошибки файлов - в err (stderr).
   *kolvo_oshibok - сколько файлов не удалось открыть или прочитать (для кода возврата) */
status_code print_result(FILE *out, FILE *err, const search_result *rez, size_t *kolvo_oshibok) {
    if (out == NULL || err == NULL || rez == NULL || kolvo_oshibok == NULL) {
        return INVALID_ARGS;
    }
    *kolvo_oshibok = 0;
    for (size_t i = 0; i < rez->kolvo; i++) {
        const file_result *fr = &rez->faily[i];
        if (fr->status == FILE_OPEN_ERROR) {
            fprintf(err, "Error: can not open file '%s'\n", fr->put);
            (*kolvo_oshibok)++;
            continue;
        }
        if (fr->status == FILE_READ_ERROR) {
            fprintf(err, "Error: can not read file '%s'\n", fr->put);
            (*kolvo_oshibok)++;
            continue;
        }
        /* "1 match", но "2 matches" и "no matches" */
        if (fr->kolvo == 0) {
            fprintf(out, "File '%s': no matches\n", fr->put);
        } else if (fr->kolvo == 1) {
            fprintf(out, "File '%s': 1 match\n", fr->put);
        } else {
            fprintf(out, "File '%s': %zu matches\n", fr->put, fr->kolvo);
        }
        for (size_t j = 0; j < fr->kolvo; j++) {
            fprintf(out, "  line %zu, position %zu\n", fr->spisok[j].stroka,
                    fr->spisok[j].pozitsiya);
        }
    }
    return OK;
}

/* Один пример демонстрации: заголовок с подстрокой, потом результат (или ошибка функции).
   Сам вызов find_substring делает demo (там список файлов записан прямо в вызове),
   здесь - только печать. sc - что вернула find_substring */
status_code print_demo(const char *podstroka, const status_code sc, const search_result *rez) {
    if (podstroka == NULL || rez == NULL) {
        return INVALID_ARGS;
    }
    printf("Search \"");
    print_escaped(stdout, podstroka);
    printf("\":\n");
    if (sc == OK) {
        size_t kolvo_oshibok = 0;
        /* в демонстрации ошибки файлов тоже печатаем в stdout - чтобы все шло по порядку */
        print_result(stdout, stdout, rez, &kolvo_oshibok);
    } else if (sc == EMPTY_SUBSTRING) {
        printf("Error: the substring must not be empty\n");
    } else if (sc == ALLOC_ERROR) {
        printf("Error: not enough memory\n");
    } else {
        printf("Error: internal error\n");
    }
    printf("\n");
    return OK;
}

/* Демонстрация: несколько вызовов find_substring с разным числом файлов, записанных
   прямо в вызове (для этого функция с переменным числом аргументов и нужна).
   Файлы лежат в tests/data/ - программу запускают из папки лабораторной */
status_code demo(void) {
    search_result rez;
    status_code sc;

    /* 1) обычное слово в двух файлах */
    sc = find_substring(&rez, "cat", 2, "tests/data/task3_text.txt", "tests/data/task3_cats.txt");
    print_demo("cat", sc, &rez);
    free_search_result(&rez);

    /* 2) подстрока с переводом строки: конец одной строки и начало следующей */
    sc = find_substring(&rez, "dog\nThe", 1, "tests/data/task3_text.txt");
    print_demo("dog\nThe", sc, &rez);
    free_search_result(&rez);

    /* 3) перекрывающиеся вхождения: "aa" в "aaaa" - три раза */
    sc = find_substring(&rez, "aa", 1, "tests/data/task3_aaaa.txt");
    print_demo("aa", sc, &rez);
    free_search_result(&rez);

    /* 4) три файла, один из них не существует - остальные все равно обработаны */
    sc = find_substring(&rez, "the", 3, "tests/data/task3_text.txt", "tests/data/task3_nofile.txt",
                        "tests/data/task3_cats.txt");
    print_demo("the", sc, &rez);
    free_search_result(&rez);

    /* 5) пустая подстрока - ошибка функции */
    sc = find_substring(&rez, "", 1, "tests/data/task3_text.txt");
    print_demo("", sc, &rez);
    free_search_result(&rez);
    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    /* 1) Без аргументов - демонстрация */
    if (argc == 1) {
        demo();
        return 0;
    }
    /* 2) С аргументами: подстрока и хотя бы один файл */
    if (argc < 3) {
        fprintf(stderr, "Usage: lab2_task3                     (demo)\n");
        fprintf(stderr, "       lab2_task3 <substring> <file1> [file2 ...]\n");
        fprintf(stderr, "In the substring \\n is a new line, \\t is a tab, \\\\ is \\\n");
        return 1;
    }

    /* подстрока: \n, \t, \\ -> настоящие символы */
    char *podstroka = NULL;
    status_code sc = parse_escapes(argv[1], &podstroka);
    if (sc == BAD_ESCAPE) {
        /* "\x", "\" в конце: понятны только \n, \t и \\ */
        fprintf(stderr, "Error: wrong '\\' in the substring, use only \\n, \\t or \\\\\n");
        return 1;
    }
    if (sc != OK) {
        /* ALLOC_ERROR; INVALID_ARGS быть не может: argv[1] не NULL */
        fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: internal error\n");
        return 1;
    }
    if (podstroka[0] == '\0') {
        /* пустой аргумент "" - пустая подстрока */
        fprintf(stderr, "Error: the substring must not be empty\n");
        free(podstroka);
        return 1;
    }

    /* 3) Файлы обрабатываем по одному: find_substring(&rez, podstroka, 1, argv[i]).
       Число аргументов "..." должно быть известно при компиляции, а файлов в командной
       строке может быть сколько угодно - поэтому вызов на каждый файл.
       Ошибка одного файла не останавливает остальные; код возврата 1, если ошибки были */
    int kod = 0;
    for (int i = 2; i < argc; i++) {
        search_result rez;
        sc = find_substring(&rez, podstroka, 1, argv[i]);
        if (sc != OK) {
            /* ALLOC_ERROR; INVALID_ARGS и EMPTY_SUBSTRING уже исключены выше */
            fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n"
                                              : "Error: internal error\n");
            free(podstroka);
            return 1;
        }
        size_t kolvo_oshibok = 0;
        print_result(stdout, stderr, &rez, &kolvo_oshibok);
        if (kolvo_oshibok > 0) {
            kod = 1;
        }
        free_search_result(&rez);
    }
    free(podstroka);
    return kod;
}
