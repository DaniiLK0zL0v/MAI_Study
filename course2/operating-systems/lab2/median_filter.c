/*
 * Лабораторная работа 2 по ОС, вариант 11, Windows.
 * Наложить K раз медианный фильтр на матрицу целых чисел. Размер окна задаёт пользователь.
 * Наибольшее число потоков, работающих одновременно, задаётся ключом -t.
 *
 * Медианный фильтр с окном W x W (W нечётное): каждый элемент заменяется медианой
 * W*W чисел вокруг него (сам элемент - в центре окна). Медиана - число, которое стоит
 * посередине, если эти W*W чисел упорядочить. За краем матрицы берётся ближайший
 * элемент края (координата "прижимается" к границе).
 *
 * Запуск:
 *   median_filter -t T -w W -k K                    матрица со стандартного ввода:
 *                                                   "строки столбцы", затем все числа по строкам;
 *                                                   результат печатается в том же виде (без размеров)
 *   median_filter -t T -w W -k K -g R C SEED        случайная матрица R x C (SEED - начало
 *                                                   генератора); печатаются время и контрольная сумма
 *   median_filter -t T -w W -k K -g R C SEED -b     замер для 1, 2, ..., T потоков:
 *                                                   время, ускорение и эффективность
 *   median_filter                                   без ключей (двойной щелчок по exe): программа
 *                                                   сама спрашивает T, W, K, размеры и матрицу
 *                                                   (каждая строка матрицы - отдельной строкой)
 *
 * Модель параллельности - Fork & Join (лекция 3): на каждом из K проходов создаются T потоков
 * (CreateThread), каждый обрабатывает свой отрезок строк, затем главный поток ждёт их всех
 * (WaitForMultipleObjects). Потоки читают только исходную матрицу прохода и пишут каждый
 * в свои строки новой матрицы - общих изменяемых данных нет, поэтому синхронизация
 * (мьютексы и т.п.) не нужна. Новый проход начинается, только когда закончен предыдущий.
 */

#include <windows.h>
#include <conio.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ограничения на ключи */
#define MAX_THREADS 64          /* WaitForMultipleObjects ждёт не больше 64 объектов сразу */
#define MAX_WINDOW 99           /* окно 99 x 99 - уже почти 10000 чисел на каждый элемент */
#define MAX_TIMES 1000          /* сколько раз можно наложить фильтр */
#define MAX_SIDE 20000          /* наибольшее число строк и столбцов */
#define MAX_CELLS 25000000      /* наибольшее число элементов (100 МБ на одну матрицу) */
#define MAX_TOKEN 32            /* самое длинное число во вводе (в символах) */
#define MAX_WORDS 32            /* сколько слов может быть в строке с ключами (режим вопросов) */

/* Что передаётся одному потоку: откуда читать, куда писать и какие строки - его */
typedef struct {
    const int *src;     /* матрица до этого прохода (только чтение)    */
    int *dst;           /* матрица после прохода (поток пишет свои строки) */
    int rows;           /* размеры матрицы                              */
    int cols;
    int window;         /* размер окна W                                */
    int row_from;       /* первая строка потока                         */
    int row_to;         /* строка после последней (не включается)       */
} thread_task;

/* Ключи запуска */
typedef struct {
    int threads;        /* -t: наибольшее число потоков               */
    int window;         /* -w: размер окна                            */
    int times;          /* -k: сколько раз наложить фильтр            */
    int generate;       /* -g задан: матрица случайная                */
    int rows;           /* для -g: размеры и начало генератора        */
    int cols;
    unsigned int seed;
    int benchmark;      /* -b: замер для 1..T потоков                 */
} options;

/* ===================================================================== */
/* Разбор ключей                                                          */
/* ===================================================================== */

static void print_usage(void) {
    fprintf(stderr, "Usage: median_filter -t THREADS -w WINDOW -k TIMES [-g ROWS COLS SEED [-b]]\n");
}

/*
 * Строка str -> целое число от low до high.
 * Всё число должно состоять из цифр (можно со знаком), без лишних символов в конце.
 * Возвращает 0 - успех, 1 - это не число или оно вне границ.
 */
static int parse_number(const char *str, long long low, long long high, long long *value) {
    char *end = NULL;
    /* strtoll сам пропускает пробелы в начале - их не разрешаем */
    if (str[0] == '\0' || str[0] == ' ' || str[0] == '\t') {
        return 1;
    }
    errno = 0;
    long long number = strtoll(str, &end, 10);
    /* *end != '\0' - после числа есть что-то ещё; errno == ERANGE - не влезло в long long */
    if (*end != '\0' || errno == ERANGE || number < low || number > high) {
        return 1;
    }
    *value = number;
    return 0;
}

