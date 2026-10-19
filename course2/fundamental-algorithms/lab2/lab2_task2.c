/* _POSIX_C_SOURCE нужно написать ДО всех #include. В режиме -std=c99 заголовки
   Linux прячут "не стандартные C" функции, а stat (для проверки "вход и выход -
   один файл") описан в стандарте POSIX. Эта строка говорит: "POSIX тоже нужен" */
#define _POSIX_C_SOURCE 200809L
/*
 * Лабораторная 2, задание 2. Сортировка сотрудников.
 *
 * Запуск:  lab2_task2 <входной файл> <флаг> <выходной файл>
 *     флаг: -a или /a - по возрастанию, -d или /d - по убыванию
 * Входной файл: в каждой строке один сотрудник, 4 слова через пробел:
 *     id имя фамилия зарплата
 *     id       - целое неотрицательное число (0 .. LLONG_MAX)
 *     имя      - непустая строка только из латинских букв (A-Z, a-z)
 *     фамилия  - непустая строка только из латинских букв
 *     зарплата - неотрицательное число: цифры, можно с дробной частью через точку (1500, 1500.75)
 *     Пустые строки пропускаются. Любая неправильная строка - ошибка с номером строки,
 *     выходной файл тогда не создается.
 * Выходной файл: те же сотрудники в том же виде, отсортированные
 *     сначала по зарплате, при равных зарплатах - по фамилии, потом по имени, потом по id.
 *     При флаге d весь порядок обратный. Если у двух записей совпало все это, а зарплаты
 *     равны только с точностью eps ("1000" и "1000.0000001"), раньше идет меньший текст зарплаты -
 *     так ответ всегда один и тот же.
 * Пример:  1 Ivan Petrov 1000 / 2 Anna Ivanova 2000 / 3 Oleg Ivanov 1000
 *     -a  ->  3 Oleg Ivanov 1000 / 1 Ivan Petrov 1000 / 2 Anna Ivanova 2000
 *
 * Сортирует стандартная функция qsort (своя сортировка запрещена условием).
 * qsort сравнивает элементы функцией-компаратором с фиксированным прототипом
 * int (*)(const void *, const void *) - передать в нее eps нельзя, а глобальные
 * переменные запрещены. Поэтому вещественные числа сравниваются не в компараторе:
 * при чтении зарплата один раз переводится в целое число "сколько eps в ней помещается"
 * (функция salary_to_units с параметром eps), а компаратор сравнивает уже целые числа.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort, strtod */
#include <string.h>   /* strlen, memcpy, strcmp */
#include <limits.h>   /* LLONG_MAX - самое большое long long */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */
#include <ctype.h>    /* isspace */
#include <math.h>     /* isinf, llround */

#ifndef _WIN32
#include <sys/stat.h> /* stat - сведения о файле (Linux) */
#endif

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* в функцию передали NULL или eps <= 0 */
    INVALID_FLAG,      /* флаг не -a, /a, -d, /d */
    INVALID_NUMBER,    /* строка - не число нужного вида */
    OVERFLOW_ERROR,    /* число не помещается в свой тип */
    END_OF_INPUT,      /* строки кончились (это не ошибка, а сигнал для цикла чтения) */
    BAD_LINE,          /* в строке сотрудника не 4 слова */
    BAD_ID,            /* id - не целое число от 0 до LLONG_MAX */
    BAD_NAME,          /* имя пустое или не только из латинских букв */
    BAD_SURNAME,       /* фамилия пустая или не только из латинских букв */
    BAD_SALARY,        /* зарплата - не неотрицательное число вида 1500 или 1500.75 */
    SALARY_TOO_BIG,    /* зарплата слишком большая: salary / eps не помещается в long long */
    WRITE_ERROR,       /* не получилось записать в выходной файл */
    BAD_PATH,          /* (Windows) из пути не получился полный путь - путь неправильный */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Направление сортировки */
typedef enum {
    ASCENDING,       /* флаг a */
    DESCENDING           /* флаг d */
} sort_order;

/* Сотрудник. Строки - в динамической памяти (любой длины).
   Зарплата хранится тремя способами, у каждого своя работа:
     salary       - само вещественное число (как требует условие);
     salary_text  - текст зарплаты из файла: в выходной файл пишем его, чтобы
                    не потерять и не округлить ни одной цифры (1500.125 останется 1500.125);
     salary_units - зарплата в единицах eps, округленная до целого: по ней сортируем */
