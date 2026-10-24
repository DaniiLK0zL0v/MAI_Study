/*
 * parent.c — Родительский процесс (Вариант 11, Группа 3, Windows)
 *
 * Схема взаимодействия:
 *
 *   User (ввод) --> [parent] --pipe1--> [child1] --pipe_mid--> [child2] --pipe2--> [parent] --> User (вывод)
 *
 * parent: принимает строки от пользователя, отправляет в child1 через pipe1,
 *         получает результат от child2 через pipe2 и выводит на экран.
 * child1: переводит строки в верхний регистр.
 * child2: заменяет все пробельные символы на символ «_».
 *
 * Используемые системные вызовы Windows:
 *   CreatePipe, CreateProcess, SetHandleInformation,
 *   WriteFile, ReadFile, WaitForSingleObject, CloseHandle
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define BUF_SIZE 4096

/* Вспомогательная функция: вывод сообщения об ошибке Windows API */
static void PrintError(const char *msg) {
    fprintf(stderr, "Ошибка: %s (код: %lu)\n", msg, GetLastError());
}

int main(void) {
    /* === Дескрипторы каналов === */
    HANDLE hPipe1Read,   hPipe1Write;    /* pipe1:    parent -> child1       */
    HANDLE hPipeMidRead, hPipeMidWrite;  /* pipe_mid: child1 -> child2       */
    HANDLE hPipe2Read,   hPipe2Write;    /* pipe2:    child2 -> parent       */

    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA        si1, si2;
    PROCESS_INFORMATION pi1, pi2;

    char buf[BUF_SIZE];
    DWORD dwWritten, dwRead;

    /* Атрибуты безопасности: разрешаем наследование дескрипторов */
    sa.nLength              = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle       = TRUE;
    sa.lpSecurityDescriptor = NULL;

    /* ============================================================
     * 1. Создание трёх каналов (pipe)
     * ============================================================ */
    if (!CreatePipe(&hPipe1Read, &hPipe1Write, &sa, 0)) {
        PrintError("CreatePipe (pipe1)");
        return 1;
    }
    if (!CreatePipe(&hPipeMidRead, &hPipeMidWrite, &sa, 0)) {
        PrintError("CreatePipe (pipe_mid)");
        return 1;
    }
    if (!CreatePipe(&hPipe2Read, &hPipe2Write, &sa, 0)) {
        PrintError("CreatePipe (pipe2)");
        return 1;
    }

    /* ============================================================
     * 2. Создание child1
     *    stdin  = hPipe1Read      (читает от parent)
     *    stdout = hPipeMidWrite   (пишет в child2)
     *
     *    Перед созданием child1 делаем НЕнаследуемыми дескрипторы,
     *    которые child1 НЕ должен получить.
     * ============================================================ */

    /* Parent оставляет себе: hPipe1Write (запись) и hPipe2Read (чтение) */
    SetHandleInformation(hPipe1Write,   HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hPipe2Read,    HANDLE_FLAG_INHERIT, 0);
    /* child1 НЕ должен наследовать pipe_mid чтение и pipe2 запись */
    SetHandleInformation(hPipeMidRead,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hPipe2Write,   HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si1, sizeof(si1));
    si1.cb         = sizeof(STARTUPINFOA);
    si1.hStdInput  = hPipe1Read;       /* stdin  <- pipe1 */
    si1.hStdOutput = hPipeMidWrite;    /* stdout -> pipe_mid */
    si1.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si1.dwFlags   |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi1, sizeof(pi1));

    if (!CreateProcessA(NULL, "child1.exe", NULL, NULL, TRUE,
                        0, NULL, NULL, &si1, &pi1)) {
        PrintError("CreateProcess (child1.exe)");
        return 1;
    }
    printf("[parent] child1 (PID %lu) создан.\n", pi1.dwProcessId);

    /* ============================================================
     * 3. Создание child2
     *    stdin  = hPipeMidRead   (читает от child1)
     *    stdout = hPipe2Write    (пишет в parent)
     *
     *    Возвращаем наследуемость нужным дескрипторам
     *    и убираем у ненужных.
     * ============================================================ */

    /* child1 уже создан, убираем наследуемость его дескрипторов */
    SetHandleInformation(hPipe1Read,    HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hPipeMidWrite, HANDLE_FLAG_INHERIT, 0);
    /* child2 должен наследовать: pipe_mid чтение и pipe2 запись */
    SetHandleInformation(hPipeMidRead,  HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    SetHandleInformation(hPipe2Write,   HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    ZeroMemory(&si2, sizeof(si2));
    si2.cb         = sizeof(STARTUPINFOA);
    si2.hStdInput  = hPipeMidRead;     /* stdin  <- pipe_mid */
    si2.hStdOutput = hPipe2Write;      /* stdout -> pipe2 */
    si2.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si2.dwFlags   |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi2, sizeof(pi2));

    if (!CreateProcessA(NULL, "child2.exe", NULL, NULL, TRUE,
                        0, NULL, NULL, &si2, &pi2)) {
        PrintError("CreateProcess (child2.exe)");
        return 1;
    }
    printf("[parent] child2 (PID %lu) создан.\n", pi2.dwProcessId);

    /* ============================================================
     * 4. Закрытие дескрипторов, не нужных parent'у
     *    Parent использует только: hPipe1Write (запись) и hPipe2Read (чтение)
     * ============================================================ */
    CloseHandle(hPipe1Read);
    CloseHandle(hPipeMidRead);
    CloseHandle(hPipeMidWrite);
    CloseHandle(hPipe2Write);

    /* ============================================================
     * 5. Основной цикл: ввод пользователя -> pipe1 -> ... -> pipe2 -> вывод
     * ============================================================ */
    printf("[parent] Введите строки (пустая строка или Ctrl+Z для выхода):\n");
    printf("-----------------------------------------------------------\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        /* Читаем строку от пользователя */
        if (fgets(buf, BUF_SIZE, stdin) == NULL) {
            /* EOF (Ctrl+Z в Windows) */
            break;
        }

        /* Пустая строка — завершение */
        if (buf[0] == '\n' || (buf[0] == '\r' && buf[1] == '\n')) {
            break;
        }

        /* Отправляем строку в pipe1 (child1 прочитает её из stdin) */
        DWORD len = (DWORD)strlen(buf);
        if (!WriteFile(hPipe1Write, buf, len, &dwWritten, NULL)) {
            PrintError("WriteFile (pipe1)");
            break;
        }

        /* Читаем результат из pipe2 (child2 записал его в stdout) */
        /* Читаем посимвольно до '\n', чтобы получить ровно одну строку */
        {
            int pos = 0;
            char ch;
            while (pos < BUF_SIZE - 1) {
                if (!ReadFile(hPipe2Read, &ch, 1, &dwRead, NULL) || dwRead == 0) {
                    /* pipe закрыт — дочерний процесс завершился */
                    buf[pos] = '\0';
                    if (pos > 0) {
                        printf("Результат: %s\n", buf);
                    }
                    printf("[parent] Дочерний процесс завершился.\n");
                    goto cleanup;
                }
                buf[pos++] = ch;
                if (ch == '\n') {
                    break;
                }
            }
            buf[pos] = '\0';
            printf("Результат: %s", buf);
            /* Добавляем перевод строки, если его нет */
            if (pos > 0 && buf[pos - 1] != '\n') {
                printf("\n");
            }
        }
    }

cleanup:
    /* ============================================================
     * 6. Завершение: закрываем каналы и ждём дочерние процессы
     * ============================================================ */
    CloseHandle(hPipe1Write);
    CloseHandle(hPipe2Read);

    printf("[parent] Ожидание завершения дочерних процессов...\n");
    WaitForSingleObject(pi1.hProcess, 5000);  /* таймаут 5 сек */
    WaitForSingleObject(pi2.hProcess, 5000);

    /* Получаем коды завершения */
    {
        DWORD exitCode1 = 0, exitCode2 = 0;
        GetExitCodeProcess(pi1.hProcess, &exitCode1);
        GetExitCodeProcess(pi2.hProcess, &exitCode2);
        printf("[parent] child1 завершён (код: %lu)\n", exitCode1);
        printf("[parent] child2 завершён (код: %lu)\n", exitCode2);
    }

    CloseHandle(pi1.hProcess);
    CloseHandle(pi1.hThread);
    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);

    printf("[parent] Родительский процесс завершён.\n");
    return 0;
}
