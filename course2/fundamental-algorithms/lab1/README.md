# Лабораторная работа № 1 по курсу «Фундаментальные алгоритмы»

М8О-213БВ-25

Козлов Даниил Ильич

Язык C (стандарт C99). Целевая среда проверки - Linux, gcc, valgrind.

## Состав

| Задание | Исходный код | Тесты (`tests/`) | Входные данные (`tests/data/`) |
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

Программы собираются в папку `build/`.

```sh
make                              # gcc -std=c99 -Wall -Wextra -Wpedantic -g ... -lm  ->  build/lab1_taskN
make test                         # все тесты
make memcheck                     # все тесты под valgrind
bash run_tests.sh 4               # тесты одного задания
bash run_tests.sh --memcheck 4    # то же под valgrind
./build/lab1_task1 -p 17          # запуск одной программы
make clean                        # удалить build/
```

Файл тестов (`tests/tests_taskN.txt`): первая значимая строка `program: <имя>`, далее блоки, разделенные пустой строкой.
Поля блока: `args` (аргументы; аргумент с пробелами - в кавычках), `input` (строка стандартного ввода),
`out` / `outre` (строка stdout; `outre` - регулярное выражение POSIX ERE), `err` (строка stderr), `code` (код возврата),
`file` / `fout` (файл, который должна создать программа, и его строки). Строки сравниваются целиком, без учета пробелов в конце.
Пути в тестах (`tests/data/...`) указаны относительно корня репозитория: `run_tests.sh` запускает программы из него.

## Проверка утечек и ошибок памяти

Для заданий с динамическим выделением памяти требуется проверка valgrind (Linux) или Dr. Memory (Windows).

**Linux.** `make memcheck` запускает каждый тест из `tests/tests_task*.txt` (включая ветви с ошибочным вводом,
стандартный ввод задания 10 и выходные файлы заданий 4, 7, 8) под

```sh
valgrind -q --leak-check=full --show-leak-kinds=definite,indirect,possible \
         --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes --error-exitcode=99
```

Тест считается непройденным, если valgrind завершился с кодом 99 (ошибка доступа к памяти или утечка); в конце выводится итог по каждому заданию.

Результат проверки (Ubuntu в WSL 2, gcc 15.2.0, valgrind 3.26.0): сборка с `-Werror` без предупреждений,
214 из 214 тестов, valgrind - 0 ошибок и 0 утечек во всех 10 заданиях. Та же проверка выполняется
GitHub Actions при каждом push (`.github/workflows/check.yml`); результат - во вкладке Actions.

**Windows.** Dr. Memory 2.6.0 (`drmemory -batch -- <программа> <аргументы>`) и heob 4.2 (`heob64 -a1 -f1`).
Задания 2, 4, 8, 9: 0 ошибок, 0 утечек. Dr. Memory сообщает одно срабатывание `UNADDRESSABLE ACCESS`
до вызова `main`, оно подавлено отдельным файлом:

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

На Windows программы собирались командой `zig cc` (zig 0.16.0) с флагами
`-std=c99 -Wall -Wextra -Wpedantic -g -fno-sanitize=undefined`:

- `zig cc` - драйвер clang 21.1.0 (LLVM), а не GCC из дистрибутива MinGW;
- при цели по умолчанию `x86_64-windows-gnu` от mingw-w64 берутся только заголовки и стартовый код;
  исполняемые файлы импортируют Universal CRT (`api-ms-win-crt-*.dll`), `msvcrt.dll` не импортируется,
  `__USE_MINGW_ANSI_STDIO` равно 0, то есть `printf` - из UCRT, который поддерживает форматы C99 (`%lld`, `%zu`).
  Известные проблемы связки MinGW + `msvcrt.dll` (форматы `%lld`/`%zu`, `long double`) к этим сборкам не относятся;
  `long double` в коде не используется.

Чтобы поведение не зависело от платформы, целые значения из командной строки хранятся в `long long`
(`long` на Windows занимает 4 байта, на Linux - 8), а псевдослучайные числа в задании 9 дает собственный генератор,
а не `rand()`, у которого разный диапазон в разных библиотеках и скрытое глобальное состояние.
Те же 214 тестов проходят и на Windows.
