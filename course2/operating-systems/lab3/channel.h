/*
 * Лабораторная 3 по ОС, вариант 11, Windows. Общий код для parent.c, child1.c, child2.c:
 * "канал" на именованной разделяемой памяти и двух именованных семафорах.
 *
 * Канал - это объект разделяемой памяти (CreateFileMapping с INVALID_HANDLE_VALUE: память берется
 * из оперативной памяти / файла подкачки, а не из файла на диске) с буфером на одну порцию данных:
 *     length - сколько байт в порции, end - 1, если это сигнал "данные кончились", data - сами байты.
 * Чтобы писатель и читатель не мешали друг другу, у канала два семафора:
 *     empty - буфер свободен, можно писать (начальное значение 1);
 *     full  - в буфере порция, можно читать (начальное значение 0).
 * Писатель: ждет empty -> пишет порцию -> отпускает full.
 * Читатель: ждет full  -> читает порцию -> отпускает empty.
 * Так в каждый момент с буфером работает только один процесс.
 *
 * Имена объектов: "Local\os_lab3_<PID родителя>_<канал>[_empty|_full]". PID у работающих процессов
 * разный, поэтому две пары программ, запущенные одновременно, не мешают друг другу.
 *
 * Ожидание семафора - вместе с "сторожевыми" объектами (watch): описатели процессов и событие остановки.
 * Если сторожевой объект сработал раньше семафора (процесс на другой стороне завершился или родитель
 * попросил остановиться) - функция возвращает CHANNEL_STOPPED, а не ждет вечно.
 *
 * Функции в заголовке объявлены static inline: каждая программа получает свою копию,
 * а неиспользованные функции не вызывают предупреждений компилятора.
 */
#ifndef CHANNEL_H
#define CHANNEL_H

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Размер порции: сколько байт строки передается за один раз */
#define CHUNK_SIZE 4096

/* Наибольшая длина имени объекта ("Local\os_lab3_4294967295_middle_empty" и т.п. короче) */
#define NAME_SIZE 128

/* Наибольшее число сторожевых объектов в одном ожидании (WaitForMultipleObjects - до 64) */
#define MAX_WATCH 8

/* Статус-коды функций канала */
typedef enum {
    CHANNEL_OK,
    CHANNEL_INVALID_ARGS,   /* NULL или неправильные данные */
    CHANNEL_SYSTEM_ERROR,   /* системный вызов не удался (код - GetLastError, сообщение уже напечатано) */
    CHANNEL_STOPPED         /* сработал сторожевой объект: другой процесс завершился или просит остановиться */
} channel_status;

/* То, что лежит в разделяемой памяти */
typedef struct {
    DWORD length;            /* сколько байт в data */
    LONG end;                /* 1 - данных больше не будет */
    char data[CHUNK_SIZE];
} channel_buffer;

/* Канал в одном процессе: описатели объектов и адрес буфера в памяти этого процесса */
typedef struct {
    HANDLE mapping;          /* объект разделяемой памяти */
    channel_buffer *buffer;  /* где эта память видна в нашем процессе (MapViewOfFile) */
    HANDLE empty;            /* семафор "буфер свободен" */
    HANDLE full;             /* семафор "в буфере порция" */
} channel;

/* Сообщение об ошибке системного вызова: кто (имя программы), что не получилось и код ошибки */
static inline void channel_print_error(const char *who, const char *what) {
    fprintf(stderr, "Error: %s: %s failed (Windows error %lu)\n", who, what, (unsigned long)GetLastError());
}

/* Полное имя объекта: "Local\<prefix>_<name><suffix>". Возвращает 0 - успех, 1 - имя не поместилось */
static inline int channel_make_name(char *out, const char *prefix, const char *name, const char *suffix) {
    int written = snprintf(out, NAME_SIZE, "Local\\%s_%s%s", prefix, name, suffix);
    return (written < 0 || written >= NAME_SIZE) ? 1 : 0;
}

/* Пустой канал: все описатели NULL (такой можно безопасно закрывать) */
static inline void channel_init(channel *ch) {
    ch->mapping = NULL;
    ch->buffer = NULL;
    ch->empty = NULL;
    ch->full = NULL;
}

/* Закрыть канал: отменить отображение памяти и закрыть описатели.
   Объект ОС исчезает, когда закроют его последний описатель (в последнем процессе) */