typedef struct {
    long long id;
    char *name;
    char *surname;
    double salary;
    char *salary_text;
    long long salary_units;
} Employee;

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

/* Освободить строки одного сотрудника и обнулить указатели.
   free(NULL) ничего не делает, поэтому можно вызывать и для недозаполненного сотрудника */
status_code free_employee(Employee *emp) {
    if (emp == NULL) {
        return INVALID_ARGS;
    }
    free(emp->name);
    free(emp->surname);
    free(emp->salary_text);
    emp->name = NULL;
    emp->surname = NULL;
    emp->salary_text = NULL;
    return OK;
}

/* Освободить массив сотрудников: сначала строки каждого, потом сам массив */
status_code free_employees(Employee *arr, const size_t count) {
    if (arr == NULL) {
        return OK;
    }
    for (size_t i = 0; i < count; i++) {
        free_employee(&arr[i]);
    }
    free(arr);
    return OK;
}

/* ===================== разбор полей строки ===================== */

/* Строка -> id: целое неотрицательное число long long. Только цифры:
   "-1", "+5", "1.5", "12a", "" - ошибка (INVALID_NUMBER); больше LLONG_MAX - OVERFLOW_ERROR.
   scanf("%u") тут не годится: он принимает "-1" как огромное число,
   а при переполнении у scanf неопределенное поведение */
status_code parse_id(const char *str, long long *id) {
    if (str == NULL || id == NULL) {
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
        /* value * 10 + digit > LLONG_MAX  <=>  value > (LLONG_MAX - digit) / 10
           (проверяем до умножения - само умножение могло бы переполниться) */
        if (value > (LLONG_MAX - digit) / 10) {
            return OVERFLOW_ERROR;
        }
        value = value * 10 + digit;
    }
    *id = value;
    return OK;
}

/* Слово непустое и состоит только из латинских букв?
   isalpha не берем: ее ответ зависит от настроек языка системы (locale),
   а условие требует именно латинский алфавит - проверяем диапазоны 'A'..'Z' и 'a'..'z' */
status_code is_latin_word(const char *str, int *da) {
    if (str == NULL || da == NULL) {
        return INVALID_ARGS;
    }
    *da = (str[0] != '\0');   /* пустая строка - не подходит */
    for (size_t i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            *da = 0;
            break;
        }
    }
    return OK;
}

/* Строка -> зарплата (double). Сначала сами проверяем вид числа:
   одна или больше цифр, потом можно точку и еще одну или больше цифр ("1500", "0.5", "1500.75").
   Ошибка (INVALID_NUMBER): "-5", "+5", ".5", "5.", "1,5", "1e3", "inf", "", "12a".
   Только после проверки переводим строку в число стандартной функцией strtod.
   Без нашей проверки strtod приняла бы и "-5", и "inf", и "0x1A", и " 5".
   Если цифр так много, что число больше самого большого double, strtod вернет
   бесконечность - это OVERFLOW_ERROR */
status_code parse_salary(const char *str, double *salary) {
    if (str == NULL || salary == NULL) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    /* целая часть: хотя бы одна цифра */
    if (str[i] < '0' || str[i] > '9') {
        return INVALID_NUMBER;
    }
    while (str[i] >= '0' && str[i] <= '9') {
        i++;
    }
    /* дробная часть: если есть точка, после нее хотя бы одна цифра */
    if (str[i] == '.') {
        i++;
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_NUMBER;
        }
        while (str[i] >= '0' && str[i] <= '9') {
            i++;
        }
    }
    /* после числа ничего быть не должно */
    if (str[i] != '\0') {
        return INVALID_NUMBER;
    }
    /* вид правильный - переводим. Второй параметр (где strtod остановилась)
       не нужен: что строка - число целиком, мы уже проверили сами */
    double value = strtod(str, NULL);
    if (isinf(value)) {
        return OVERFLOW_ERROR;
    }
    *salary = value;
    return OK;
}

