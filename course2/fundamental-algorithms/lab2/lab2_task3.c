/*
 * Лабораторная 2, задание 3. Поиск подстроки в файлах.
 *
 * Главная функция задания - с переменным числом аргументов:
 *     find_substring(&result, substring, file_count, path1, path2, ...)
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
    size_t line;
    size_t position;
} occurrence;

/* Результат поиска в одном файле */
typedef struct {
    char *path;                 /* путь к файлу (своя копия в динамической памяти) */
    status_code status;        /* OK, FILE_OPEN_ERROR или FILE_READ_ERROR */
    occurrence *list;        /* найденные вхождения по порядку (NULL, если нет ни одного) */
    size_t count;              /* сколько вхождений найдено */
    size_t size;             /* сколько элементов выделено в list */
} file_result;

/* Результат всего поиска: по одному file_result на каждый файл, в порядке аргументов */
typedef struct {
    file_result *files;
    size_t count;
} search_result;

/* ===================== память ===================== */

/* Копия строки в динамической памяти (освобождает вызывающий) */
status_code copy_string(const char *str, char **copy) {
    if (str == NULL || copy == NULL) {
        return INVALID_ARGS;
    }
    size_t length = strlen(str);
    char *new_str = (char *)malloc(length + 1);   /* + 1 байт на '\0' */
    if (new_str == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(new_str, str, length + 1);   /* копируем вместе с '\0' */
    *copy = new_str;
    return OK;
}

/* Освободить результат поиска: у каждого файла - путь и список вхождений, потом сам массив.
   free(NULL) ничего не делает, поэтому недозаполненные файлы (NULL-указатели) тоже можно */
status_code free_search_result(search_result *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    if (result->files != NULL) {
        for (size_t i = 0; i < result->count; i++) {
            free(result->files[i].path);
            free(result->files[i].list);
        }
        free(result->files);
    }
    result->files = NULL;
    result->count = 0;
    return OK;
}

/* Добавить вхождение в конец списка файла. Список растет удвоением: 8, 16, 32, ... */
status_code add_occurrence(file_result *fr, const size_t line, const size_t position) {
    if (fr == NULL) {
        return INVALID_ARGS;
    }
    if (fr->count == fr->size) {
        /* новый размер: 8 для начала, потом вдвое больше; проверяем, что
           (новый размер * размер элемента) помещается в size_t */
        size_t new_size = (fr->size == 0) ? 8 : fr->size * 2;
        if (fr->size > SIZE_MAX / 2 || new_size > SIZE_MAX / sizeof(occurrence)) {
            return ALLOC_ERROR;
        }
        /* realloc - во временную переменную: если он вернет NULL, старый список
           останется на месте, и его освободит free_search_result.
           realloc(NULL, ...) работает как malloc - так создается первый список */
        occurrence *new_ptr = (occurrence *)realloc(fr->list, new_size * sizeof(occurrence));
        if (new_ptr == NULL) {
            return ALLOC_ERROR;
        }
        fr->list = new_ptr;
        fr->size = new_size;
    }
    fr->list[fr->count].line = line;
    fr->list[fr->count].position = position;
    fr->count++;
    return OK;
}

/* ===================== поиск ===================== */

/* Поиск всех вхождений подстроки (длина m >= 1) в открытом файле.
   Файл читаем по одному символу. В памяти держим "окно" - последние m прочитанных
   символов (массив window) и для каждого из них его строку и позицию (массив positions).
   Окно - кольцевое: символ номер k (считая с 0 от начала файла) лежит в клетке k % m,
   новый символ записывается на место самого старого. Когда прочитано уже m символов,
   самый старый символ окна лежит в клетке consumed % m - с него и сравниваем
   окно с подстрокой посимвольно (сами, без strncmp). Совпало все - вхождение,
   его начало - строка и позиция самого старого символа окна.
   Пример, подстрока "aba" (m = 3), файл "xabab":
       прочитали x a b  -> окно "xab" - не совпало
       прочитали a      -> окно "aba" (x вытеснен) - совпало, начало: строка 1, позиция 2
       прочитали b      -> окно "bab" - не совпало
   Строка и позиция: после '\n' начинается новая строка (line + 1, position = 1),
   после любого другого символа - position + 1. Позиция считается в байтах:
   русская буква в UTF-8 - 2 байта, то есть 2 позиции.
   Результат дописывается в fr; файл не закрывает - его закроет вызывающий */
status_code search_in_stream(FILE *in, const char *substring, const size_t m, file_result *fr) {
    if (in == NULL || substring == NULL || fr == NULL || m == 0) {
        return INVALID_ARGS;
    }
    /* m <= длина строки substring, которая уже лежит в памяти, так что
       m * sizeof(occurrence) переполниться не может только если проверим */
    if (m > SIZE_MAX / sizeof(occurrence)) {
        return ALLOC_ERROR;
    }
    char *window = (char *)malloc(m);
    if (window == NULL) {
        return ALLOC_ERROR;
    }
    occurrence *positions = (occurrence *)malloc(m * sizeof(occurrence));
    if (positions == NULL) {
        free(window);
        return ALLOC_ERROR;
    }

    size_t line = 1;         /* где стоит символ, который сейчас прочитаем */
    size_t position = 1;
    size_t consumed = 0;     /* сколько символов прочитано (до m - пока окно не заполнено) */
    size_t cell = 0;         /* клетка окна, куда пишем новый символ (= номер символа % m) */
    status_code sc = OK;
    int c;
    /* c - int, а не char: fgetc возвращает байт 0..255 или EOF (-1), в char их не различить */
    while ((c = fgetc(in)) != EOF) {
        /* 1) кладем символ в окно (на место самого старого) вместе с его местом в файле */
        window[cell] = (char)c;
        positions[cell].line = line;
        positions[cell].position = position;
        cell++;
        if (cell == m) {
            cell = 0;        /* дошли до конца массива - по кругу в начало */
        }
        if (consumed < m) {
            consumed++;
        }

        /* 2) окно заполнено - сравниваем его с подстрокой. Самый старый символ теперь
           в клетке cell (туда запишется следующий символ) */
        if (consumed == m) {
            int matched = 1;
            size_t k = cell;
            for (size_t j = 0; j < m; j++) {
                if (window[k] != substring[j]) {
                    matched = 0;
                    break;   /* первое несовпадение - дальше сравнивать незачем */
                }
                k++;
                if (k == m) {
                    k = 0;
                }
            }
            if (matched) {
                sc = add_occurrence(fr, positions[cell].line, positions[cell].position);
                if (sc != OK) {
                    break;   /* ALLOC_ERROR: память освободим ниже, после цикла */
                }
            }
        }

        /* 3) место следующего символа */
        if (c == '\n') {
            line++;
            position = 1;
        } else {
            position++;
        }
    }
    /* EOF бывает и в конце файла, и при ошибке чтения - различает их ferror */
    if (sc == OK && ferror(in)) {
        sc = FILE_READ_ERROR;
    }
    free(positions);
    free(window);
    return sc;
}

/* Поиск в одном файле по пути. Ошибки файла (не открылся, не читается) - это не ошибка
   функции: они записываются в fr->status, и функция возвращает OK, чтобы поиск
   в остальных файлах продолжился. Ошибкой функции считается только нехватка памяти.
   Файл открываем в режиме "rb" (двоичный): тогда на Windows "\r\n" не превращается
   в "\n", и позиции считаются одинаково на Windows и Linux ('\r' - обычный символ) */
status_code search_in_file(const char *path, const char *substring, const size_t m,
                           file_result *fr) {
    if (path == NULL || substring == NULL || fr == NULL || m == 0) {
        return INVALID_ARGS;
    }
    FILE *in = fopen(path, "rb");
    if (in == NULL) {
        fr->status = FILE_OPEN_ERROR;
        return OK;
    }
    status_code sc = search_in_stream(in, substring, m, fr);
    fclose(in);
    if (sc == FILE_READ_ERROR) {
        /* найденное до ошибки не показываем - ответ по этому файлу неполный */
        fr->status = FILE_READ_ERROR;
        free(fr->list);
        fr->list = NULL;
        fr->count = 0;
        fr->size = 0;
        return OK;
    }
    if (sc != OK) {
        return sc;   /* ALLOC_ERROR */
    }
    fr->status = OK;
    return OK;
}

/* Главная функция задания: поиск подстроки substring в file_count файлах,
   пути к которым переданы после file_count (каждый - строка char *).
   Как работает переменное число аргументов:
     va_list args          - "указатель" на безымянные аргументы;
     va_start(args, file_count) - встать сразу после последнего именованного параметра;
     va_arg(args, const char *)   - взять следующий аргумент этого типа;
     va_end(args)          - закончить (вызывать обязательно, на любом выходе).
   Сколько аргументов передали, функция сама узнать не может - поэтому их число
   передается параметром file_count. Передать меньше путей, чем file_count, нельзя:
   va_arg прочитает мусор (неопределенное поведение), это ошибка вызывающего.
   Результат - в *result (освобождать free_search_result); при ошибке функции *result пустой */
status_code find_substring(search_result *result, const char *substring, const size_t file_count,
                           ...) {
    if (result == NULL || substring == NULL || file_count == 0) {
        return INVALID_ARGS;
    }
    result->files = NULL;
    result->count = 0;
    size_t m = strlen(substring);
    if (m == 0) {
        return EMPTY_SUBSTRING;   /* пустая подстрока "входит" везде - такой поиск бессмыслен */
    }
    if (file_count > SIZE_MAX / sizeof(file_result)) {
        return ALLOC_ERROR;
    }
    file_result *files = (file_result *)malloc(file_count * sizeof(file_result));
    if (files == NULL) {
        return ALLOC_ERROR;
    }
    /* сначала все файлы "пустые": тогда free_search_result при ошибке посередине
       может освобождать все подряд (free(NULL) ничего не делает) */
    for (size_t i = 0; i < file_count; i++) {
        files[i].path = NULL;
        files[i].status = OK;
        files[i].list = NULL;
        files[i].count = 0;
        files[i].size = 0;
    }
    result->files = files;
    result->count = file_count;

    va_list args;
    va_start(args, file_count);
    status_code sc = OK;
    for (size_t i = 0; i < file_count; i++) {
        const char *path = va_arg(args, const char *);
        if (path == NULL) {
            sc = INVALID_ARGS;
            break;
        }
        sc = copy_string(path, &files[i].path);
        if (sc != OK) {
            break;
        }
        sc = search_in_file(path, substring, m, &files[i]);
        if (sc != OK) {
            break;
        }
    }
    va_end(args);
    if (sc != OK) {
        free_search_result(result);   /* освобождает все, что успели выделить, и обнуляет result */
        return sc;
    }
    return OK;
}

/* ===================== ввод и вывод (только для main) ===================== */

/* Подстрока из командной строки -> настоящая строка: "\n" (два символа: '\' и 'n')
   превращается в один символ перевода строки, "\t" - в табуляцию, "\\" - в одну '\'.
   Иначе перевод строки в аргумент программы не передать. Другая буква после '\'
   (или '\' в самом конце) - BAD_ESCAPE. Результат в динамической памяти (free) */
status_code parse_escapes(const char *str, char **result) {
    if (str == NULL || result == NULL) {
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
    *result = buf;
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
   *error_count - сколько файлов не удалось открыть или прочитать (для кода возврата) */
status_code print_result(FILE *out, FILE *err, const search_result *result, size_t *error_count) {
    if (out == NULL || err == NULL || result == NULL || error_count == NULL) {
        return INVALID_ARGS;
    }
    *error_count = 0;
    for (size_t i = 0; i < result->count; i++) {
        const file_result *fr = &result->files[i];
        if (fr->status == FILE_OPEN_ERROR) {
            fprintf(err, "Error: can not open file '%s'\n", fr->path);
            (*error_count)++;
            continue;
        }
        if (fr->status == FILE_READ_ERROR) {
            fprintf(err, "Error: can not read file '%s'\n", fr->path);
            (*error_count)++;
            continue;
        }
        /* "1 match", но "2 matches" и "no matches" */
        if (fr->count == 0) {
            fprintf(out, "File '%s': no matches\n", fr->path);
        } else if (fr->count == 1) {
            fprintf(out, "File '%s': 1 match\n", fr->path);
        } else {
            fprintf(out, "File '%s': %zu matches\n", fr->path, fr->count);
        }
        for (size_t j = 0; j < fr->count; j++) {
            fprintf(out, "  line %zu, position %zu\n", fr->list[j].line,
                    fr->list[j].position);
        }
    }
    return OK;
}

/* Один пример демонстрации: заголовок с подстрокой, потом результат (или ошибка функции).
   Сам вызов find_substring делает demo (там список файлов записан прямо в вызове),
   здесь - только печать. sc - что вернула find_substring */
status_code print_demo(const char *substring, const status_code sc, const search_result *result) {
    if (substring == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    printf("Search \"");
    print_escaped(stdout, substring);
    printf("\":\n");
    if (sc == OK) {
        size_t error_count = 0;
        /* в демонстрации ошибки файлов тоже печатаем в stdout - чтобы все шло по порядку */
        print_result(stdout, stdout, result, &error_count);
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
    search_result result;
    status_code sc;

    /* 1) обычное слово в двух файлах */
    sc = find_substring(&result, "cat", 2, "tests/data/task3_text.txt", "tests/data/task3_cats.txt");
    print_demo("cat", sc, &result);
    free_search_result(&result);

    /* 2) подстрока с переводом строки: конец одной строки и начало следующей */
    sc = find_substring(&result, "dog\nThe", 1, "tests/data/task3_text.txt");
    print_demo("dog\nThe", sc, &result);
    free_search_result(&result);

    /* 3) перекрывающиеся вхождения: "aa" в "aaaa" - три раза */
    sc = find_substring(&result, "aa", 1, "tests/data/task3_aaaa.txt");
    print_demo("aa", sc, &result);
    free_search_result(&result);

    /* 4) три файла, один из них не существует - остальные все равно обработаны */
    sc = find_substring(&result, "the", 3, "tests/data/task3_text.txt", "tests/data/task3_nofile.txt",
                        "tests/data/task3_cats.txt");
    print_demo("the", sc, &result);
    free_search_result(&result);

    /* 5) пустая подстрока - ошибка функции */
    sc = find_substring(&result, "", 1, "tests/data/task3_text.txt");
    print_demo("", sc, &result);
    free_search_result(&result);
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
    char *substring = NULL;
    status_code sc = parse_escapes(argv[1], &substring);
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
    if (substring[0] == '\0') {
        /* пустой аргумент "" - пустая подстрока */
        fprintf(stderr, "Error: the substring must not be empty\n");
        free(substring);
        return 1;
    }

    /* 3) Файлы обрабатываем по одному: find_substring(&result, substring, 1, argv[i]).
       Число аргументов "..." должно быть известно при компиляции, а файлов в командной
       строке может быть сколько угодно - поэтому вызов на каждый файл.
       Ошибка одного файла не останавливает остальные; код возврата 1, если ошибки были */
    int exit_code = 0;
    for (int i = 2; i < argc; i++) {
        search_result result;
        sc = find_substring(&result, substring, 1, argv[i]);
        if (sc != OK) {
            /* ALLOC_ERROR; INVALID_ARGS и EMPTY_SUBSTRING уже исключены выше */
            fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n"
                                              : "Error: internal error\n");
            free(substring);
            return 1;
        }
        size_t error_count = 0;
        print_result(stdout, stderr, &result, &error_count);
        if (error_count > 0) {
            exit_code = 1;
        }
        free_search_result(&result);
    }
    free(substring);
    return exit_code;
}
