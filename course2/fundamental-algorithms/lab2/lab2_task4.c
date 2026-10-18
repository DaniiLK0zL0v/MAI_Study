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
#define PEREBOR_MAX_FILES 22

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

/* Все, что прочитали из ввода. n и kolvo нужны и при ошибке - для сообщения
   ("ждали 5 файлов, нашли 3") */
typedef struct {
    file_info *files;      /* массив прочитанных файлов */
    size_t kolvo;          /* сколько файлов прочитано */
    long long n;           /* сколько файлов обещано в первой строке */
    long long capacity;    /* вместимость накопителя */
} input_data;

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

/* Освободить массив файлов: сначала имя каждого файла, потом сам массив.
   free(NULL) ничего не делает, поэтому пустой массив освобождать тоже можно */
status_code free_files(file_info *files, const size_t kolvo) {
    if (files == NULL) {
        return OK;
    }
    for (size_t i = 0; i < kolvo; i++) {
        free(files[i].name);
    }
    free(files);
    return OK;
}

/* Освободить результат (у выбранных файлов свои копии имен) и обнулить его */
status_code free_result(backup_result *rez) {
    if (rez == NULL) {
        return INVALID_ARGS;
    }
    free_files(rez->files, rez->count);
    rez->files = NULL;
    rez->count = 0;
    rez->total_size = 0;
    rez->total_importance = 0;
    return OK;
}

/* ===================== выбор файлов (главная функция и способы) ===================== */

/* Способ 1 - таблица по размеру ("классический" рюкзак).
   kandidaty - номера файлов, которые участвуют (их m штук), W - вместимость.
   luchshaya[w] - самая большая важность, которую можно набрать с суммарным
   размером не больше w из уже рассмотренных файлов.
   Добавляем файлы по одному. Для файла (размер s, важность v):
       взять его в рюкзак вместимости w выгодно, если luchshaya[w - s] + v > luchshaya[w]
   (место s отдаем под этот файл, остаток w - s заполняем как лучше было раньше).
   w перебираем от большего к меньшему: так luchshaya[w - s] еще "старая", без этого
   файла, и файл не возьмется дважды.
   vzyat[i][w] = 1 - "в лучшем наборе для вместимости w файл i взят"; по этой таблице
   в конце восстанавливаем сам набор: идем по файлам с последнего, и если файл
   взят - отнимаем его размер от w.
   vybran[i] = 1 - кандидат i попал в ответ */
status_code knapsack_by_size(const file_info *files, const size_t *kandidaty, const size_t m,
                             const long long W, unsigned char *vybran) {
    if (files == NULL || kandidaty == NULL || vybran == NULL || W < 0) {
        return INVALID_ARGS;
    }
    /* Размеры проверил вызывающий (m * (W + 1) <= TABLE_LIMIT), так что все
       помещается в size_t; ширина строки таблицы - W + 1 (вместимости 0..W) */
    size_t shirina = (size_t)W + 1;
    long long *luchshaya = (long long *)malloc(shirina * sizeof(long long));
    if (luchshaya == NULL) {
        return ALLOC_ERROR;
    }
    /* Таблица vzyat - m строк по shirina байт, хранится одним куском:
       клетка [i][w] лежит на месте i * shirina + w */
    unsigned char *vzyat = (unsigned char *)malloc(m * shirina);
    if (vzyat == NULL) {
        free(luchshaya);
        return ALLOC_ERROR;
    }
    /* пока не взяли ни одного файла, важность 0 при любой вместимости */
    for (size_t w = 0; w < shirina; w++) {
        luchshaya[w] = 0;
    }

    for (size_t i = 0; i < m; i++) {
        long long s = files[kandidaty[i]].size;
        long long v = files[kandidaty[i]].importance;
        unsigned char *stroka = vzyat + i * shirina;   /* строка таблицы для файла i */
        /* при вместимости меньше s файл не помещается - "не взят" */
        for (size_t w = 0; w < shirina; w++) {
            stroka[w] = 0;
        }
        /* w от W вниз до s (кандидаты отобраны так, что s <= W).
           Цикл с long long: при w = s - 1 он должен закончиться, а size_t
           при "w >= s" и s = 0 никогда бы не закончился (но s >= 1, это для надежности) */
        for (long long w = W; w >= s; w--) {
            /* luchshaya[w - s] + v > LLONG_MAX? Проверка через вычитание.
               Если да - есть набор, который помещается (размер <= w), а его важность
               не помещается в long long. Значит, и ответ (он не меньше) не напечатать */
            if (v > LLONG_MAX - luchshaya[w - s]) {
                free(vzyat);
                free(luchshaya);
                return OVERFLOW_ERROR;
            }
            long long s_nim = luchshaya[w - s] + v;
            if (s_nim > luchshaya[w]) {
                luchshaya[w] = s_nim;
                stroka[w] = 1;
            }
        }
    }

    /* Восстановление набора: с последнего файла к первому */
    long long w = W;
    for (size_t k = m; k > 0; k--) {
        size_t i = k - 1;   /* цикл по k, потому что size_t i >= 0 всегда истинно */
        if (vzyat[i * shirina + (size_t)w]) {
            vybran[i] = 1;
            w -= files[kandidaty[i]].size;
        } else {
            vybran[i] = 0;
        }
    }

    free(vzyat);
    free(luchshaya);
    return OK;
}

