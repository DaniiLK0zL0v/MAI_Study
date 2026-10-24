/*
 * Лабораторная 3, задание 7. Жители поселения: односвязный список по возрасту, изменения и отмена (Undo).
 *
 * Запуск:  lab3_task7 <файл с жителями>
 * Строка файла:  фамилия имя [отчество] дд.ММ.гггг пол доход
 *     фамилия, имя - непустые, только латинские буквы; отчество - латинские буквы или его нет
 *     (тогда в строке 5 слов, а не 6); дата рождения - настоящая дата не позже сегодняшней;
 *     пол - M или W; доход - неотрицательное число (цифры и не больше одной точки).
 * Список упорядочен по возрасту: от младших к старшим (по дате рождения от поздней к ранней);
 * при равных датах - в порядке добавления.
 * Потом диалог - команды со стандартного ввода:
 *     list                                  все жители с номерами
 *     find <поле> <значение> [...]          поиск; поля: surname, name, patronymic (- = нет отчества),
 *                                           birth, gender, income; несколько пар - все должны совпасть
 *     add                                   добавить (поля - по одному в строке)
 *     edit <номер>                          изменить жителя с этим номером (все поля заново)
 *     delete <номер>                        удалить
 *     save                                  выгрузить список в файл (путь - следующей строкой)
 *     undo                                  отменить последние [N/2] изменений; N - сколько изменений
 *                                           (add, edit, delete) сделано с начала или с прошлой отмены
 *     help, exit
 */
#define _POSIX_C_SOURCE 200809L   /* isatty, fileno - функции POSIX */

#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, strtod */
#include <string.h>   /* strcmp, strlen, memcpy */
#include <stdint.h>   /* SIZE_MAX */
#include <ctype.h>    /* isspace */
#include <math.h>     /* fabs */
#include <time.h>     /* time, localtime - сегодняшняя дата */
#ifdef _WIN32
#include <io.h>       /* _isatty, _fileno */
#else
#include <unistd.h>   /* isatty, fileno */
#endif

#define EPS 1e-9   /* точность сравнения доходов (передается в функции параметром) */

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* NULL или неправильные данные в функции */
    INVALID_VALUE,     /* значение поля в неправильном формате */
    END_OF_INPUT,      /* ввод кончился */
    BAD_LINE,          /* в строке файла не 5 и не 6 слов */
    BAD_SURNAME,
    BAD_NAME,
    BAD_PATRONYMIC,
    BAD_BIRTH,         /* не дата или дата в будущем */
    BAD_GENDER,
    BAD_INCOME,
    NOT_FOUND,         /* нет жителя с таким номером / такими данными */
    TIME_ERROR,        /* не удалось узнать сегодняшнюю дату */
    FILE_ERROR,        /* файл не открылся */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

typedef struct {
    char *surname;
    char *name;
    char *patronymic;   /* "" - отчества нет */
    int day;
    int month;
    int year;
    char gender;        /* 'M' или 'W' */
    double income;
} Liver;

typedef struct liver_node {
    Liver data;
    struct liver_node *next;
} liver_node;

/* Одно изменение для отмены */
typedef enum {
    CHANGE_ADD,      /* добавили жителя */
    CHANGE_DELETE,   /* удалили жителя before */
    CHANGE_EDIT      /* данные before заменили новыми */
} change_kind;

typedef struct {
    change_kind kind;
    size_t before_position;   /* где житель стоял до изменения (0 - первый); для delete и edit */
    size_t after_position;    /* куда встал после изменения; для add и edit */
    Liver before;             /* копия старых данных (свои строки); для delete и edit */
} change;

/* Журнал изменений с начала работы или с прошлой отмены: его длина и есть N */
typedef struct {
    change *items;
    size_t count;
    size_t capacity;
} history;

/* ===================== Liver: память, копии, сравнения ===================== */

status_code copy_string(const char *str, char **copy) {
    if (str == NULL || copy == NULL) {
        return INVALID_ARGS;
    }
    size_t length = strlen(str);
    char *result = (char *)malloc(length + 1);
    if (result == NULL) {
        return ALLOC_ERROR;
    }
    memcpy(result, str, length + 1);
    *copy = result;
    return OK;
}

