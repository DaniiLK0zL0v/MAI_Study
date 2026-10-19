/*
 * Лабораторная 2, задание 4. Выбор файлов для резервного копирования (задача о рюкзаке).
 *
 * Есть файлы: у каждого имя, размер и важность. Есть накопитель вместимостью capacity.
 * Нужно выбрать файлы так, чтобы их суммарный размер был не больше capacity,
 * а суммарная важность - самая большая. Каждый файл берется целиком и не больше одного раза.
 *
 * Запуск:  lab2_task4 < входной_файл      (или ввод с клавиатуры, конец ввода - Ctrl+D / Ctrl+Z)
 * Ввод (стандартный поток ввода):
 *     n capacity                 - количество файлов (>= 1) и вместимость (>= 0)
 *     имя размер важность        - n таких строк; размер >= 1, важность >= 0
 * Вывод:
 *     TOTAL_SIZE S
 *     TOTAL_IMPORTANCE I
 *     FILES k
 *     имена выбранных файлов, по одному в строке, в порядке ввода
 * Пример:  3 10 / a 5 10 / b 4 40 / c 6 30   ->   TOTAL_SIZE 10, TOTAL_IMPORTANCE 70, FILES 2: b, c
 *
 * Главная функция задания - choose_backup(файлы, количество, вместимость, &результат).
 * Задача о рюкзаке "трудная": быстрого способа для любых чисел не известно.
 * Поэтому функция выбирает из трех точных способов тот, которому нужно меньше всего работы:
 *   1) таблица по размеру:  для каждой вместимости 0..capacity - лучшая важность;
 *   2) таблица по важности: для каждой важности 0..сумма - самый маленький размер;
 *   3) полный перебор всех наборов - когда файлов мало.
 * Если все три способа требуют слишком много памяти или времени (огромные числа
 * и много файлов) - честная ошибка TOO_BIG, а не попытка выделить гигабайты.
 */
#include <stdio.h>
#include <stdlib.h>   /* malloc, realloc, free, qsort */
#include <string.h>   /* strlen, strcmp, memcpy */
#include <limits.h>   /* LLONG_MAX - самое большое long long */
#include <stdint.h>   /* SIZE_MAX - самое большое size_t */
#include <ctype.h>    /* isspace */

/* Ограничение размера таблиц (способы 1 и 2): не больше стольких клеток
   "файл x значение". Одна клетка таблицы выбора - 1 байт, значит таблица
   не больше 10 МБ; строка лучших значений - до 8 байт на значение */
#define TABLE_LIMIT 10000000LL
/* Полный перебор (способ 3) - только если файлов не больше стольких:
   2^22 = 4194304 наборов - это меньше секунды */
#define BRUTE_FORCE_MAX_FILES 22

/* Статус-коды: что вернула функция */
typedef enum {
    OK,
    INVALID_ARGS,      /* в функцию передали NULL или неправильное количество */
    INVALID_DATA,      /* у файла размер <= 0 или важность < 0, вместимость < 0 */
    INVALID_NUMBER,    /* строка - не целое неотрицательное число */
    OVERFLOW_ERROR,    /* число больше LLONG_MAX / сумма важностей не помещается в long long */
    END_OF_INPUT,      /* строки кончились (это не ошибка, а сигнал для цикла чтения) */
    NO_DATA,           /* ввод пустой - нет даже первой строки */
    BAD_FIRST_LINE,    /* в первой строке не два слова */
    BAD_N,             /* n - не целое число от 1 до LLONG_MAX */
    BAD_CAPACITY,      /* capacity - не целое число от 0 до LLONG_MAX */
    BAD_FILE_LINE,     /* в строке файла не три слова */
    BAD_SIZE,          /* размер - не целое число от 1 до LLONG_MAX */
    BAD_IMPORTANCE,    /* важность - не целое число от 0 до LLONG_MAX */
    TOO_FEW_FILES,     /* строк с файлами меньше n */
    TOO_MANY_FILES,    /* после n файлов есть еще непустые строки */
    TOO_BIG,           /* ни один способ решения не помещается в ограничения */
    ALLOC_ERROR        /* malloc / realloc не смогли выделить память */
} status_code;

/* Описание одного файла */
typedef struct {
    char *name;            /* имя - строка в динамической памяти */
    long long size;        /* размер, >= 1 */
    long long importance;  /* важность, >= 0 */
} file_info;

/* Результат выбора: массив выбранных файлов (копии описаний, у каждого
   своя копия имени), их количество, суммарный размер и суммарная важность */
