/*
 * Лабораторная 3, задание 5. Студенты: поиск, сортировка (qsort), трассировочный файл.
 *
 * Запуск:  lab3_task5 <файл со студентами> <трассировочный файл>
 * Файл со студентами - по одному студенту в строке:
 *     id имя фамилия группа оценка1 оценка2 оценка3 оценка4 оценка5
 *     id - целое >= 0 (у каждого свое); имя и фамилия - только латинские буквы;
 *     группа - непустое слово без пробелов; оценки - целые 0..100. Пустые строки пропускаются.
 * Потом диалог - команды со стандартного ввода, по одной в строке:
 *     find id <id> | find surname <s> | find name <s> | find group <g>   - поиск, вывод на экран
 *     sort id | sort surname | sort name | sort group                    - сортировка и вывод
 *     list                     - все студенты в текущем порядке
 *     trace <id>               - в трассировочный файл: фамилия и имя, группа, средняя оценка
 *     trace best               - в трассировочный файл: студенты, у которых средняя оценка
 *                                выше средней по всем студентам
 *     help, exit
 * Подсказки ("> ") печатаются, только если команды вводятся с клавиатуры.
 *
 * Трассировочный файл создается заново при запуске (старое содержимое стирается) и пишется
 * по мере команд trace. Он не может быть тем же файлом, что файл со студентами: это проверяется
 * по самому файлу (same_file), а не по строке пути.
 */
/* _POSIX_C_SOURCE - до всех #include: stat и isatty - функции POSIX, в режиме -std=c99
   заголовки Linux показывают их только после этой строки */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort, _fullpath */
#include <string.h>   /* strcmp */
#include <limits.h>   /* LLONG_MAX */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace */
#ifdef _WIN32
#include <io.h>         /* _isatty, _fileno */
#else
#include <sys/stat.h>   /* stat - сведения о файле для same_file */
#include <unistd.h>     /* isatty, fileno */
#endif

#define MARKS_COUNT 5   /* оценок у каждого студента */
#define MAX_MARK 100    /* наибольшая оценка */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    INVALID_NUMBER,    /* строка - не целое неотрицательное число */
    OVERFLOW_ERROR,    /* число больше LLONG_MAX / сумма не помещается */
    END_OF_INPUT,      /* строки кончились (сигнал для цикла чтения) */
    BAD_LINE,          /* в строке файла не 9 слов */
    BAD_ID,            /* id - не целое 0..LLONG_MAX */
    BAD_NAME,          /* имя - не латинские буквы */
    BAD_SURNAME,       /* фамилия - не латинские буквы */
    BAD_MARK,          /* оценка - не целое 0..100 */
    DUPLICATE_ID,      /* id уже был */
    NOT_FOUND,         /* студента с таким id нет */
    NO_STUDENTS,       /* студентов нет - средней по всем не бывает */
    BAD_PATH,          /* путь к файлу неправильный (Windows, same_file) */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

typedef struct {
    long long id;
    char *name;              /* имя, латинские буквы */
    char *surname;           /* фамилия, латинские буквы */
    char *group;             /* группа, непустая строка без пробелов */
    unsigned char *marks;    /* динамический массив из MARKS_COUNT оценок */
} Student;

typedef struct {
    Student *items;
    size_t count;
    size_t capacity;
} student_list;

/* По какому полю искать или сортировать */
typedef enum {
    FIELD_ID,
    FIELD_SURNAME,
    FIELD_NAME,
    FIELD_GROUP
} student_field;

/* ===================== память ===================== */

/* Копия строки в динамической памяти */
status_code copy_string(const char *str, char **copy) {
    if (str == NULL || copy == NULL) {
        return INVALID_ARGS;
    }
    size_t length = 0;
    while (str[length] != '\0') {
        length++;
    }
    char *result = (char *)malloc(length + 1);
    if (result == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i <= length; i++) {   /* вместе с '\0' */
        result[i] = str[i];
    }
    *copy = result;
    return OK;
}