/* Пустой житель: строки NULL (такого можно безопасно очищать) */
status_code liver_init(Liver *liver) {
    if (liver == NULL) {
        return INVALID_ARGS;
    }
    memset(liver, 0, sizeof(Liver));
    liver->surname = NULL;
    liver->name = NULL;
    liver->patronymic = NULL;
    return OK;
}

status_code liver_clear(Liver *liver) {
    if (liver == NULL) {
        return INVALID_ARGS;
    }
    free(liver->surname);
    free(liver->name);
    free(liver->patronymic);
    liver_init(liver);
    return OK;
}

/* Полная копия жителя (со своими строками) */
status_code liver_copy(const Liver *src, Liver *dst) {
    if (src == NULL || dst == NULL) {
        return INVALID_ARGS;
    }
    Liver result;
    liver_init(&result);
    status_code sc = copy_string(src->surname, &result.surname);
    if (sc == OK) {
        sc = copy_string(src->name, &result.name);
    }
    if (sc == OK) {
        sc = copy_string(src->patronymic, &result.patronymic);
    }
    if (sc != OK) {
        liver_clear(&result);
        return sc;
    }
    result.day = src->day;
    result.month = src->month;
    result.year = src->year;
    result.gender = src->gender;
    result.income = src->income;
    *dst = result;
    return OK;
}

/* Дата как число гггг*10000 + мм*100 + дд: у более поздней даты число больше */
status_code birth_key(const Liver *liver, long *key) {
    if (liver == NULL || key == NULL) {
        return INVALID_ARGS;
    }
    *key = liver->year * 10000L + liver->month * 100L + liver->day;
    return OK;
}

/* ===================== список ===================== */

/* Вставить копию жителя в позицию position (0 - в начало): узел создается и встает перед
   тем, кто сейчас на этой позиции. Нужна для отмены: житель возвращается точно на свое место */
status_code list_insert_at(liver_node **head, const size_t position, const Liver *liver) {
    if (head == NULL || liver == NULL) {
        return INVALID_ARGS;
    }
    liver_node **link = head;
    for (size_t i = 0; i < position; i++) {
        if (*link == NULL) {
            return INVALID_ARGS;   /* позиция за концом списка */
        }
        link = &(*link)->next;
    }
    liver_node *node = (liver_node *)malloc(sizeof(liver_node));
    if (node == NULL) {
        return ALLOC_ERROR;
    }
    status_code sc = liver_copy(liver, &node->data);
    if (sc != OK) {
        free(node);
        return sc;
    }
    node->next = *link;
    *link = node;
    return OK;
}

/*
 * Вставить копию жителя на свое место по возрасту: перед первым, кто старше (родился раньше).
 * Равные даты - после уже стоящих: при равном возрасте сохраняется порядок добавления.
 * *position - куда встал (0 - первый), нужно журналу для отмены.
 */
status_code list_insert(liver_node **head, const Liver *liver, size_t *position) {
    if (head == NULL || liver == NULL || position == NULL) {
        return INVALID_ARGS;
    }
    long key = 0;
    birth_key(liver, &key);
    size_t index = 0;
    for (const liver_node *n = *head; n != NULL; n = n->next) {
        long current = 0;
        birth_key(&n->data, &current);
        if (current < key) {
            break;   /* этот житель старше нового - новый встает перед ним */
        }
        index++;
    }
    status_code sc = list_insert_at(head, index, liver);
    if (sc == OK) {
        *position = index;
    }
    return sc;
}

/* Убрать узел, на который указывает *link, и освободить его */
status_code list_unlink(liver_node **link) {
    if (link == NULL || *link == NULL) {
        return INVALID_ARGS;
    }
    liver_node *node = *link;
    *link = node->next;
    liver_clear(&node->data);
    free(node);
    return OK;
}

/* Указатель на поле next перед жителем с номером number (1, 2, ...) */
status_code list_find_number(liver_node **head, const size_t number, liver_node ***link) {
    if (head == NULL || link == NULL) {
        return INVALID_ARGS;
    }
    liver_node **current = head;
    for (size_t i = 1; *current != NULL; i++) {
        if (i == number) {
            *link = current;
            return OK;
        }
        current = &(*current)->next;
    }
    return NOT_FOUND;
}