/* Зарплата -> целое число единиц eps: units = salary / eps, округленное до целого.
   Зачем: сравнивать вещественные числа можно только через eps, который передан
   параметром функции. Компаратору qsort параметр не добавить (его прототип задан),
   поэтому сравнение готовим здесь: зарплаты, которые после округления до точности eps
   совпадают, получают одинаковое число единиц и дальше считаются равными.
   Компаратор сравнивает уже целые числа - точно и без eps.
   Плюс такого способа: порядок "транзитивный" (если a = b и b = c, то a = c).
   Прямое сравнение fabs(a - b) < eps этим не обладает (0, 0.6*eps, 1.2*eps:
   соседние "равны", а крайние нет), а qsort с таким компаратором может
   выдать неотсортированный массив.
   Ограничение: salary / eps должно поместиться в long long (LLONG_MAX ~ 9.2 * 10^18),
   при eps = 10^-6 это зарплаты меньше ~9.2 * 10^12 - иначе SALARY_TOO_BIG */
status_code salary_to_units(const double salary, const double eps, long long *units) {
    if (units == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* отрицательная зарплата сюда не попадает (parse_salary не пропускает минус),
       проверка - чтобы функция была правильной и сама по себе */
    if (salary < 0) {
        return INVALID_ARGS;
    }
    double eps_units = salary / eps;
    /* (double)LLONG_MAX = 2^63 - это точная граница: все, что меньше,
       после округления помещается в long long */
    if (eps_units >= (double)LLONG_MAX) {
        return SALARY_TOO_BIG;
    }
    /* llround - округление до ближайшего целого (из math.h, C99) */
    *units = llround(eps_units);
    return OK;
}

/* ===================== чтение файла ===================== */

/* Прочитать одну строку любой длины (без '\n') в динамическую память.
   Буфер начинается с 16 байт и удваивается через realloc, когда кончается место.
   END_OF_INPUT - строк больше нет (файл кончился, а символов не было) */
status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t size = 16;   /* сколько байт выделено */
    size_t length = 0;     /* сколько символов уже прочитано */
    char *buf = (char *)malloc(size);
    if (buf == NULL) {
        return ALLOC_ERROR;
    }
    int c = fgetc(in);
    if (c == EOF) {
        free(buf);
        return END_OF_INPUT;
    }
    while (c != EOF && c != '\n') {
        /* нужен байт под этот символ и еще один под '\0' в конце */
        if (length + 1 >= size) {
            if (size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            /* realloc - во временную переменную: если он вернет NULL,
               старый блок останется, и мы сможем его освободить */
            char *new_ptr = (char *)realloc(buf, size * 2);
            if (new_ptr == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = new_ptr;
            size *= 2;
        }
        buf[length] = (char)c;
        length++;
        c = fgetc(in);
    }
    buf[length] = '\0';
    *line = buf;
    return OK;
}

/* Разбить строку на слова (слова разделены пробелами, табуляциями, '\r' от Windows).
   Строка меняется: после каждого слова пишется '\0', а в words[] кладутся
   адреса начал слов. Больше max_words слов не записываем, но считаем всех:
   *count - сколько слов в строке на самом деле (чтобы заметить лишние) */
status_code split_words(char *line, char **words, const size_t max_words, size_t *count) {
    if (line == NULL || words == NULL || count == NULL) {
        return INVALID_ARGS;
    }
    size_t k = 0;
    size_t i = 0;
    while (line[i] != '\0') {
        /* пропускаем пробелы перед словом. (unsigned char) - русская буква в char
           отрицательна, а isspace с отрицательным числом - неопределенное поведение */
        while (line[i] != '\0' && isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] == '\0') {
            break;
        }
        /* начало слова */
        if (k < max_words) {
            words[k] = &line[i];
        }
        k++;
        /* идем до конца слова */
        while (line[i] != '\0' && !isspace((unsigned char)line[i])) {
            i++;
        }
        /* конец слова: ставим '\0' и идем дальше (если это не конец строки) */
        if (line[i] != '\0') {
            line[i] = '\0';
            i++;
        }
    }
    *count = k;
    return OK;
}

/* Разобрать одну непустую строку файла в сотрудника.
   Каждая ошибка - свой статус, чтобы main напечатал, что именно не так.
   Строки сотрудника (имя, фамилия, текст зарплаты) - копии в динамической памяти;
   при любой ошибке уже скопированное освобождается, и *emp не меняется */
status_code parse_employee(char *line, const double eps, Employee *emp) {
    if (line == NULL || emp == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    /* больше 5 слов запоминать не нужно: уже 5-е слово - ошибка (лишнее слово) */
    char *words[5];
    size_t word_count = 0;
    split_words(line, words, 5, &word_count);
    if (word_count != 4) {
        return BAD_LINE;
    }

    /* временный сотрудник: заполняем по полю, в *emp кладем только целиком готового */
    Employee tmp;
    tmp.name = NULL;
    tmp.surname = NULL;
    tmp.salary_text = NULL;

    /* 1) id: любая ошибка числа (не число, слишком большое) - BAD_ID */
    if (parse_id(words[0], &tmp.id) != OK) {
        return BAD_ID;
    }
    /* 2) имя и 3) фамилия: только латинские буквы */
    int da = 0;
    is_latin_word(words[1], &da);
    if (!da) {
        return BAD_NAME;
    }
    is_latin_word(words[2], &da);
    if (!da) {
        return BAD_SURNAME;
    }
    /* 4) зарплата: само число, потом число единиц eps для сортировки */
    if (parse_salary(words[3], &tmp.salary) != OK) {
        /* INVALID_NUMBER (не тот вид) и OVERFLOW_ERROR (больше double) - для
           пользователя это одна ошибка: "зарплата должна быть числом ..." */
        return BAD_SALARY;
    }
    status_code sc = salary_to_units(tmp.salary, eps, &tmp.salary_units);
    if (sc != OK) {
        return sc;   /* SALARY_TOO_BIG */
    }

    /* все поля правильные - копируем строки (words указывают внутрь line,
       а line вызывающий освободит сразу после разбора) */
    sc = copy_string(words[1], &tmp.name);
    if (sc == OK) {
        sc = copy_string(words[2], &tmp.surname);
    }
    if (sc == OK) {
        sc = copy_string(words[3], &tmp.salary_text);
    }
    if (sc != OK) {
        free_employee(&tmp);   /* освобождаем то, что успели скопировать */
        return sc;
    }
    *emp = tmp;
    return OK;
}

/* Добавить сотрудника в конец массива, который растет удвоением (как буфер строки).
   *size - сколько элементов выделено, *count - сколько занято */
status_code add_employee(Employee **arr, size_t *size, size_t *count, const Employee *emp) {
    if (arr == NULL || size == NULL || count == NULL || emp == NULL) {
        return INVALID_ARGS;
    }
    if (*count == *size) {
        /* новый размер: 8 для начала, потом вдвое больше; проверяем, что
           (новый размер * размер элемента) помещается в size_t */
        size_t new_size = (*size == 0) ? 8 : *size * 2;
        if (*size > SIZE_MAX / 2 || new_size > SIZE_MAX / sizeof(Employee)) {
            return ALLOC_ERROR;
        }
        Employee *new_ptr = (Employee *)realloc(*arr, new_size * sizeof(Employee));
        if (new_ptr == NULL) {
            return ALLOC_ERROR;   /* старый массив цел - его освободит вызывающий */
        }
        *arr = new_ptr;
        *size = new_size;
    }
    (*arr)[*count] = *emp;   /* копируется структура: указатели на строки переходят в массив */
    (*count)++;
    return OK;
}

/* Прочитать всех сотрудников из открытого файла в динамический массив.
   Пустые строки (и строки из одних пробелов) пропускаем.
   *line_number - номер строки, на которой остановились (считаем и пустые строки,
   так номер совпадет с номером в файле); при ошибке по нему main скажет, где ошибка.
   При ошибке все прочитанное освобождается, *arr и *count не меняются.
   Файл пустой (или только пустые строки) - не ошибка: сотрудников 0 */
status_code read_employees(FILE *in, const double eps, Employee **arr, size_t *count,
                           size_t *line_number) {
    if (in == NULL || arr == NULL || count == NULL || line_number == NULL || eps <= 0) {
        return INVALID_ARGS;
    }
    Employee *array = NULL;
    size_t size = 0;   /* сколько элементов выделено */
    size_t n = 0;        /* сколько сотрудников прочитано */
    *line_number = 0;

    while (1) {
        char *line = NULL;
        status_code sc = read_line(in, &line);
        if (sc == END_OF_INPUT) {
            break;   /* файл кончился - все прочитано */
        }
        if (sc != OK) {
            free_employees(array, n);
            return sc;   /* ALLOC_ERROR */
        }
        (*line_number)++;

        /* пустую строку пропускаем: ищем в ней хоть один не пробельный символ */
        int empty = 1;
        for (size_t i = 0; line[i] != '\0'; i++) {
            if (!isspace((unsigned char)line[i])) {
                empty = 0;
                break;
            }
        }
        if (empty) {
            free(line);
            continue;
        }

        /* разбираем строку и сразу освобождаем ее: нужные строки скопированы в emp */
        Employee emp;
        sc = parse_employee(line, eps, &emp);
        free(line);
        if (sc == OK) {
            sc = add_employee(&array, &size, &n, &emp);
            if (sc != OK) {
                free_employee(&emp);   /* сотрудник не попал в массив - освобождаем отдельно */
            }
        }
        if (sc != OK) {
            /* ошибка в строке - дальше не читаем, освобождаем все прочитанное */
            free_employees(array, n);
            return sc;
        }
    }
    *arr = array;   /* при 0 сотрудников array = NULL - это нормально */
    *count = n;
    return OK;
}

/* ===================== сравнение и сортировка ===================== */

/* Сравнить два слова из латинских букв "по алфавиту".
   Сначала без учета регистра ("adam" раньше "Zed", хотя в таблице ASCII 'Z' < 'a'),
   а если слова отличаются только регистром - то обычным strcmp (заглавные раньше:
   "Ivanov" раньше "ivanov"). Так одинаковые слова дают 0, разные - никогда не 0.
   *result < 0 - a раньше b, 0 - одинаковые, > 0 - a позже b */
status_code compare_words(const char *a, const char *b, int *result) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    size_t i = 0;
    while (1) {
        /* маленькая буква из большой: 'A'..'Z' -> 'a'..'z' (в ASCII разница 32 = 'a' - 'A') */
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            /* первая разная буква решает; если одно слово кончилось ('\0' = 0),
               оно меньше - короткое слово раньше длинного ("Ann" раньше "Anna") */
            *result = (ca < cb) ? -1 : 1;
            return OK;
        }
        if (ca == '\0') {
            break;   /* кончились оба слова одновременно - без учета регистра равны */
        }
        i++;
    }
    *result = strcmp(a, b);
    return OK;
}