/* Способ 2 - таблица по важности. Хорош, когда размеры огромные (например,
   в байтах), а важности маленькие.
   S - сумма важностей всех кандидатов.
   min_razmer[v] - самый маленький суммарный размер, с которым можно набрать
   важность ровно v; NEDOSTIZHIMO - такой важности набрать нельзя
   (или для этого нужно больше места, чем capacity - нам такое тоже не подходит).
   Для файла (размер s, важность v): min_razmer[x] можно улучшить до
   min_razmer[x - v] + s. Ответ - самая большая x, у которой min_razmer[x] <= capacity.
   x перебираем сверху вниз - по той же причине, что и в способе 1 */
status_code knapsack_by_importance(const file_info *files, const size_t *kandidaty, const size_t m,
                                   const long long S, const long long capacity,
                                   unsigned char *vybran) {
    if (files == NULL || kandidaty == NULL || vybran == NULL || S < 0 || capacity < 0) {
        return INVALID_ARGS;
    }
    /* NEDOSTIZHIMO = -1: настоящий размер набора не бывает отрицательным */
    const long long NEDOSTIZHIMO = -1;
    size_t shirina = (size_t)S + 1;   /* важности 0..S */
    long long *min_razmer = (long long *)malloc(shirina * sizeof(long long));
    if (min_razmer == NULL) {
        return ALLOC_ERROR;
    }
    unsigned char *vzyat = (unsigned char *)malloc(m * shirina);
    if (vzyat == NULL) {
        free(min_razmer);
        return ALLOC_ERROR;
    }
    /* важность 0 набирается пустым набором (размер 0), остальные пока нельзя */
    min_razmer[0] = 0;
    for (size_t x = 1; x < shirina; x++) {
        min_razmer[x] = NEDOSTIZHIMO;
    }

    for (size_t i = 0; i < m; i++) {
        long long s = files[kandidaty[i]].size;
        long long v = files[kandidaty[i]].importance;   /* у кандидатов v >= 1 */
        unsigned char *stroka = vzyat + i * shirina;
        for (size_t x = 0; x < shirina; x++) {
            stroka[x] = 0;
        }
        for (long long x = S; x >= v; x--) {
            long long bez = min_razmer[x - v];   /* размер набора без этого файла */
            if (bez == NEDOSTIZHIMO) {
                continue;
            }
            /* bez + s <= capacity? Пишем через вычитание: bez + s могло бы
               переполнить long long, а capacity - bez >= 0 всегда (bez <= capacity) */
            if (s > capacity - bez) {
                continue;
            }
            long long s_nim = bez + s;
            if (min_razmer[x] == NEDOSTIZHIMO || s_nim < min_razmer[x]) {
                min_razmer[x] = s_nim;
                stroka[x] = 1;
            }
        }
    }

    /* лучшая важность - самая большая достижимая (важность 0 достижима всегда) */
    long long x = S;
    while (min_razmer[x] == NEDOSTIZHIMO) {
        x--;
    }

    /* восстановление: как в способе 1, только отнимаем важность */
    for (size_t k = m; k > 0; k--) {
        size_t i = k - 1;
        if (vzyat[i * shirina + (size_t)x]) {
            vybran[i] = 1;
            x -= files[kandidaty[i]].importance;
        } else {
            vybran[i] = 0;
        }
    }

    free(vzyat);
    free(min_razmer);
    return OK;
}

