/*
 * Лабораторная работа 1 по ОС, вариант 11 (группа вариантов 3), Windows.
 * child2 - второй дочерний процесс: заменяет все пробельные символы на '_'.
 *
 * parent запускает child2 так, что его стандартный ввод - канал pipe_mid (от child1),
 * а стандартный вывод - канал pipe2 (к parent). Сам child2 о каналах ничего не знает:
 * он просто читает свой stdin и пишет в свой stdout, пока ввод не закончится.
 *
 * Пробельные символы - те, для которых isspace() истинна: пробел, '\t', '\r', '\v', '\f' и '\n'.
 * '\n' не заменяем: это граница между строками, без неё все строки слились бы в одну.
 *
 * Данные читаем и пишем порциями через ReadFile/WriteFile, а не построчно
 * (почему - см. комментарий в начале child1.c).
 */

#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

/* Размер порции для чтения и записи */
#define BUF_SIZE 4096

/* Сообщение об ошибке системного вызова: что не получилось и код ошибки Windows */
static void print_system_error(const char *what) {
    fprintf(stderr, "Error: child2: %s failed (Windows error %lu)\n", what, (unsigned long)GetLastError());
}

/*
 * Записать ровно size байт (WriteFile может записать меньше - тогда дописываем остаток).
 * Возвращает 0 - успех, 1 - ошибка (сообщение уже напечатано).
 */
static int write_all(HANDLE output, const char *data, DWORD size) {
    DWORD done = 0;
    while (done < size) {
        DWORD written = 0;
        if (!WriteFile(output, data + done, size - done, &written, NULL)) {
            print_system_error("WriteFile");
            return 1;
        }
        done += written;
    }
    return 0;
}

int main(void) {
    /* описатели стандартного ввода и вывода - концы каналов, которые дал parent */
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
        print_system_error("GetStdHandle");
        return 1;
    }

    /* буфер для одной порции - в куче (malloc), а не на стеке */
    char *buf = malloc(BUF_SIZE);
    if (buf == NULL) {
        fprintf(stderr, "Error: child2: not enough memory\n");
        return 1;
    }

    /* result: 0 - всё хорошо, 1 - была ошибка; цикл идёт, пока не кончится ввод или не случится ошибка */
    int result = 0;
    while (result == 0) {
        /* ReadFile ждёт, пока в канале появятся данные, и возвращает столько, сколько есть */
        DWORD received = 0;
        if (!ReadFile(input, buf, BUF_SIZE, &received, NULL)) {
            /* ERROR_BROKEN_PIPE - child1 завершился и pipe_mid закрыт: ввод закончился, это не ошибка */
            if (GetLastError() != ERROR_BROKEN_PIPE) {
                print_system_error("ReadFile");
                result = 1;
            }
            break;
        }
        /* 0 байт - тоже конец ввода (так бывает, когда ввод - файл) */
        if (received == 0) {
            break;
        }

        /* каждый пробельный символ, кроме '\n', -> '_' */
        for (DWORD i = 0; i < received; i++) {
            if (buf[i] != '\n' && isspace((unsigned char)buf[i])) {
                buf[i] = '_';
            }
        }

        /* результат - в pipe2, его читает parent */
        result = write_all(output, buf, received);
    }

    free(buf);
    return result;
}