/*
 * Взять значение ключа key: следующий аргумент после argv[*index], число от low до high.
 * (Имя ключа передаётся отдельно: у -g три значения подряд, и argv[*index] - уже не сам ключ.)
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int key_value(int argc, char *argv[], int *index, const char *key, long long low, long long high,
                     const char *what, long long *value) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "Error: key %s needs a value\n", key);
        return 1;
    }
    (*index)++;
    if (parse_number(argv[*index], low, high, value) != 0) {
        fprintf(stderr, "Error: %s must be a number from %lld to %lld, not '%s'\n", what, low, high, argv[*index]);
        return 1;
    }
    return 0;
}

/*
 * Разобрать все ключи в opts.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int parse_options(int argc, char *argv[], options *opts) {
    long long value = 0;
    /* -1 - ключ ещё не встретился */
    opts->threads = -1;
    opts->window = -1;
    opts->times = -1;
    opts->generate = 0;
    opts->benchmark = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (key_value(argc, argv, &i, "-t", 1, MAX_THREADS, "the number of threads", &value) != 0) {
                return 1;
            }
            opts->threads = (int)value;
        } else if (strcmp(argv[i], "-w") == 0) {
            if (key_value(argc, argv, &i, "-w", 1, MAX_WINDOW, "the window size", &value) != 0) {
                return 1;
            }
            /* у окна чётного размера нет центрального элемента */
            if (value % 2 == 0) {
                fprintf(stderr, "Error: the window size must be odd\n");
                return 1;
            }
            opts->window = (int)value;
        } else if (strcmp(argv[i], "-k") == 0) {
            if (key_value(argc, argv, &i, "-k", 0, MAX_TIMES, "K", &value) != 0) {
                return 1;
            }
            opts->times = (int)value;
        } else if (strcmp(argv[i], "-g") == 0) {
            /* три значения подряд: строки, столбцы, начало генератора */
            opts->generate = 1;
            if (key_value(argc, argv, &i, "-g", 1, MAX_SIDE, "the number of rows", &value) != 0) {
                return 1;
            }
            opts->rows = (int)value;
            if (key_value(argc, argv, &i, "-g", 1, MAX_SIDE, "the number of columns", &value) != 0) {
                return 1;
            }
            opts->cols = (int)value;
            if (key_value(argc, argv, &i, "-g", 0, 4294967295LL, "the seed", &value) != 0) {
                return 1;
            }
            opts->seed = (unsigned int)value;
        } else if (strcmp(argv[i], "-b") == 0) {
            opts->benchmark = 1;
        } else {
            fprintf(stderr, "Error: unknown key '%s'\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    /* обязательные ключи */
    if (opts->threads < 0 || opts->window < 0 || opts->times < 0) {
        fprintf(stderr, "Error: keys -t, -w and -k are required\n");
        print_usage();
        return 1;
    }
    /* замер имеет смысл только на случайной матрице: её можно создать заново для каждого замера */
    if (opts->benchmark && !opts->generate) {
        fprintf(stderr, "Error: key -b works only with -g\n");
        return 1;
    }
    if (opts->generate && (long long)opts->rows * opts->cols > MAX_CELLS) {
        fprintf(stderr, "Error: the matrix is too big (more than %d elements)\n", MAX_CELLS);
        return 1;
    }
    return 0;
}

/* ===================================================================== */
/* Матрица: чтение, создание, печать                                      */
/* ===================================================================== */

/*
 * Прочитать со стандартного ввода одно слово (символы до пробела, табуляции или конца строки).
 * *found = 0 - ввод закончился, слова нет.
 * Возвращает 0 - успех, 1 - слово длиннее MAX_TOKEN символов (такого числа не бывает).
 */
static int read_token(char *token, int *found) {
    int symbol = getchar();
    /* пропускаем пробельные символы перед словом */
    while (symbol == ' ' || symbol == '\t' || symbol == '\n' || symbol == '\r') {
        symbol = getchar();
    }
    *found = 0;
    if (symbol == EOF) {
        return 0;
    }
    /* собираем слово до пробельного символа или конца ввода */
    int length = 0;
    while (symbol != EOF && symbol != ' ' && symbol != '\t' && symbol != '\n' && symbol != '\r') {
        if (length == MAX_TOKEN) {
            token[length] = '\0';
            return 1;
        }
        token[length++] = (char)symbol;
        symbol = getchar();
    }
    token[length] = '\0';
    *found = 1;
    return 0;
}

/*
 * Прочитать со стандартного ввода одно число от low до high.
 * what - что это за число (для сообщения об ошибке).
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int read_number(long long low, long long high, const char *what, long long *value) {
    char token[MAX_TOKEN + 1];
    int found = 0;
    if (read_token(token, &found) != 0) {
        fprintf(stderr, "Error: %s is too long: '%s...'\n", what, token);
        return 1;
    }
    if (!found) {
        fprintf(stderr, "Error: the input ended, %s expected\n", what);
        return 1;
    }
    if (parse_number(token, low, high, value) != 0) {
        fprintf(stderr, "Error: %s must be a number from %lld to %lld, not '%s'\n", what, low, high, token);
        return 1;
    }
    return 0;
}

/*
 * Пропустить невидимую метку BOM (байты EF BB BF) в самом начале ввода, если она есть.
 * Её пишут Блокнот (файл в UTF-8) и PowerShell (при передаче текста программе).
 * Число с байта EF начаться не может, поэтому такой байт в начале - либо BOM, либо ошибка ввода.
 * Возвращает 0 - успех, 1 - в начале ввода неправильные байты (сообщение уже напечатано).
 */
static int skip_bom(void) {
    int symbol = getchar();
    if (symbol == 0xEF) {
        if (getchar() != 0xBB || getchar() != 0xBF) {
            fprintf(stderr, "Error: wrong symbols at the start of the input\n");
            return 1;
        }
    } else if (symbol != EOF) {
        /* это не BOM - возвращаем символ во ввод, его прочитают дальше */
        ungetc(symbol, stdin);
    }
    return 0;
}

/*
 * Прочитать матрицу со стандартного ввода: "строки столбцы", затем строки*столбцы целых чисел.
 * from_keyboard = 1 - матрицу вводит человек: после последнего числа сразу заканчиваем
 * (иначе пришлось бы ещё нажимать Ctrl+Z, чтобы программа убедилась, что больше ничего нет).
 * Память под матрицу выделяется здесь (malloc), освобождает вызывающий.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int read_matrix(int **matrix, int *rows, int *cols, int from_keyboard) {
    if (skip_bom() != 0) {
        return 1;
    }

    long long value = 0;
    if (read_number(1, MAX_SIDE, "the number of rows", &value) != 0) {
        return 1;
    }
    *rows = (int)value;
    if (read_number(1, MAX_SIDE, "the number of columns", &value) != 0) {
        return 1;
    }
    *cols = (int)value;
    if ((long long)*rows * *cols > MAX_CELLS) {
        fprintf(stderr, "Error: the matrix is too big (more than %d elements)\n", MAX_CELLS);
        return 1;
    }

    size_t count = (size_t)*rows * (size_t)*cols;
    *matrix = malloc(count * sizeof(int));
    if (*matrix == NULL) {
        fprintf(stderr, "Error: not enough memory for the matrix\n");
        return 1;
    }

    /* элементы - любые числа типа int */
    for (size_t i = 0; i < count; i++) {
        if (read_number(INT_MIN, INT_MAX, "a matrix element", &value) != 0) {
            free(*matrix);
            *matrix = NULL;
            return 1;
        }
        (*matrix)[i] = (int)value;
    }

    if (from_keyboard) {
        return 0;
    }
    /* после матрицы ничего быть не должно: иначе размеры, скорее всего, указаны неверно */
    char token[MAX_TOKEN + 1];
    int found = 0;
    read_token(token, &found);
    if (found) {
        fprintf(stderr, "Error: extra data after the matrix: '%s'\n", token);
        free(*matrix);
        *matrix = NULL;
        return 1;
    }
    return 0;
}