status_code list_destroy(liver_node **head) {
    if (head == NULL) {
        return INVALID_ARGS;
    }
    while (*head != NULL) {
        list_unlink(head);
    }
    return OK;
}

/* ===================== журнал и отмена ===================== */

status_code history_clear(history *h) {
    if (h == NULL) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < h->count; i++) {
        liver_clear(&h->items[i].before);
    }
    h->count = 0;
    return OK;
}

status_code history_destroy(history *h) {
    if (h == NULL) {
        return INVALID_ARGS;
    }
    history_clear(h);
    free(h->items);
    h->items = NULL;
    h->capacity = 0;
    return OK;
}

/* Записать изменение: вид, позиции до и после, копия старых данных (before; для add - NULL) */
status_code history_add(history *h, const change_kind kind, const size_t before_position, const size_t after_position,
                        const Liver *before) {
    if (h == NULL) {
        return INVALID_ARGS;
    }
    if (h->count == h->capacity) {
        size_t new_capacity = h->capacity == 0 ? 8 : h->capacity * 2;
        if (new_capacity > SIZE_MAX / sizeof(change)) {
            return ALLOC_ERROR;
        }
        change *tmp = (change *)realloc(h->items, new_capacity * sizeof(change));
        if (tmp == NULL) {
            return ALLOC_ERROR;
        }
        h->items = tmp;
        h->capacity = new_capacity;
    }
    change c;
    memset(&c, 0, sizeof(change));
    c.kind = kind;
    c.before_position = before_position;
    c.after_position = after_position;
    liver_init(&c.before);
    if (before != NULL) {
        status_code sc = liver_copy(before, &c.before);
        if (sc != OK) {
            return sc;
        }
    }
    h->items[h->count] = c;
    h->count++;
    return OK;
}

/* Изменения: каждое меняет список и записывается в журнал */
status_code do_add(liver_node **head, history *h, const Liver *liver) {
    if (head == NULL || h == NULL || liver == NULL) {
        return INVALID_ARGS;
    }
    size_t position = 0;
    status_code sc = list_insert(head, liver, &position);
    if (sc != OK) {
        return sc;
    }
    return history_add(h, CHANGE_ADD, 0, position, NULL);
}

status_code do_delete(liver_node **head, history *h, const size_t number) {
    if (head == NULL || h == NULL) {
        return INVALID_ARGS;
    }
    liver_node **link = NULL;
    status_code sc = list_find_number(head, number, &link);
    if (sc != OK) {
        return sc;
    }
    sc = history_add(h, CHANGE_DELETE, number - 1, 0, &(*link)->data);   /* копия - до удаления */
    if (sc != OK) {
        return sc;
    }
    return list_unlink(link);
}

/* Изменить жителя: старого убрать, нового вставить на его место по возрасту */
status_code do_edit(liver_node **head, history *h, const size_t number, const Liver *liver) {
    if (head == NULL || h == NULL || liver == NULL) {
        return INVALID_ARGS;
    }
    liver_node **link = NULL;
    status_code sc = list_find_number(head, number, &link);
    if (sc != OK) {
        return sc;
    }
    Liver old;
    sc = liver_copy(&(*link)->data, &old);   /* старые данные - для журнала */
    if (sc != OK) {
        return sc;
    }
    list_unlink(link);
    size_t position = 0;
    sc = list_insert(head, liver, &position);
    if (sc == OK) {
        sc = history_add(h, CHANGE_EDIT, number - 1, position, &old);
    }
    liver_clear(&old);
    return sc;
}

/*
 * Отменить последние [N/2] изменений (N - длина журнала), от последнего к первому.
 * Отменяем в обратном порядке, поэтому перед отменой каждого изменения список точно такой,
 * каким был сразу после него, - и позиции из журнала точные:
 *   добавление - убрать жителя с позиции after;
 *   удаление   - вставить сохраненного жителя на позицию before;
 *   изменение  - убрать с позиции after, вставить старые данные на позицию before.
 * После отмены журнал очищается: N считается заново.
 */
