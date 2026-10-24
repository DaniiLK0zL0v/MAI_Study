/*
 * child1.c — Дочерний процесс №1 (Вариант 11)
 * 
 * Читает строки из stdin (перенаправлен из pipe1),
 * переводит каждый символ в верхний регистр,
 * и отправляет результат в stdout (перенаправлен в pipe_mid -> child2).
 */

#include <stdio.h>
#include <ctype.h>

#define BUF_SIZE 4096

int main(void) {
    char buf[BUF_SIZE];

    /* Читаем строки до EOF (пока parent не закроет pipe1) */
    while (fgets(buf, BUF_SIZE, stdin) != NULL) {
        int i;
        for (i = 0; buf[i] != '\0'; i++) {
            buf[i] = (char)toupper((unsigned char)buf[i]);
        }
        fputs(buf, stdout);
        fflush(stdout);  /* Обязательно сбрасываем буфер для корректной работы pipe */
    }

    return 0;
}