typedef struct {
    file_info *files;
    size_t count;
    long long total_size;
    long long total_importance;
} backup_result;

/* Все, что прочитали из ввода. n и item_count нужны и при ошибке - для сообщения
   ("ждали 5 файлов, нашли 3") */
typedef struct {
    file_info *files;      /* массив прочитанных файлов */
    size_t item_count;          /* сколько файлов прочитано */
    long long n;           /* сколько файлов обещано в первой строке */
    long long capacity;    /* вместимость накопителя */
} input_data;

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

/* Освободить массив файлов: сначала имя каждого файла, потом сам массив.
   free(NULL) ничего не делает, поэтому пустой массив освобождать тоже можно */
status_code free_files(file_info *files, const size_t item_count) {
    if (files == NULL) {
        return OK;
    }
    for (size_t i = 0; i < item_count; i++) {
        free(files[i].name);
    }
    free(files);
    return OK;
}

/* Освободить результат (у выбранных файлов свои копии имен) и обнулить его */
status_code free_result(backup_result *result) {
    if (result == NULL) {
        return INVALID_ARGS;
    }
    free_files(result->files, result->count);
    result->files = NULL;
    result->count = 0;
    result->total_size = 0;
    result->total_importance = 0;
    return OK;
}

/* ===================== выбор файлов (главная функция и способы) ===================== */

/* Способ 1 - таблица по размеру ("классический" рюкзак).
   candidates - номера файлов, которые участвуют (их m штук), W - вместимость.
   best[w] - самая большая важность, которую можно набрать с суммарным
   размером не больше w из уже рассмотренных файлов.
   Добавляем файлы по одному. Для файла (размер s, важность v):
       взять его в рюкзак вместимости w выгодно, если best[w - s] + v > best[w]
   (место s отдаем под этот файл, остаток w - s заполняем как лучше было раньше).
   w перебираем от большего к меньшему: так best[w - s] еще "старая", без этого
   файла, и файл не возьмется дважды.
   take[i][w] = 1 - "в лучшем наборе для вместимости w файл i взят"; по этой таблице
   в конце восстанавливаем сам набор: идем по файлам с последнего, и если файл
   взят - отнимаем его размер от w.
   chosen[i] = 1 - кандидат i попал в ответ */
status_code knapsack_by_size(const file_info *files, const size_t *candidates, const size_t m,
                             const long long W, unsigned char *chosen) {
    if (files == NULL || candidates == NULL || chosen == NULL || W < 0) {
        return INVALID_ARGS;
    }
    /* Размеры проверил вызывающий (m * (W + 1) <= TABLE_LIMIT), так что все
       помещается в size_t; ширина строки таблицы - W + 1 (вместимости 0..W) */
    size_t width = (size_t)W + 1;
    long long *best = (long long *)malloc(width * sizeof(long long));
    if (best == NULL) {
        return ALLOC_ERROR;
    }
    /* Таблица take - m строк по width байт, хранится одним куском:
       клетка [i][w] лежит на месте i * width + w */
    unsigned char *take = (unsigned char *)malloc(m * width);
    if (take == NULL) {
        free(best);
        return ALLOC_ERROR;
    }
    /* пока не взяли ни одного файла, важность 0 при любой вместимости */
    for (size_t w = 0; w < width; w++) {
        best[w] = 0;
    }

    for (size_t i = 0; i < m; i++) {
        long long s = files[candidates[i]].size;
        long long v = files[candidates[i]].importance;
        unsigned char *row = take + i * width;   /* строка таблицы для файла i */
        /* при вместимости меньше s файл не помещается - "не взят" */
        for (size_t w = 0; w < width; w++) {
            row[w] = 0;
        }
        /* w от W вниз до s (кандидаты отобраны так, что s <= W).
           Цикл с long long: при w = s - 1 он должен закончиться, а size_t
           при "w >= s" и s = 0 никогда бы не закончился (но s >= 1, это для надежности) */
        for (long long w = W; w >= s; w--) {
            /* best[w - s] + v > LLONG_MAX? Проверка через вычитание.
               Если да - есть набор, который помещается (размер <= w), а его важность
               не помещается в long long. Значит, и ответ (он не меньше) не напечатать */
            if (v > LLONG_MAX - best[w - s]) {
                free(take);
                free(best);
                return OVERFLOW_ERROR;
            }
            long long with_it = best[w - s] + v;
            if (with_it > best[w]) {
                best[w] = with_it;
                row[w] = 1;
            }
        }
    }

    /* Восстановление набора: с последнего файла к первому */
    long long w = W;
    for (size_t k = m; k > 0; k--) {
        size_t i = k - 1;   /* цикл по k, потому что size_t i >= 0 всегда истинно */
        if (take[i * width + (size_t)w]) {
            chosen[i] = 1;
            w -= files[candidates[i]].size;
        } else {
            chosen[i] = 0;
        }
    }

    free(take);
    free(best);
    return OK;
}

