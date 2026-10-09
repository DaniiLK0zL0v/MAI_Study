# Сборка на Linux (gcc):
#   make           - собрать все программы
#   make test      - собрать и запустить все тесты
#   make memcheck  - собрать и запустить все тесты под valgrind (утечки и ошибки памяти)
#   make clean     - удалить собранные программы

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -g
LDLIBS = -lm

PROGS = lab1_task1 lab1_task2 lab1_task3 lab1_task4 lab1_task5 \
        lab1_task6 lab1_task7 lab1_task8 lab1_task9 lab1_task10

all: $(PROGS)

lab1_task%: lab1_task%.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test: all
	bash ./run_tests.sh

memcheck: all
	bash ./run_tests.sh --memcheck

clean:
	rm -f $(PROGS)

.PHONY: all test memcheck clean