/*
 * Заполнить матрицу случайными числами от 0 до 999.
 * Генератор - линейный конгруэнтный: x = x * 1103515245 + 12345 (по модулю 2^32,
 * переполнение unsigned int - это и есть взятие по модулю). Одинаковый seed - одинаковая матрица:
 * так можно сравнить результаты при разном числе потоков.
 */
static void fill_random(int *matrix, size_t count, unsigned int seed) {
    unsigned int state = seed;
    for (size_t i = 0; i < count; i++) {
        state = state * 1103515245u + 12345u;
        /* старшие биты у такого генератора "случайнее" младших */
        matrix[i] = (int)((state >> 16) % 1000u);
    }
}

/*
 * Контрольная сумма матрицы: h = h * 31 + элемент (по модулю 2^64).
 * Зависит от каждого элемента и от его места: если две матрицы различаются,
 * суммы почти наверняка разные.
 */
static unsigned long long checksum(const int *matrix, size_t count) {
    unsigned long long hash = 0;
    for (size_t i = 0; i < count; i++) {
        hash = hash * 31u + (unsigned int)matrix[i];
    }
    return hash;
}

/* Напечатать матрицу: строка матрицы - строка вывода, числа через пробел */
static void print_matrix(const int *matrix, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            printf(c == 0 ? "%d" : " %d", matrix[(size_t)r * cols + c]);
        }
        printf("\n");
    }
}

/* ===================================================================== */
/* Медианный фильтр                                                       */
/* ===================================================================== */

