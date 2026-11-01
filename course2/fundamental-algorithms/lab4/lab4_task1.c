/*
 * Лабораторная 4, задание 1. Макрозамены в тексте (как #define в языке C) на хеш-таблице.
 *
 * Запуск:  lab4_task1 <файл> [--stats]
 * Файл начинается с директив "#define <имя> <значение>", дальше - обычный текст. Программа печатает
 * текст (без директив), заменив в нём каждое имя макроса на его значение.
 *   - Имя - латинские буквы и цифры, прописные и строчные различаются (Pi и PI - разные имена).
 *   - Значение - всё после имени и пробелов до конца строки (может быть пустым и содержать что угодно).
 *   - Заменяются только целые слова: слово - самая длинная цепочка букв и цифр. Имя PI заменяется
 *     в "PI+1" и "(PI)", но не в "PIE" и не в "xPI" - как в препроцессоре C.
 *   - Вставленное значение повторно не просматривается: директивы не вкладываются друг в друга.
 *   - Повторная директива с тем же именем заменяет значение (действует последняя).
 *   - Раздел директив кончается на первой строке, которая не начинается с "#define".
 * С флагом --stats перед текстом печатается состояние хеш-таблицы: размер, длины цепочек, число пересборок.
 *
 * Хеш-таблица (метод цепочек). Имя читается как число в системе счисления с основанием 62
 * (цифры 0-9 = 0..9, A-Z = 10..35, a-z = 36..61), хеш - остаток от деления этого числа на HASHSIZE.
 * Число для длинного имени огромное, поэтому считаем его по модулю 2^64: unsigned long long при
 * переполнении сам отбрасывает старшие разряды, это и есть остаток от деления на 2^64.
 * HASHSIZE у нас всегда степень двойки (128, 256, ...), а для делителя 2^k верно
 *     число mod 2^k = (число mod 2^64) mod 2^k,
 * поэтому хеш получается точно таким, как требует условие.
 *
 * Кэширование: (число mod 2^64) считается один раз, когда макрос добавляется, и хранится в нём.
 * При пересборке таблицы с другим HASHSIZE новый хеш - один остаток от деления, без прохода по имени.
 *
 * Равномерность: после каждого изменения таблицы сравниваются самая короткая и самая длинная цепочки.
 * Пустые корзины не считаются: пока макросов меньше, чем корзин, пустые корзины неизбежны, и по букве
 * условия таблица была бы "неравномерной" всегда. Если самая длинная цепочка в 2 раза длиннее самой
 * короткой или больше, перебираем размеры 128, 256, ..., MAX_HASHSIZE и берём самый маленький, при
 * котором распределение равномерное; если такого нет - самый равномерный из них, и только если он
 * лучше текущего (иначе пересобирать бесполезно).
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free */
#include <string.h>   /* strcmp, memcpy */

#define START_HASHSIZE 128     /* начальный размер таблицы - по условию */
#define MAX_HASHSIZE 65536     /* наибольший размер, который пробуем при пересборке */
#define START_CAPACITY 64      /* начальный размер буфера строки; дальше растёт вдвое */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    WRONG_DIRECTIVE,   /* строка начинается с #define, но директива записана неверно */
    FILE_ERROR,        /* ошибка чтения или записи */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Один макрос: элемент цепочки в корзине хеш-таблицы */
typedef struct macro {
    char *name;
    size_t name_length;
    char *value;
    unsigned long long cached;   /* имя как число по основанию 62, по модулю 2^64 (кэш для пересборки) */
    struct macro *next;          /* следующий макрос в той же корзине */
} macro;

/* Хеш-таблица: массив корзин, в каждой - цепочка (односвязный список) макросов */
typedef struct {
    macro **buckets;
    size_t size;        /* HASHSIZE - число корзин, всегда степень двойки */
    size_t count;       /* сколько макросов в таблице */
    size_t rebuilds;    /* сколько раз таблица пересобиралась */
} hash_table;

/* Растущий буфер строки: строка ввода или результат замен */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} buffer;

/* ===================== имена и хеш ===================== */

/*
 * Цифра символа в системе счисления с основанием 62: 0-9 -> 0..9, A-Z -> 10..35, a-z -> 36..61.
 * Символ, который не может входить в имя, -> *digit = -1.
 * Буквы сравниваются явно (а не isalpha): isalpha зависит от локали и могла бы принять русские буквы.
 */