status_code undo_changes(liver_node **head, history *h, size_t *undone) {
    if (head == NULL || h == NULL || undone == NULL) {
        return INVALID_ARGS;
    }
    size_t k = h->count / 2;
    *undone = 0;
    status_code sc = OK;
    for (size_t i = 0; i < k && sc == OK; i++) {
        const change *c = &h->items[h->count - 1 - i];
        liver_node **link = NULL;
        if (c->kind == CHANGE_ADD || c->kind == CHANGE_EDIT) {
            sc = list_find_number(head, c->after_position + 1, &link);
            if (sc == OK) {
                sc = list_unlink(link);
            }
        }
        if (sc == OK && (c->kind == CHANGE_DELETE || c->kind == CHANGE_EDIT)) {
            sc = list_insert_at(head, c->before_position, &c->before);
        }
        if (sc == OK) {
            (*undone)++;
        }
    }
    history_clear(h);
    return sc;
}

/* ===================== поиск ===================== */

/* Условия поиска: поле -> значение; заданы только те, где has_* = 1 */
typedef struct {
    int has_surname, has_name, has_patronymic, has_birth, has_gender, has_income;
    const char *surname, *name, *patronymic;
    int day, month, year;
    char gender;
    double income;
} search_query;

/* Подходит ли житель под все заданные условия */
status_code liver_matches(const Liver *liver, const search_query *q, const double eps, int *match) {
    if (liver == NULL || q == NULL || match == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    *match = (!q->has_surname || strcmp(liver->surname, q->surname) == 0)
             && (!q->has_name || strcmp(liver->name, q->name) == 0)
             && (!q->has_patronymic || strcmp(liver->patronymic, q->patronymic) == 0)
             && (!q->has_birth || (liver->day == q->day && liver->month == q->month && liver->year == q->year))
             && (!q->has_gender || liver->gender == q->gender)
             && (!q->has_income || fabs(liver->income - q->income) < eps);
    return OK;
}

/* ===================== разбор значений ===================== */

/* Непустая строка из латинских букв (allow_empty = 1 - можно пустую) */
status_code check_latin(const char *str, const int allow_empty) {
    if (str == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return allow_empty ? OK : INVALID_VALUE;
    }
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (!((str[i] >= 'a' && str[i] <= 'z') || (str[i] >= 'A' && str[i] <= 'Z'))) {
            return INVALID_VALUE;
        }
    }
    return OK;
}

/* Сегодняшняя дата числом гггг*10000 + мм*100 + дд */
status_code today_key(long *key) {
    if (key == NULL) {
        return INVALID_ARGS;
    }
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        return TIME_ERROR;
    }
    const struct tm *local = localtime(&now);
    if (local == NULL) {
        return TIME_ERROR;
    }
    *key = (local->tm_year + 1900) * 10000L + (local->tm_mon + 1) * 100L + local->tm_mday;
    return OK;
}

/* "дд.ММ.гггг" -> день, месяц, год; настоящая дата (високосные годы) */
status_code parse_date(const char *str, int *day, int *month, int *year) {
    if (str == NULL || day == NULL || month == NULL || year == NULL) {
        return INVALID_ARGS;
    }
    const char pattern[] = "dd.dd.dddd";
    for (int i = 0; i < 10; i++) {
        if (str[i] == '\0' || (pattern[i] == 'd' ? (str[i] < '0' || str[i] > '9') : str[i] != pattern[i])) {
            return INVALID_VALUE;
        }
    }
    if (str[10] != '\0') {
        return INVALID_VALUE;
    }
    int d = (str[0] - '0') * 10 + (str[1] - '0');
    int m = (str[3] - '0') * 10 + (str[4] - '0');
    int y = (str[6] - '0') * 1000 + (str[7] - '0') * 100 + (str[8] - '0') * 10 + (str[9] - '0');
    const int days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (y < 1 || m < 1 || m > 12 || d < 1 || d > days_in_month[m - 1] + (m == 2 && leap ? 1 : 0)) {
        return INVALID_VALUE;
    }
    *day = d;
    *month = m;
    *year = y;
    return OK;
}

/* Неотрицательное число: цифры и не больше одной точки; само число считает strtod */
status_code parse_income(const char *str, double *income) {
    if (str == NULL || income == NULL) {
        return INVALID_ARGS;
    }
    int digits = 0;
    int dots = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            digits++;
        } else if (str[i] == '.') {
            dots++;
        } else {
            return INVALID_VALUE;
        }
    }
    if (digits == 0 || dots > 1) {
        return INVALID_VALUE;
    }
    double value = strtod(str, NULL);
    if (!(value <= 1e300)) {
        return INVALID_VALUE;   /* слишком длинное число - бесконечность */
    }
    *income = value;
    return OK;
}