/* Сравнить двух сотрудников по правилу из условия (по возрастанию):
   зарплата, при равных - фамилия, при равных - имя, при равных - id.
   Зарплаты сравниваются по salary_units - целым числам (см. salary_to_units),
   поэтому eps здесь не нужен. Пятый признак (текст зарплаты) - только чтобы порядок
   всегда был одним и тем же, см. шаг 5. *result < 0 - a раньше b, 0 - равны, > 0 - a позже b */
status_code compare_employees(const Employee *a, const Employee *b, int *result) {
    if (a == NULL || b == NULL || result == NULL) {
        return INVALID_ARGS;
    }
    /* 1) зарплата */
    if (a->salary_units != b->salary_units) {
        *result = (a->salary_units < b->salary_units) ? -1 : 1;
        return OK;
    }
    /* 2) фамилия */
    compare_words(a->surname, b->surname, result);
    if (*result != 0) {
        return OK;
    }
    /* 3) имя */
    compare_words(a->name, b->name, result);
    if (*result != 0) {
        return OK;
    }
    /* 4) id. Не "a->id - b->id": разность больших чисел может переполнить
       long long (и даже int, если вернуть ее из компаратора) */
    if (a->id != b->id) {
        *result = (a->id < b->id) ? -1 : 1;
        return OK;
    }
    /* 5) Условие тут кончается, но записи еще могут отличаться текстом зарплаты:
       "1000" и "1000.0000001" равны с точностью eps, а в файл пишутся по-разному.
       qsort не обещает, в каком порядке поставит "равные" элементы, - и на разных
       системах порядок мог бы быть разным. Сравниваем текст зарплаты (strcmp):
       тогда 0 получается, только если строки сотрудников совпадают целиком,
       и выходной файл всегда один и тот же */
    *result = strcmp(a->salary_text, b->salary_text);
    return OK;
}

