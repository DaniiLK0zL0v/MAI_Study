/*
 * Лабораторная работа 1 по ОС, вариант 11 (группа вариантов 3), Windows.
 * parent - родительский процесс.
 *
 * Схема (три неименованных канала, CreatePipe):
 *
 *   пользователь -> parent --pipe1--> child1 --pipe_mid--> child2 --pipe2--> parent -> экран
 *
 *   child1 переводит латинские буквы в верхний регистр,
 *   child2 заменяет пробельные символы (пробел, табуляция и т.п.) на '_'.
 *   pipe1 подключён к стандартному вводу child1, pipe_mid - к выводу child1 и вводу child2,
 *   pipe2 - к стандартному выводу child2.
 *
 * parent читает со своего стандартного ввода строки любой длины, отправляет каждую в pipe1
 * и печатает то, что вернулось из pipe2. Конец ввода (Ctrl+Z и Enter в консоли или конец файла) -
 * конец работы: parent закрывает pipe1, child1 видит конец ввода и завершается, закрывается
 * pipe_mid, child2 тоже видит конец ввода и завершается. parent ждёт обоих детей.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Сколько байт строки отправлять в pipe1 за один раз (почему порциями - см. send_line) */
#define PART_SIZE 4096

/* Начальный размер буфера для строки; дальше буфер растёт вдвое */
#define START_CAPACITY 64

/* Все объекты ОС, которые создаёт parent, - в одном месте, чтобы их было легко закрыть */
typedef struct {
    HANDLE pipe1_read;      /* pipe1: читает child1 (его stdin)       */
    HANDLE pipe1_write;     /* pipe1: пишет parent                    */
    HANDLE middle_read;     /* pipe_mid: читает child2 (его stdin)    */
    HANDLE middle_write;    /* pipe_mid: пишет child1 (его stdout)    */
    HANDLE pipe2_read;      /* pipe2: читает parent                   */
    HANDLE pipe2_write;     /* pipe2: пишет child2 (его stdout)       */
    PROCESS_INFORMATION child1;   /* описатели процесса и потока child1 */
    PROCESS_INFORMATION child2;
    int child1_started;     /* 1 - процесс child1 создан, его надо дождаться */
    int child2_started;
} system_objects;

/* Сообщение об ошибке системного вызова: что не получилось и код ошибки Windows */
static void print_system_error(const char *what) {
    fprintf(stderr, "Error: %s failed (Windows error %lu)\n", what, (unsigned long)GetLastError());
}

/* Закрыть описатель, если он открыт, и запомнить, что он закрыт (чтобы не закрыть дважды) */
static void close_handle(HANDLE *handle) {
    if (*handle != NULL) {
        CloseHandle(*handle);
        *handle = NULL;
    }
}

/*
 * Полный путь к программе ребёнка: та же папка, где лежит parent.exe, и имя name.
 * Так parent находит детей, из какой бы папки его ни запустили.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int get_child_path(const char *name, char *path, DWORD size) {
    /* GetModuleFileNameA(NULL, ...) - полный путь к exe текущего процесса */
    DWORD length = GetModuleFileNameA(NULL, path, size);
    if (length == 0) {
        print_system_error("GetModuleFileName");
        return 1;
    }
    /* если путь не поместился, Windows обрезает его и возвращает size */
    if (length >= size) {
        fprintf(stderr, "Error: the path to parent.exe is too long\n");
        return 1;
    }

    /* отрезаем имя parent.exe: ищем последний '\' (или '/') */
    DWORD folder_end = length;
    while (folder_end > 0 && path[folder_end - 1] != '\\' && path[folder_end - 1] != '/') {
        folder_end--;
    }

    /* дописываем имя ребёнка вместо имени parent.exe; проверяем, что хватит места с '\0' */
    size_t name_length = strlen(name);
    if (folder_end + name_length + 1 > size) {
        fprintf(stderr, "Error: the path to %s is too long\n", name);
        return 1;
    }
    memcpy(path + folder_end, name, name_length + 1);
    return 0;
}