status_code base62_digit(const char c, int *digit) {
    if (digit == NULL) {
        return INVALID_ARGS;
    }
    if (c >= '0' && c <= '9') {
        *digit = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        *digit = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'z') {
        *digit = c - 'a' + 36;
    } else {
        *digit = -1;
    }
    return OK;
}

/*
 * Имя как число по основанию 62, по модулю 2^64. Схема Горнера: число = (...(d1 * 62 + d2) * 62 + ...).
 * Переполнение unsigned long long определено стандартом: результат берётся по модулю 2^64.
 */
status_code name_number(const char *name, const size_t length, unsigned long long *number) {
    if (name == NULL || number == NULL || length == 0) {
        return INVALID_ARGS;
    }
    unsigned long long result = 0;
    for (size_t i = 0; i < length; i++) {
        int digit = 0;
        base62_digit(name[i], &digit);
        if (digit < 0) {
            return INVALID_ARGS;   /* в имени только буквы и цифры - вызывающий это уже проверил */
        }
        result = result * 62ULL + (unsigned long long)digit;
    }
    *number = result;
    return OK;
}

/*
 * Конец цепочки символов одного вида, начиная с позиции start: name_chars = 1 - цепочка букв и цифр
 * (слово, имя), name_chars = 0 - цепочка остальных символов. *end - позиция первого символа другого вида.
 */
status_code run_end(const char *line, const size_t length, const size_t start, const int name_chars,
                    size_t *end) {
    if (line == NULL || end == NULL) {
        return INVALID_ARGS;
    }
    size_t i = start;
    while (i < length) {
        int digit = 0;
        base62_digit(line[i], &digit);
        if ((digit >= 0) != (name_chars != 0)) {
            break;   /* символ другого вида - цепочка кончилась */
        }
        i++;
    }
    *end = i;
    return OK;
}

/* Номер корзины: число mod size. size - степень двойки, поэтому остаток - это младшие биты числа */
status_code bucket_index(const unsigned long long number, const size_t size, size_t *index) {
    if (index == NULL || size == 0) {
        return INVALID_ARGS;
    }
    *index = (size_t)(number % (unsigned long long)size);
    return OK;
}

/* ===================== хеш-таблица ===================== */

status_code table_init(hash_table *table, const size_t size) {
    if (table == NULL || size == 0) {
        return INVALID_ARGS;
    }
    /* calloc обнуляет память: все корзины пустые (NULL) */
    table->buckets = (macro **)calloc(size, sizeof(macro *));
    if (table->buckets == NULL) {
        return ALLOC_ERROR;
    }
    table->size = size;
    table->count = 0;
    table->rebuilds = 0;
    return OK;
}

status_code table_free(hash_table *table) {
    if (table == NULL) {
        return INVALID_ARGS;
    }
    if (table->buckets != NULL) {
        for (size_t i = 0; i < table->size; i++) {
            macro *item = table->buckets[i];
            while (item != NULL) {
                macro *next = item->next;
                free(item->name);
                free(item->value);
                free(item);
                item = next;
            }
        }
        free(table->buckets);
    }
    table->buckets = NULL;
    table->size = 0;
    table->count = 0;
    return OK;
}

/*
 * Найти макрос с именем name (length символов; name может быть не завершена нулём - это кусок строки).
 * *found = NULL - такого макроса нет.
 */
status_code table_find(const hash_table *table, const char *name, const size_t length, macro **found) {
    if (table == NULL || name == NULL || found == NULL || length == 0) {
        return INVALID_ARGS;
    }
    unsigned long long number = 0;
    status_code sc = name_number(name, length, &number);
    if (sc != OK) {
        return sc;
    }
    size_t index = 0;
    bucket_index(number, table->size, &index);
    *found = NULL;
    /* идём по цепочке своей корзины; сначала сравниваем кэш (быстро), потом само имя */
    for (macro *item = table->buckets[index]; item != NULL; item = item->next) {
        if (item->cached == number && item->name_length == length && memcmp(item->name, name, length) == 0) {
            *found = item;
            break;
        }
    }
    return OK;
}