static inline void channel_close(channel *ch) {
    if (ch->buffer != NULL) {
        UnmapViewOfFile(ch->buffer);
        ch->buffer = NULL;
    }
    if (ch->mapping != NULL) {
        CloseHandle(ch->mapping);
        ch->mapping = NULL;
    }
    if (ch->empty != NULL) {
        CloseHandle(ch->empty);
        ch->empty = NULL;
    }
    if (ch->full != NULL) {
        CloseHandle(ch->full);
        ch->full = NULL;
    }
}

/*
 * Создать канал (это делает родитель): разделяемая память нужного размера и два семафора.
 * who - имя программы для сообщений об ошибках.
 */
static inline channel_status channel_create(channel *ch, const char *prefix, const char *name, const char *who) {
    char full_name[NAME_SIZE];
    if (ch == NULL || prefix == NULL || name == NULL || who == NULL) {
        return CHANNEL_INVALID_ARGS;
    }
    channel_init(ch);
    if (channel_make_name(full_name, prefix, name, "") != 0) {
        return CHANNEL_INVALID_ARGS;
    }
    /* INVALID_HANDLE_VALUE вместо файла - память в оперативной памяти (файл подкачки), а не на диске;
       PAGE_READWRITE - читать и писать; 0, sizeof - размер (старшие и младшие 32 бита) */
    ch->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(channel_buffer), full_name);
    if (ch->mapping == NULL) {
        channel_print_error(who, "CreateFileMapping");
        return CHANNEL_SYSTEM_ERROR;
    }
    /* объект с таким именем уже был - значит имя занято чужой программой, работать с ним нельзя */
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Error: %s: shared memory '%s' already exists\n", who, full_name);
        channel_close(ch);
        return CHANNEL_SYSTEM_ERROR;
    }
    /* MapViewOfFile - сделать эту память видимой в нашем процессе; 0, 0, 0 - вся память с начала */
    ch->buffer = (channel_buffer *)MapViewOfFile(ch->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (ch->buffer == NULL) {
        channel_print_error(who, "MapViewOfFile");
        channel_close(ch);
        return CHANNEL_SYSTEM_ERROR;
    }
    ch->buffer->length = 0;
    ch->buffer->end = 0;
    /* семафоры: empty = 1 (буфер свободен), full = 0 (данных нет); наибольшее значение - 1 */
    if (channel_make_name(full_name, prefix, name, "_empty") != 0) {
        channel_close(ch);
        return CHANNEL_INVALID_ARGS;
    }
    ch->empty = CreateSemaphoreA(NULL, 1, 1, full_name);
    if (ch->empty == NULL) {
        channel_print_error(who, "CreateSemaphore");
        channel_close(ch);
        return CHANNEL_SYSTEM_ERROR;
    }
    if (channel_make_name(full_name, prefix, name, "_full") != 0) {
        channel_close(ch);
        return CHANNEL_INVALID_ARGS;
    }
    ch->full = CreateSemaphoreA(NULL, 0, 1, full_name);
    if (ch->full == NULL) {
        channel_print_error(who, "CreateSemaphore");
        channel_close(ch);
        return CHANNEL_SYSTEM_ERROR;
    }
    return CHANNEL_OK;
}

/* Открыть канал, созданный родителем (это делают дети): по тем же именам */
static inline channel_status channel_open(channel *ch, const char *prefix, const char *name, const char *who) {
    char full_name[NAME_SIZE];
    if (ch == NULL || prefix == NULL || name == NULL || who == NULL) {
        return CHANNEL_INVALID_ARGS;
    }
    channel_init(ch);
    if (channel_make_name(full_name, prefix, name, "") != 0) {
        return CHANNEL_INVALID_ARGS;
    }
    ch->mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, full_name);
    if (ch->mapping == NULL) {
        channel_print_error(who, "OpenFileMapping");
        return CHANNEL_SYSTEM_ERROR;
    }
    ch->buffer = (channel_buffer *)MapViewOfFile(ch->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (ch->buffer == NULL) {
        channel_print_error(who, "MapViewOfFile");
        channel_close(ch);
        return CHANNEL_SYSTEM_ERROR;
    }
    /* SEMAPHORE_ALL_ACCESS - и ждать (WaitFor...), и отпускать (ReleaseSemaphore) */
    if (channel_make_name(full_name, prefix, name, "_empty") != 0) {
        channel_close(ch);
        return CHANNEL_INVALID_ARGS;
    }
    ch->empty = OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, full_name);
    if (ch->empty == NULL) {
        channel_print_error(who, "OpenSemaphore");
        channel_close(ch);
        return CHANNEL_SYSTEM_ERROR;
    }
    if (channel_make_name(full_name, prefix, name, "_full") != 0) {
        channel_close(ch);
        return CHANNEL_INVALID_ARGS;
    }
    ch->full = OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, full_name);
    if (ch->full == NULL) {
        channel_print_error(who, "OpenSemaphore");
        channel_close(ch);
        return CHANNEL_SYSTEM_ERROR;
    }
    return CHANNEL_OK;
}