status_code student_clear(Student *s) {
    if (s == NULL) {
        return INVALID_ARGS;
    }
    free(s->name);
    free(s->surname);
    free(s->group);
    free(s->marks);
    s->name = NULL;
    s->surname = NULL;
    s->group = NULL;
    s->marks = NULL;
    return OK;
}

status_code list_destroy(student_list *list) {
    if (list == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < list->count; i++) {
        student_clear(&list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    return OK;
}

/* Добавить студента в конец (массив растет вдвое). Строки и оценки *s переходят в массив */
status_code list_append(student_list *list, const Student *s) {
    if (list == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        if (new_capacity > SIZE_MAX / sizeof(Student)) {
            return ALLOC_ERROR;
        }
        Student *tmp = (Student *)realloc(list->items, new_capacity * sizeof(Student));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        list->items = tmp;
        list->capacity = new_capacity;
    }
    list->items[list->count] = *s;
    list->count++;
    return OK;
}

/* ===================== средние оценки ===================== */

/* Сумма оценок студента (целое: так средние сравниваются точно, без вещественных чисел) */
status_code mark_sum(const Student *s, long long *sum) {
    if (s == NULL || sum == NULL || s->marks == NULL) {
        return INVALID_ARGS;
    }
    long long total = 0;
    for (int i = 0; i < MARKS_COUNT; i++) {
        total += s->marks[i];
    }
    *sum = total;
    return OK;
}

/* Средняя оценка студента - только для печати */
status_code mark_average(const Student *s, double *average) {
    if (average == NULL) {
        return INVALID_ARGS;
    }
    long long sum = 0;
    status_code sc = mark_sum(s, &sum);
    if (sc != OK) {
        return sc;
    }
    *average = (double)sum / MARKS_COUNT;
    return OK;
}

/*
 * Студенты, чья средняя оценка выше средней по всем. Результат - массив указателей
 * (освобождает вызывающий) и средняя по всем (для печати).
 * Сравнение без вещественных чисел (и без eps): средняя студента sum / 5, средняя по всем
 * total / (5 * n). sum / 5 > total / (5 * n)  <=>  sum * n > total.
 */
status_code select_above_average(const student_list *list, const Student ***result, size_t *count, double *overall) {
    if (list == NULL || result == NULL || count == NULL || overall == NULL) {
        return INVALID_ARGS;
    }
    if (list->count == 0) {
        return NO_STUDENTS;
    }
    long long total = 0;
    for (size_t i = 0; i < list->count; i++) {
        long long sum = 0;
        mark_sum(&list->items[i], &sum);
        if (total > LLONG_MAX - sum) {
            return OVERFLOW_ERROR;
        }
        total += sum;
    }
    /* sum * n не больше 500 * n: проверяем, что 500 * n помещается в long long */
    if (list->count > (size_t)(LLONG_MAX / (MAX_MARK * MARKS_COUNT))) {
        return OVERFLOW_ERROR;
    }
    const Student **selected = (const Student **)malloc(list->count * sizeof(const Student *));
    if (selected == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    for (size_t i = 0; i < list->count; i++) {
        long long sum = 0;
        mark_sum(&list->items[i], &sum);
        if (sum * (long long)list->count > total) {
            selected[n] = &list->items[i];
            n++;
        }
    }
    *result = selected;
    *count = n;
    *overall = (double)total / ((double)list->count * MARKS_COUNT);
    return OK;
}

/* ===================== поиск и сортировка ===================== */

/* Найти студента по id: *position - номер в массиве */
status_code find_by_id(const student_list *list, const long long id, size_t *position) {
    if (list == NULL || position == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].id == id) {
            *position = i;
            return OK;
        }
    }
    return NOT_FOUND;
}

/*
 * Найти всех студентов, у которых поле field равно value (для FIELD_ID value - запись числа).
 * Результат - массив указателей (освобождает вызывающий), порядок - текущий порядок списка.
 */
status_code find_students(const student_list *list, const student_field field, const char *value, const long long id,
                          const Student ***result, size_t *count) {
    if (list == NULL || result == NULL || count == NULL || (field != FIELD_ID && value == NULL)) {
        return INVALID_ARGS;
    }
    *result = NULL;
    *count = 0;
    if (list->count == 0) {
        return OK;
    }
    const Student **found = (const Student **)malloc(list->count * sizeof(const Student *));
    if (found == NULL) {
        return ALLOC_ERROR;
    }
    size_t n = 0;
    for (size_t i = 0; i < list->count; i++) {
        const Student *s = &list->items[i];
        int match = 0;
        switch (field) {
            case FIELD_ID:
                match = s->id == id;
                break;
            case FIELD_SURNAME:
                match = strcmp(s->surname, value) == 0;
                break;
            case FIELD_NAME:
                match = strcmp(s->name, value) == 0;
                break;
            case FIELD_GROUP:
                match = strcmp(s->group, value) == 0;
                break;
        }
        if (match) {
            found[n] = s;
            n++;
        }
    }
    *result = found;
    *count = n;
    return OK;
}

/* Компараторы для qsort. При равных значениях - по id, чтобы порядок не зависел от реализации qsort
   (qsort не обязан сохранять порядок равных элементов) */
int compare_by_id(const void *a, const void *b) {
    const Student *x = (const Student *)a;
    const Student *y = (const Student *)b;
    return (x->id > y->id) - (x->id < y->id);
}

int compare_by_surname(const void *a, const void *b) {
    int order = strcmp(((const Student *)a)->surname, ((const Student *)b)->surname);
    return order != 0 ? order : compare_by_id(a, b);
}

int compare_by_name(const void *a, const void *b) {
    int order = strcmp(((const Student *)a)->name, ((const Student *)b)->name);
    return order != 0 ? order : compare_by_id(a, b);
}

int compare_by_group(const void *a, const void *b) {
    int order = strcmp(((const Student *)a)->group, ((const Student *)b)->group);
    return order != 0 ? order : compare_by_id(a, b);
}

/* Отсортировать список по полю: в qsort передается компаратор этого поля */
status_code sort_students(student_list *list, const student_field field) {
    if (list == NULL) {
        return INVALID_ARGS;
    }
    int (*comparator)(const void *, const void *) = compare_by_id;
    switch (field) {
        case FIELD_ID:
            comparator = compare_by_id;
            break;
        case FIELD_SURNAME:
            comparator = compare_by_surname;
            break;
        case FIELD_NAME:
            comparator = compare_by_name;
            break;
        case FIELD_GROUP:
            comparator = compare_by_group;
            break;
    }
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(Student), comparator);
    }
    return OK;
}