/*
 * Запустить программу path как дочерний процесс:
 * его стандартный ввод - описатель input, стандартный вывод - output, ошибки - как у parent.
 *
 * Ребёнок получает копии только тех описателей, у которых стоит флаг "наследуемый".
 * Все каналы создаются ненаследуемыми; здесь флаг ставится только двум нужным концам,
 * а после запуска снимается. Если бы, например, child2 унаследовал пишущий конец pipe_mid,
 * то pipe_mid не закрылся бы и после выхода child1, и child2 вечно ждал бы конца ввода.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int start_child(const char *path, HANDLE input, HANDLE output, PROCESS_INFORMATION *info) {
    if (!SetHandleInformation(input, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)
        || !SetHandleInformation(output, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
        print_system_error("SetHandleInformation");
        return 1;
    }

    /* STARTUPINFO: STARTF_USESTDHANDLES - взять stdin/stdout/stderr ребёнка из этой структуры */
    STARTUPINFOA startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    /* CreateProcess: имя программы - path, командная строка не нужна,
       TRUE - передать ребёнку наследуемые описатели */
    ZeroMemory(info, sizeof(*info));
    BOOL created = CreateProcessA(path, NULL, NULL, NULL, TRUE, 0, NULL, NULL, &startup, info);
    if (!created) {
        /* GetLastError надо прочитать сразу, до других вызовов */
        DWORD code = GetLastError();
        fprintf(stderr, "Error: can not start %s (Windows error %lu)\n", path, (unsigned long)code);
    }

    /* снимаем флаг: следующий ребёнок не должен получить эти концы каналов */
    SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
    return created ? 0 : 1;
}

/*
 * Прочитать одну строку со стандартного ввода в буфер *line (вместе с '\n' в конце).
 * Буфер растёт вдвое через realloc, поэтому длина строки не ограничена.
 * Если последняя строка файла без '\n', он всё равно дописывается: детям и выводу так проще.
 * *end = 1 - ввод закончился, строки нет.
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
            /* realloc - через временную переменную: при неудаче старый буфер остаётся целым */
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
    /* ввод закончился: если что-то успели прочитать - это последняя строка без '\n' */
    if (*length == 0) {
        *end = 1;
    } else {
        (*line)[(*length)++] = '\n';
    }
    return 0;
}

/*
 * Записать в канал ровно size байт. WriteFile может записать меньше, чем просили, -
 * тогда дописываем остаток.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int write_all(HANDLE pipe, const char *data, DWORD size) {
    DWORD done = 0;
    while (done < size) {
        DWORD written = 0;
        if (!WriteFile(pipe, data + done, size - done, &written, NULL)) {
            /* ERROR_NO_DATA / ERROR_BROKEN_PIPE: читающий конец закрыт - child1 завершился */
            print_system_error("WriteFile to pipe1");
            return 1;
        }
        done += written;
    }
    return 0;
}

/*
 * Прочитать из канала ровно size байт. ReadFile возвращает столько, сколько уже есть
 * в канале, - поэтому читаем в цикле, пока не наберём всё.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int read_exact(HANDLE pipe, char *buf, DWORD size) {
    DWORD done = 0;
    while (done < size) {
        DWORD received = 0;
        if (!ReadFile(pipe, buf + done, size - done, &received, NULL)) {
            /* ERROR_BROKEN_PIPE: пишущий конец закрыт - child2 завершился раньше времени */
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                fprintf(stderr, "Error: child2 finished before sending the answer\n");
            } else {
                print_system_error("ReadFile from pipe2");
            }
            return 1;
        }
        if (received == 0) {
            fprintf(stderr, "Error: child2 finished before sending the answer\n");
            return 1;
        }
        done += received;
    }
    return 0;
}

/*
 * Отправить строку детям и напечатать ответ.
 *
 * Строку отправляем порциями по PART_SIZE байт и после каждой порции читаем ответ на неё.
 * Если отправить сразу длинную строку (больше, чем вмещают каналы), будет взаимная блокировка:
 * parent ждёт, пока освободится место в pipe1, а сам не читает pipe2; pipe2 полон,
 * child2 ждёт места в pipe2 и не читает pipe_mid; child1 ждёт места в pipe_mid и не читает pipe1.
 * Порция в 4096 байт всегда помещается в буферы каналов, поэтому так блокировки нет.
 *
 * Оба ребёнка меняют каждый символ на ровно один символ (буква -> буква, пробел -> '_'),
 * поэтому ответ на порцию из n байт - ровно n байт.
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int send_line(HANDLE to_child, HANDLE from_child, const char *line, size_t length, char *answer) {
    size_t sent = 0;
    while (sent < length) {
        DWORD part = (DWORD)(length - sent < PART_SIZE ? length - sent : PART_SIZE);
        if (write_all(to_child, line + sent, part) != 0 || read_exact(from_child, answer, part) != 0) {
            return 1;
        }
        /* ответ - на экран (или в файл, если вывод parent перенаправлен) */
        fwrite(answer, 1, part, stdout);
        sent += part;
    }
    /* строка напечатана целиком - сразу показываем её, не дожидаясь заполнения буфера stdout */
    fflush(stdout);
    return 0;
}

/*
 * Закрыть все каналы parent и дождаться детей, которые были запущены.
 * Сначала закрываем каналы, потом ждём: child1 увидит конец ввода (pipe1 закрыт) и выйдет,
 * за ним child2. Если ждать с открытыми каналами - дети не узнают, что пора выходить.
 * Возвращает 0 - оба ребёнка завершились с кодом 0, 1 - нет.
 */
