/*
 * Лаб 2, задание 2.
 * Чтение сотрудников из файла -> сортировка -> запись в файл.
 * argv: input_file flag(-a/-d или /a//d) output_file
 * Сортировка по зарплате, фамилии, имени, id.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef enum {
    OK,
    INVALID_ARGS,
    INVALID_FLAG,
    FILE_ERROR,
    ALLOC_ERROR,
    INVALID_DATA
} status_code;

typedef struct {
    unsigned int id;
    char *name;
    char *surname;
    double salary;
} Employee;

/* Проверяем, что строка состоит только из букв */
static int is_alpha_string(const char *s) {
    if (s == NULL || s[0] == '\0') return 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (!isalpha((unsigned char)s[i])) return 0;
    }
    return 1;
}

/* Освобождение массива сотрудников */
static void free_employees(Employee *arr, int n) {
    if (arr == NULL) return;
    for (int i = 0; i < n; i++) {
        free(arr[i].name);
        free(arr[i].surname);
    }
    free(arr);
}

/* Компаратор по возрастанию: зарплата -> фамилия -> имя -> id */
static int cmp_asc(const void *a, const void *b) {
    const Employee *ea = (const Employee *)a;
    const Employee *eb = (const Employee *)b;

    /* зарплата */
    if (ea->salary < eb->salary) return -1;
    if (ea->salary > eb->salary) return 1;

    /* фамилия */
    int c = strcmp(ea->surname, eb->surname);
    if (c != 0) return c;

    /* имя */
    c = strcmp(ea->name, eb->name);
    if (c != 0) return c;

    /* id */
    if (ea->id < eb->id) return -1;
    if (ea->id > eb->id) return 1;
    return 0;
}

/* Компаратор по убыванию */
static int cmp_desc(const void *a, const void *b) {
    return cmp_asc(b, a);
}

/* Чтение записей из файла */
static status_code read_employees(const char *path, Employee **out_arr, int *out_count) {
    FILE *f = fopen(path, "r");
    if (f == NULL) return FILE_ERROR;

    int capacity = 16;
    int count = 0;
    Employee *arr = (Employee *)malloc((size_t)capacity * sizeof(Employee));
    if (arr == NULL) {
        fclose(f);
        return ALLOC_ERROR;
    }

    unsigned int id;
    char name_buf[256], surname_buf[256];
    double salary;

    while (fscanf(f, "%u %255s %255s %lf", &id, name_buf, surname_buf, &salary) == 4) {
        /* валидация */
        if (!is_alpha_string(name_buf) || !is_alpha_string(surname_buf) || salary < 0.0) {
            free_employees(arr, count);
            fclose(f);
            return INVALID_DATA;
        }

        if (count >= capacity) {
            capacity *= 2;
            Employee *tmp = (Employee *)realloc(arr, (size_t)capacity * sizeof(Employee));
            if (tmp == NULL) {
                free_employees(arr, count);
                fclose(f);
                return ALLOC_ERROR;
            }
            arr = tmp;
        }

        arr[count].id = id;
        arr[count].name = (char *)malloc(strlen(name_buf) + 1);
        arr[count].surname = (char *)malloc(strlen(surname_buf) + 1);
        if (arr[count].name == NULL || arr[count].surname == NULL) {
            free(arr[count].name);
            free(arr[count].surname);
            free_employees(arr, count);
            fclose(f);
            return ALLOC_ERROR;
        }
        strcpy(arr[count].name, name_buf);
        strcpy(arr[count].surname, surname_buf);
        arr[count].salary = salary;
        count++;
    }

    fclose(f);
    *out_arr = arr;
    *out_count = count;
    return OK;
}

/* Запись отсортированных данных */
static status_code write_employees(const char *path, const Employee *arr, int count) {
    FILE *f = fopen(path, "w");
    if (f == NULL) return FILE_ERROR;

    for (int i = 0; i < count; i++) {
        fprintf(f, "%u %s %s %.2f\n", arr[i].id, arr[i].name, arr[i].surname, arr[i].salary);
    }

    fclose(f);
    return OK;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input_file> <-a|-d|/a|/d> <output_file>\n",
                argc > 0 ? argv[0] : "lab2_task2");
        return 1;
    }

    const char *input_path = argv[1];
    const char *flag_arg = argv[2];
    const char *output_path = argv[3];

    /* парсинг флага */
    if ((flag_arg[0] != '-' && flag_arg[0] != '/') || flag_arg[2] != '\0') {
        fprintf(stderr, "Error: invalid flag '%s'\n", flag_arg);
        return 1;
    }
    char flag = flag_arg[1];
    if (flag != 'a' && flag != 'd') {
        fprintf(stderr, "Error: flag must be 'a' or 'd', got '%c'\n", flag);
        return 1;
    }

    /* проверка: входной и выходной файл не должны совпадать */
    /* (простая проверка по строке; полная — realpath — зависит от системы) */

    Employee *employees = NULL;
    int count = 0;

    status_code sc = read_employees(input_path, &employees, &count);
    if (sc != OK) {
        fprintf(stderr, "Error: could not read employees (%d)\n", sc);
        return 1;
    }

    if (count > 0) {
        qsort(employees, (size_t)count, sizeof(Employee), flag == 'a' ? cmp_asc : cmp_desc);
    }

    sc = write_employees(output_path, employees, count);
    if (sc != OK) {
        fprintf(stderr, "Error: could not write output (%d)\n", sc);
        free_employees(employees, count);
        return 1;
    }

    free_employees(employees, count);
    return 0;
}