/* Пол: ровно "M" или "W" */
status_code parse_gender(const char *str, char *gender) {
    if (str == NULL || gender == NULL) {
        return INVALID_ARGS;
    }
    if ((str[0] != 'M' && str[0] != 'W') || str[1] != '\0') {
        return INVALID_VALUE;
    }
    *gender = str[0];
    return OK;
}

/*
 * Проверить поля жителя (строки) и собрать Liver (строки - копии в динамической памяти).
 * Каждой ошибке - свой статус, чтобы сообщить, какое поле неправильное.
 */
status_code make_liver(const char *surname, const char *name, const char *patronymic, const char *birth,
                       const char *gender, const char *income, const long today, Liver *liver) {
    if (surname == NULL || name == NULL || patronymic == NULL || birth == NULL || gender == NULL
        || income == NULL || liver == NULL) {
        return INVALID_ARGS;
    }
    Liver result;
    liver_init(&result);
    if (check_latin(surname, 0) != OK) {
        return BAD_SURNAME;
    }
    if (check_latin(name, 0) != OK) {
        return BAD_NAME;
    }
    if (check_latin(patronymic, 1) != OK) {
        return BAD_PATRONYMIC;
    }
    if (parse_date(birth, &result.day, &result.month, &result.year) != OK) {
        return BAD_BIRTH;
    }
    long key = 0;
    birth_key(&result, &key);
    if (key > today) {
        return BAD_BIRTH;   /* еще не родился */
    }
    if (parse_gender(gender, &result.gender) != OK) {
        return BAD_GENDER;
    }
    if (parse_income(income, &result.income) != OK) {
        return BAD_INCOME;
    }
    status_code sc = copy_string(surname, &result.surname);
    if (sc == OK) {
        sc = copy_string(name, &result.name);
    }
    if (sc == OK) {
        sc = copy_string(patronymic, &result.patronymic);
    }
    if (sc != OK) {
        liver_clear(&result);
        return sc;
    }
    *liver = result;
    return OK;
}

/* ===================== ввод ===================== */

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

/* Убрать пробелы (и '\r') в начале и в конце строки - на месте */
status_code trim(char *str) {
    if (str == NULL) {
        return INVALID_ARGS;
    }
    size_t start = 0;
    while (str[start] != '\0' && isspace((unsigned char)str[start])) {
        start++;
    }
    size_t last = start;
    for (size_t i = start; str[i] != '\0'; i++) {
        if (!isspace((unsigned char)str[i])) {
            last = i + 1;
        }
    }
    size_t length = last - start;
    for (size_t i = 0; i < length; i++) {
        str[i] = str[start + i];
    }
    str[length] = '\0';
    return OK;
}

/* Прочитать файл жителей в список. *line_number - строка с ошибкой */
status_code read_file(FILE *in, const long today, liver_node **head, size_t *line_number) {
    if (in == NULL || head == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    *head = NULL;
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
        char *w[6];
        size_t count = 0;
        split_words(line, w, 6, &count);
        if (count == 0) {
            free(line);
            continue;
        }
        Liver liver;
        if (count == 6) {
            sc = make_liver(w[0], w[1], w[2], w[3], w[4], w[5], today, &liver);
        } else if (count == 5) {
            sc = make_liver(w[0], w[1], "", w[2], w[3], w[4], today, &liver);   /* без отчества */
        } else {
            sc = BAD_LINE;
        }
        free(line);
        if (sc == OK) {
            size_t position = 0;
            sc = list_insert(head, &liver, &position);
            liver_clear(&liver);
        }
    }
    if (sc != OK) {
        list_destroy(head);
    }
    return sc;
}

/* ===================== вывод ===================== */

status_code print_liver(FILE *out, const size_t number, const Liver *l) {
    if (out == NULL || l == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "%zu. %s %s%s%s, %02d.%02d.%04d, %c, income %.2f\n", number, l->surname, l->name,
            l->patronymic[0] != '\0' ? " " : "", l->patronymic, l->day, l->month, l->year, l->gender, l->income);
    return OK;
}

