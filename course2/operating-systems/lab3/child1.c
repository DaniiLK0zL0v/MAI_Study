/*
 * Лабораторная 3 по ОС, вариант 11 (группа вариантов 3), Windows.
 * child1 - первый дочерний процесс: переводит латинские буквы в верхний регистр.
 *
 * Запускает его parent с аргументами:  префикс входной_канал выходной_канал PID_parent
 * (для child1 входной канал - first, выходной - middle). Каналы - разделяемая память и семафоры
 * (см. channel.h); child1 открывает их по именам.
 *
 * Работа: принять порцию из входного канала -> поменять буквы -> отправить в выходной канал.
 * Порция с флагом end - конец данных: переслать ее дальше и завершиться.
 * Каждое ожидание сторожат событие остановки и процесс parent: если parent попросил остановиться
 * или завершился, child1 тоже завершается (а не ждет вечно).
 */

#include "channel.h"
#include <ctype.h>
#include <stdlib.h>

/* Имя программы для сообщений */
#define WHO "child1"

/* Преобразование одной порции: toupper меняет только латинские буквы a-z */
static void transform(char *data, DWORD length) {
    for (DWORD i = 0; i < length; i++) {
        data[i] = (char)toupper((unsigned char)data[i]);
    }
}

int main(int argc, char *argv[]) {
    /* аргументы задает parent; запуск руками без них - ошибка */
    if (argc != 5) {
        fprintf(stderr, "Usage: " WHO " <prefix> <input channel> <output channel> <parent pid>  (started by parent)\n");
        return 1;
    }
    const char *prefix = argv[1];
    char *end_ptr = NULL;
    unsigned long parent_pid = strtoul(argv[4], &end_ptr, 10);
    if (argv[4][0] == '\0' || *end_ptr != '\0') {
        fprintf(stderr, "Error: " WHO ": wrong parent pid '%s'\n", argv[4]);
        return 1;
    }

    int result = 0;
    channel input;
    channel output;
    channel_init(&input);
    channel_init(&output);
    HANDLE watch[2] = {NULL, NULL};   /* событие остановки и процесс parent */
    char *buf = NULL;

    /* сторожа: событие остановки (по имени) и процесс parent (по PID; право SYNCHRONIZE - чтобы ждать) */
    char stop_name[NAME_SIZE];
    if (channel_make_name(stop_name, prefix, "stop", "") != 0) {
        fprintf(stderr, "Error: " WHO ": the prefix is too long\n");
        result = 1;
    } else if ((watch[0] = OpenEventA(SYNCHRONIZE, FALSE, stop_name)) == NULL) {
        channel_print_error(WHO, "OpenEvent");
        result = 1;
    } else if ((watch[1] = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)parent_pid)) == NULL) {
        channel_print_error(WHO, "OpenProcess");
        result = 1;
    } else if (channel_open(&input, prefix, argv[2], WHO) != CHANNEL_OK
               || channel_open(&output, prefix, argv[3], WHO) != CHANNEL_OK) {
        result = 1;   /* сообщение уже напечатано */
    } else if ((buf = (char *)malloc(CHUNK_SIZE)) == NULL) {
        fprintf(stderr, "Error: " WHO ": not enough memory\n");
        result = 1;
    }

    /* главный цикл: принять, преобразовать, отправить; до порции end */
    while (result == 0) {
        DWORD length = 0;
        LONG end = 0;
        channel_status st = channel_receive(&input, buf, &length, &end, watch, 2, WHO);
        if (st == CHANNEL_OK) {
            transform(buf, length);
            st = channel_send(&output, buf, length, end, watch, 2, WHO);
        }
        if (st == CHANNEL_STOPPED) {
            result = 1;   /* parent остановил работу или завершился - он сам сообщил о причине */
        } else if (st != CHANNEL_OK) {
            result = 1;   /* сообщение о системной ошибке уже напечатано */
        } else if (end) {
            break;        /* конец данных переслан дальше - работа закончена */
        }
    }

    free(buf);
    channel_close(&input);
    channel_close(&output);
    for (int i = 0; i < 2; i++) {
        if (watch[i] != NULL) {
            CloseHandle(watch[i]);
        }
    }
    return result;
}
