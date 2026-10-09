# Лабораторная работа № 1 по курсу «Фундаментальные алгоритмы»

М8О-213БВ-25

Козлов Даниил Ильич

Язык C (стандарт C99). Целевая среда проверки - Linux, gcc, valgrind.

## Состав

| Задание | Исходный код | Тесты | Входные данные (`tests_data/`) |
|---|---|---|---|
| 1 | `lab1_task1.c` | `tests_task1.txt` | - (аргументы командной строки) |
| 2 | `lab1_task2.c` | `tests_task2.txt` | - |
| 3 | `lab1_task3.c` | `tests_task3.txt` | - |
| 4 | `lab1_task4.c` | `tests_task4.txt` | `task4_in.txt`, `task4_noeol.txt`, `task4_empty.txt` |
| 5 | `lab1_task5.c` | `tests_task5.txt` | - |
| 6 | `lab1_task6.c` | `tests_task6.txt` | - (демонстрация без аргументов) |
| 7 | `lab1_task7.c` | `tests_task7.txt` | `task7_f1.txt`, `task7_f2.txt`, `task7_in.txt`, `task7_empty.txt` |
| 8 | `lab1_task8.c` | `tests_task8.txt` | `task8_in.txt`, `task8_big.txt`, `task8_bad.txt`, `task8_empty.txt` |
| 9 | `lab1_task9.c` | `tests_task9.txt` | - |
| 10 | `lab1_task10.c` | `tests_task10.txt` | - (стандартный ввод, строки `input:` в файле тестов) |

Каждое задание - один самостоятельный файл `.c`. Динамическая память используется в заданиях 2, 4, 8, 9.

## Сборка и проверка

```sh
make                              # gcc -std=c99 -Wall -Wextra -Wpedantic -g ... -lm
make test                         # все тесты
make memcheck                     # все тесты под valgrind
bash run_tests.sh 4               # тесты одного задания
bash run_tests.sh --memcheck 4    # то же под valgrind
make clean
```

Файл тестов: первая значимая строка `program: <имя>`, далее блоки, разделенные пустой строкой.
Поля блока: `args` (аргументы; аргумент с пробелами - в кавычках), `input` (строка стандартного ввода),
`out` / `outre` (строка stdout; `outre` - регулярное выражение POSIX ERE), `err` (строка stderr), `code` (код возврата),
`file` / `fout` (файл, который должна создать программа, и его строки). Строки сравниваются целиком, без учета пробелов в конце.

## Проверка утечек и ошибок памяти

Для заданий с динамическим выделением памяти требуется проверка valgrind (Linux) или Dr. Memory (Windows).

**Linux.** `make memcheck` запускает каждый тест из `tests_task*.txt` (включая ветви с ошибочным вводом, стандартный ввод задания 10
и выходные файлы заданий 4, 7, 8) под

```sh
valgrind -q --leak-check=full --show-leak-kinds=definite,indirect,possible \
         --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=99
```

Тест считается непройденным, если valgrind завершился с кодом 99 (ошибка доступа к памяти или утечка); в конце выводится итог по каждому заданию.

**Windows.** Dr. Memory 2.6.0, `drmemory -batch -suppress drmemory_startup.supp -- <программа> <аргументы>`.
Задания 2, 4, 8, 9: 0 ошибок, 0 утечек (`leaks` и `possible leaks`). Подавлено одно срабатывание `UNADDRESSABLE ACCESS`,
возникающее до вызова `main`:

```
___chkstk_ms                 compiler_rt (zig), stack_probe.zig
_pei386_runtime_relocator    mingw-w64 crt, pseudo-reloc.c
__tmainCRTStartup
mainCRTStartup
```

`___chkstk_ms` - проба стека: перед выделением большого кадра она читает страницы ниже текущей вершины стека, что Dr. Memory
принимает за обращение за пределы стека. Около 14 КБ, которые Dr. Memory относит к `still-reachable`, выделяет C-рантайм:
программа из одного `printf` дает 14 093 байта, задания - от 14 137 до 14 383 байт.

## Сборка на Windows: zig вместо MinGW

На Windows программы собирались командой `zig cc` (zig 0.16.0):

- `zig cc` - драйвер clang 21.1.0 (LLVM), а не GCC из дистрибутива MinGW;
- при цели по умолчанию `x86_64-windows-gnu` от mingw-w64 берутся только заголовки и стартовый код;
  исполняемые файлы импортируют Universal CRT (`api-ms-win-crt-*.dll`), `msvcrt.dll` не импортируется,
  `__USE_MINGW_ANSI_STDIO` равно 0, то есть `printf` - из UCRT, который поддерживает форматы C99 (`%lld`, `%zu`).
  Известные проблемы связки MinGW + `msvcrt.dll` (форматы `%lld`/`%zu`, `long double`) к этим сборкам не относятся;
  `long double` в коде не используется;
- код также компилируется для `x86_64-linux-gnu`
  (`zig cc -target x86_64-linux-gnu -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow`) без предупреждений.

Чтобы поведение не зависело от платформы, целые значения из командной строки хранятся в `long long`
(`long` на Windows занимает 4 байта, на Linux - 8), а псевдослучайные числа в задании 9 дает собственный генератор,
а не `rand()`, у которого разный диапазон в разных библиотеках и скрытое глобальное состояние.

Локально gcc и valgrind не запускались. Сборку gcc с `-Werror`, тесты и проверку valgrind на Ubuntu выполняет
GitHub Actions (`.github/workflows/check.yml`); результат - во вкладке Actions репозитория.