/* Состояние полного перебора (способ 3): чтобы не передавать в рекурсию
   десять параметров, все общее лежит в одной структуре */
typedef struct {
    const file_info *files;
    const size_t *kandidaty;
    size_t m;
    unsigned char *tekushiy;     /* набор, который строим сейчас: 1 - файл взят */
    unsigned char *luchshiy;     /* лучший найденный набор */
    long long luchshaya_vazhnost;
} perebor_state;

/* Перебор всех наборов рекурсией: для файла i пробуем два варианта -
   "взять" (если помещается в ostatok) и "не брать", и идем к файлу i + 1.
   Когда файлы кончились (i == m) - набор готов, сравниваем с лучшим.
   ostatok - сколько места еще свободно, vazhnost - важность уже взятых файлов.
   Глубина рекурсии - m <= PEREBOR_MAX_FILES, это немного */
status_code perebor(perebor_state *st, const size_t i, const long long ostatok,
                    const long long vazhnost) {
    if (st == NULL) {
        return INVALID_ARGS;
    }
    if (i == st->m) {
        /* набор готов: если он лучше - запоминаем его копию */
        if (vazhnost > st->luchshaya_vazhnost) {
            st->luchshaya_vazhnost = vazhnost;
            memcpy(st->luchshiy, st->tekushiy, st->m);
        }
        return OK;
    }
    const file_info *f = &st->files[st->kandidaty[i]];
    status_code sc = OK;
    /* вариант "взять": только если файл помещается в оставшееся место */
    if (f->size <= ostatok) {
        /* важность набора, который помещается, больше LLONG_MAX - ответ не напечатать */
        if (f->importance > LLONG_MAX - vazhnost) {
            return OVERFLOW_ERROR;
        }
        st->tekushiy[i] = 1;
        sc = perebor(st, i + 1, ostatok - f->size, vazhnost + f->importance);
        if (sc != OK) {
            return sc;
        }
    }
    /* вариант "не брать" */
    st->tekushiy[i] = 0;
    return perebor(st, i + 1, ostatok, vazhnost);
}

/* Способ 3 - полный перебор всех 2^m наборов */
status_code knapsack_perebor(const file_info *files, const size_t *kandidaty, const size_t m,
                             const long long capacity, unsigned char *vybran) {
    if (files == NULL || kandidaty == NULL || vybran == NULL || capacity < 0) {
        return INVALID_ARGS;
    }
    unsigned char *tekushiy = (unsigned char *)malloc(m);
    if (tekushiy == NULL) {
        return ALLOC_ERROR;
    }
    /* лучший набор вначале - пустой (важность 0) */
    for (size_t i = 0; i < m; i++) {
        tekushiy[i] = 0;
        vybran[i] = 0;
    }
    perebor_state st;
    st.files = files;
    st.kandidaty = kandidaty;
    st.m = m;
    st.tekushiy = tekushiy;
    st.luchshiy = vybran;          /* лучший набор пишем сразу в ответ */
    st.luchshaya_vazhnost = 0;
    status_code sc = perebor(&st, 0, capacity, 0);
    free(tekushiy);
    return sc;
}