/* Способ 2 - таблица по важности. Хорош, когда размеры огромные (например,
   в байтах), а важности маленькие.
   S - сумма важностей всех кандидатов.
   min_size[v] - самый маленький суммарный размер, с которым можно набрать
   важность ровно v; UNREACHABLE - такой важности набрать нельзя
   (или для этого нужно больше места, чем capacity - нам такое тоже не подходит).
   Для файла (размер s, важность v): min_size[x] можно улучшить до
   min_size[x - v] + s. Ответ - самая большая x, у которой min_size[x] <= capacity.
   x перебираем сверху вниз - по той же причине, что и в способе 1 */
status_code knapsack_by_importance(const file_info *files, const size_t *candidates, const size_t m,
                                   const long long S, const long long capacity,
                                   unsigned char *chosen) {
    if (files == NULL || candidates == NULL || chosen == NULL || S < 0 || capacity < 0) {
        return INVALID_ARGS;
    }
    /* UNREACHABLE = -1: настоящий размер набора не бывает отрицательным */
    const long long UNREACHABLE = -1;
    size_t width = (size_t)S + 1;   /* важности 0..S */
    long long *min_size = (long long *)malloc(width * sizeof(long long));
    if (min_size == NULL) {
        return ALLOC_ERROR;
    }
    unsigned char *take = (unsigned char *)malloc(m * width);
    if (take == NULL) {
        free(min_size);
        return ALLOC_ERROR;
    }
    /* важность 0 набирается пустым набором (размер 0), остальные пока нельзя */
    min_size[0] = 0;
    for (size_t x = 1; x < width; x++) {
        min_size[x] = UNREACHABLE;
    }

    for (size_t i = 0; i < m; i++) {
        long long s = files[candidates[i]].size;
        long long v = files[candidates[i]].importance;   /* у кандидатов v >= 1 */
        unsigned char *row = take + i * width;
        for (size_t x = 0; x < width; x++) {
            row[x] = 0;
        }
        for (long long x = S; x >= v; x--) {
            long long without = min_size[x - v];   /* размер набора без этого файла */
            if (without == UNREACHABLE) {
                continue;
            }
            /* without + s <= capacity? Пишем через вычитание: without + s могло бы
               переполнить long long, а capacity - without >= 0 всегда (without <= capacity) */
            if (s > capacity - without) {
                continue;
            }
            long long with_it = without + s;
            if (min_size[x] == UNREACHABLE || with_it < min_size[x]) {
                min_size[x] = with_it;
                row[x] = 1;
            }
        }
    }

    /* лучшая важность - самая большая достижимая (важность 0 достижима всегда) */
    long long x = S;
    while (min_size[x] == UNREACHABLE) {
        x--;
    }

    /* восстановление: как в способе 1, только отнимаем важность */
    for (size_t k = m; k > 0; k--) {
        size_t i = k - 1;
        if (take[i * width + (size_t)x]) {
            chosen[i] = 1;
            x -= files[candidates[i]].importance;
        } else {
            chosen[i] = 0;
        }
    }

    free(take);
    free(min_size);
    return OK;
}

/* Состояние полного перебора (способ 3): чтобы не передавать в рекурсию
   десять параметров, все общее лежит в одной структуре */
typedef struct {
    const file_info *files;
    const size_t *candidates;
    size_t m;
    unsigned char *current;     /* набор, который строим сейчас: 1 - файл взят */
    unsigned char *best_set;     /* лучший найденный набор */
    long long best_importance;
} brute_force_state;

/* Перебор всех наборов рекурсией: для файла i пробуем два варианта -
   "взять" (если помещается в remaining) и "не брать", и идем к файлу i + 1.
   Когда файлы кончились (i == m) - набор готов, сравниваем с лучшим.
   remaining - сколько места еще свободно, taken_importance - важность уже взятых файлов.
   Глубина рекурсии - m <= BRUTE_FORCE_MAX_FILES, это немного */
