# Сборка на Linux (нужен JDK 17 или новее: javac и java). Классы собираются в папку build/:
#   make           - скомпилировать src/*.java и tests/Tests.java
#   make run       - запустить демонстрацию (Main)
#   make test      - проверки Tests + сравнение вывода Main с tests/main_expected.txt
#   make memcheck  - для Java не нужен (память освобождает сборщик мусора); есть, потому что его вызывает CI
#   make clean     - удалить папку build/

JAVAC = javac
JAVA = java
# -encoding UTF-8 - в исходниках русские комментарии; --release 17 - код совместим с Java 17;
# -Xlint:all -Werror - все предупреждения компилятора включены и считаются ошибками
JFLAGS = -encoding UTF-8 --release 17 -Xlint:all -Werror
CLASSES = build/classes
SRCS = $(wildcard src/*.java) tests/Tests.java

all: build/compiled

# файл-отметка: компилировать заново, только если какой-то .java новее него
build/compiled: $(SRCS)
	mkdir -p $(CLASSES)
	$(JAVAC) $(JFLAGS) -d $(CLASSES) $(SRCS)
	touch $@

run: all
	$(JAVA) -cp $(CLASSES) Main

test: all
	$(JAVA) -cp $(CLASSES) Tests
	$(JAVA) -cp $(CLASSES) Main > build/main_output.txt
	diff -u --strip-trailing-cr tests/main_expected.txt build/main_output.txt
	@echo "Main output matches tests/main_expected.txt"

memcheck:
	@echo "memcheck: not needed for Java (memory is freed by the garbage collector)"

clean:
	rm -rf build

.PHONY: all run test memcheck clean