/* Главная функция задания: выбрать файлы для накопителя вместимостью capacity.
   files, n - массив описаний и его длина; rez - сюда кладем ответ (его массив
   и имена в динамической памяти, освобождать - free_result).
   Шаги:
     1) отбрасываем файлы, которые точно не нужны: больше capacity (не помещаются)
        и с важностью 0 (место занимают, важности не добавляют). Остальные - кандидаты;
     2) если все кандидаты помещаются вместе - берем всех, таблицы не нужны
        (это и спасает при огромной capacity: 10^18 и маленькие файлы);
     3) иначе считаем, сколько работы нужно каждому способу, и берем самый дешевый:
          таблица по размеру   - m * (capacity + 1) клеток,
          таблица по важности  - m * (S + 1) клеток (S - сумма важностей),
          перебор              - 2^m наборов (только при m <= PEREBOR_MAX_FILES);
        если ни один не проходит по ограничениям - TOO_BIG;
     4) собираем ответ в порядке ввода. */
status_code choose_backup(const file_info *files, const size_t n, const long long capacity,
                          backup_result *rez) {
    if (rez == NULL || (files == NULL && n > 0)) {
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
    size_t *kandidaty = (size_t *)malloc((n > 0 ? n : 1) * sizeof(size_t));
    if (kandidaty == NULL) {
        return ALLOC_ERROR;
    }
    size_t m = 0;
    long long S = 0;              /* сумма важностей кандидатов */
    int S_ogromnaya = 0;          /* 1 - сумма важностей не помещается в long long */
    long long summa_razmerov = 0; /* сумма размеров кандидатов (пока не больше capacity) */
    int vse_pomeshayutsya = 1;    /* 1 - все кандидаты вместе помещаются в capacity */
    for (size_t i = 0; i < n; i++) {
        if (files[i].size > capacity || files[i].importance == 0) {
            continue;
        }
        kandidaty[m] = i;
        m++;
        /* S + importance > LLONG_MAX? Проверка через вычитание, без переполнения.
           Огромная S - еще не ошибка: все файлы вместе могут не поместиться,
           и ответ окажется меньше. Просто способ 2 (таблица по важности) тогда
           не подходит, а способы 1 и 3 сами следят за переполнением */
        if (!S_ogromnaya) {
            if (files[i].importance > LLONG_MAX - S) {
                S_ogromnaya = 1;
            } else {
                S += files[i].importance;
            }
        }
        /* сумму размеров считаем, только пока она не больше capacity:
           как только стало больше - ясно, что все не поместятся, дальше не нужно
           (и переполнения так не бывает: summa_razmerov <= capacity <= LLONG_MAX) */
        if (vse_pomeshayutsya) {
            if (files[i].size > capacity - summa_razmerov) {
                vse_pomeshayutsya = 0;
            } else {
                summa_razmerov += files[i].size;
            }
        }
    }

    /* vybran[i] = 1 - кандидат номер i попал в ответ */
    unsigned char *vybran = (unsigned char *)malloc(m > 0 ? m : 1);
    if (vybran == NULL) {
        free(kandidaty);
        return ALLOC_ERROR;
    }

    status_code sc = OK;
    if (vse_pomeshayutsya) {
        /* ----- шаг 2: помещаются все - берем всех ----- */
        /* ответ - все кандидаты, его важность = S; если S не помещается в long long,
           ответ напечатать нельзя */
        if (S_ogromnaya) {
            free(vybran);
            free(kandidaty);
            return OVERFLOW_ERROR;
        }
        for (size_t i = 0; i < m; i++) {
            vybran[i] = 1;
        }
    } else {
        /* ----- шаг 3: выбор способа ----- */
        /* Здесь m >= 2: один кандидат всегда помещается (его размер <= capacity).
           "Работа" способа - сколько клеток или наборов он переберет.
           -1 - способ не подходит (слишком много).
           Проверки "(W + 1) <= TABLE_LIMIT / m" записаны через деление, чтобы
           само умножение m * (W + 1) не переполнилось при огромной capacity */
        long long rabota_razmer = -1;
        long long rabota_vazhnost = -1;
        long long rabota_perebor = -1;
        long long mm = (long long)m;
        if (capacity < TABLE_LIMIT / mm) {
            rabota_razmer = mm * (capacity + 1);
        }
        if (!S_ogromnaya && S < TABLE_LIMIT / mm) {
            rabota_vazhnost = mm * (S + 1);
        }
        if (m <= PEREBOR_MAX_FILES) {
            rabota_perebor = 1LL << m;   /* 2^m: сдвиг единицы на m разрядов */
        }

        /* самый дешевый из подходящих: sposob 1, 2 или 3; 0 - ни один не подходит */
        int sposob = 0;
        long long luchshaya_rabota = -1;
        if (rabota_razmer >= 0) {
            sposob = 1;
            luchshaya_rabota = rabota_razmer;
        }
        if (rabota_vazhnost >= 0 && (sposob == 0 || rabota_vazhnost < luchshaya_rabota)) {
            sposob = 2;
            luchshaya_rabota = rabota_vazhnost;
        }
        if (rabota_perebor >= 0 && (sposob == 0 || rabota_perebor < luchshaya_rabota)) {
            sposob = 3;
            luchshaya_rabota = rabota_perebor;
        }

        if (sposob == 1) {
            sc = knapsack_by_size(files, kandidaty, m, capacity, vybran);
        } else if (sposob == 2) {
            sc = knapsack_by_importance(files, kandidaty, m, S, capacity, vybran);
        } else if (sposob == 3) {
            sc = knapsack_perebor(files, kandidaty, m, capacity, vybran);
        } else {
            sc = TOO_BIG;
        }
    }
    if (sc != OK) {
        free(vybran);
        free(kandidaty);
        return sc;
    }

    /* ----- шаг 4: ответ ----- */
    /* сколько выбрано - столько и элементов в массиве ответа */
    size_t count = 0;
    for (size_t i = 0; i < m; i++) {
        count += vybran[i];
    }
    file_info *vybrannye = (file_info *)malloc((count > 0 ? count : 1) * sizeof(file_info));
    if (vybrannye == NULL) {
        free(vybran);
        free(kandidaty);
        return ALLOC_ERROR;
    }
    /* кандидаты идут по возрастанию номеров, значит и ответ - в порядке ввода.
       Имя копируем: ответ не должен зависеть от того, когда освободят входной массив */
    size_t k = 0;
    long long total_size = 0;
    long long total_importance = 0;
    for (size_t i = 0; i < m; i++) {
        if (!vybran[i]) {
            continue;
        }
        const file_info *f = &files[kandidaty[i]];
        sc = copy_string(f->name, &vybrannye[k].name);
        if (sc != OK) {
            free_files(vybrannye, k);   /* освобождаем уже скопированные имена */
            free(vybran);
            free(kandidaty);
            return sc;
        }
        vybrannye[k].size = f->size;
        vybrannye[k].importance = f->importance;
        /* переполнения нет: размер набора <= capacity, важность <= S */
        total_size += f->size;
        total_importance += f->importance;
        k++;
    }

    free(vybran);
    free(kandidaty);
    rez->files = vybrannye;
    rez->count = count;
    rez->total_size = total_size;
    rez->total_importance = total_importance;
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
    size_t razmer = 16;   /* сколько байт выделено */
    size_t dlina = 0;     /* сколько символов уже прочитано */
    char *buf = (char *)malloc(razmer);
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
        if (dlina + 1 >= razmer) {
            if (razmer > SIZE_MAX / 2) {
                free(buf);
                return ALLOC_ERROR;
            }
            /* realloc - во временную переменную: если он вернет NULL,
               старый блок останется, и мы сможем его освободить */
            char *novyy = (char *)realloc(buf, razmer * 2);
            if (novyy == NULL) {
                free(buf);
                return ALLOC_ERROR;
            }
            buf = novyy;
            razmer *= 2;
        }
        buf[dlina] = (char)c;
        dlina++;
        c = fgetc(in);
    }
    buf[dlina] = '\0';
    *line = buf;
    return OK;
}