/* ===================== разбор строк ===================== */

/* Строка -> целое 0..LLONG_MAX (только цифры) */
status_code parse_number(const char *str, long long *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_NUMBER;
    }
    long long value = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_NUMBER;
        }
        int digit = str[i] - '0';
        if (value > (LLONG_MAX - digit) / 10) {
            return OVERFLOW_ERROR;
        }
        value = value * 10 + digit;
    }
    *number = value;
    return OK;
}

/* Непустая строка только из латинских букв */
status_code check_latin(const char *str, int *ok) {
    if (str == NULL || ok == NULL) {
        return INVALID_ARGS;
    }
    *ok = str[0] != '\0';
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (!((str[i] >= 'a' && str[i] <= 'z') || (str[i] >= 'A' && str[i] <= 'Z'))) {
            *ok = 0;
        }
    }
    return OK;
}

/* Разбить строку на слова (пробелы, табуляции, '\r'). Строка меняется; *count - сколько слов
   на самом деле (больше max_words тоже считаем, чтобы заметить лишние) */
status_code split_words(char *line, char **words, const size_t max_words, size_t *count) {
    if (line == NULL || words == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    size_t k = 0;
    size_t i = 0;
    while (line[i] != '\0') {
        while (line[i] != '\0' && isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] == '\0') {
            break;
        }
        if (k < max_words) {
            words[k] = &line[i];
        }
        k++;
        while (line[i] != '\0' && !isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] != '\0') {
            line[i] = '\0';
            i++;
        }
    }
    *count = k;
    return OK;
}