/* Сравнение двух int для qsort: отрицательное - a < b, 0 - равны, положительное - a > b */
static int compare_ints(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    /* не x - y: разность может переполнить int */
    return (x > y) - (x < y);
}

/* Прижать координату к границам: меньше 0 -> 0, больше last -> last */
static int clamp(int value, int last) {
    if (value < 0) {
        return 0;
    }
    if (value > last) {
        return last;
    }
    return value;
}

/*
 * Функция потока: медианный фильтр для строк row_from .. row_to - 1.
 * Возвращает код завершения потока: 0 - успех, 1 - не хватило памяти.
 * WINAPI - соглашение о вызове, которого ждёт CreateThread.
 */
static DWORD WINAPI filter_rows(LPVOID arg) {
    const thread_task *task = (const thread_task *)arg;
    int half = task->window / 2;
    int area = task->window * task->window;

    /* у каждого потока свой буфер для чисел окна - потоки не мешают друг другу */
    int *values = malloc((size_t)area * sizeof(int));
    if (values == NULL) {
        return 1;
    }

    for (int r = task->row_from; r < task->row_to; r++) {
        for (int c = 0; c < task->cols; c++) {
            /* собираем W*W чисел вокруг (r, c); за краем - ближайший элемент края */
            int count = 0;
            for (int dr = -half; dr <= half; dr++) {
                int row = clamp(r + dr, task->rows - 1);
                const int *line = task->src + (size_t)row * task->cols;
                for (int dc = -half; dc <= half; dc++) {
                    values[count++] = line[clamp(c + dc, task->cols - 1)];
                }
            }
            /* упорядочиваем и берём средний элемент (count = W*W - нечётное) */
            qsort(values, (size_t)count, sizeof(int), compare_ints);
            task->dst[(size_t)r * task->cols + c] = values[count / 2];
        }
    }

    free(values);
    return 0;
}

/*
 * Дождаться count потоков из handles, проверить их коды завершения и закрыть описатели.
 * Возвращает 0 - все потоки отработали успешно, 1 - нет (сообщение уже напечатано).
 */
static int join_threads(HANDLE *handles, int count) {
    int result = 0;
    if (count == 0) {
        return 0;
    }
    /* TRUE - ждать, пока завершатся все, INFINITE - без ограничения по времени */
    if (WaitForMultipleObjects((DWORD)count, handles, TRUE, INFINITE) == WAIT_FAILED) {
        fprintf(stderr, "Error: WaitForMultipleObjects failed (Windows error %lu)\n", (unsigned long)GetLastError());
        result = 1;
    }
    for (int i = 0; i < count; i++) {
        DWORD code = 0;
        if (result == 0 && GetExitCodeThread(handles[i], &code) && code != 0) {
            fprintf(stderr, "Error: not enough memory in a thread\n");
            result = 1;
        }
        CloseHandle(handles[i]);
    }
    return result;
}

/*
 * Наложить фильтр times раз, используя не больше threads потоков одновременно.
 * matrix - исходная матрица, в ней же оказывается результат; spare - вторая матрица того же
 * размера: проход читает одну матрицу и пишет в другую, потом они меняются ролями.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int apply_filter(int *matrix, int *spare, int rows, int cols, int window, int times, int threads) {
    HANDLE handles[MAX_THREADS];
    thread_task tasks[MAX_THREADS];
    /* потоков больше, чем строк, не нужно: у лишних не было бы работы */
    if (threads > rows) {
        threads = rows;
    }

    int *src = matrix;
    int *dst = spare;
    for (int pass = 0; pass < times; pass++) {
        /* Fork: делим строки поровну; первые rows % threads потоков получают на строку больше */
        int base = rows / threads;
        int extra = rows % threads;
        int next_row = 0;
        int created = 0;
        for (int i = 0; i < threads; i++) {
            tasks[i].src = src;
            tasks[i].dst = dst;
            tasks[i].rows = rows;
            tasks[i].cols = cols;
            tasks[i].window = window;
            tasks[i].row_from = next_row;
            tasks[i].row_to = next_row + base + (i < extra ? 1 : 0);
            next_row = tasks[i].row_to;

            /* CreateThread: стек по умолчанию, функция filter_rows с аргументом &tasks[i],
               0 - поток сразу начинает работать */
            handles[i] = CreateThread(NULL, 0, filter_rows, &tasks[i], 0, NULL);
            if (handles[i] == NULL) {
                fprintf(stderr, "Error: CreateThread failed (Windows error %lu)\n", (unsigned long)GetLastError());
                /* уже созданные потоки работают с нашими матрицами - их надо дождаться */
                join_threads(handles, created);
                return 1;
            }
            created++;
        }

        /* Join: ждём все потоки этого прохода */
        if (join_threads(handles, created) != 0) {
            return 1;
        }

        /* результат прохода - исходные данные следующего: меняем матрицы ролями */
        int *tmp = src;
        src = dst;
        dst = tmp;
    }

    /* после нечётного числа проходов результат лежит в spare - копируем его в matrix */
    if (src != matrix) {
        memcpy(matrix, src, (size_t)rows * (size_t)cols * sizeof(int));
    }
    return 0;
}