/* Разбить строку на слова (слова разделены пробелами, табуляциями, '\r').
   Строка меняется: после каждого слова пишется '\0', а в slova[] кладутся
   адреса начал слов. Больше max_slov слов не записываем, но считаем всех:
   *kolvo - сколько слов в строке на самом деле (чтобы заметить лишние) */
status_code split_words(char *line, char **slova, const size_t max_slov, size_t *kolvo) {
    if (line == NULL || slova == NULL || kolvo == NULL) {
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
        if (k < max_slov) {
            slova[k] = &line[i];
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
    *kolvo = k;
    return OK;
}

/* Строка -> целое неотрицательное число long long. Только цифры: "-1", "+5",
   "1.5", "12a", "" - ошибка (INVALID_NUMBER); больше LLONG_MAX - OVERFLOW_ERROR.
   scanf("%lld") тут не годится: на переполнении у него неопределенное поведение,
   а "%u" вообще принимает "-1" как огромное число */
status_code parse_number(const char *str, long long *chislo) {
    if (str == NULL || chislo == NULL) {
        return INVALID_ARGS;
    }
    if (str[0] == '\0') {
        return INVALID_NUMBER;
    }
    long long znach = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return INVALID_NUMBER;
        }
        int cifra = str[i] - '0';
        /* znach * 10 + cifra > LLONG_MAX  <=>  znach > (LLONG_MAX - cifra) / 10
           (так проверяем до умножения - само умножение могло бы переполниться) */
        if (znach > (LLONG_MAX - cifra) / 10) {
            return OVERFLOW_ERROR;
        }
        znach = znach * 10 + cifra;
    }
    *chislo = znach;
    return OK;
}