status_code brute_force(brute_force_state *st, const size_t i, const long long remaining,
                    const long long taken_importance) {
    if (st == NULL) {
        return INVALID_ARGS;
    }
    if (i == st->m) {
        /* набор готов: если он лучше - запоминаем его копию */
        if (taken_importance > st->best_importance) {
            st->best_importance = taken_importance;
            memcpy(st->best_set, st->current, st->m);
        }
        return OK;
    }
    const file_info *f = &st->files[st->candidates[i]];
    status_code sc = OK;
    /* вариант "взять": только если файл помещается в оставшееся место */
    if (f->size <= remaining) {
        /* важность набора, который помещается, больше LLONG_MAX - ответ не напечатать */
        if (f->importance > LLONG_MAX - taken_importance) {
            return OVERFLOW_ERROR;
        }
        st->current[i] = 1;
        sc = brute_force(st, i + 1, remaining - f->size, taken_importance + f->importance);
        if (sc != OK) {
            return sc;
        }
    }
    /* вариант "не брать" */
    st->current[i] = 0;
    return brute_force(st, i + 1, remaining, taken_importance);
}

/* Способ 3 - полный перебор всех 2^m наборов */
status_code knapsack_brute_force(const file_info *files, const size_t *candidates, const size_t m,
                             const long long capacity, unsigned char *chosen) {
    if (files == NULL || candidates == NULL || chosen == NULL || capacity < 0) {
        return INVALID_ARGS;
    }
    unsigned char *current = (unsigned char *)malloc(m);
    if (current == NULL) {
        return ALLOC_ERROR;
    }
    /* лучший набор вначале - пустой (важность 0) */
    for (size_t i = 0; i < m; i++) {
        current[i] = 0;
        chosen[i] = 0;
    }
    brute_force_state st;
    st.files = files;
    st.candidates = candidates;
    st.m = m;
    st.current = current;
    st.best_set = chosen;          /* лучший набор пишем сразу в ответ */
    st.best_importance = 0;
    status_code sc = brute_force(&st, 0, capacity, 0);
    free(current);
    return sc;
}

/* Главная функция задания: выбрать файлы для накопителя вместимостью capacity.
   files, n - массив описаний и его длина; result - сюда кладем ответ (его массив
   и имена в динамической памяти, освобождать - free_result).
   Шаги:
     1) отбрасываем файлы, которые точно не нужны: больше capacity (не помещаются)
        и с важностью 0 (место занимают, важности не добавляют). Остальные - кандидаты;
     2) если все кандидаты помещаются вместе - берем всех, таблицы не нужны
        (это и спасает при огромной capacity: 10^18 и маленькие файлы);
     3) иначе считаем, сколько работы нужно каждому способу, и берем самый дешевый:
          таблица по размеру   - m * (capacity + 1) клеток,
          таблица по важности  - m * (S + 1) клеток (S - сумма важностей),
          перебор              - 2^m наборов (только при m <= BRUTE_FORCE_MAX_FILES);
        если ни один не проходит по ограничениям - TOO_BIG;
     4) собираем ответ в порядке ввода. */