/* ===================================================================== */
/* Время                                                                  */
/* ===================================================================== */

/* Текущее время в секундах по точному счётчику Windows (QueryPerformanceCounter) */
static double now_seconds(void) {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

/* Число логических процессоров компьютера (ядер с учётом Hyper-Threading) */
static int processor_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
}

/*
 * Наложить фильтр на копию original и измерить время.
 * work и spare - рабочие матрицы того же размера.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int timed_run(const options *opts, const int *original, int *work, int *spare, int threads,
                     double *seconds, unsigned long long *sum) {
    size_t count = (size_t)opts->rows * (size_t)opts->cols;
    memcpy(work, original, count * sizeof(int));
    /* замеряем только сам фильтр, без создания матрицы */
    double start = now_seconds();
    if (apply_filter(work, spare, opts->rows, opts->cols, opts->window, opts->times, threads) != 0) {
        return 1;
    }
    *seconds = now_seconds() - start;
    *sum = checksum(work, count);
    return 0;
}

/* ===================================================================== */
/* Три режима работы                                                      */
/* ===================================================================== */

/* Матрица со стандартного ввода -> фильтр -> результат на стандартный вывод.
   from_keyboard = 1 - матрицу вводит человек (см. read_matrix); тогда перед ответом - заголовок */
static int run_input_mode(const options *opts, int from_keyboard) {
    int *matrix = NULL;
    int rows = 0;
    int cols = 0;
    if (read_matrix(&matrix, &rows, &cols, from_keyboard) != 0) {
        return 1;
    }
    if (from_keyboard) {
        fprintf(stderr, "\nResult:\n");
    }
    int *spare = malloc((size_t)rows * (size_t)cols * sizeof(int));
    if (spare == NULL) {
        fprintf(stderr, "Error: not enough memory for the matrix\n");
        free(matrix);
        return 1;
    }

    int result = apply_filter(matrix, spare, rows, cols, opts->window, opts->times, opts->threads);
    if (result == 0) {
        print_matrix(matrix, rows, cols);
    }
    free(matrix);
    free(spare);
    return result;
}

/*
 * Случайная матрица: -g (один запуск с T потоками) или -g -b (замер для 1..T потоков).
 * Нужны три матрицы: original (не меняется), work (результат) и spare (для смены ролей).
 */
static int run_generate_mode(const options *opts) {
    size_t count = (size_t)opts->rows * (size_t)opts->cols;
    int *original = malloc(count * sizeof(int));
    int *work = malloc(count * sizeof(int));
    int *spare = malloc(count * sizeof(int));
    if (original == NULL || work == NULL || spare == NULL) {
        fprintf(stderr, "Error: not enough memory for the matrix\n");
        free(original);
        free(work);
        free(spare);
        return 1;
    }
    fill_random(original, count, opts->seed);

    /* сколько потоков реально будет работать: не больше, чем строк */
    int used = opts->threads < opts->rows ? opts->threads : opts->rows;
    printf("matrix: %d x %d, window: %d, K: %d, logical processors: %d\n",
           opts->rows, opts->cols, opts->window, opts->times, processor_count());

    int result = 0;
    double seconds = 0.0;
    unsigned long long sum = 0;
    if (!opts->benchmark) {
        /* PID - чтобы найти процесс в Диспетчере задач и посмотреть число его потоков */
        printf("process id: %lu, threads: %d\n", (unsigned long)GetCurrentProcessId(), used);
        fflush(stdout);
        result = timed_run(opts, original, work, spare, opts->threads, &seconds, &sum);
        if (result == 0) {
            printf("time: %.4f s\n", seconds);
            printf("checksum: %llu\n", sum);
        }
    } else {
        /* T(1) - время с одним потоком; ускорение S = T(1) / T(p), эффективность E = S / p */
        double time_one = 0.0;
        unsigned long long sum_one = 0;
        printf("threads  time, s   speedup  efficiency\n");
        for (int p = 1; p <= used && result == 0; p++) {
            result = timed_run(opts, original, work, spare, p, &seconds, &sum);
            if (result != 0) {
                break;
            }
            if (p == 1) {
                time_one = seconds;
                sum_one = sum;
            } else if (sum != sum_one) {
                /* при любом числе потоков результат обязан быть одинаковым - иначе ошибка в программе */
                fprintf(stderr, "Error: internal error (the result depends on the number of threads)\n");
                result = 1;
                break;
            }
            /* очень быстрый проход может занять "0 секунд" - тогда ускорение не считаем */
            double speedup = seconds > 0.0 ? time_one / seconds : 0.0;
            printf("%7d  %8.4f  %7.2f  %10.2f\n", p, seconds, speedup, speedup / p);
            fflush(stdout);
        }
        if (result == 0) {
            printf("checksum: %llu\n", sum_one);
        }
    }

    free(original);
    free(work);
    free(spare);
    return result;
}