/* Строка пустая или из одних пробелов? (такие строки при чтении пропускаем) */
status_code is_blank(const char *line, int *pustaya) {
    if (line == NULL || pustaya == NULL) {
        return INVALID_ARGS;
    }
    *pustaya = 1;
    for (size_t i = 0; line[i] != '\0'; i++) {
        if (!isspace((unsigned char)line[i])) {
            *pustaya = 0;
            break;
        }
    }
    return OK;
}

/* Прочитать следующую непустую строку. *nomer_stroki - номер строки во вводе
   (считаем все строки, и пустые тоже - так номер совпадет с номером в файле) */
status_code read_nonblank_line(FILE *in, char **line, size_t *nomer_stroki) {
    if (in == NULL || line == NULL || nomer_stroki == NULL) {
        return INVALID_ARGS;
    }
    while (1) {
        char *str = NULL;
        status_code sc = read_line(in, &str);
        if (sc != OK) {
            return sc;   /* END_OF_INPUT или ALLOC_ERROR */
        }
        (*nomer_stroki)++;
        int pustaya = 0;
        is_blank(str, &pustaya);
        if (!pustaya) {
            *line = str;
            return OK;
        }
        free(str);   /* пустая строка не нужна - читаем следующую */
    }
}

/* Добавить файл в конец массива, который растет удвоением (как буфер строки).
   razmer - сколько элементов выделено, data->kolvo - сколько занято */
status_code add_file(input_data *data, size_t *razmer, char *name, const long long size,
                     const long long importance) {
    if (data == NULL || razmer == NULL || name == NULL) {
        return INVALID_ARGS;
    }
    if (data->kolvo == *razmer) {
        /* новый размер: 8 для начала, потом вдвое больше; проверяем, что
           (новый размер * размер элемента) помещается в size_t */
        size_t novyy_razmer = (*razmer == 0) ? 8 : *razmer * 2;
        if (*razmer > SIZE_MAX / 2 || novyy_razmer > SIZE_MAX / sizeof(file_info)) {
            return ALLOC_ERROR;
        }
        file_info *novyy = (file_info *)realloc(data->files, novyy_razmer * sizeof(file_info));
        if (novyy == NULL) {
            return ALLOC_ERROR;
        }
        data->files = novyy;
        *razmer = novyy_razmer;
    }
    data->files[data->kolvo].name = name;
    data->files[data->kolvo].size = size;
    data->files[data->kolvo].importance = importance;
    data->kolvo++;
    return OK;
}