status_code print_list(FILE *out, const liver_node *head) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    size_t count = 0;
    for (const liver_node *n = head; n != NULL; n = n->next) {
        count++;
    }
    fprintf(out, "Residents: %zu\n", count);
    size_t number = 1;
    for (const liver_node *n = head; n != NULL; n = n->next) {
        print_liver(out, number, &n->data);
        number++;
    }
    return OK;
}

/* Выгрузка в формате файла жителей (доход - %.15g: столько цифр double хранит точно) */
status_code save_list(FILE *out, const liver_node *head) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    for (const liver_node *n = head; n != NULL; n = n->next) {
        const Liver *l = &n->data;
        fprintf(out, "%s %s%s%s %02d.%02d.%04d %c %.15g\n", l->surname, l->name, l->patronymic[0] != '\0' ? " " : "",
                l->patronymic, l->day, l->month, l->year, l->gender, l->income);
    }
    return OK;
}

status_code print_help(FILE *out) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    fprintf(out, "Commands: list, find <field> <value> [...] (fields: surname name patronymic birth gender income),\n");
    fprintf(out, "          add, edit <number>, delete <number>, save, undo, help, exit\n");
    return OK;
}

/* Сообщение о неправильном поле */
status_code print_field_error(FILE *out, const status_code sc) {
    if (out == NULL) {
        return INVALID_ARGS;
    }
    switch (sc) {
        case BAD_SURNAME:
            fprintf(out, "surname must contain only Latin letters");
            break;
        case BAD_NAME:
            fprintf(out, "name must contain only Latin letters");
            break;
        case BAD_PATRONYMIC:
            fprintf(out, "patronymic must contain only Latin letters (or be empty)");
            break;
        case BAD_BIRTH:
            fprintf(out, "birth date must be a real date dd.MM.yyyy, not later than today");
            break;
        case BAD_GENDER:
            fprintf(out, "gender must be M or W");
            break;
        case BAD_INCOME:
            fprintf(out, "income must be a number 0 or more (digits and one dot)");
            break;
        default:
            fprintf(out, "wrong value");
            break;
    }
    return OK;
}

/* ===================== диалог ===================== */

typedef struct {
    FILE *in;
    int interactive;
    long today;
} dialog;

/* Спросить строку (подсказка - только при вводе с клавиатуры); строка без пробелов по краям */
status_code ask_line(const dialog *d, const char *prompt, char **line) {
    if (d == NULL || prompt == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    if (d->interactive) {
        printf("%s: ", prompt);
        fflush(stdout);
    }
    status_code sc = read_line(d->in, line);
    if (sc == OK) {
        trim(*line);
    }
    return sc;
}

/* Спросить все поля жителя; неправильные данные - сообщение и все поля заново */
status_code ask_liver(const dialog *d, Liver *liver) {
    if (d == NULL || liver == NULL) {
        return INVALID_ARGS;
    }
    const char *prompts[6] = {"Surname", "Name", "Patronymic (empty line - none)", "Birth date dd.MM.yyyy",
                              "Gender M/W", "Income"};
    while (1) {
        char *fields[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
        status_code sc = OK;
        for (int i = 0; i < 6 && sc == OK; i++) {
            sc = ask_line(d, prompts[i], &fields[i]);
        }
        if (sc == OK) {
            sc = make_liver(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], d->today, liver);
        }
        for (int i = 0; i < 6; i++) {
            free(fields[i]);
        }
        if (sc == OK || sc == END_OF_INPUT || sc == ALLOC_ERROR) {
            return sc;
        }
        fprintf(stderr, "Error: ");
        print_field_error(stderr, sc);
        fprintf(stderr, ", enter all fields again\n");
    }
}

/* Номер жителя из строки: целое >= 1 */
status_code parse_position(const char *str, size_t *number) {
    if (str == NULL || number == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_VALUE;
    }
    size_t value = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9' || value > (SIZE_MAX - 9) / 10) {
            return INVALID_VALUE;
        }
        value = value * 10 + (size_t)(str[i] - '0');
    }
    if (value == 0) {
        return INVALID_VALUE;
    }
    *number = value;
    return OK;
}

