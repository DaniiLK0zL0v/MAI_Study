/*
 * Лабораторная 3 по ОС, вариант 11 (группа вариантов 3), Windows.
 * parent - родительский процесс. Это лабораторная 1, где каналы (pipe) заменены
 * именованной разделяемой памятью (shared memory) и семафорами.
 *
 * Схема (три канала на разделяемой памяти, см. channel.h):
 *
 *   пользователь -> parent --first--> child1 --middle--> child2 --second--> parent -> экран
 *
 *   child1 переводит латинские буквы в верхний регистр,
 *   child2 заменяет пробельные символы (пробел, табуляция и т.п.) на '_'.
 *
 * parent создает три канала и событие остановки, запускает детей и передает им в командной
 * строке префикс имен объектов, имена своих каналов и свой PID. Затем читает со своего ввода строки
 * любой длины, отправляет каждую в канал first и печатает то, что вернулось из канала second.
 * Конец ввода (Ctrl+Z и Enter в консоли или конец файла) - parent отправляет порцию с флагом end,
 * она проходит через child1 и child2 и возвращается; дети после этого завершаются.
 *
 * Имена объектов: "Local\os_lab3_<PID parent>_...": у каждого запуска свои, две пары программ
 * друг другу не мешают.
 * Если ребенок неожиданно завершился - parent замечает это (его описатель процесса - "сторож"
 * в каждом ожидании), сообщает об ошибке и взводит событие остановки, чтобы второй ребенок тоже вышел.
 */

#include "channel.h"
#include <stdlib.h>

/* Начальный размер буфера для строки; дальше буфер растет вдвое */
#define START_CAPACITY 64

/* Все объекты ОС, которые создает parent, - в одном месте, чтобы их было легко закрыть */
typedef struct {
    channel first;        /* parent -> child1 */
    channel middle;       /* child1 -> child2 */
    channel second;       /* child2 -> parent */
    HANDLE stop;          /* событие "остановитесь" для детей (при ошибке у parent) */
    PROCESS_INFORMATION child1;
    PROCESS_INFORMATION child2;
    int child1_started;   /* 1 - процесс создан, его надо дождаться */
    int child2_started;
} system_objects;

/* Сообщение об ошибке системного вызова */
static void print_system_error(const char *what) {
    channel_print_error("parent", what);
}

/*
 * Полный путь к программе ребенка: та же папка, где лежит parent.exe, и имя name.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int get_child_path(const char *name, char *path, DWORD size) {
    DWORD length = GetModuleFileNameA(NULL, path, size);
    if (length == 0) {
        print_system_error("GetModuleFileName");
        return 1;
    }
    if (length >= size) {
        fprintf(stderr, "Error: parent: the path to parent.exe is too long\n");
        return 1;
    }
    /* отрезаем имя parent.exe: ищем последний '\' (или '/') */
    DWORD folder_end = length;
    while (folder_end > 0 && path[folder_end - 1] != '\\' && path[folder_end - 1] != '/') {
        folder_end--;
    }
    size_t name_length = strlen(name);
    if (folder_end + name_length + 1 > size) {
        fprintf(stderr, "Error: parent: the path to %s is too long\n", name);
        return 1;
    }
    memcpy(path + folder_end, name, name_length + 1);
    return 0;
}

/*
 * Запустить ребенка: командная строка "путь" префикс входной_канал выходной_канал PID_parent.
 * Ребенку передается только stderr parent (чтобы его сообщения об ошибках были видны);
 * разделяемую память и семафоры он открывает сам по именам.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int start_child(const char *path, const char *prefix, const char *input, const char *output,
                       PROCESS_INFORMATION *info) {
    /* командная строка; CreateProcess может менять ее, поэтому - свой массив, а не константа */
    char command[MAX_PATH + 3 * NAME_SIZE];
    int written = snprintf(command, sizeof(command), "\"%s\" %s %s %s %lu", path, prefix, input, output,
                           (unsigned long)GetCurrentProcessId());
    if (written < 0 || (size_t)written >= sizeof(command)) {
        fprintf(stderr, "Error: parent: the command line for %s is too long\n", path);
        return 1;
    }
    /* stderr ребенка - тот же, что у parent; stdin и stdout ему не нужны */
    STARTUPINFOA startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = NULL;
    startup.hStdOutput = NULL;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    /* TRUE - передать наследуемые описатели: наследуемый у нас только stderr (см. main) */
    ZeroMemory(info, sizeof(*info));
    if (!CreateProcessA(path, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, info)) {
        DWORD code = GetLastError();
        fprintf(stderr, "Error: parent: can not start %s (Windows error %lu)\n", path, (unsigned long)code);
        return 1;
    }
    return 0;
}