/* Компараторы для qsort: по возрастанию и по убыванию.
   Это единственные функции без status_code: их прототип задан функцией qsort
   (int, два параметра const void *). qsort передает адреса двух элементов массива;
   ответ < 0 - первый должен стоять раньше, 0 - все равно, > 0 - позже */
int compare_asc(const void *a, const void *b) {
    int result = 0;
    compare_employees((const Employee *)a, (const Employee *)b, &result);
    return result;
}

/* По убыванию - то же сравнение с переставленными сотрудниками: кто был "раньше",
   становится "позже". Порядок обратный по всем признакам сразу */
int compare_desc(const void *a, const void *b) {
    int result = 0;
    compare_employees((const Employee *)b, (const Employee *)a, &result);
    return result;
}

/* Отсортировать массив сотрудников стандартной функцией qsort */
status_code sort_employees(Employee *arr, const size_t count, const sort_order order) {
    if (arr == NULL && count > 0) {
        return INVALID_ARGS;
    }
    if (count < 2) {
        return OK;   /* 0 или 1 сотрудник уже "отсортированы" (и qsort с NULL не зовем) */
    }
    /* qsort(массив, количество, размер одного элемента, компаратор) */
    if (order == ASCENDING) {
        qsort(arr, count, sizeof(Employee), compare_asc);
    } else {
        qsort(arr, count, sizeof(Employee), compare_desc);
    }
    return OK;
}

