# Сборка на Linux (gcc). Программы собираются в папку build/:
#   make           - собрать все программы
#   make test      - собрать и запустить все тесты
#   make memcheck  - собрать и запустить все тесты под valgrind (утечки и ошибки памяти)
#   make clean     - удалить папку build/

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -g
LDLIBS = -lm

# все файлы заданий (labN_task1.c, labN_task2.c, ...) -> build/labN_task1, build/labN_task2, ...
SRCS = $(wildcard *_task*.c)
PROGS = $(SRCS:%.c=build/%)

all: $(PROGS)

# "| build" - сначала создать папку build (если ее еще нет)
build/%: %.c | build
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

build:
	mkdir -p build

test: all
	bash ./run_tests.sh

memcheck: all
	bash ./run_tests.sh --memcheck

clean:
	rm -rf build

.PHONY: all test memcheck clean
