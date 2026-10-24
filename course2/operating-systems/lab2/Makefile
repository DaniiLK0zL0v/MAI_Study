# platform: windows
# (эта строка - метка для GitHub Actions: лабу собирает и проверяет задача на Windows, а не на Linux)
#
# Сборка на Windows в Git Bash или MSYS2 компилятором zig cc (или gcc из MinGW: make CC=gcc).
# Программа собирается в папку build/.
#   make           - собрать median_filter.exe
#   make test      - собрать и запустить все тесты
#   make memcheck  - собрать и запустить все тесты под Dr. Memory (ошибки памяти и утечки)
#   make clean     - удалить папку build/

CC = zig cc
# -fno-sanitize=undefined: без него zig добавляет проверку UB, и Dr. Memory видит ложные ошибки
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -g -fno-sanitize=undefined

PROGS = build/median_filter.exe

all: $(PROGS)

# "| build" - сначала создать папку build (если ее еще нет)
build/%.exe: %.c | build
	$(CC) $(CFLAGS) -o $@ $<

build:
	mkdir -p build

test: all
	bash ./run_tests.sh

memcheck: all
	bash ./run_tests.sh --memcheck

clean:
	rm -rf build

.PHONY: all test memcheck clean