/* ===================== аргументы и файлы ===================== */

/* Разобрать флаг: ровно 2 символа, первый '-' или '/', второй 'a' или 'd' */
status_code parse_flag(const char *str, sort_order *order) {
    if (str == NULL || order == NULL) {
        return INVALID_ARGS;
    }
    /* str[0] != '\0' проверяем раньше, чем смотреть str[1] и str[2]:
       && не идет дальше первого ложного условия, так что за конец строки не выходим */
    if ((str[0] != '-' && str[0] != '/') || str[1] == '\0' || str[2] != '\0') {
        return INVALID_FLAG;
    }
    if (str[1] == 'a') {
        *order = ASCENDING;
    } else if (str[1] == 'd') {
        *order = DESCENDING;
    } else {
        return INVALID_FLAG;
    }
    return OK;
}

#ifdef _WIN32
/* Только для Windows: полный путь файла ("in.txt" -> "C:\...\lab2\in.txt") в динамической памяти.
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
            return ALLOC_ERROR;
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

/* Два пути указывают на один и тот же файл? Сравнивать строки путей нельзя:
   "in.txt", "./in.txt" и "../lab2/in.txt" - разные строки, но один файл.
   Linux: stat дает номер устройства (st_dev) и номер файла на нем (st_ino) -
   у одного файла они совпадают, как бы ни был записан путь.
   Windows: full_path делает из пути полный ("C:\...\in.txt"), полные пути
   сравниваем без учета регистра (_stricmp) - в Windows "IN.TXT" и "in.txt" один файл.
   Файла еще нет (выходной файл часто так) - значит, это точно не входной файл.
   Путь неправильный - тоже "разные": fopen дальше сам сообщит об ошибке */
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
        free(full1);   /* free(NULL) ничего не делает - можно не проверять */
        free(full2);
        return ALLOC_ERROR;
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

/* Записать сотрудников в открытый файл: по одному в строке, "id имя фамилия зарплата".
   Зарплату пишем ее текстом из входного файла - без округления.
   fprintf возвращает отрицательное число, если записать не получилось (например, диск полон) */
status_code write_employees(FILE *out, const Employee *arr, const size_t count) {
    if (out == NULL || (arr == NULL && count > 0)) {
        return INVALID_ARGS;
    }
    for (size_t i = 0; i < count; i++) {
        if (fprintf(out, "%lld %s %s %s\n", arr[i].id, arr[i].name, arr[i].surname,
                    arr[i].salary_text) < 0) {
            return WRITE_ERROR;
        }
    }
    return OK;
}

/* ===================== main ===================== */