/*
 * Прочитать одну строку со стандартного ввода в буфер *line (вместе с '\n' в конце).
 * Буфер растет вдвое через realloc. *end = 1 - ввод закончился, строки нет.
 * Возвращает 0 - успех, 1 - не хватило памяти.
 */
static int read_line(char **line, size_t *length, size_t *capacity, int *end) {
    int symbol;
    *length = 0;
    *end = 0;
    while ((symbol = getchar()) != EOF) {
        /* +2: место для этого символа и для '\n', который может понадобиться в конце */
        if (*length + 2 > *capacity) {
            size_t new_capacity = *capacity * 2;
            char *tmp = realloc(*line, new_capacity);
            if (tmp == NULL) {
                return 1;
            }
            *line = tmp;
            *capacity = new_capacity;
        }
        (*line)[(*length)++] = (char)symbol;
        if (symbol == '\n') {
            return 0;
        }
    }
    /* последняя строка без '\n' - дописываем его */
    if (*length == 0) {
        *end = 1;
    } else {
        (*line)[(*length)++] = '\n';
    }
    return 0;
}

/* Сообщение, если канал вернул не CHANNEL_OK */
static void report_channel(channel_status st) {
    if (st == CHANNEL_STOPPED) {
        fprintf(stderr, "Error: parent: a child process ended unexpectedly\n");
    } else if (st == CHANNEL_INVALID_ARGS) {
        fprintf(stderr, "Error: internal error\n");   /* аргументы задает сама программа - такого не бывает */
    }
    /* CHANNEL_SYSTEM_ERROR - сообщение уже напечатано */
}

/*
 * Отправить строку детям и напечатать ответ. Как в лабораторной 1: строка уходит порциями
 * по CHUNK_SIZE байт, и после каждой порции parent ждет ответ на нее. Буфер у канала на одну порцию;
 * если отправлять, не читая ответы, все три буфера заполнятся и процессы будут ждать друг друга вечно.
 * Оба ребенка меняют каждый символ на ровно один символ, поэтому ответ - той же длины.
 * watch - описатели детей: если ребенок завершился, ожидание прерывается.
 */
static channel_status send_line(system_objects *o, const char *line, size_t length, char *answer,
                                const HANDLE *watch) {
    size_t sent = 0;
    while (sent < length) {
        DWORD part = (DWORD)(length - sent < CHUNK_SIZE ? length - sent : CHUNK_SIZE);
        channel_status st = channel_send(&o->first, line + sent, part, 0, watch, 2, "parent");
        if (st != CHANNEL_OK) {
            return st;
        }
        /* ответ может прийти и короче (если бы ребенок менял длину) - читаем, пока не наберем part байт */
        DWORD received = 0;
        while (received < part) {
            DWORD got = 0;
            LONG end = 0;
            st = channel_receive(&o->second, answer, &got, &end, watch, 2, "parent");
            if (st != CHANNEL_OK) {
                return st;
            }
            if (end) {
                fprintf(stderr, "Error: parent: the children finished before sending the answer\n");
                return CHANNEL_SYSTEM_ERROR;
            }
            fwrite(answer, 1, got, stdout);
            received += got;
        }
        sent += part;
    }
    fflush(stdout);
    return CHANNEL_OK;
}

/*
 * Закрыть все и дождаться детей. failed = 1 - была ошибка: тогда сначала взводим событие остановки,
 * чтобы дети, которые ждут данных, вышли. Без ошибки дети к этому моменту уже завершаются сами
 * (они получили порцию end).
 * Возвращает 0 - оба ребенка завершились с кодом 0, 1 - нет.
 */
static int finish(system_objects *o, int failed) {
    int result = 0;
    if (failed && o->stop != NULL) {
        SetEvent(o->stop);
    }
    PROCESS_INFORMATION *children[2] = {&o->child1, &o->child2};
    int started[2] = {o->child1_started, o->child2_started};
    const char *names[2] = {"child1", "child2"};
    for (int i = 0; i < 2; i++) {
        if (!started[i]) {
            continue;
        }
        DWORD code = 0;
        if (WaitForSingleObject(children[i]->hProcess, INFINITE) == WAIT_FAILED) {
            print_system_error("WaitForSingleObject");
            result = 1;
        } else if (!GetExitCodeProcess(children[i]->hProcess, &code)) {
            print_system_error("GetExitCodeProcess");
            result = 1;
        } else if (code != 0 && !failed) {
            fprintf(stderr, "Error: parent: %s finished with code %lu\n", names[i], (unsigned long)code);
            result = 1;
        }
        CloseHandle(children[i]->hProcess);
        CloseHandle(children[i]->hThread);
    }
    channel_close(&o->first);
    channel_close(&o->middle);
    channel_close(&o->second);
    if (o->stop != NULL) {
        CloseHandle(o->stop);
        o->stop = NULL;
    }
    return result;
}

