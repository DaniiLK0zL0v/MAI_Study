#!/usr/bin/env bash
# Запуск тестов из файлов tests_taskN.txt (Linux / WSL / Git Bash; нужен bash 4.3+).
#   ./run_tests.sh                - все файлы tests_task*.txt
#   ./run_tests.sh 1              - только tests_task1.txt
#   ./run_tests.sh --memcheck     - то же, но каждая программа запускается под valgrind;
#   ./run_tests.sh --memcheck 1     тест не пройден, если valgrind нашёл ошибку памяти или утечку
# Перед запуском собрать программы: make

cd "$(dirname "$0")" || exit 1
TIMEOUT=60   # секунд на один тест
# только для Git Bash на Windows: не превращать аргумент "/s" в путь "C:/Program Files/Git/s"
export MSYS_NO_PATHCONV=1

if [ -t 1 ]; then
    GREEN=$'\e[32m'; RED=$'\e[31m'; YELLOW=$'\e[33m'; CYAN=$'\e[36m'; NC=$'\e[0m'
else
    GREEN=""; RED=""; YELLOW=""; CYAN=""; NC=""
fi

MEMCHECK=0
if [ "$1" = "--memcheck" ]; then
    MEMCHECK=1
    shift
    if ! command -v valgrind >/dev/null 2>&1; then
        echo "${RED}Error: valgrind not found (install it: sudo apt install valgrind)${NC}"
        exit 1
    fi
    TIMEOUT=600   # под valgrind программы работают в десятки раз медленнее
    # код 99 программа сама никогда не возвращает - так отличаем ошибку памяти
    VALGRIND=(valgrind -q --leak-check=full --show-leak-kinds=definite,indirect,possible
              --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes
              --error-exitcode=99)
fi

if [ -n "$1" ]; then
    files=("tests_task$1.txt")
else
    files=($(ls tests_task*.txt 2>/dev/null | sort -V))
