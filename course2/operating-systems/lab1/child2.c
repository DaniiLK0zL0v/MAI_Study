/*
 * child2.c — Дочерний процесс №2 (Вариант 11)
 * 
 * Читает строки из stdin (перенаправлен из pipe_mid от child1),
 * заменяет все пробельные символы (пробел, табуляция и т.д.) на '_',
 * и отправляет результат в stdout (перенаправлен в pipe2 -> parent).
 */

#include <stdio.h>
#include <ctype.h>

#define BUF_SIZE 4096

int main(void) {
    char buf[BUF_SIZE];

    /* Читаем строки до EOF (пока child1 не завершится и pipe_mid не закроется) */
    while (fgets(buf, BUF_SIZE, stdin) != NULL) {
        int i;
        for (i = 0; buf[i] != '\0'; i++) {
            /* isspace проверяет: ' ', '\t', '\n', '\r', '\v', '\f' */
            if (buf[i] != '\n' && isspace((unsigned char)buf[i])) {
                buf[i] = '_';
            }
        }
        fputs(buf, stdout);
        fflush(stdout);  /* Обязательно сбрасываем буфер для корректной работы pipe */
    }

    return 0;
}