status_code choose_backup(const file_info *files, const size_t n, const long long capacity,
                          backup_result *result) {
    if (result == NULL || (files == NULL && n > 0)) {
        return INVALID_ARGS;
    }
    if (capacity < 0) {
        return INVALID_DATA;
    }
    for (size_t i = 0; i < n; i++) {
        if (files[i].name == NULL || files[i].size <= 0 || files[i].importance < 0) {
            return INVALID_DATA;
        }
    }

    /* ----- шаг 1: кандидаты ----- */
    /* n может быть 0 - тогда malloc(0); чтобы не зависеть от того, что malloc(0)
       вернет (NULL или не NULL), просим хотя бы 1 элемент */
    size_t *candidates = (size_t *)malloc((n > 0 ? n : 1) * sizeof(size_t));
    if (candidates == NULL) {
        return ALLOC_ERROR;
    }
    size_t m = 0;
    long long S = 0;              /* сумма важностей кандидатов */
    int S_huge = 0;          /* 1 - сумма важностей не помещается в long long */
    long long size_sum = 0; /* сумма размеров кандидатов (пока не больше capacity) */
    int all_fit = 1;    /* 1 - все кандидаты вместе помещаются в capacity */
    for (size_t i = 0; i < n; i++) {
        if (files[i].size > capacity || files[i].importance == 0) {
            continue;
        }
        candidates[m] = i;
        m++;
        /* S + importance > LLONG_MAX? Проверка через вычитание, без переполнения.
           Огромная S - еще не ошибка: все файлы вместе могут не поместиться,
           и ответ окажется меньше. Просто способ 2 (таблица по важности) тогда
           не подходит, а способы 1 и 3 сами следят за переполнением */
        if (!S_huge) {
            if (files[i].importance > LLONG_MAX - S) {
                S_huge = 1;
            } else {
                S += files[i].importance;
            }
        }
        /* сумму размеров считаем, только пока она не больше capacity:
           как только стало больше - ясно, что все не поместятся, дальше не нужно
           (и переполнения так не бывает: size_sum <= capacity <= LLONG_MAX) */
        if (all_fit) {
            if (files[i].size > capacity - size_sum) {
                all_fit = 0;
            } else {
                size_sum += files[i].size;
            }
        }
    }

    /* chosen[i] = 1 - кандидат номер i попал в ответ */
    unsigned char *chosen = (unsigned char *)malloc(m > 0 ? m : 1);
    if (chosen == NULL) {
        free(candidates);
        return ALLOC_ERROR;
    }

    status_code sc = OK;
    if (all_fit) {
        /* ----- шаг 2: помещаются все - берем всех ----- */
        /* ответ - все кандидаты, его важность = S; если S не помещается в long long,
           ответ напечатать нельзя */
        if (S_huge) {
            free(chosen);
            free(candidates);
            return OVERFLOW_ERROR;
        }
        for (size_t i = 0; i < m; i++) {
            chosen[i] = 1;
        }
    } else {
        /* ----- шаг 3: выбор способа ----- */
        /* Здесь m >= 2: один кандидат всегда помещается (его размер <= capacity).
           "Работа" способа - сколько клеток или наборов он переберет.
           -1 - способ не подходит (слишком много).
           Проверки "(W + 1) <= TABLE_LIMIT / m" записаны через деление, чтобы
           само умножение m * (W + 1) не переполнилось при огромной capacity */
        long long work_size = -1;
        long long work_importance = -1;
        long long work_brute_force = -1;
        long long mm = (long long)m;
        if (capacity < TABLE_LIMIT / mm) {
            work_size = mm * (capacity + 1);
        }
        if (!S_huge && S < TABLE_LIMIT / mm) {
            work_importance = mm * (S + 1);
        }
        if (m <= BRUTE_FORCE_MAX_FILES) {
            work_brute_force = 1LL << m;   /* 2^m: сдвиг единицы на m разрядов */
        }

        /* самый дешевый из подходящих: method 1, 2 или 3; 0 - ни один не подходит */
        int method = 0;
        long long best_work = -1;
        if (work_size >= 0) {
            method = 1;
            best_work = work_size;
        }
        if (work_importance >= 0 && (method == 0 || work_importance < best_work)) {
            method = 2;
            best_work = work_importance;
        }
        if (work_brute_force >= 0 && (method == 0 || work_brute_force < best_work)) {
            method = 3;
            best_work = work_brute_force;
        }

        if (method == 1) {
            sc = knapsack_by_size(files, candidates, m, capacity, chosen);
        } else if (method == 2) {
            sc = knapsack_by_importance(files, candidates, m, S, capacity, chosen);
        } else if (method == 3) {
            sc = knapsack_brute_force(files, candidates, m, capacity, chosen);
        } else {
            sc = TOO_BIG;
        }
    }
    if (sc != OK) {
        free(chosen);
        free(candidates);
        return sc;
    }

    /* ----- шаг 4: ответ ----- */
    /* сколько выбрано - столько и элементов в массиве ответа */
    size_t count = 0;
    for (size_t i = 0; i < m; i++) {
        count += chosen[i];
    }
    file_info *chosen_files = (file_info *)malloc((count > 0 ? count : 1) * sizeof(file_info));
    if (chosen_files == NULL) {
        free(chosen);
        free(candidates);
        return ALLOC_ERROR;
    }
    /* кандидаты идут по возрастанию номеров, значит и ответ - в порядке ввода.
       Имя копируем: ответ не должен зависеть от того, когда освободят входной массив */
    size_t k = 0;
    long long total_size = 0;
    long long total_importance = 0;
    for (size_t i = 0; i < m; i++) {
        if (!chosen[i]) {
            continue;
        }
        const file_info *f = &files[candidates[i]];
        sc = copy_string(f->name, &chosen_files[k].name);
        if (sc != OK) {
            free_files(chosen_files, k);   /* освобождаем уже скопированные имена */
            free(chosen);
            free(candidates);
            return sc;
        }
        chosen_files[k].size = f->size;
        chosen_files[k].importance = f->importance;
        /* переполнения нет: размер набора <= capacity, важность <= S */
        total_size += f->size;
        total_importance += f->importance;
        k++;
    }

    free(chosen);
    free(candidates);
    result->files = chosen_files;
    result->count = count;
    result->total_size = total_size;
    result->total_importance = total_importance;
    return OK;
}