fi
if [ ${#files[@]} -eq 0 ] || [ ! -f "${files[0]}" ]; then
    echo "${RED}No test files found${NC}"
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# разбить строку аргументов на части; "в кавычках" - один аргумент
split_args() {
    local s="$1"
    ARGS=()
    while [[ $s =~ ^[[:space:]]*(\"([^\"]*)\"|([^[:space:]]+))(.*)$ ]]; do
        if [ -n "${BASH_REMATCH[1]}" ] && [ "${BASH_REMATCH[1]:0:1}" = '"' ]; then
            ARGS+=("${BASH_REMATCH[2]}")
        else
            ARGS+=("${BASH_REMATCH[3]}")
        fi
        s="${BASH_REMATCH[4]}"
    done
}

# файл -> строки без \r и пробелов в конце, без пустых строк в конце
normalize() {
    sed -e 's/\r$//' -e 's/[[:space:]]*$//' "$1" | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}'
}

# сравнить строки файла $1 с ожидаемыми (массивы KIND и TEXT с префиксом $2)
same_lines() {
    local fakt=() line i=0
    # "|| [ -n "$line" ]" - чтобы не потерять последнюю строку без перевода строки
    while IFS= read -r line || [ -n "$line" ]; do fakt+=("$line"); done < <(normalize "$1")
    local -n kinds="$2_KIND" texts="$2_TEXT"
    [ ${#fakt[@]} -eq ${#texts[@]} ] || return 1
    for ((i = 0; i < ${#fakt[@]}; i++)); do
        if [ "${kinds[$i]}" = "re" ]; then
            [[ ${fakt[$i]} =~ ^(${texts[$i]})$ ]] || return 1
        else
            [ "${fakt[$i]}" = "${texts[$i]}" ] || return 1
        fi
    done
    return 0
}

show_diff() {
    local -n kinds="$2_KIND" texts="$2_TEXT"
    echo "     ${YELLOW}$1 expected:${NC}"
    for ((i = 0; i < ${#texts[@]}; i++)); do
        [ "${kinds[$i]}" = "re" ] && echo "       ~ ${texts[$i]}" || echo "       ${texts[$i]}"
    done
    echo "     ${YELLOW}$1 got:${NC}"
    normalize "$3" | awk '{ print "       " $0 }'
}

vsego=0
proshlo=0
ITOGI=()   # итог проверки памяти по каждому заданию

run_test() {
    vsego=$((vsego + 1))
    [ -n "$FILE" ] && rm -f "$FILE"

    # команда запуска: сама программа или программа под valgrind
    local cmd=("$EXE" "${ARGS[@]}")
    if [ $MEMCHECK -eq 1 ]; then
        : >"$TMP/vg"
        cmd=("${VALGRIND[@]}" "--log-file=$TMP/vg" "${cmd[@]}")
    fi

    # запуск: stdout и stderr отдельно, input - на вход программы
    if [ ${#INPUT[@]} -gt 0 ]; then
        printf '%s\n' "${INPUT[@]}" | timeout $TIMEOUT "${cmd[@]}" >"$TMP/out" 2>"$TMP/err"
    else
        timeout $TIMEOUT "${cmd[@]}" </dev/null >"$TMP/out" 2>"$TMP/err"
    fi
    local kod=$?

    local ok=1 okOut=1 okErr=1 okCode=1 okFile=1 okMem=1
    # ошибку памяти определяем по коду 99; в журнале valgrind бывают и безобидные предупреждения
    if [ $MEMCHECK -eq 1 ] && [ $kod -eq 99 ]; then
        okMem=0
        mem_oshibok=$((mem_oshibok + 1))
    fi
    same_lines "$TMP/out" OUT || okOut=0
    same_lines "$TMP/err" ERR || okErr=0
    [ -z "$CODE" ] || [ $okMem -eq 0 ] || [ "$kod" -eq "$CODE" ] || okCode=0
    if [ -n "$FILE" ]; then
        if [ -f "$FILE" ]; then same_lines "$FILE" FOUT || okFile=0; else okFile=0; fi
    fi
    [ $okOut -eq 1 ] && [ $okErr -eq 1 ] && [ $okCode -eq 1 ] && [ $okFile -eq 1 ] && [ $okMem -eq 1 ] || ok=0

    local komanda="$PROGRAM ${ARGS[*]}"
    [ ${#INPUT[@]} -gt 0 ] && komanda="$komanda  < ${#INPUT[@]} input lines"
    if [ $ok -eq 1 ]; then
        proshlo=$((proshlo + 1))
        echo "${GREEN}PASS${NC} $NAME  [$komanda]"
        [ -n "$FILE" ] && rm -f "$FILE"
    else
        echo "${RED}FAIL${NC} $NAME  [$komanda]"
        [ $kod -eq 124 ] && echo "     ${YELLOW}program did not finish in $TIMEOUT s${NC}"
        if [ $okMem -eq 0 ]; then
            echo "     ${YELLOW}valgrind found memory errors:${NC}"
            awk '{ print "       " $0 }' "$TMP/vg"
        fi
        [ $okOut -eq 0 ] && show_diff "output" OUT "$TMP/out"
        [ $okErr -eq 0 ] && show_diff "errors (stderr)" ERR "$TMP/err"
        [ $okCode -eq 0 ] && echo "     ${YELLOW}exit code expected $CODE, got $kod${NC}"
        if [ $okFile -eq 0 ]; then
            if [ -f "$FILE" ]; then show_diff "file $FILE" FOUT "$FILE"
            else echo "     ${YELLOW}file $FILE was not created${NC}"; fi
        fi
    fi
}

# тест закончился: запустить (или посчитать непройденным, если нет программы)
finish_test() {
    [ $EST -eq 1 ] || return
    zapuskov=$((zapuskov + 1))
    if [ $zapuskat -eq 1 ]; then run_test; else vsego=$((vsego + 1)); fi
}

new_test() {
    NAME=""; ARGS=(); INPUT=(); CODE=""; FILE=""; EST=0
    OUT_KIND=(); OUT_TEXT=(); ERR_KIND=(); ERR_TEXT=(); FOUT_KIND=(); FOUT_TEXT=()
}

for f in "${files[@]}"; do
    PROGRAM=""
    new_test
    zapuskat=1
    zapuskov=0
    mem_oshibok=0
    echo
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"
        line="${line%"${line##*[![:space:]]}"}"   # убрать пробелы в конце
        [[ $line == \#* ]] && continue
        if [ -z "$line" ]; then
            finish_test
            new_test
            continue
        fi
        if [[ $line =~ ^program:[[:space:]]*(.*)$ ]]; then
            PROGRAM="${BASH_REMATCH[1]}"
            EXE="./$PROGRAM"
            # на Windows (Git Bash) программа с .exe
            [ -f "$EXE" ] || EXE="./$PROGRAM.exe"
            echo "${CYAN}===== $f  ($PROGRAM) =====${NC}"
            if [ ! -x "$EXE" ]; then
                echo "${RED}Error: program '$PROGRAM' not found. Run make first.${NC}"
                zapuskat=0
            fi
            continue
        fi
        if [[ ! $line =~ ^([a-z]+):[[:space:]]?(.*)$ ]]; then
            echo "${YELLOW}Warning: can not understand line '$line' in $f${NC}"
            continue
        fi
        EST=1
        key="${BASH_REMATCH[1]}"; val="${BASH_REMATCH[2]}"
        case "$key" in
            name)  NAME="$val" ;;
            args)  split_args "$val" ;;
            input) INPUT+=("$val") ;;
            out)   OUT_KIND+=("eq"); OUT_TEXT+=("$val") ;;
            outre) OUT_KIND+=("re"); OUT_TEXT+=("$val") ;;
            err)   ERR_KIND+=("eq"); ERR_TEXT+=("$val") ;;
            code)  CODE="$val" ;;
            file)  FILE="$val" ;;
            fout)  FOUT_KIND+=("eq"); FOUT_TEXT+=("$val") ;;
            *)     echo "${YELLOW}Warning: unknown field '$key' in $f${NC}" ;;
        esac
    done < "$f"
    finish_test
    if [ $MEMCHECK -eq 1 ]; then
        if [ $zapuskat -eq 0 ]; then
            ITOGI+=("$PROGRAM: not checked (program not found)")
        elif [ $mem_oshibok -eq 0 ]; then
            ITOGI+=("$PROGRAM: $zapuskov runs, no memory errors")
        else
            ITOGI+=("$PROGRAM: $zapuskov runs, memory errors in $mem_oshibok")
        fi
    fi
done

if [ $MEMCHECK -eq 1 ]; then
    echo
    echo "${CYAN}valgrind summary:${NC}"
    for s in "${ITOGI[@]}"; do echo "  $s"; done
fi

echo
if [ $proshlo -eq $vsego ]; then
    echo "${GREEN}All tests passed: $proshlo of $vsego${NC}"
    exit 0
else
    echo "${RED}Passed $proshlo of $vsego, failed $((vsego - proshlo))${NC}"
    exit 1
fi