int main(int argc, char *argv[]) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "Error: parent takes no arguments, it reads lines from the input\n");
        return 1;
    }

    /* пути к детям: рядом с parent.exe */
    char child1_path[MAX_PATH];
    char child2_path[MAX_PATH];
    if (get_child_path("child1.exe", child1_path, MAX_PATH) != 0
        || get_child_path("child2.exe", child2_path, MAX_PATH) != 0) {
        return 1;
    }

    /* префикс имен: свой у каждого запуска (PID работающего процесса уникален) */
    char prefix[NAME_SIZE];
    snprintf(prefix, sizeof(prefix), "os_lab3_%lu", (unsigned long)GetCurrentProcessId());

    system_objects o;
    ZeroMemory(&o, sizeof(o));
    channel_init(&o.first);
    channel_init(&o.middle);
    channel_init(&o.second);
    o.stop = NULL;

    /* три канала и событие остановки (ручной сброс: TRUE; вначале не взведено: FALSE) */
    if (channel_create(&o.first, prefix, "first", "parent") != CHANNEL_OK
        || channel_create(&o.middle, prefix, "middle", "parent") != CHANNEL_OK
        || channel_create(&o.second, prefix, "second", "parent") != CHANNEL_OK) {
        finish(&o, 1);
        return 1;
    }
    char stop_name[NAME_SIZE];
    channel_make_name(stop_name, prefix, "stop", "");
    o.stop = CreateEventA(NULL, TRUE, FALSE, stop_name);
    if (o.stop == NULL) {
        print_system_error("CreateEvent");
        finish(&o, 1);
        return 1;
    }

    /* stderr parent должен достаться детям: разрешаем его наследование (неудача не страшна) */
    HANDLE error_handle = GetStdHandle(STD_ERROR_HANDLE);
    if (error_handle != NULL && error_handle != INVALID_HANDLE_VALUE) {
        SetHandleInformation(error_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }

    /* дети: child1 читает first и пишет в middle; child2 читает middle и пишет в second */
    if (start_child(child1_path, prefix, "first", "middle", &o.child1) != 0) {
        finish(&o, 1);
        return 1;
    }
    o.child1_started = 1;
    if (start_child(child2_path, prefix, "middle", "second", &o.child2) != 0) {
        finish(&o, 1);
        return 1;
    }
    o.child2_started = 1;
    /* сторожа для всех ожиданий parent: если ребенок завершится, ожидание прервется */
    HANDLE watch[2] = {o.child1.hProcess, o.child2.hProcess};

    size_t capacity = START_CAPACITY;
    char *line = malloc(capacity);
    char *answer = malloc(CHUNK_SIZE);
    if (line == NULL || answer == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        free(line);
        free(answer);
        finish(&o, 1);
        return 1;
    }

    /* подсказка - только если вводит человек с клавиатуры (GetConsoleMode удается только для консоли) */
    DWORD console_mode = 0;
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &console_mode)) {
        fprintf(stderr, "Enter lines (Ctrl+Z and Enter - finish):\n");
    }

    /* главный цикл: строка -> дети -> ответ на экран */
    int failed = 0;
    channel_status st = CHANNEL_OK;
    while (!failed) {
        size_t length = 0;
        int end = 0;
        if (read_line(&line, &length, &capacity, &end) != 0) {
            fprintf(stderr, "Error: not enough memory for the line\n");
            failed = 1;
        } else if (end) {
            break;
        } else {
            st = send_line(&o, line, length, answer, watch);
            if (st != CHANNEL_OK) {
                report_channel(st);
                failed = 1;
            }
        }
    }

    /* конец ввода: порция end проходит через обоих детей и возвращается - значит, все обработано.
       Получив end, child1 пересылает его и завершается - это нормально, поэтому при ожидании ответа
       сторожим только child2 (иначе выход child1 приняли бы за ошибку) */
    if (!failed) {
        st = channel_send(&o.first, NULL, 0, 1, watch, 2, "parent");
        LONG end = 0;
        while (st == CHANNEL_OK && !end) {
            DWORD got = 0;
            st = channel_receive(&o.second, answer, &got, &end, &watch[1], 1, "parent");
        }
        if (st != CHANNEL_OK) {
            report_channel(st);
            failed = 1;
        }
    }

    free(line);
    free(answer);
    if (finish(&o, failed) != 0) {
        failed = 1;
    }
    return failed;
}
