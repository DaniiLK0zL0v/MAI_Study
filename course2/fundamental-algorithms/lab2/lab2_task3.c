/*
 * Лаб 2, задание 3.
 * Функция с переменным числом аргументов: поиск подстроки во всех файлах.
 * Без strstr / strncmp.
 * Вывод: файл, номер строки (с 1), позиция символа в строке.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

typedef enum {
    OK,
    INVALID_ARGS,
    FILE_ERROR,
    ALLOC_ERROR
} status_code;

/* Длина строки без string.h */
static size_t my_strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

/* Наивный поиск подстроки в тексте файла.
 * Подстрока может содержать '\n' и любые символы.
 * Для каждого вхождения вычисляем номер строки и позицию в строке. */
static status_code search_in_file(const char *filepath, const char *substr, size_t sub_len) {
    FILE *f = fopen(filepath, "r");
    if (f == NULL) return FILE_ERROR;

    /* Читаем весь файл в буфер */
    size_t capacity = 4096;
    size_t size = 0;
    char *buf = (char *)malloc(capacity);
    if (buf == NULL) {
        fclose(f);
        return ALLOC_ERROR;
    }

    int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (size + 1 >= capacity) {
            capacity *= 2;
            char *tmp = (char *)realloc(buf, capacity);
            if (tmp == NULL) {
                free(buf);
                fclose(f);
                return ALLOC_ERROR;
            }
            buf = tmp;
        }
        buf[size++] = (char)ch;
    }
    fclose(f);
    buf[size] = '\0';

    if (sub_len == 0 || size < sub_len) {
        free(buf);
        return OK;
    }

    printf("File: %s\n", filepath);
    int found_any = 0;

    /* Построим массив «номер строки» и «позиция в строке» для каждого символа */
    for (size_t i = 0; i <= size - sub_len; i++) {
        /* Проверяем совпадение подстроки */
        int match = 1;
        for (size_t j = 0; j < sub_len; j++) {
            if (buf[i + j] != substr[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            /* Вычислим строку и позицию для позиции i */
            int line = 1;
            int col = 1;
            for (size_t k = 0; k < i; k++) {
                if (buf[k] == '\n') {
                    line++;
                    col = 1;
                } else {
                    col++;
                }
            }
            printf("  Line %d, Position %d\n", line, col);
            found_any = 1;
        }
    }

    if (!found_any) {
        printf("  (no occurrences found)\n");
    }

    free(buf);
    return OK;
}

/* Функция с переменным числом аргументов.
 * Аргументы: подстрока, количество файлов, пути к файлам */
status_code find_substring(const char *substr, int file_count, ...) {
    if (substr == NULL || file_count <= 0) {
        return INVALID_ARGS;
    }

    size_t sub_len = my_strlen(substr);

    va_list args;
    va_start(args, file_count);

    for (int i = 0; i < file_count; i++) {
        const char *filepath = va_arg(args, const char *);
        if (filepath == NULL) {
            va_end(args);
            return INVALID_ARGS;
        }
        status_code sc = search_in_file(filepath, substr, sub_len);
        if (sc != OK) {
            fprintf(stderr, "Warning: error processing file '%s' (%d)\n", filepath, sc);
        }
    }

    va_end(args);
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <substring> <file1> [file2] ...\n",
                argc > 0 ? argv[0] : "lab2_task3");
        return 1;
    }

    const char *substr = argv[1];
    int file_count = argc - 2;

    /* Создаём va_list вручную невозможно, поэтому вызываем search_in_file напрямую */
    size_t sub_len = my_strlen(substr);
    for (int i = 0; i < file_count; i++) {
        status_code sc = search_in_file(argv[2 + i], substr, sub_len);
        if (sc != OK) {
            fprintf(stderr, "Error processing file '%s' (%d)\n", argv[2 + i], sc);
        }
    }

    /* Также демонстрируем работу функции find_substring с var args */
    if (file_count >= 1) {
        printf("\n--- Demo via find_substring() ---\n");
        /* Для демонстрации вызовем с первым файлом */
        find_substring(substr, 1, argv[2]);
    }

    return 0;
}