int main(int argc, char *argv[]) {
    /* Точность сравнения зарплат: зарплаты, совпадающие после округления до 10^-6,
       считаются равными. Передается параметром во все функции, которым она нужна */
    const double EPS = 1e-6;

    /* 1) Аргументы: ровно 3 (входной файл, флаг, выходной файл) */
    if (argc != 4) {
        fprintf(stderr, "Usage: lab2_task2 <input file> <flag> <output file>\n");
        fprintf(stderr, "Flags: -a or /a - ascending order, -d or /d - descending order\n");
        return 1;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[3];

    /* sc - статус последней вызванной функции, одна переменная на весь main */
    status_code sc = OK;
    sort_order order = ASCENDING;
    if (parse_flag(argv[2], &order) != OK) {
        /* здесь возможен только INVALID_FLAG: "-x", "a", "-ad", "" */
        fprintf(stderr, "Error: wrong flag '%s', use -a, /a, -d or /d\n", argv[2]);
        return 1;
    }

    /* 2) Входной и выходной файл не должны быть одним файлом: выходной файл открывается
       на запись ("w"), и fopen сразу стирает его содержимое - входные данные пропали бы.
       same_file ловит и разные записи одного пути: "in.txt" и "./in.txt" */
    int equal = 0;
    sc = same_file(in_path, out_path, &equal);
    if (sc == ALLOC_ERROR) {
        /* на Windows same_file выделяет память под полные пути */
        fprintf(stderr, "Error: not enough memory\n");
        return 1;
    }
    if (sc != OK) {
        fprintf(stderr, "Error: internal error\n");   /* INVALID_ARGS: NULL-ов здесь не бывает */
        return 1;
    }
    if (equal) {
        fprintf(stderr, "Error: input and output must be different files\n");
        return 1;
    }

    /* 3) Чтение. Файл открыли - прочитали - сразу закрыли (при любом исходе чтения) */
    FILE *in = fopen(in_path, "r");
    if (in == NULL) {
        fprintf(stderr, "Error: can not open file '%s'\n", in_path);
        return 1;
    }
    Employee *arr = NULL;
    size_t count = 0;
    size_t line_number = 0;
    sc = read_employees(in, EPS, &arr, &count, &line_number);
    fclose(in);
    if (sc != OK) {
        /* read_employees при ошибке уже все освободил. Каждая ошибка в строке -
           свое сообщение с номером строки, чтобы было видно, что исправить в файле */
        switch (sc) {
            case BAD_LINE:
                /* не 4 слова: нет зарплаты, имя из двух слов, лишнее слово */
                fprintf(stderr, "Error: line %zu: a line must be: id name surname salary\n",
                        line_number);
                break;
            case BAD_ID:
                /* id отрицательный, дробный, не число, больше LLONG_MAX */
                fprintf(stderr, "Error: line %zu: id must be a whole number from 0 to %lld\n",
                        line_number, LLONG_MAX);
                break;
            case BAD_NAME:
                /* в имени цифры, дефис, русские буквы */
                fprintf(stderr, "Error: line %zu: name must have only latin letters\n",
                        line_number);
                break;
            case BAD_SURNAME:
                /* то же для фамилии */
                fprintf(stderr, "Error: line %zu: surname must have only latin letters\n",
                        line_number);
                break;
            case BAD_SALARY:
                /* отрицательная, с запятой, "1e3", не число */
                fprintf(stderr, "Error: line %zu: salary must be a number like 1500 or 1500.75 "
                                "(not negative)\n", line_number);
                break;
            case SALARY_TOO_BIG:
                /* salary / EPS не помещается в long long; граница ~9.22e+12 при EPS = 1e-6 */
                fprintf(stderr, "Error: line %zu: salary is too big, it must be less than %.3g\n",
                        line_number, (double)LLONG_MAX * EPS);
                break;
            case ALLOC_ERROR:
                fprintf(stderr, "Error: not enough memory\n");
                break;
            default:
                /* INVALID_ARGS и прочие статусы, которых тут быть не может.
                   Если сообщение появилось - ошибка в самой программе */
                fprintf(stderr, "Error: internal error\n");
                break;
        }
        return 1;
    }

    /* 4) Сортировка - отдельно от чтения и записи (работа с данными, а не с файлами) */
    sc = sort_employees(arr, count, order);
    if (sc != OK) {
        /* INVALID_ARGS быть не может: arr и count получены из read_employees */
        fprintf(stderr, "Error: internal error\n");
        free_employees(arr, count);
        return 1;
    }

    /* 5) Запись. Выходной файл создаем только сейчас, когда все данные правильные:
       при ошибке во входном файле выходной не создается и не портится */
    FILE *out = fopen(out_path, "w");
    if (out == NULL) {
        fprintf(stderr, "Error: can not create file '%s'\n", out_path);
        free_employees(arr, count);
        return 1;
    }
    sc = write_employees(out, arr, count);
    /* fclose тоже может не суметь записать (данные копятся в буфере и
       дописываются при закрытии) - поэтому его результат тоже проверяем */
    if (fclose(out) != 0 && sc == OK) {
        sc = WRITE_ERROR;
    }
    free_employees(arr, count);
    if (sc != OK) {
        /* WRITE_ERROR - диск полон, нет прав; INVALID_ARGS быть не может */
        fprintf(stderr, "Error: can not write to file '%s'\n", out_path);
        return 1;
    }

    printf("Done: %zu employees are written to %s\n", count, out_path);
    return 0;
}