/* Чтение всего ввода: первая строка "n capacity", потом n строк "имя размер важность".
   Пустые строки пропускаем. После n файлов непустых строк быть не должно.
   При ошибке все прочитанное освобождается (data->files = NULL), но data->n,
   data->kolvo и *nomer_stroki остаются - main напечатает, где ошибка.
   Массив файлов растет по мере чтения, а не выделяется сразу на n элементов:
   n может быть огромным (10^18), а строк при этом - три */
status_code read_input(FILE *in, input_data *data, size_t *nomer_stroki) {
    if (in == NULL || data == NULL || nomer_stroki == NULL) {
        return INVALID_ARGS;
    }
    data->files = NULL;
    data->kolvo = 0;
    data->n = 0;
    data->capacity = 0;
    *nomer_stroki = 0;

    /* ----- первая строка: n capacity ----- */
    char *line = NULL;
    status_code sc = read_nonblank_line(in, &line, nomer_stroki);
    if (sc == END_OF_INPUT) {
        return NO_DATA;
    }
    if (sc != OK) {
        return sc;
    }
    char *slova[3];       /* больше трех слов в строке нам не нужно */
    size_t kolvo_slov = 0;
    split_words(line, slova, 3, &kolvo_slov);
    if (kolvo_slov != 2) {
        free(line);
        return BAD_FIRST_LINE;
    }
    /* n: целое от 1; capacity: целое от 0. Ошибку числа (не число / слишком
       большое) сообщаем одним текстом "должно быть целым от .. до .." */
    if (parse_number(slova[0], &data->n) != OK || data->n < 1) {
        free(line);
        return BAD_N;
    }
    if (parse_number(slova[1], &data->capacity) != OK) {
        free(line);
        return BAD_CAPACITY;
    }
    free(line);

    /* ----- строки файлов ----- */
    size_t razmer = 0;   /* сколько элементов выделено в data->files */
    while ((long long)data->kolvo < data->n) {
        sc = read_nonblank_line(in, &line, nomer_stroki);
        if (sc == END_OF_INPUT) {
            /* файлов меньше, чем обещано: номер строки тут - последняя строка ввода */
            free_files(data->files, data->kolvo);
            data->files = NULL;
            return TOO_FEW_FILES;
        }
        if (sc != OK) {
            free_files(data->files, data->kolvo);
            data->files = NULL;
            return sc;
        }
        split_words(line, slova, 3, &kolvo_slov);
        long long size = 0;
        long long importance = 0;
        sc = OK;
        if (kolvo_slov != 3) {
            sc = BAD_FILE_LINE;
        } else if (parse_number(slova[1], &size) != OK || size < 1) {
            sc = BAD_SIZE;
        } else if (parse_number(slova[2], &importance) != OK) {
            sc = BAD_IMPORTANCE;
        }
        /* имя - копия первого слова (строку line сейчас освободим) */
        char *name = NULL;
        if (sc == OK) {
            sc = copy_string(slova[0], &name);
        }
        free(line);
        if (sc == OK) {
            sc = add_file(data, &razmer, name, size, importance);
            if (sc != OK) {
                free(name);   /* имя не попало в массив - освобождаем отдельно */
            }
        }
        if (sc != OK) {
            free_files(data->files, data->kolvo);
            data->files = NULL;
            return sc;
        }
    }

    /* ----- после n файлов: только пустые строки ----- */
    sc = read_nonblank_line(in, &line, nomer_stroki);
    if (sc == OK) {
        free(line);
        free_files(data->files, data->kolvo);
        data->files = NULL;
        return TOO_MANY_FILES;
    }
    if (sc != END_OF_INPUT) {
        free_files(data->files, data->kolvo);
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
   указателей на имена. *est_povtor = 1 и *povtor = повторившееся имя, если нашли */
status_code check_unique(const file_info *files, const size_t n, int *est_povtor,
                         const char **povtor) {
    if (est_povtor == NULL || povtor == NULL || (files == NULL && n > 0)) {
        return INVALID_ARGS;
    }
    *est_povtor = 0;
    if (n < 2) {
        return OK;
    }
    const char **imena = (const char **)malloc(n * sizeof(const char *));
    if (imena == NULL) {
        return ALLOC_ERROR;
    }
    for (size_t i = 0; i < n; i++) {
        imena[i] = files[i].name;
    }
    qsort(imena, n, sizeof(const char *), compare_names);
    for (size_t i = 0; i + 1 < n; i++) {
        if (strcmp(imena[i], imena[i + 1]) == 0) {
            *est_povtor = 1;
            *povtor = imena[i];   /* указывает на имя в files - оно живет дольше */
            break;
        }
    }
    free(imena);
    return OK;
}

/* ===================== main ===================== */

int main(void) {
    /* 1) Чтение ввода. Каждая ошибка - свое сообщение с номером строки */
    input_data data;
    size_t nomer_stroki = 0;
    status_code sc = read_input(stdin, &data, &nomer_stroki);
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
                        nomer_stroki);
                break;
            case BAD_N:
                /* n = 0, -3, "abc", больше LLONG_MAX */
                fprintf(stderr, "Error: line %zu: n must be a whole number from 1 to %lld\n",
                        nomer_stroki, LLONG_MAX);
                break;
            case BAD_CAPACITY:
                /* capacity = -1, "1.5", больше LLONG_MAX */
                fprintf(stderr, "Error: line %zu: capacity must be a whole number from 0 to %lld\n",
                        nomer_stroki, LLONG_MAX);
                break;
            case BAD_FILE_LINE:
                /* в строке файла не три слова (имя с пробелом, нет важности...) */
                fprintf(stderr, "Error: line %zu: a file line must be: name size importance\n",
                        nomer_stroki);
                break;
            case BAD_SIZE:
                /* размер 0, отрицательный, не число, слишком большой */
                fprintf(stderr, "Error: line %zu: size must be a whole number from 1 to %lld\n",
                        nomer_stroki, LLONG_MAX);
                break;
            case BAD_IMPORTANCE:
                /* важность отрицательная, не число, слишком большая */
                fprintf(stderr, "Error: line %zu: importance must be a whole number from 0 to %lld\n",
                        nomer_stroki, LLONG_MAX);
                break;
            case TOO_FEW_FILES:
                /* ввод кончился раньше, чем прочитали n файлов */
                fprintf(stderr, "Error: expected %lld files, but found only %zu\n",
                        data.n, data.kolvo);
                break;
            case TOO_MANY_FILES:
                /* после n файлов есть еще данные */
                fprintf(stderr, "Error: line %zu: there are more than %lld files\n",
                        nomer_stroki, data.n);
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
    int est_povtor = 0;
    const char *povtor = NULL;
    sc = check_unique(data.files, data.kolvo, &est_povtor, &povtor);
    if (sc != OK) {
        /* здесь возможен только ALLOC_ERROR */
        fprintf(stderr, "Error: not enough memory\n");
        free_files(data.files, data.kolvo);
        return 1;
    }
    if (est_povtor) {
        fprintf(stderr, "Error: file name '%s' is used more than once\n", povtor);
        free_files(data.files, data.kolvo);
        return 1;
    }

    /* 3) Сам выбор файлов */
    backup_result rez;
    sc = choose_backup(data.files, data.kolvo, data.capacity, &rez);
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
        free_files(data.files, data.kolvo);
        return 1;
    }

    /* 4) Вывод в формате из условия; имена - в порядке ввода */
    printf("TOTAL_SIZE %lld\n", rez.total_size);
    printf("TOTAL_IMPORTANCE %lld\n", rez.total_importance);
    printf("FILES %zu\n", rez.count);
    for (size_t i = 0; i < rez.count; i++) {
        printf("%s\n", rez.files[i].name);
    }

    free_result(&rez);
    free_files(data.files, data.kolvo);
    return 0;
}