/*
 * Длины самой короткой и самой длинной непустой цепочки, если бы в таблице было size корзин.
 * Хеши берутся из кэша (cached mod size) - имена заново не просматриваются.
 * Пустая таблица -> обе длины 0.
 */
status_code chain_lengths(const hash_table *table, const size_t size, size_t *shortest, size_t *longest) {
    if (table == NULL || shortest == NULL || longest == NULL || size == 0) {
        return INVALID_ARGS;
    }
    size_t *counts = (size_t *)calloc(size, sizeof(size_t));
    if (counts == NULL) {
        return ALLOC_ERROR;
    }
    /* раскладываем все макросы по корзинам воображаемой таблицы размера size */
    for (size_t i = 0; i < table->size; i++) {
        for (const macro *item = table->buckets[i]; item != NULL; item = item->next) {
            size_t index = 0;
            bucket_index(item->cached, size, &index);
            counts[index]++;
        }
    }
    *shortest = 0;
    *longest = 0;
    for (size_t i = 0; i < size; i++) {
        if (counts[i] == 0) {
            continue;   /* пустые корзины не считаем (см. комментарий в начале файла) */
        }
        if (*shortest == 0 || counts[i] < *shortest) {
            *shortest = counts[i];
        }
        if (counts[i] > *longest) {
            *longest = counts[i];
        }
    }
    free(counts);
    return OK;
}

/*
 * Сравнить два распределения: (shortest1, longest1) равномернее, чем (shortest2, longest2)?
 * Сравниваем отношения longest / shortest без деления: a/b < c/d  <=>  a*d < c*b (все числа > 0).
 * При равных отношениях лучше то, где самая длинная цепочка короче (поиск быстрее).
 */
status_code more_even(const size_t shortest1, const size_t longest1, const size_t shortest2,
                      const size_t longest2, int *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    unsigned long long left = (unsigned long long)longest1 * shortest2;
    unsigned long long right = (unsigned long long)longest2 * shortest1;
    *result = left < right || (left == right && longest1 < longest2);
    return OK;
}

/*
 * Пересобрать таблицу с новым размером new_size. Макросы не копируются: те же элементы
 * перевешиваются в новые корзины, номер корзины - из кэша (cached mod new_size).
 */
status_code table_rebuild(hash_table *table, const size_t new_size) {
    if (table == NULL || new_size == 0) {
        return INVALID_ARGS;
    }
    macro **new_buckets = (macro **)calloc(new_size, sizeof(macro *));
    if (new_buckets == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < table->size; i++) {
        macro *item = table->buckets[i];
        while (item != NULL) {
            macro *next = item->next;
            size_t index = 0;
            bucket_index(item->cached, new_size, &index);
            /* в начало цепочки новой корзины */
            item->next = new_buckets[index];
            new_buckets[index] = item;
            item = next;
        }
    }
    free(table->buckets);
    table->buckets = new_buckets;
    table->size = new_size;
    table->rebuilds++;
    return OK;
}

/*
 * Проверить равномерность и при необходимости пересобрать таблицу (см. комментарий в начале файла).
 * Таблица неравномерная, если самая длинная цепочка >= 2 * самая короткая.
 */
status_code table_balance(hash_table *table) {
    if (table == NULL) {
        return INVALID_ARGS;
    }
    size_t shortest = 0;
    size_t longest = 0;
    status_code sc = chain_lengths(table, table->size, &shortest, &longest);
    if (sc != OK) {
        return sc;
    }
    if (table->count == 0 || longest < 2 * shortest) {
        return OK;   /* распределение равномерное - ничего не делаем */
    }

    /* перебираем размеры 128, 256, ... и ищем самый маленький равномерный; заодно запоминаем лучший */
    size_t best_size = table->size;
    size_t best_shortest = shortest;
    size_t best_longest = longest;
    for (size_t size = START_HASHSIZE; size <= MAX_HASHSIZE; size *= 2) {
        if (size == table->size) {
            continue;
        }
        size_t try_shortest = 0;
        size_t try_longest = 0;
        sc = chain_lengths(table, size, &try_shortest, &try_longest);
        if (sc != OK) {
            return sc;
        }
        int better = 0;
        more_even(try_shortest, try_longest, best_shortest, best_longest, &better);
        if (better) {
            best_size = size;
            best_shortest = try_shortest;
            best_longest = try_longest;
        }
        if (try_longest < 2 * try_shortest) {
            best_size = size;   /* первый (самый маленький) равномерный размер - дальше не ищем */
            break;
        }
    }
    if (best_size == table->size) {
        return OK;   /* ни один размер не лучше текущего - пересобирать бесполезно */
    }
    return table_rebuild(table, best_size);
}