/* ===================================================================== */
/* Режим вопросов: запуск без ключей (например, двойным щелчком по exe)    */
/* ===================================================================== */

/* Ввод идёт с клавиатуры? GetConsoleMode удаётся только для настоящей консоли
   (для файла, канала и устройства NUL - нет; _isatty считает NUL терминалом, поэтому он не подходит) */
static int input_is_console(void) {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) ? 1 : 0;
}

/*
 * Прочитать одну строку ввода (без '\n') в буфер *line; буфер растёт вдвое через realloc.
 * Возвращает 0 - строка прочитана, 1 - ввод закончился (строки нет), 2 - не хватило памяти.
 */
static int read_input_line(char **line, size_t *capacity) {
    size_t length = 0;
    int symbol = getchar();
    if (symbol == EOF) {
        return 1;
    }
    while (symbol != EOF && symbol != '\n') {
        /* +1 - место для '\0' в конце строки */
        if (length + 1 >= *capacity) {
            size_t new_capacity = *capacity * 2;
            char *tmp = realloc(*line, new_capacity);
            if (tmp == NULL) {
                return 2;
            }
            *line = tmp;
            *capacity = new_capacity;
        }
        (*line)[length++] = (char)symbol;
        symbol = getchar();
    }
    (*line)[length] = '\0';
    return 0;
}

/*
 * Разобрать строку line: в ней должно быть ровно count чисел от low до high (через пробелы).
 * Числа записываются в values. Строка портится: после каждого числа ставится '\0'.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано, строку введут заново).
 */
static int parse_line(char *line, int count, long long low, long long high, const char *what,
                      long long *values) {
    int found = 0;
    char *p = line;
    while (1) {
        /* пропускаем пробелы перед числом; '\r' - остаток конца строки Windows */
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            break;   /* строка кончилась */
        }
        /* число - до следующего пробела или до конца строки */
        char *start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r') {
            p++;
        }
        int last = (*p == '\0');
        *p = '\0';
        if (found == count) {
            fprintf(stderr, "Error: too many numbers, enter %d, try again\n", count);
            return 1;
        }
        if (parse_number(start, low, high, &values[found]) != 0) {
            fprintf(stderr, "Error: %s must be a number from %lld to %lld, not '%s', try again\n",
                    what, low, high, start);
            return 1;
        }
        found++;
        if (!last) {
            p++;     /* за поставленный '\0' - к следующему числу */
        }
    }
    if (found < count) {
        fprintf(stderr, "Error: too few numbers, enter %d, try again\n", count);
        return 1;
    }
    return 0;
}

/*
 * Задать вопрос question (только если ввод с клавиатуры) и прочитать ответ - строку из count чисел
 * от low до high. Неправильный ответ - сообщение и тот же вопрос ещё раз.
 * line, capacity - общий буфер для строк ввода.
 * Возвращает 0 - успех, 1 - ввод закончился или не хватило памяти (сообщение уже напечатано).
 */
static int ask_numbers(int console, const char *question, int count, long long low, long long high,
                       const char *what, long long *values, char **line, size_t *capacity) {
    while (1) {
        if (console) {
            fprintf(stderr, "%s", question);
        }
        int st = read_input_line(line, capacity);
        if (st == 1) {
            fprintf(stderr, "Error: the input ended, %s expected\n", what);
            return 1;
        }
        if (st == 2) {
            fprintf(stderr, "Error: not enough memory for the line\n");
            return 1;
        }
        if (parse_line(*line, count, low, high, what, values) == 0) {
            return 0;
        }
    }
}

/* Ответ похож на строку с ключами: начинается с '-' (но не "-5" - это отрицательное число)
   или со слова median_filter */
static int looks_like_keys(const char *line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (line[0] == '-') {
        return !(line[1] >= '0' && line[1] <= '9');
    }
    return strncmp(line, "median_filter", strlen("median_filter")) == 0;
}

/*
 * Разрезать строку на слова (по пробелам): в конец каждого слова ставится '\0', адреса слов - в words.
 * Возвращает число слов или -1, если слов больше max.
 */