/* Разобрать условия find: пары "поле значение" */
status_code parse_query(char **words, const size_t count, search_query *q, const char **problem) {
    if (words == NULL || q == NULL || problem == NULL) {
        return INVALID_ARGS;
    }
    memset(q, 0, sizeof(search_query));
    if (count == 0 || count % 2 != 0) {
        *problem = "use: find <field> <value> [<field> <value> ...]";
        return INVALID_VALUE;
    }
    for (size_t i = 0; i < count; i += 2) {
        const char *field = words[i];
        const char *value = words[i + 1];
        if (strcmp(field, "surname") == 0 && check_latin(value, 0) == OK) {
            q->has_surname = 1;
            q->surname = value;
        } else if (strcmp(field, "name") == 0 && check_latin(value, 0) == OK) {
            q->has_name = 1;
            q->name = value;
        } else if (strcmp(field, "patronymic") == 0 && (strcmp(value, "-") == 0 || check_latin(value, 0) == OK)) {
            q->has_patronymic = 1;
            q->patronymic = strcmp(value, "-") == 0 ? "" : value;
        } else if (strcmp(field, "birth") == 0 && parse_date(value, &q->day, &q->month, &q->year) == OK) {
            q->has_birth = 1;
        } else if (strcmp(field, "gender") == 0 && parse_gender(value, &q->gender) == OK) {
            q->has_gender = 1;
        } else if (strcmp(field, "income") == 0 && parse_income(value, &q->income) == OK) {
            q->has_income = 1;
        } else {
            *problem = "wrong field or value in find";
            return INVALID_VALUE;
        }
    }
    return OK;
}

/* Выполнить команду. Наружу - только ALLOC_ERROR / END_OF_INPUT (ввод кончился посреди add/edit/save) */
status_code run_command(const dialog *d, liver_node **head, history *h, char *line, int *finish) {
    if (d == NULL || head == NULL || h == NULL || line == NULL || finish == NULL) {
        return INVALID_ARGS;
    }
    *finish = 0;
    char *words[13];
    size_t count = 0;
    split_words(line, words, 13, &count);
    if (count == 0) {
        return OK;
    }
    const char *command = words[0];
    int simple = strcmp(command, "list") == 0 || strcmp(command, "add") == 0 || strcmp(command, "save") == 0
                 || strcmp(command, "undo") == 0 || strcmp(command, "help") == 0 || strcmp(command, "exit") == 0;
    int numbered = strcmp(command, "edit") == 0 || strcmp(command, "delete") == 0;
    if (simple && count != 1) {
        fprintf(stderr, "Error: command '%s' takes no arguments\n", command);
        return OK;
    }
    size_t number = 0;
    if (numbered && (count != 2 || parse_position(words[1], &number) != OK)) {
        fprintf(stderr, "Error: use: %s <number>, number from the list (1, 2, ...)\n", command);
        return OK;
    }
    if (strcmp(command, "exit") == 0) {
        *finish = 1;
        return OK;
    }
    if (strcmp(command, "help") == 0) {
        return print_help(stdout);
    }
    if (strcmp(command, "list") == 0) {
        return print_list(stdout, *head);
    }
    if (strcmp(command, "find") == 0) {
        if (count > 13) {
            fprintf(stderr, "Error: too many conditions in find\n");
            return OK;
        }
        search_query q;
        const char *problem = NULL;
        if (parse_query(words + 1, count - 1, &q, &problem) != OK) {
            fprintf(stderr, "Error: %s\n", problem);
            return OK;
        }
        /* номера - как в list, чтобы по ним можно было сделать edit / delete */
        size_t found = 0;
        for (const liver_node *n = *head; n != NULL; n = n->next) {
            int match = 0;
            liver_matches(&n->data, &q, EPS, &match);
            found += match ? 1 : 0;
        }
        printf("Found: %zu\n", found);
        size_t position = 1;
        for (const liver_node *n = *head; n != NULL; n = n->next) {
            int match = 0;
            liver_matches(&n->data, &q, EPS, &match);
            if (match) {
                print_liver(stdout, position, &n->data);
            }
            position++;
        }
        return OK;
    }
    if (strcmp(command, "add") == 0 || strcmp(command, "edit") == 0) {
        int is_add = strcmp(command, "add") == 0;
        liver_node **link = NULL;
        if (!is_add && list_find_number(head, number, &link) != OK) {
            fprintf(stderr, "Error: there is no resident number %zu\n", number);
            return OK;
        }
        Liver liver;
        status_code sc = ask_liver(d, &liver);
        if (sc != OK) {
            return sc;   /* END_OF_INPUT или ALLOC_ERROR */
        }
        sc = is_add ? do_add(head, h, &liver) : do_edit(head, h, number, &liver);
        liver_clear(&liver);
        if (sc == OK) {
            printf(is_add ? "Added\n" : "Changed\n");
        }
        return sc;
    }
    if (strcmp(command, "delete") == 0) {
        status_code sc = do_delete(head, h, number);
        if (sc == NOT_FOUND) {
            fprintf(stderr, "Error: there is no resident number %zu\n", number);
            return OK;
        }
        if (sc == OK) {
            printf("Deleted\n");
        }
        return sc;
    }
    if (strcmp(command, "save") == 0) {
        char *path = NULL;
        status_code sc = ask_line(d, "File path", &path);
        if (sc != OK) {
            return sc;
        }
        FILE *out = path[0] != '\0' ? fopen(path, "w") : NULL;
        if (out == NULL) {
            fprintf(stderr, "Error: can not create file '%s'\n", path);
        } else {
            save_list(out, *head);
            fclose(out);
            printf("Saved to '%s'\n", path);
        }
        free(path);
        return OK;
    }
    if (strcmp(command, "undo") == 0) {
        size_t n = h->count;
        if (n / 2 == 0) {
            /* [N/2] = 0 - отменять нечего; это не отмена изменений, поэтому журнал не очищается:
               после следующего изменения N = 2, и undo отменит одно */
            printf("Nothing to undo: %zu modification(s), [N/2] = 0\n", n);
            return OK;
        }
        size_t undone = 0;
        status_code sc = undo_changes(head, h, &undone);
        if (sc == NOT_FOUND || sc == INVALID_ARGS) {
            fprintf(stderr, "Error: internal error\n");   /* журнал и список всегда согласованы */
            return OK;
        }
        if (sc == OK) {
            printf("Undone %zu of %zu modification(s)\n", undone, n);
        }
        return sc;
    }
    fprintf(stderr, "Error: unknown command '%s' (type help)\n", command);
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: lab3_task7 <residents file>\n");
        return 1;
    }
    dialog d;
    d.in = stdin;
