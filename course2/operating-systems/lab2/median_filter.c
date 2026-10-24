/*
 * median_filter.c — Лабораторная работа №2 по ОС (Вариант 11)
 *
 * Задание: Наложить K раз медианный фильтр на матрицу целых чисел.
 *          Размер окна задаётся пользователем.
 *          Максимальное количество потоков задаётся ключом запуска (-t).
 *
 * Модель параллелизма: Fork & Join (Лекция #3)
 *   - Для каждой итерации создаются num_threads потоков
 *   - Строки матрицы равномерно распределяются между потоками
 *   - После завершения всех потоков — обмен буферов и следующая итерация
 *   - Гонки данных отсутствуют: каждый поток пишет в свой диапазон строк,
 *     чтение из исходной матрицы — без модификации (read-only)
 *
 * Используемые средства Windows для работы с потоками:
 *   _beginthreadex()         — создание потока (безопасно для CRT, в отличие от CreateThread)
 *   WaitForMultipleObjects() — ожидание завершения группы потоков (Join)
 *   CloseHandle()            — закрытие дескриптора потока
 *   QueryPerformanceCounter/Frequency — высокоточный замер времени
 *
 * Компиляция:
 *   MSVC: cl /W3 /O2 /Fe:median_filter.exe median_filter.c
 *   GCC:  gcc -Wall -O2 -o median_filter.exe median_filter.c
 *
 * Запуск:
 *   median_filter.exe -r 500 -c 500 -w 3 -k 5 -t 4
 *   median_filter.exe -r 1000 -c 1000 -w 5 -k 3 -t 8 -b   (режим бенчмарка)
 */

#include <windows.h>
#include <process.h>   /* _beginthreadex */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Максимальное число потоков.
 * Ограничено MAXIMUM_WAIT_OBJECTS (64) из-за WaitForMultipleObjects().
 */
#define MAX_THREADS 64

/* ================================================================
 * Структура данных, передаваемая каждому потоку
 * ================================================================ */
typedef struct {
    const int *src;      /* Исходная матрица (только чтение)             */
    int       *dst;      /* Результирующая матрица (запись)              */
    int        rows;     /* Общее количество строк                       */
    int        cols;     /* Общее количество столбцов                    */
    int        win;      /* Размер окна фильтра (нечётное число)         */
    int        row_from; /* Первая строка для обработки (включительно)    */
    int        row_to;   /* Последняя строка (не включительно)           */
} ThreadData;

/* ================================================================
 * Вспомогательные функции
 * ================================================================ */

/* Функция сравнения для qsort (целые числа) */
static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