static int split_words(char *line, char *words[], int max) {
    int count = 0;
    char *p = line;
    while (1) {
        while (*p == ' ' || *p == '\t' || *p == '\r') {
            p++;
        }
        if (*p == '\0') {
            return count;
        }
        if (count == max) {
            return -1;
        }
        words[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r') {
            p++;
        }
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
}

/*
 * Первый вопрос: число потоков. Вместо числа можно ввести всю строку с ключами, как в командной
 * строке ("-t 4 -w 3 -k 1 -g 1500 1500 1 -b" или "median_filter -t ..."); тогда *keys_given = 1,
 * и все настройки уже в opts. Неправильный ответ - сообщение и тот же вопрос снова.
 * Возвращает 0 - успех, 1 - ввод закончился или не хватило памяти (сообщение уже напечатано).
 */
static int ask_threads_or_keys(int console, options *opts, int *keys_given, char **line, size_t *capacity) {
    *keys_given = 0;
    while (1) {
        if (console) {
            fprintf(stderr, "Number of threads (1..64) or the keys (for example: -t 4 -w 3 -k 1): ");
        }
        int st = read_input_line(line, capacity);
        if (st == 1) {
            fprintf(stderr, "Error: the input ended, the number of threads expected\n");
            return 1;
        }
        if (st == 2) {
            fprintf(stderr, "Error: not enough memory for the line\n");
            return 1;
        }

        if (looks_like_keys(*line)) {
            /* words[0] - имя программы, как argv[0] у настоящего запуска; ключи - с words[1] */
            char *words[MAX_WORDS + 1];
            int count = split_words(*line, words + 1, MAX_WORDS);
            if (count < 0) {
                fprintf(stderr, "Error: too many words (more than %d), try again\n", MAX_WORDS);
                continue;
            }
            char program[] = "median_filter";
            char **argv = words;
            int argc = count + 1;
            /* слово median_filter (или median_filter.exe) в начале - это и есть имя программы */
            if (strncmp(words[1], "median_filter", strlen("median_filter")) == 0) {
                argv = words + 1;
                argc = count;
            } else {
                words[0] = program;
            }
            /* разбор - та же функция, что для настоящих ключей; ошибку она печатает сама */
            if (parse_options(argc, argv, opts) == 0) {
                *keys_given = 1;
                return 0;
            }
            continue;
        }

        long long value = 0;
        if (parse_line(*line, 1, 1, MAX_THREADS, "the number of threads", &value) == 0) {
            opts->threads = (int)value;
            return 0;
        }
    }
}

/*
 * Спросить окно, K и размеры матрицы (число потоков уже спросила ask_threads_or_keys).
 * Возвращает 0 - успех, 1 - ввод закончился или не хватило памяти (сообщение уже напечатано).
 */
static int ask_settings(int console, options *opts, int *rows, int *cols, char **line, size_t *capacity) {
    long long values[2];

    /* окно: число от 1 до 99, и обязательно нечётное (иначе нет центрального элемента) */
    while (1) {
        if (ask_numbers(console, "Window size (odd number 1..99): ", 1, 1, MAX_WINDOW,
                        "the window size", values, line, capacity) != 0) {
            return 1;
        }
        if (values[0] % 2 != 0) {
            break;
        }
        fprintf(stderr, "Error: the window size must be odd, try again\n");
    }
    opts->window = (int)values[0];

    if (ask_numbers(console, "How many times to apply the filter, K (0..1000): ", 1, 0, MAX_TIMES,
                    "K", values, line, capacity) != 0) {
        return 1;
    }
    opts->times = (int)values[0];

    /* размеры - два числа в одной строке; слишком большую матрицу просим ввести заново */
    while (1) {
        if (ask_numbers(console, "Number of rows and columns (two numbers): ", 2, 1, MAX_SIDE,
                        "the size of the matrix", values, line, capacity) != 0) {
            return 1;
        }
        if (values[0] * values[1] <= MAX_CELLS) {
            break;
        }
        fprintf(stderr, "Error: the matrix is too big (more than %d elements), try again\n", MAX_CELLS);
    }
    *rows = (int)values[0];
    *cols = (int)values[1];
    return 0;
}

/*
 * Спросить матрицу: каждая строка матрицы - отдельная строка ввода из cols чисел.
 * Ошибка в строке - вводится заново только эта строка.
 * Возвращает 0 - успех, 1 - ввод закончился или не хватило памяти (сообщение уже напечатано).
 */
static int ask_matrix(int console, int *matrix, int rows, int cols, char **line, size_t *capacity) {
    long long *values = malloc((size_t)cols * sizeof(long long));
    if (values == NULL) {
        fprintf(stderr, "Error: not enough memory for the matrix\n");
        return 1;
    }
    int result = 0;
    for (int i = 0; i < rows && result == 0; i++) {
        /* вопрос с номером строки: "Row 2 of 3: " */
        char question[64];
        snprintf(question, sizeof(question), "Row %d of %d: ", i + 1, rows);
        result = ask_numbers(console, question, cols, INT_MIN, INT_MAX, "a matrix element", values,
                             line, capacity);
        for (int j = 0; j < cols && result == 0; j++) {
            matrix[(size_t)i * cols + j] = (int)values[j];
        }
    }
    free(values);
    return result;
}

/*
 * Не дать окну закрыться сразу: при запуске двойным щелчком окно закрывается вместе с программой.
 * Ждём нажатия клавиши через _getch: она читает клавишу прямо из консоли, мимо буфера stdin.
 * Через getchar нельзя: в буфере мог остаться конец строки после последнего числа,
 * getchar сразу вернул бы его, и окно закрылось бы, не показав результат.
 */
static void wait_for_key(int console) {
    if (!console) {
        return;
    }
    fprintf(stderr, "\nPress any key to exit\n");
    /* выбросить клавиши, нажатые раньше (например, лишний Enter), - ждём именно новое нажатие */
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    _getch();
}

/*
 * Запуск без ключей: программа сама спрашивает число потоков, окно, K, размеры и матрицу
 * (вопросы печатаются, только если ввод с клавиатуры), накладывает фильтр и печатает результат.
 */
static int run_dialog_mode(void) {
    int console = input_is_console();
    if (console) {
        fprintf(stderr, "Median filter (lab 2, variant 11). Type the answers and press Enter.\n");
        fprintf(stderr, "Instead of the number of threads you can type the keys: "
                        "-t THREADS -w WINDOW -k TIMES [-g ROWS COLS SEED [-b]]\n\n");
    }

    int result = 0;
    options opts;
    int keys_given = 0;
    int rows = 0;
    int cols = 0;
    int *matrix = NULL;
    int *spare = NULL;
    size_t capacity = 64;      /* начальный размер буфера строки ввода; дальше растёт вдвое */
    char *line = malloc(capacity);
    if (line == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        wait_for_key(console);
        return 1;
    }
    /* BOM бывает только у файла или канала. С клавиатуры его нет, а getchar здесь ждал бы ввода
       ещё до первого вопроса - поэтому для консоли не проверяем */
    if (!console && skip_bom() != 0) {
        free(line);
        return 1;
    }
    if (ask_threads_or_keys(console, &opts, &keys_given, &line, &capacity) != 0) {
        free(line);
        wait_for_key(console);
        return 1;
    }
    /* ввели ключи - работаем так же, как при запуске с этими ключами */
    if (keys_given) {
        free(line);
        if (opts.generate) {
            result = run_generate_mode(&opts);
        } else {
            if (console) {
                fprintf(stderr, "Enter the matrix: the number of rows and columns, then all the numbers "
                                "(in one line or in several):\n");
            }
            result = run_input_mode(&opts, console);
        }
        wait_for_key(console);
        return result;
    }

    if (ask_settings(console, &opts, &rows, &cols, &line, &capacity) != 0) {
        result = 1;            /* сообщение уже напечатано */
    } else {
        /* две матрицы: на каждом проходе одна - исходная, другая - результат */
        matrix = malloc((size_t)rows * (size_t)cols * sizeof(int));
        spare = malloc((size_t)rows * (size_t)cols * sizeof(int));
        if (matrix == NULL || spare == NULL) {
            fprintf(stderr, "Error: not enough memory for the matrix\n");
            result = 1;
        } else if (ask_matrix(console, matrix, rows, cols, &line, &capacity) != 0) {
            result = 1;
        }
    }

    if (result == 0) {
        double start = now_seconds();
        result = apply_filter(matrix, spare, rows, cols, opts.window, opts.times, opts.threads);
        double seconds = now_seconds() - start;
        if (result == 0) {
            /* время и число потоков - только человеку: в тестах время каждый раз разное */
            if (console) {
                int used = opts.threads < rows ? opts.threads : rows;
                fprintf(stderr, "\nResult (threads: %d, time: %.4f s):\n", used, seconds);
            }
            print_matrix(matrix, rows, cols);
            fflush(stdout);
        }
    }

    free(line);
    free(matrix);
    free(spare);
    wait_for_key(console);
    return result;
}

int main(int argc, char *argv[]) {
    /* без ключей (например, двойной щелчок по exe) - программа сама спрашивает всё нужное */
    if (argc == 1) {
        return run_dialog_mode();
    }
    options opts;
    if (parse_options(argc, argv, &opts) != 0) {
        return 1;
    }
    /* матрица либо случайная (-g), либо со стандартного ввода */
    if (opts.generate) {
        return run_generate_mode(&opts);
    }
    /* запуск с ключами: ввод может быть из файла, поэтому проверяем, что после матрицы ничего нет */
    return run_input_mode(&opts, 0);
}