/* ===================== ввод (только для main) ===================== */

/* Прочитать одну строку любой длины (без '\n') в динамическую память.
   Буфер начинается с 16 байт и удваивается через realloc, когда кончается место.
   END_OF_INPUT - строк больше нет (файл кончился, а символов не было) */
status_code read_line(FILE *in, char **line) {
    if (in == NULL || line == NULL) {
        return INVALID_ARGS;
    }
    size_t buf_size = 16;   /* сколько байт выделено */
    size_t length = 0;     /* сколько символов уже прочитано */
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
        /* нужен байт под этот символ и еще один под '\0' в конце */
        if (length + 1 >= buf_size) {
            if (buf_size > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            /* realloc - во временную переменную: если он вернет NULL,
               старый блок останется, и мы сможем его освободить */
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

/* Разбить строку на слова (слова разделены пробелами, табуляциями, '\r').
   Строка меняется: после каждого слова пишется '\0', а в words[] кладутся
   адреса начал слов. Больше max_words слов не записываем, но считаем всех:
   *item_count - сколько слов в строке на самом деле (чтобы заметить лишние) */
status_code split_words(char *line, char **words, const size_t max_words, size_t *item_count) {
    if (line == NULL || words == NULL || item_count == NULL) {
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
    *item_count = k;
    return OK;
}

/* Строка -> целое неотрицательное число long long. Только цифры: "-1", "+5",
   "1.5", "12a", "" - ошибка (INVALID_NUMBER); больше LLONG_MAX - OVERFLOW_ERROR.
   scanf("%lld") тут не годится: на переполнении у него неопределенное поведение,
   а "%u" вообще принимает "-1" как огромное число */
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
        /* value * 10 + digit > LLONG_MAX  <=>  value > (LLONG_MAX - digit) / 10
           (так проверяем до умножения - само умножение могло бы переполниться) */
        if (value > (LLONG_MAX - digit) / 10) {
            return OVERFLOW_ERROR;
        }
        value = value * 10 + digit;
    }
    *number = value;
    return OK;
}

/* Строка пустая или из одних пробелов? (такие строки при чтении пропускаем) */
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

/* Прочитать следующую непустую строку. *line_number - номер строки во вводе
   (считаем все строки, и пустые тоже - так номер совпадет с номером в файле) */
status_code read_nonblank_line(FILE *in, char **line, size_t *line_number) {
    if (in == NULL || line == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    while (1) {
        char *str = NULL;
        status_code sc = read_line(in, &str);
        if (sc != OK) {
            return sc;   /* END_OF_INPUT или ALLOC_ERROR */
        }
        (*line_number)++;
        int empty = 0;
        is_blank(str, &empty);
        if (!empty) {
            *line = str;
            return OK;
        }
        free(str);   /* пустая строка не нужна - читаем следующую */
    }
}

/* Добавить файл в конец массива, который растет удвоением (как буфер строки).
   buf_size - сколько элементов выделено, data->item_count - сколько занято */
status_code add_file(input_data *data, size_t *buf_size, char *name, const long long size,
                     const long long importance) {
    if (data == NULL || buf_size == NULL || name == NULL) {
        return INVALID_ARGS;
    }
    if (data->item_count == *buf_size) {
        /* новый размер: 8 для начала, потом вдвое больше; проверяем, что
           (новый размер * размер элемента) помещается в size_t */
        size_t new_size = (*buf_size == 0) ? 8 : *buf_size * 2;
        if (*buf_size > SIZE_MAX / 2 || new_size > SIZE_MAX / sizeof(file_info)) {
            return ALLOC_ERROR;
        }
        file_info *new_ptr = (file_info *)realloc(data->files, new_size * sizeof(file_info));
        if (new_ptr == NULL) {
            return ALLOC_ERROR;
        }
        data->files = new_ptr;
        *buf_size = new_size;
    }
    data->files[data->item_count].name = name;
    data->files[data->item_count].size = size;
    data->files[data->item_count].importance = importance;
    data->item_count++;
    return OK;
}

/* Чтение всего ввода: первая строка "n capacity", потом n строк "имя размер важность".
   Пустые строки пропускаем. После n файлов непустых строк быть не должно.
   При ошибке все прочитанное освобождается (data->files = NULL), но data->n,
   data->item_count и *line_number остаются - main напечатает, где ошибка.
   Массив файлов растет по мере чтения, а не выделяется сразу на n элементов:
   n может быть огромным (10^18), а строк при этом - три */
status_code read_input(FILE *in, input_data *data, size_t *line_number) {
    if (in == NULL || data == NULL || line_number == NULL) {
        return INVALID_ARGS;
    }
    data->files = NULL;
    data->item_count = 0;
    data->n = 0;
    data->capacity = 0;
    *line_number = 0;

    /* ----- первая строка: n capacity ----- */
    char *line = NULL;
    status_code sc = read_nonblank_line(in, &line, line_number);
    if (sc == END_OF_INPUT) {
        return NO_DATA;
    }
    if (sc != OK) {
        return sc;
    }
    char *words[3];       /* больше трех слов в строке нам не нужно */
    size_t word_count = 0;
    split_words(line, words, 3, &word_count);
    if (word_count != 2) {
        free(line);
        return BAD_FIRST_LINE;
    }
    /* n: целое от 1; capacity: целое от 0. Ошибку числа (не число / слишком
       большое) сообщаем одним текстом "должно быть целым от .. до .." */
    if (parse_number(words[0], &data->n) != OK || data->n < 1) {
        free(line);
        return BAD_N;
    }
    if (parse_number(words[1], &data->capacity) != OK) {
        free(line);
        return BAD_CAPACITY;
    }
    free(line);

    /* ----- строки файлов ----- */
    size_t buf_size = 0;   /* сколько элементов выделено в data->files */
    while ((long long)data->item_count < data->n) {
        sc = read_nonblank_line(in, &line, line_number);
        if (sc == END_OF_INPUT) {
            /* файлов меньше, чем обещано: номер строки тут - последняя строка ввода */
            free_files(data->files, data->item_count);
            data->files = NULL;
            return TOO_FEW_FILES;
        }
        if (sc != OK) {
            free_files(data->files, data->item_count);
            data->files = NULL;
            return sc;
        }
        split_words(line, words, 3, &word_count);
        long long size = 0;
        long long importance = 0;
        sc = OK;
        if (word_count != 3) {
            sc = BAD_FILE_LINE;
        } else if (parse_number(words[1], &size) != OK || size < 1) {
            sc = BAD_SIZE;
        } else if (parse_number(words[2], &importance) != OK) {
            sc = BAD_IMPORTANCE;
        }
        /* имя - копия первого слова (строку line сейчас освободим) */
        char *name = NULL;
        if (sc == OK) {
            sc = copy_string(words[0], &name);
        }
        free(line);
        if (sc == OK) {
            sc = add_file(data, &buf_size, name, size, importance);
            if (sc != OK) {
                free(name);   /* имя не попало в массив - освобождаем отдельно */
            }
        }
        if (sc != OK) {
            free_files(data->files, data->item_count);
            data->files = NULL;
            return sc;
        }
    }

    /* ----- после n файлов: только пустые строки ----- */
    sc = read_nonblank_line(in, &line, line_number);
    if (sc == OK) {
        free(line);
        free_files(data->files, data->item_count);
        data->files = NULL;
        return TOO_MANY_FILES;
    }
    if (sc != END_OF_INPUT) {
        free_files(data->files, data->item_count);
        data->files = NULL;
        return sc;
    }
    return OK;
}

/* Компаратор для qsort: сравнивает две строки (элементы массива - const char *).
   Единственная функция без status_code: ее прототип задан функцией qsort.
   qsort передает адреса элементов, то есть адреса указателей на строки */
int compare_names(const void *a, const void *b) {
    const char *const *pa = (const char *const *)a;
    const char *const *pb = (const char *const *)b;
    return strcmp(*pa, *pb);
}

/* Проверка, что имена всех файлов разные. Простой способ - сравнить каждое
   с каждым - это n*n/2 сравнений. Быстрее: отсортировать список имен (qsort,
   около n*log(n)), тогда одинаковые имена окажутся рядом - сравниваем только соседей.
   Сортируем не сами файлы (их порядок нужен для ответа), а отдельный массив
   указателей на имена. *has_duplicate = 1 и *duplicate = повторившееся имя, если нашли */
status_code check_unique(const file_info *files, const size_t n, int *has_duplicate,
                         const char **duplicate) {
    if (has_duplicate == NULL || duplicate == NULL || (files == NULL && n > 0)) {
        return INVALID_ARGS;
    }
    *has_duplicate = 0;
    if (n < 2) {
        return OK;
    }
    const char **names = (const char **)malloc(n * sizeof(const char *));
    if (names == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < n; i++) {
        names[i] = files[i].name;
    }
    qsort(names, n, sizeof(const char *), compare_names);
    for (size_t i = 0; i + 1 < n; i++) {
        if (strcmp(names[i], names[i + 1]) == 0) {
            *has_duplicate = 1;
            *duplicate = names[i];   /* указывает на имя в files - оно живет дольше */
            break;
        }
    }
    free(names);
    return OK;
}

/* ===================== main ===================== */

int main(void) {
    /* 1) Чтение ввода. Каждая ошибка - свое сообщение с номером строки */
    input_data data;
    size_t line_number = 0;
    status_code sc = read_input(stdin, &data, &line_number);
    if (sc != OK) {
        /* read_input при ошибке уже все освободил */
        switch (sc) {
            case NO_DATA:
                /* ввод пустой (или одни пустые строки) */
                fprintf(stderr, "Error: no input, the first line must be: n capacity\n");
                break;
            case BAD_FIRST_LINE:
                /* в первой строке не два слова: "5", "5 10 7" */
                fprintf(stderr, "Error: line %zu: the first line must be: n capacity\n",
                        line_number);
                break;
            case BAD_N:
                /* n = 0, -3, "abc", больше LLONG_MAX */
                fprintf(stderr, "Error: line %zu: n must be a whole number from 1 to %lld\n",
                        line_number, LLONG_MAX);
                break;
            case BAD_CAPACITY:
                /* capacity = -1, "1.5", больше LLONG_MAX */
                fprintf(stderr, "Error: line %zu: capacity must be a whole number from 0 to %lld\n",
                        line_number, LLONG_MAX);
                break;
            case BAD_FILE_LINE:
                /* в строке файла не три слова (имя с пробелом, нет важности...) */
                fprintf(stderr, "Error: line %zu: a file line must be: name size importance\n",
                        line_number);
                break;
            case BAD_SIZE:
                /* размер 0, отрицательный, не число, слишком большой */
                fprintf(stderr, "Error: line %zu: size must be a whole number from 1 to %lld\n",
                        line_number, LLONG_MAX);
                break;
            case BAD_IMPORTANCE:
                /* важность отрицательная, не число, слишком большая */
                fprintf(stderr, "Error: line %zu: importance must be a whole number from 0 to %lld\n",
                        line_number, LLONG_MAX);
                break;
            case TOO_FEW_FILES:
                /* ввод кончился раньше, чем прочитали n файлов */
                fprintf(stderr, "Error: expected %lld files, but found only %zu\n",
                        data.n, data.item_count);
                break;
            case TOO_MANY_FILES:
                /* после n файлов есть еще данные */
                fprintf(stderr, "Error: line %zu: there are more than %lld files\n",
                        line_number, data.n);
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

    /* 2) Имена должны быть разными (по условию они уникальны - проверяем) */
    int has_duplicate = 0;
    const char *duplicate = NULL;
    sc = check_unique(data.files, data.item_count, &has_duplicate, &duplicate);
    if (sc != OK) {
        /* здесь возможен только ALLOC_ERROR */
        fprintf(stderr, "Error: not enough memory\n");
        free_files(data.files, data.item_count);
        return 1;
    }
    if (has_duplicate) {
        fprintf(stderr, "Error: file name '%s' is used more than once\n", duplicate);
        free_files(data.files, data.item_count);
        return 1;
    }

    /* 3) Сам выбор файлов */
    backup_result result;
    sc = choose_backup(data.files, data.item_count, data.capacity, &result);
    if (sc != OK) {
        if (sc == OVERFLOW_ERROR) {
            /* сумма важностей не помещается в long long - ответ не напечатать */
            fprintf(stderr, "Error: the sum of importances is too big\n");
        } else if (sc == TOO_BIG) {
            /* огромные числа и много файлов: таблицы и перебор слишком большие */
            fprintf(stderr, "Error: the task is too big: too many files with too big "
                            "capacity and importances\n");
        } else if (sc == ALLOC_ERROR) {
            fprintf(stderr, "Error: not enough memory\n");
        } else {
            /* INVALID_ARGS / INVALID_DATA: данные уже проверены при чтении */
            fprintf(stderr, "Error: internal error\n");
        }
        free_files(data.files, data.item_count);
        return 1;
    }

    /* 4) Вывод в формате из условия; имена - в порядке ввода */
    printf("TOTAL_SIZE %lld\n", result.total_size);
    printf("TOTAL_IMPORTANCE %lld\n", result.total_importance);
    printf("FILES %zu\n", result.count);
    for (size_t i = 0; i < result.count; i++) {
        printf("%s\n", result.files[i].name);
    }

    free_result(&result);
    free_files(data.files, data.item_count);
    return 0;
}