#ifdef _WIN32
    d.interactive = _isatty(_fileno(stdin));
#else
    d.interactive = isatty(fileno(stdin));
#endif
    if (today_key(&d.today) != OK) {
        fprintf(stderr, "Error: can not get the system date\n");
        return 1;
    }

    FILE *in = fopen(argv[1], "r");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", argv[1]);
        return 1;
    }
    liver_node *head = NULL;
    size_t line_number = 0;
    status_code sc = read_file(in, d.today, &head, &line_number);
    fclose(in);
    if (sc != OK) {
        if (sc == BAD_LINE) {
            fprintf(stderr, "Error: line %zu: must be: surname name [patronymic] dd.MM.yyyy M|W income\n", line_number);
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
        } else {
            fprintf(stderr, "Error: line %zu: ", line_number);
            print_field_error(stderr, sc);
            fprintf(stderr, "\n");
        }
        return 1;
    }

    history h = {NULL, 0, 0};
    if (d.interactive) {
        print_help(stdout);
    }
    int result = 0;
    int finish = 0;
    while (!finish) {
        if (d.interactive) {
            printf("> ");
            fflush(stdout);
        }
        char *line = NULL;
        sc = read_line(d.in, &line);
        if (sc == END_OF_INPUT) {
            break;
        }
        if (sc == OK) {
            sc = run_command(&d, &head, &h, line, &finish);
            free(line);
        }
        if (sc == END_OF_INPUT) {
            fprintf(stderr, "Error: the input ended in the middle of a command\n");
            result = 1;
            break;
        }
        if (sc != OK) {
            fprintf(stderr, sc == ALLOC_ERROR ? "Error: not enough memory\n" : "Error: internal error\n");
            result = 1;
            break;
        }
    }
    history_destroy(&h);
    list_destroy(&head);
    return result;
}