static int finish(system_objects *objects) {
    int result = 0;
    close_handle(&objects->pipe1_read);
    close_handle(&objects->pipe1_write);
    close_handle(&objects->middle_read);
    close_handle(&objects->middle_write);
    close_handle(&objects->pipe2_read);
    close_handle(&objects->pipe2_write);

    /* по очереди для обоих детей: дождаться, узнать код завершения, закрыть описатели */
    PROCESS_INFORMATION *children[2] = { &objects->child1, &objects->child2 };
    int started[2] = { objects->child1_started, objects->child2_started };
    const char *names[2] = { "child1", "child2" };
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
        } else if (code != 0) {
            fprintf(stderr, "Error: %s finished with code %lu\n", names[i], (unsigned long)code);
            result = 1;
        }
        CloseHandle(children[i]->hProcess);
        CloseHandle(children[i]->hThread);
    }
    return result;
}

int main(int argc, char *argv[]) {
    (void)argv;
    /* программа работает только со стандартным вводом; аргументы - ошибка пользователя */
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

    /* все описатели - пустые (NULL), процессов пока нет */
    system_objects objects;
    ZeroMemory(&objects, sizeof(objects));

    /* три канала; NULL вместо SECURITY_ATTRIBUTES - описатели не наследуются,
       0 - размер буфера канала по умолчанию */
    if (!CreatePipe(&objects.pipe1_read, &objects.pipe1_write, NULL, 0)
        || !CreatePipe(&objects.middle_read, &objects.middle_write, NULL, 0)
        || !CreatePipe(&objects.pipe2_read, &objects.pipe2_write, NULL, 0)) {
        print_system_error("CreatePipe");
        finish(&objects);
        return 1;
    }

    /* stderr parent должен достаться детям, чтобы их сообщения об ошибках были видны.
       Если stderr не наследуемый (бывает, когда его перенаправили), разрешаем наследование;
       неудача не страшна - потеряются только сообщения детей */
    HANDLE error_handle = GetStdHandle(STD_ERROR_HANDLE);
    if (error_handle != NULL && error_handle != INVALID_HANDLE_VALUE) {
        SetHandleInformation(error_handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }

    /* child1: читает pipe1, пишет в pipe_mid */
    if (start_child(child1_path, objects.pipe1_read, objects.middle_write, &objects.child1) != 0) {
        finish(&objects);
        return 1;
    }
    objects.child1_started = 1;

    /* child2: читает pipe_mid, пишет в pipe2 */
    if (start_child(child2_path, objects.middle_read, objects.pipe2_write, &objects.child2) != 0) {
        finish(&objects);
        return 1;
    }
    objects.child2_started = 1;

    /* Концы каналов, которые теперь есть у детей, parent закрывает у себя.
       Иначе, например, pipe2 не закроется и после выхода child2: parent сам держит его пишущий конец. */
    close_handle(&objects.pipe1_read);
    close_handle(&objects.middle_read);
    close_handle(&objects.middle_write);
    close_handle(&objects.pipe2_write);

    /* буферы: для строки пользователя (растёт) и для ответа на одну порцию */
    size_t capacity = START_CAPACITY;
    char *line = malloc(capacity);
    char *answer = malloc(PART_SIZE);
    if (line == NULL || answer == NULL) {
        fprintf(stderr, "Error: not enough memory\n");
        free(line);
        free(answer);
        finish(&objects);
        return 1;
    }

    /* Подсказка - только если вводит человек с клавиатуры (а не файл); в stderr, чтобы не мешать выводу.
       GetConsoleMode удаётся только для настоящей консоли. (_isatty не подходит:
       он считает "терминалом" и пустое устройство NUL.) */
    DWORD console_mode = 0;
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &console_mode)) {
        fprintf(stderr, "Enter lines (Ctrl+Z and Enter - finish):\n");
    }

    /* главный цикл: строка пользователя -> дети -> ответ на экран */
    int result = 0;
    int end = 0;
    while (result == 0) {
        size_t length = 0;
        if (read_line(&line, &length, &capacity, &end) != 0) {
            fprintf(stderr, "Error: not enough memory for the line\n");
            result = 1;
        } else if (end) {
            break;
        } else if (send_line(objects.pipe1_write, objects.pipe2_read, line, length, answer) != 0) {
            result = 1;
        }
    }

    free(line);
    free(answer);
    /* закрыть каналы и дождаться детей; ошибка детей - тоже ошибка всей программы */
    if (finish(&objects) != 0) {
        result = 1;
    }
    return result;
}