/* Разобрать строку файла в *s (строки и оценки - в динамической памяти) */
status_code parse_student(char *line, Student *s) {
    if (line == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    char *words[4 + MARKS_COUNT];
    size_t count = 0;
    split_words(line, words, 4 + MARKS_COUNT, &count);
    if (count != 4 + MARKS_COUNT) {
        return BAD_LINE;
    }
    long long id = 0;
    if (parse_number(words[0], &id) != OK) {
        return BAD_ID;
    }
    int ok = 0;
    check_latin(words[1], &ok);
    if (!ok) {
        return BAD_NAME;
    }
    check_latin(words[2], &ok);
    if (!ok) {
        return BAD_SURNAME;
    }
    unsigned char marks[MARKS_COUNT];
    for (int i = 0; i < MARKS_COUNT; i++) {
        long long mark = 0;
        if (parse_number(words[4 + i], &mark) != OK || mark > MAX_MARK) {
            return BAD_MARK;
        }
        marks[i] = (unsigned char)mark;
    }
    /* все проверено - выделяем память */
    Student result = {id, NULL, NULL, NULL, NULL};
    status_code sc = copy_string(words[1], &result.name);
    if (sc == OK) {
        sc = copy_string(words[2], &result.surname);
    }
    if (sc == OK) {
        sc = copy_string(words[3], &result.group);
    }
    if (sc == OK) {
        result.marks = (unsigned char *)malloc(MARKS_COUNT);
        if (result.marks == NULL) {
            sc = ALLOC_ERROR;
        }
    }
    if (sc != OK) {
        student_clear(&result);
        return sc;
    }
    for (int i = 0; i < MARKS_COUNT; i++) {
        result.marks[i] = marks[i];
    }
    *s = result;
    return OK;
}

/* ===================== ввод ===================== */

/* Прочитать строку любой длины (без '\n'); END_OF_INPUT - строк больше нет */
status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t buf_size = 16;
    size_t length = 0;
    char *buf = (char *)malloc(buf_size);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    int c = fgetc(in);
    if (c == EOF) {
        free(buf);
        return END_OF_INPUT;
    }
    while (c != EOF && c != '\n') {
        if (length + 1 >= buf_size) {
            if (buf_size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            char *new_ptr = (char *)realloc(buf, buf_size * 2);
            if (new_ptr == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = new_ptr;
            buf_size *= 2;
        }
        buf[length] = (char)c;
        length++;
        c = fgetc(in);
    }
    buf[length] = '\0';
    *line = buf;
    return OK;
}

/* Пропустить метку BOM (EF BB BF) в начале файла - ее пишет Блокнот */
status_code skip_bom(FILE *in) {
    if (in == NULL) {
        return INVALID_ARGS;
    }
    int c = fgetc(in);
    if (c != 0xEF) {
        if (c != EOF) {
            ungetc(c, in);
        }
        return OK;
    }
    c = fgetc(in);
    if (c == 0xBB) {
        c = fgetc(in);
        if (c != 0xBF && c != EOF) {
            ungetc(c, in);
        }
    } else if (c != EOF) {
        ungetc(c, in);
    }
    return OK;
}

/* Строка пустая или из одних пробелов? */
status_code is_blank(const char *line, int *empty) {
    if (line == NULL || empty == NULL) {
        return INVALID_ARGS;
    }
    *empty = 1;
    for (size_t i = 0; line[i] != '\0'; i++) {
        if (!isspace((unsigned char)line[i])) {
            *empty = 0;
            break;
        }
    }
    return OK;
}

/*
 * Прочитать файл студентов в список. *line_number - номер строки с ошибкой.
 * При ошибке список освобождается.
 */
status_code read_students(FILE *in, student_list *list, size_t *line_number) {
    if (in == NULL || list == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    *line_number = 0;
    skip_bom(in);
    status_code sc = OK;
    while (sc == OK) {
        char *line = NULL;
        sc = read_line(in, &line);
        if (sc == END_OF_INPUT) {
            sc = OK;
            break;
        }
        if (sc != OK) {
            break;
        }
        (*line_number)++;
        int empty = 0;
        is_blank(line, &empty);
        if (empty) {
            free(line);
            continue;
        }
        Student s;
        sc = parse_student(line, &s);
        free(line);
        if (sc != OK) {
            break;
        }
        size_t position = 0;
        if (find_by_id(list, s.id, &position) == OK) {
            student_clear(&s);
            sc = DUPLICATE_ID;
            break;
        }
        sc = list_append(list, &s);
        if (sc != OK) {
            student_clear(&s);
        }
    }
    if (sc != OK) {
        list_destroy(list);
    }
    return sc;
}

/* ===================== файлы ===================== */

#ifdef _WIN32
/* Только для Windows: полный путь файла в динамической памяти (буфер растет вдвое, пока
   _fullpath не поместит путь; путь в Windows не длиннее 32767 символов) */
status_code full_path(const char *path, char **full) {
    if (path == NULL || full == NULL) {
        return INVALID_ARGS;
    }
    for (size_t size = 260; size <= 65536; size *= 2) {
        char *buf = (char *)malloc(size);
        if (buf == NULL) {
            return ALLOC_ERROR;
        }
        if (_fullpath(buf, path, size) != NULL) {
            *full = buf;
            return OK;
        }
        free(buf);
    }
    return BAD_PATH;
}
#endif

/* Ведут ли два пути к одному файлу. Сравниваются файлы, а не строки: "in.txt", "./in.txt" и
   полный путь - один файл. Linux: номер устройства и номер файла (stat); Windows: полные пути
   без учета регистра. Если файла нет (трассировочного еще нет) - это не один файл */
status_code same_file(const char *path1, const char *path2, int *equal) {
    if (path1 == NULL || path2 == NULL || equal == NULL) {
        return INVALID_ARGS;
    }
    *equal = 0;
#ifdef _WIN32
    char *full1 = NULL;
    char *full2 = NULL;
    status_code sc1 = full_path(path1, &full1);
    status_code sc2 = full_path(path2, &full2);
    if (sc1 == ALLOC_ERROR || sc2 == ALLOC_ERROR) {
        free(full1);
        free(full2);
        return ALLOC_ERROR;
    }
    if (sc1 == OK && sc2 == OK && _stricmp(full1, full2) == 0) {
        *equal = 1;
    }
    free(full1);
    free(full2);
#else
    struct stat info1;
    struct stat info2;
    if (stat(path1, &info1) == 0 && stat(path2, &info2) == 0 &&
        info1.st_dev == info2.st_dev && info1.st_ino == info2.st_ino) {
        *equal = 1;
    }
#endif
    return OK;
}

/* ===================== вывод ===================== */

status_code print_student(FILE *out, const Student *s) {
    if (out == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    double average = 0.0;
    mark_average(s, &average);
    fprintf(out, "%lld %s %s, group %s, marks", s->id, s->surname, s->name, s->group);
    for (int i = 0; i < MARKS_COUNT; i++) {
        fprintf(out, " %u", (unsigned)s->marks[i]);
    }
    fprintf(out, ", average %.2f\n", average);
    return OK;
}

status_code print_found(FILE *out, const Student *const *found, const size_t count) {
    if (out == NULL || (found == NULL && count > 0)) {
        return INVALID_ARGS;
    }
    fprintf(out, "Found: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        print_student(out, found[i]);
    }
    return OK;
}

status_code print_list(FILE *out, const student_list *list) {
    if (out == NULL || list == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Students: %zu\n", list->count);
    for (size_t i = 0; i < list->count; i++) {
        print_student(out, &list->items[i]);
    }
    return OK;
}

/* Трассировка одного студента: фамилия и имя, группа, средняя оценка */
status_code trace_student(FILE *trace, const Student *s) {
    if (trace == NULL || s == NULL) {
        return INVALID_ARGS;
    }
    double average = 0.0;
    mark_average(s, &average);
    fprintf(trace, "%s %s, group %s, average %.2f\n", s->surname, s->name, s->group, average);
    fflush(trace);
    return OK;
}

/* Трассировка лучших: средняя по всем и фамилии с именами тех, у кого средняя выше */
status_code trace_best(FILE *trace, const Student *const *best, const size_t count, const double overall) {
    if (trace == NULL || (best == NULL && count > 0)) {
        return INVALID_ARGS;
    }
    fprintf(trace, "Above the average %.2f: %zu\n", overall, count);
    for (size_t i = 0; i < count; i++) {
        fprintf(trace, "%s %s\n", best[i]->surname, best[i]->name);
    }
    fflush(trace);
    return OK;
}

status_code print_help(FILE *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Commands: find id|surname|name|group <value>, sort id|surname|name|group, list,\n");
    fprintf(out, "          trace <id>, trace best, help, exit\n");
    return OK;
}

/* ===================== диалог ===================== */

/* Слово поля -> student_field; *ok = 0 - такого поля нет */
status_code parse_field(const char *word, student_field *field, int *ok) {
    if (word == NULL || field == NULL || ok == NULL) {
        return INVALID_ARGS;
    }
    *ok = 1;
    if (strcmp(word, "id") == 0) {
        *field = FIELD_ID;
    } else if (strcmp(word, "surname") == 0) {
        *field = FIELD_SURNAME;
    } else if (strcmp(word, "name") == 0) {
        *field = FIELD_NAME;
    } else if (strcmp(word, "group") == 0) {
        *field = FIELD_GROUP;
    } else {
        *ok = 0;
    }
    return OK;
}

/*
 * Выполнить одну команду. *finish = 1 - exit. Ошибки пользователя печатаются здесь,
 * наружу возвращается только ALLOC_ERROR (тогда программа заканчивается).
 */
status_code run_command(student_list *list, FILE *trace, char *line, int *finish) {
    if (list == NULL || trace == NULL || line == NULL || finish == NULL) {
        return INVALID_ARGS;
    }
    *finish = 0;
    char *words[3];
    size_t count = 0;
    split_words(line, words, 3, &count);
    if (count == 0) {
        return OK;   /* пустая строка */
    }
    const char *command = words[0];

    if (strcmp(command, "exit") == 0 || strcmp(command, "help") == 0 || strcmp(command, "list") == 0) {
        if (count != 1) {
            fprintf(stderr, "Error: command '%s' takes no arguments\n", command);
        } else if (strcmp(command, "exit") == 0) {
            *finish = 1;
        } else if (strcmp(command, "help") == 0) {
            print_help(stdout);
        } else {
            print_list(stdout, list);
        }
        return OK;
    }

    if (strcmp(command, "find") == 0 || strcmp(command, "sort") == 0) {
        int is_find = strcmp(command, "find") == 0;
        size_t need = is_find ? 3 : 2;
        student_field field = FIELD_ID;
        int ok = 0;
        if (count == need) {
            parse_field(words[1], &field, &ok);
        }
        if (!ok) {
            fprintf(stderr, is_find ? "Error: use: find id|surname|name|group <value>\n"
                                    : "Error: use: sort id|surname|name|group\n");
            return OK;
        }
        if (!is_find) {
            sort_students(list, field);
            print_list(stdout, list);
            return OK;
        }
        long long id = 0;
        if (field == FIELD_ID && parse_number(words[2], &id) != OK) {
            fprintf(stderr, "Error: id must be a whole number from 0 to %lld\n", LLONG_MAX);
            return OK;
        }
        const Student **found = NULL;
        size_t found_count = 0;
        status_code sc = find_students(list, field, words[2], id, &found, &found_count);
        if (sc != OK) {
            return sc;   /* ALLOC_ERROR */
        }
        print_found(stdout, found, found_count);
        free(found);
        return OK;
    }

    if (strcmp(command, "trace") == 0) {
        if (count != 2) {
            fprintf(stderr, "Error: use: trace <id> or trace best\n");
            return OK;
        }
        if (strcmp(words[1], "best") == 0) {
            const Student **best = NULL;
            size_t best_count = 0;
            double overall = 0.0;
            status_code sc = select_above_average(list, &best, &best_count, &overall);
            if (sc == NO_STUDENTS) {
                fprintf(stderr, "Error: there are no students\n");
                return OK;
            }
            if (sc == OVERFLOW_ERROR) {
                fprintf(stderr, "Error: too many students to count the average\n");
                return OK;
            }
            if (sc != OK) {
                return sc;
            }
            trace_best(trace, best, best_count, overall);
            free(best);
            printf("Written to the trace file: %zu students above the average\n", best_count);
            return OK;
        }
        long long id = 0;
        if (parse_number(words[1], &id) != OK) {
            fprintf(stderr, "Error: id must be a whole number from 0 to %lld\n", LLONG_MAX);
            return OK;
        }
        size_t position = 0;
        if (find_by_id(list, id, &position) != OK) {
            fprintf(stderr, "Error: no student with id %lld\n", id);
            return OK;
        }
        trace_student(trace, &list->items[position]);
        printf("Written to the trace file: student %lld\n", id);
        return OK;
    }

    fprintf(stderr, "Error: unknown command '%s' (type help)\n", command);
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: lab3_task5 <students file> <trace file>\n");
        return 1;
    }

    /* 1) трассировочный файл не должен быть файлом со студентами (иначе "w" сотрет студентов) */
    int equal = 0;
    status_code sc = same_file(argv[1], argv[2], &equal);
    if (sc == ALLOC_ERROR) {
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    if (equal) {
        fprintf(stderr, "Error: the students file and the trace file must be different files\n");
        return 1;
    }

    /* 2) студенты */
    FILE *in = fopen(argv[1], "r");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        return 1;
    }
    student_list list;
    size_t line_number = 0;
    sc = read_students(in, &list, &line_number);
    fclose(in);
    if (sc != OK) {
        switch (sc) {
            case BAD_LINE:
                fprintf(stderr, "Error: line %zu: must be: id name surname group and 5 marks\n", line_number);
                break;
            case BAD_ID:
                fprintf(stderr, "Error: line %zu: id must be a whole number from 0 to %lld\n", line_number, LLONG_MAX);
                break;
            case BAD_NAME:
                fprintf(stderr, "Error: line %zu: name must contain only Latin letters\n", line_number);
                break;
            case BAD_SURNAME:
                fprintf(stderr, "Error: line %zu: surname must contain only Latin letters\n", line_number);
                break;
            case BAD_MARK:
                fprintf(stderr, "Error: line %zu: a mark must be a whole number from 0 to %d\n", line_number, MAX_MARK);
                break;
            case DUPLICATE_ID:
                fprintf(stderr, "Error: line %zu: this id is already used\n", line_number);
                break;
            case ALLOC_ERROR:
                fprintf(stderr, "Error: not enough memory\n");
                break;
            default:
                fprintf(stderr, "Error: internal error\n");   /* других статусов чтение не возвращает */
                break;
        }
        return 1;
    }

    /* 3) трассировочный файл - создается заново */
    FILE *trace = fopen(argv[2], "w");
    if (trace == NULL) {
        fprintf(stderr, "Error: can not create file '%s'\n", argv[2]);
        list_destroy(&list);
        return 1;
    }

    /* 4) диалог */
#ifdef _WIN32
    int interactive = _isatty(_fileno(stdin));
#else
    int interactive = isatty(fileno(stdin));
#endif
    if (interactive) {
        printf("Read %zu students from '%s'\n", list.count, argv[1]);
        print_help(stdout);
    }
    int result = 0;
    int finish = 0;
    while (!finish) {
        if (interactive) {
            printf("> ");
            fflush(stdout);
        }
        char *line = NULL;
        sc = read_line(stdin, &line);
        if (sc == END_OF_INPUT) {
            break;
        }
        if (sc == OK) {
            sc = run_command(&list, trace, line, &finish);
            free(line);
        }
        if (sc != OK) {
            fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: internal error\n");
            result = 1;
            break;
        }
    }
    fclose(trace);
    list_destroy(&list);
    return result;
}