/*
 * Дождаться семафора или любого сторожевого объекта.
 * WaitForMultipleObjects с FALSE - вернуться, когда сработал хоть один; WAIT_OBJECT_0 + i - какой.
 * Семафор стоит первым: если сработали одновременно, побеждает он (данные не теряются).
 */
static inline channel_status channel_wait(HANDLE semaphore, const HANDLE *watch, DWORD watch_count, const char *who) {
    HANDLE handles[MAX_WATCH + 1];
    if (semaphore == NULL || watch_count > MAX_WATCH || (watch == NULL && watch_count > 0) || who == NULL) {
        return CHANNEL_INVALID_ARGS;
    }
    handles[0] = semaphore;
    for (DWORD i = 0; i < watch_count; i++) {
        handles[i + 1] = watch[i];
    }
    DWORD result = WaitForMultipleObjects(watch_count + 1, handles, FALSE, INFINITE);
    if (result == WAIT_OBJECT_0) {
        return CHANNEL_OK;
    }
    if (result > WAIT_OBJECT_0 && result <= WAIT_OBJECT_0 + watch_count) {
        return CHANNEL_STOPPED;
    }
    channel_print_error(who, "WaitForMultipleObjects");
    return CHANNEL_SYSTEM_ERROR;
}

/* Отправить порцию (length байт из data, end = 1 - конец данных): ждем empty, пишем, отпускаем full */
static inline channel_status channel_send(channel *ch, const char *data, DWORD length, LONG end,
                                          const HANDLE *watch, DWORD watch_count, const char *who) {
    if (ch == NULL || ch->buffer == NULL || length > CHUNK_SIZE || (data == NULL && length > 0)) {
        return CHANNEL_INVALID_ARGS;
    }
    channel_status st = channel_wait(ch->empty, watch, watch_count, who);
    if (st != CHANNEL_OK) {
        return st;
    }
    if (length > 0) {
        memcpy(ch->buffer->data, data, length);
    }
    ch->buffer->length = length;
    ch->buffer->end = end;
    /* ReleaseSemaphore(семафор, на сколько увеличить, куда записать старое значение) */
    if (!ReleaseSemaphore(ch->full, 1, NULL)) {
        channel_print_error(who, "ReleaseSemaphore");
        return CHANNEL_SYSTEM_ERROR;
    }
    return CHANNEL_OK;
}

/* Принять порцию в data (CHUNK_SIZE байт места): ждем full, читаем, отпускаем empty */
static inline channel_status channel_receive(channel *ch, char *data, DWORD *length, LONG *end,
                                             const HANDLE *watch, DWORD watch_count, const char *who) {
    if (ch == NULL || ch->buffer == NULL || data == NULL || length == NULL || end == NULL) {
        return CHANNEL_INVALID_ARGS;
    }
    channel_status st = channel_wait(ch->full, watch, watch_count, who);
    if (st != CHANNEL_OK) {
        return st;
    }
    DWORD n = ch->buffer->length;
    if (n > CHUNK_SIZE) {
        n = CHUNK_SIZE;   /* защита: в общей памяти могла оказаться чепуха */
    }
    memcpy(data, ch->buffer->data, n);
    *length = n;
    *end = ch->buffer->end;
    if (!ReleaseSemaphore(ch->empty, 1, NULL)) {
        channel_print_error(who, "ReleaseSemaphore");
        return CHANNEL_SYSTEM_ERROR;
    }
    return CHANNEL_OK;
}

#endif