/* Ограничение значения в диапазон [lo, hi] (для обработки границ матрицы) */
static int clamp(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* Высокоточное текущее время в секундах */
static double get_time_sec(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
}

/* ================================================================
 * Функция потока: медианный фильтр для диапазона строк
 *
 * Для каждого пикселя (r, c) в назначенном диапазоне строк:
 *   1. Собирается окно win×win с центром в (r, c)
 *   2. Значения сортируются
 *   3. Медиана (центральный элемент) записывается в dst
 *
 * Граничные пиксели обрабатываются методом "зажатия" (clamp):
 * координаты за пределами матрицы отображаются на ближайший край.
 * ================================================================ */
static unsigned __stdcall filter_thread(void *arg) {
    ThreadData *td = (ThreadData *)arg;
    int half = td->win / 2;
    int win_area = td->win * td->win;

    /* Локальный буфер для значений окна — у каждого потока свой (нет гонки) */
    int *window = (int *)malloc((size_t)win_area * sizeof(int));
    if (!window) {
        fprintf(stderr, "[поток] Ошибка выделения памяти\n");
        return 1;
    }

    for (int r = td->row_from; r < td->row_to; r++) {
        for (int c = 0; c < td->cols; c++) {
            int cnt = 0;

            /* Сбор значений окна */
            for (int wr = -half; wr <= half; wr++) {
                for (int wc = -half; wc <= half; wc++) {
                    int nr = clamp(r + wr, 0, td->rows - 1);
                    int nc = clamp(c + wc, 0, td->cols - 1);
                    window[cnt++] = td->src[nr * td->cols + nc];
                }
            }

            /* Сортировка и выбор медианы */
            qsort(window, (size_t)cnt, sizeof(int), cmp_int);
            td->dst[r * td->cols + c] = window[cnt / 2];
        }
    }

    free(window);
    return 0;
}

/* ================================================================
 * Применение медианного фильтра K раз с num_threads потоками
 *
 * Использует модель Fork & Join:
 *   Для каждой из K итераций:
 *     Fork  — создание num_threads потоков
 *     Join  — ожидание завершения всех потоков (WaitForMultipleObjects)
 *     Swap  — обмен буферов src <-> dst
 *
 * Возвращает затраченное время в секундах.
 * ================================================================ */
static double apply_filter(int *mat, int rows, int cols,
                           int win, int K, int num_threads) {
    int size = rows * cols;
    int *buf = (int *)malloc((size_t)size * sizeof(int));
    if (!buf) {
        fprintf(stderr, "Ошибка выделения памяти для буфера\n");
        return -1.0;
    }

    int *src = mat;
    int *dst = buf;

    /* Ограничиваем число потоков количеством строк */
    if (num_threads > rows) num_threads = rows;
    if (num_threads < 1)    num_threads = 1;

    HANDLE     handles[MAX_THREADS];
    ThreadData td[MAX_THREADS];

    double t_start = get_time_sec();

    for (int iter = 0; iter < K; iter++) {
        /* --- Fork: распределение строк и создание потоков --- */
        int base_chunk = rows / num_threads;
        int remainder  = rows % num_threads;
        int current_row = 0;

        for (int t = 0; t < num_threads; t++) {
            int chunk = base_chunk + (t < remainder ? 1 : 0);

            td[t].src      = src;
            td[t].dst      = dst;
            td[t].rows     = rows;
            td[t].cols     = cols;
            td[t].win      = win;
            td[t].row_from = current_row;
            td[t].row_to   = current_row + chunk;
            current_row += chunk;

            handles[t] = (HANDLE)_beginthreadex(
                NULL,           /* атрибуты безопасности  */
                0,              /* размер стека (по умолч.) */
                filter_thread,  /* функция потока          */
                &td[t],         /* аргумент потока         */
                0,              /* запуск немедленно       */
                NULL            /* идентификатор потока    */
            );
            if (handles[t] == 0) {
                fprintf(stderr, "Ошибка создания потока %d (код: %lu)\n",
                        t, GetLastError());
                free(buf);
                return -1.0;
            }
        }

        /* --- Join: ожидание завершения всех потоков --- */
        WaitForMultipleObjects((DWORD)num_threads, handles, TRUE, INFINITE);

        /* Закрытие дескрипторов потоков */
        for (int t = 0; t < num_threads; t++) {
            CloseHandle(handles[t]);
        }

        /* --- Swap: обмен буферов для следующей итерации --- */
        {
            int *tmp = src;
            src = dst;
            dst = tmp;
        }
    }

    double t_end = get_time_sec();

    /* Если результат оказался в buf, копируем обратно в mat */
    if (src != mat) {
        memcpy(mat, src, (size_t)size * sizeof(int));
    }

    free(buf);
    return t_end - t_start;
}

/* ================================================================
 * Генерация матрицы случайными целыми числами 0..255
 * ================================================================ */
static void generate_matrix(int *mat, int rows, int cols, unsigned seed) {
    srand(seed);
    for (int i = 0; i < rows * cols; i++) {
        mat[i] = rand() % 256;
    }
}

/* ================================================================
 * Печать матрицы (для отладки на малых размерах)
 * ================================================================ */
static void print_matrix(const int *mat, int rows, int cols, const char *title) {
    printf("\n%s (%d x %d):\n", title, rows, cols);
    int print_rows = rows > 16 ? 16 : rows;
    int print_cols = cols > 16 ? 16 : cols;

    for (int r = 0; r < print_rows; r++) {
        for (int c = 0; c < print_cols; c++) {
            printf("%4d", mat[r * cols + c]);
        }
        if (cols > print_cols) printf("  ...");
        printf("\n");
    }
    if (rows > print_rows) printf("  ...\n");
}

/* ================================================================
 * Вывод справки
 * ================================================================ */
static void print_usage(const char *progname) {
    printf("Медианный фильтр — Лаб. работа №2 по ОС (Вариант 11)\n\n");
    printf("Использование: %s [параметры]\n\n", progname);
    printf("Параметры:\n");
    printf("  -r <строки>      Количество строк матрицы       (по умолч.: 500)\n");
    printf("  -c <столбцы>     Количество столбцов матрицы     (по умолч.: 500)\n");
    printf("  -w <окно>        Размер окна фильтра (нечётное)  (по умолч.: 3)\n");
    printf("  -k <итерации>    Количество проходов фильтра     (по умолч.: 3)\n");
    printf("  -t <потоки>      Макс. количество потоков (1-%d) (по умолч.: 4)\n", MAX_THREADS);
    printf("  -s <seed>        Зерно генератора случ. чисел    (по умолч.: time)\n");
    printf("  -b               Режим бенчмарка (замер 1..t потоков)\n");
    printf("  -p               Печатать матрицу (до/после)\n");
    printf("  -h               Показать эту справку\n");
    printf("\nПримеры:\n");
    printf("  %s -r 1000 -c 1000 -w 3 -k 5 -t 8\n", progname);
    printf("  %s -r 800 -c 800 -w 5 -k 3 -t 8 -b\n", progname);
}

/* ================================================================
 * main
 * ================================================================ */
int main(int argc, char *argv[]) {
    /* --- Параметры по умолчанию --- */
    int rows        = 500;
    int cols        = 500;
    int win         = 3;
    int K           = 3;
    int max_threads = 4;
    unsigned seed   = (unsigned)time(NULL);
    int benchmark   = 0;
    int do_print    = 0;

    /* --- Разбор аргументов командной строки --- */
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "-r") == 0 && i + 1 < argc) rows        = atoi(argv[++i]);
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) cols        = atoi(argv[++i]);
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) win         = atoi(argv[++i]);
        else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) K           = atoi(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) max_threads = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) seed        = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "-b") == 0) benchmark = 1;
        else if (strcmp(argv[i], "-p") == 0) do_print  = 1;
        else if (strcmp(argv[i], "-h") == 0) { print_usage(argv[0]); return 0; }
        else {
            fprintf(stderr, "Неизвестный параметр: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* --- Валидация параметров --- */
    if (win < 1 || win % 2 == 0) {
        fprintf(stderr, "Ошибка: размер окна должен быть нечётным положительным числом (получено: %d)\n", win);
        return 1;
    }
    if (rows < 1 || cols < 1) {
        fprintf(stderr, "Ошибка: размеры матрицы должны быть > 0\n");
        return 1;
    }
    if (max_threads < 1 || max_threads > MAX_THREADS) {
        fprintf(stderr, "Ошибка: количество потоков от 1 до %d\n", MAX_THREADS);
        return 1;
    }
    if (K < 1) {
        fprintf(stderr, "Ошибка: количество итераций должно быть >= 1\n");
        return 1;
    }

    /* --- Заголовок --- */
    printf("============================================================\n");
    printf("  Лабораторная работа №2 по ОС — Вариант 11\n");
    printf("  Медианный фильтр (многопоточная реализация)\n");
    printf("============================================================\n\n");
    printf("Параметры:\n");
    printf("  Матрица:    %d x %d\n", rows, cols);
    printf("  Окно:       %d x %d\n", win, win);
    printf("  Итерации:   %d\n", K);
    printf("  Потоки:     %d (макс.)\n", max_threads);
    printf("  Seed:       %u\n", seed);
    printf("  PID:        %lu\n\n", GetCurrentProcessId());

    printf("[подсказка] Откройте Диспетчер задач -> Подробности -> median_filter.exe\n");
    printf("            столбец \"Потоки\" покажет количество потоков процесса.\n\n");

    int size = rows * cols;

    if (!benchmark) {
        /* ============================================================
         * Обычный режим: один запуск с max_threads потоками
         * ============================================================ */
        int *mat = (int *)malloc((size_t)size * sizeof(int));
        if (!mat) {
            fprintf(stderr, "Ошибка выделения памяти (%d элементов)\n", size);
            return 1;
        }

        generate_matrix(mat, rows, cols, seed);
        if (do_print) print_matrix(mat, rows, cols, "Исходная матрица");

        printf("[main] Запуск фильтра с %d потоками...\n", max_threads);

        double elapsed = apply_filter(mat, rows, cols, win, K, max_threads);

        if (elapsed < 0) {
            free(mat);
            return 1;
        }

        printf("[main] Завершено за %.4f сек.\n", elapsed);
        if (do_print) print_matrix(mat, rows, cols, "Результат");

        free(mat);

    } else {
        /* ============================================================
         * Режим бенчмарка: замер от 1 до max_threads потоков
         *
         * Для каждого числа потоков p вычисляются:
         *   T(p)  — время выполнения
         *   S(p)  = T(1) / T(p)           — ускорение
         *   E(p)  = S(p) / p              — эффективность
         *
         * (см. Лекция #3 — закон Амдала, метрики параллелизма)
         * ============================================================ */
        printf("--- Бенчмарк ---\n\n");
        printf("%-10s %-14s %-14s %-14s\n",
               "Потоки", "Время (сек)", "Ускорение S", "Эффективн. E");
        printf("---------- -------------- -------------- --------------\n");

        double t1 = 0.0;

        for (int t = 1; t <= max_threads; t++) {
            int *mat = (int *)malloc((size_t)size * sizeof(int));
            if (!mat) {
                fprintf(stderr, "Ошибка выделения памяти\n");
                return 1;
            }

            /* Генерируем одинаковую матрицу для честного сравнения */
            generate_matrix(mat, rows, cols, seed);

            double elapsed = apply_filter(mat, rows, cols, win, K, t);
            if (elapsed < 0) {
                free(mat);
                return 1;
            }

            if (t == 1) t1 = elapsed;

            double speedup    = (elapsed > 0.0) ? t1 / elapsed : 0.0;
            double efficiency = (t > 0)         ? speedup / t  : 0.0;

            printf("%-10d %-14.4f %-14.4f %-14.4f\n",
                   t, elapsed, speedup, efficiency);

            free(mat);
        }

        printf("\nT(1) = %.4f сек\n", t1);
        printf("\nПояснение (Лекция #3, закон Амдала):\n");
        printf("  S = T(1)/T(p) — ускорение, идеальное S = p\n");
        printf("  E = S/p       — эффективность, идеальная E = 1.0\n");
        printf("  При увеличении числа потоков S растёт, но ограничен\n");
        printf("  долей последовательного кода (закон Амдала: S <= 1/a).\n");
        printf("  При p > числа ядер CPU наблюдается псевдопараллелизм\n");
        printf("  и дополнительные накладные расходы на переключение контекста.\n");
    }

    printf("\n[main] Программа завершена.\n");
    return 0;
}