/*
 * Добавить макрос name = value (строки копируются). Если такое имя уже есть - заменить значение.
 * После изменения проверяется равномерность таблицы.
 */
status_code table_put(hash_table *table, const char *name, const size_t name_length, const char *value,
                      const size_t value_length) {
    if (table == NULL || name == NULL || value == NULL || name_length == 0) {
        return INVALID_ARGS;
    }
    /* копия значения (с нулём в конце) - нужна в обоих случаях */
    char *value_copy = (char *)malloc(value_length + 1);
    if (value_copy == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(value_copy, value, value_length);
    value_copy[value_length] = '\0';

    macro *found = NULL;
    status_code sc = table_find(table, name, name_length, &found);
    if (sc != OK) {
        free(value_copy);
        return sc;
    }
    if (found != NULL) {
        /* имя уже есть: меняем только значение (элемент тот же, корзина та же) */
        free(found->value);
        found->value = value_copy;
        return table_balance(table);
    }

    /* новый макрос - в начало цепочки своей корзины */
    macro *item = (macro *)malloc(sizeof(macro));
    char *name_copy = (char *)malloc(name_length + 1);
    if (item == NULL || name_copy == NULL) {
        free(item);
        free(name_copy);
        free(value_copy);
        return ALLOC_ERROR;
    }
    memcpy(name_copy, name, name_length);
    name_copy[name_length] = '\0';
    item->name = name_copy;
    item->name_length = name_length;
    item->value = value_copy;
    name_number(name, name_length, &item->cached);   /* кэш: считается один раз */
    size_t index = 0;
    bucket_index(item->cached, table->size, &index);
    item->next = table->buckets[index];
    table->buckets[index] = item;
    table->count++;
    return table_balance(table);
}

/* ===================== буфер и чтение строк ===================== */

status_code buffer_init(buffer *buf) {
    if (buf == NULL) {
        return INVALID_ARGS;
    }
    buf->data = (char *)malloc(START_CAPACITY);
    if (buf->data == NULL) {
        return ALLOC_ERROR;
    }
    buf->length = 0;
    buf->capacity = START_CAPACITY;
    buf->data[0] = '\0';
    return OK;
}

status_code buffer_free(buffer *buf) {
    if (buf == NULL) {
        return INVALID_ARGS;
    }
    free(buf->data);
    buf->data = NULL;
    buf->length = 0;
    buf->capacity = 0;
    return OK;
}

/* Дописать в буфер count символов; буфер растёт вдвое (realloc через временную переменную) */
status_code buffer_append(buffer *buf, const char *text, const size_t count) {
    if (buf == NULL || (text == NULL && count > 0)) {
        return INVALID_ARGS;
    }
    /* +1 - место для нуля в конце */
    if (buf->length + count + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity;
        while (buf->length + count + 1 > new_capacity) {
            if (new_capacity > (size_t)-1 / 2) {
                return ALLOC_ERROR;   /* дальше удваивать некуда */
            }
            new_capacity *= 2;
        }
        char *tmp = (char *)realloc(buf->data, new_capacity);
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        buf->data = tmp;
        buf->capacity = new_capacity;
    }
    if (count > 0) {
        memcpy(buf->data + buf->length, text, count);
    }
    buf->length += count;
    buf->data[buf->length] = '\0';
    return OK;
}

/*
 * Прочитать строку любой длины (без '\n') в буфер. *has_newline = 1 - строка кончилась '\n'
 * (в выводе его надо вернуть). *end = 1 - файл кончился и строки нет.
 */
status_code read_line(FILE *file, buffer *line, int *has_newline, int *end) {
    if (file == NULL || line == NULL || has_newline == NULL || end == NULL) {
        return INVALID_ARGS;
    }
    line->length = 0;
    line->data[0] = '\0';
    *has_newline = 0;
    *end = 0;
    int symbol = fgetc(file);
    if (symbol == EOF) {
        *end = 1;
        return ferror(file) ? FILE_ERROR : OK;
    }
    while (symbol != EOF && symbol != '\n') {
        char c = (char)symbol;
        status_code sc = buffer_append(line, &c, 1);
        if (sc != OK) {
            return sc;
        }
        symbol = fgetc(file);
    }
    if (symbol == '\n') {
        *has_newline = 1;
    } else if (ferror(file)) {
        return FILE_ERROR;
    }
    return OK;
}

/* ===================== директивы и замены ===================== */

/*
 * Разобрать строку как директиву "#define <имя> <значение>".
 * *is_directive = 0 - строка не директива (с неё начинается текст).
 * Иначе name / value - указатели внутрь line и их длины (строка не меняется).
 * Директивой считается строка, которая (после пробелов) начинается с "#define" и пробела или конца строки.
 * Возвращает WRONG_DIRECTIVE, если после #define нет имени или имя записано неверно.
 */
status_code parse_directive(const char *line, const size_t length, int *is_directive, const char **name,
                            size_t *name_length, const char **value, size_t *value_length) {
    if (line == NULL || is_directive == NULL || name == NULL || name_length == NULL || value == NULL ||
        value_length == NULL) {
        return INVALID_ARGS;
    }
    const char *keyword = "#define";
    const size_t keyword_length = strlen(keyword);
    size_t i = 0;
    /* пробелы и табуляции перед # допускаются, как в C */
    while (i < length && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    *is_directive = 0;
    if (length - i < keyword_length || memcmp(line + i, keyword, keyword_length) != 0) {
        return OK;
    }
    i += keyword_length;
    /* после "#define" - пробел или конец строки; "#defineX" - не директива, а текст */
    if (i < length && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
        return OK;
    }
    *is_directive = 1;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    /* имя: одна или больше букв/цифр */
    size_t start = i;
    run_end(line, length, start, 1, &i);
    if (i == start) {
        return WRONG_DIRECTIVE;   /* имени нет или оно начинается с недопустимого символа */
    }
    /* после имени - пробел или конец строки; "#define a-b 1" - ошибка */
    if (i < length && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
        return WRONG_DIRECTIVE;
    }
    *name = line + start;
    *name_length = i - start;
    /* значение: всё после пробелов до конца строки; '\r' от конца строки Windows не входит */
    while (i < length && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    size_t end = length;
    if (end > i && line[end - 1] == '\r') {
        end--;
    }
    *value = line + i;
    *value_length = end - i;
    return OK;
}

/*
 * Выполнить замены в одной строке текста: каждое слово (цепочка букв и цифр), которое является
 * именем макроса, заменяется значением. Результат - в буфер out (старое содержимое стирается).
 */
status_code substitute_line(const hash_table *table, const char *line, const size_t length, buffer *out) {
    if (table == NULL || line == NULL || out == NULL) {
        return INVALID_ARGS;
    }
    out->length = 0;
    out->data[0] = '\0';
    size_t i = 0;
    while (i < length) {
        /* не буквы и не цифры (пробелы, знаки, русские буквы) - копируем как есть до следующего слова */
        size_t start = i;
        run_end(line, length, start, 0, &i);
        status_code sc = buffer_append(out, line + start, i - start);
        if (sc != OK) {
            return sc;
        }
        if (i == length) {
            break;
        }
        /* слово - самая длинная цепочка букв и цифр: макрос -> значение, иначе само слово */
        start = i;
        run_end(line, length, start, 1, &i);
        macro *found = NULL;
        sc = table_find(table, line + start, i - start, &found);
        if (sc == OK) {
            if (found != NULL) {
                sc = buffer_append(out, found->value, strlen(found->value));
            } else {
                sc = buffer_append(out, line + start, i - start);
            }
        }
        if (sc != OK) {
            return sc;
        }
    }
    return OK;
}

/* ===================== вывод ===================== */

/* Состояние хеш-таблицы (флаг --stats) */
status_code print_stats(FILE *stream, const hash_table *table) {
    if (stream == NULL || table == NULL) {
        return INVALID_ARGS;
    }
    size_t shortest = 0;
    size_t longest = 0;
    status_code sc = chain_lengths(table, table->size, &shortest, &longest);
    if (sc != OK) {
        return sc;
    }
    fprintf(stream, "hash table size: %zu\n", table->size);
    fprintf(stream, "macros: %zu\n", table->count);
    fprintf(stream, "shortest chain: %zu, longest chain: %zu\n", shortest, longest);
    fprintf(stream, "rebuilds: %zu\n", table->rebuilds);
    fprintf(stream, "---\n");
    return OK;
}

/* Текст сообщения для статуса (для печати ошибок в main) */
status_code status_message(const status_code sc, const char **message) {
    if (message == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case WRONG_DIRECTIVE:
            *message = "wrong #define directive";
            break;
        case FILE_ERROR:
            *message = "can not read the file";
            break;
        case ALLOC_ERROR:
            *message = "not enough memory";
            break;
        default:
            /* OK и INVALID_ARGS сюда не попадают: аргументы функций задаёт сама программа */
            *message = "internal error";
            break;
    }
    return OK;
}

/*
 * Вся работа с открытым файлом: директивы -> таблица, затем строки текста -> замены -> stdout.
 * *line_number - номер строки, на которой случилась ошибка (для сообщения).
 */
status_code process_file(FILE *file, const int show_stats, size_t *line_number) {
    if (file == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    hash_table table;
    buffer line;
    buffer out;
    status_code sc = table_init(&table, START_HASHSIZE);
    if (sc != OK) {
        return sc;
    }
    sc = buffer_init(&line);
    if (sc != OK) {
        table_free(&table);
        return sc;
    }
    sc = buffer_init(&out);
    if (sc != OK) {
        buffer_free(&line);
        table_free(&table);
        return sc;
    }

    *line_number = 0;
    int in_directives = 1;   /* 1 - ещё идут директивы, 0 - начался текст */
    int stats_printed = 0;
    while (sc == OK) {
        int has_newline = 0;
        int end = 0;
        sc = read_line(file, &line, &has_newline, &end);
        if (sc != OK || end) {
            break;
        }
        (*line_number)++;

        if (in_directives) {
            int is_directive = 0;
            const char *name = NULL;
            const char *value = NULL;
            size_t name_length = 0;
            size_t value_length = 0;
            sc = parse_directive(line.data, line.length, &is_directive, &name, &name_length, &value,
                                 &value_length);
            if (sc == OK && is_directive) {
                sc = table_put(&table, name, name_length, value, value_length);
                continue;
            }
            if (sc != OK) {
                break;
            }
            in_directives = 0;   /* первая строка без #define - дальше только текст */
        }

        /* таблица уже окончательная (все директивы - в начале файла): печатаем её состояние один раз */
        if (show_stats && !stats_printed) {
            sc = print_stats(stdout, &table);
            stats_printed = 1;
            if (sc != OK) {
                break;
            }
        }
        sc = substitute_line(&table, line.data, line.length, &out);
        if (sc == OK) {
            fwrite(out.data, 1, out.length, stdout);
            if (has_newline) {
                fputc('\n', stdout);
            }
        }
    }
    /* файл только из директив (или пустой): состояние таблицы всё равно показываем */
    if (sc == OK && show_stats && !stats_printed) {
        sc = print_stats(stdout, &table);
    }

    buffer_free(&out);
    buffer_free(&line);
    table_free(&table);
    return sc;
}

int main(int argc, char *argv[]) {
    /* аргументы: файл и необязательный флаг --stats */
    int show_stats = 0;
    if (argc == 3 && strcmp(argv[2], "--stats") == 0) {
        show_stats = 1;
    } else if (argc != 2) {
        fprintf(stderr, "Usage: lab4_task1 <file> [--stats]\n");
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        return 1;
    }
    size_t line_number = 0;
    status_code sc = process_file(file, show_stats, &line_number);
    fclose(file);
    fflush(stdout);

    if (sc != OK) {
        const char *message = NULL;
        status_message(sc, &message);
        /* номер строки нужен только там, где ошибка связана с содержимым файла */
        if (sc == WRONG_DIRECTIVE) {
            fprintf(stderr, "Error: %s in line %zu\n", message, line_number);
        } else {
            fprintf(stderr, "Error: %s\n", message);
        }
        return 1;
    }
    return 0;
}
