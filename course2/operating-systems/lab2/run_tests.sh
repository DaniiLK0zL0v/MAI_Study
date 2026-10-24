#!/usr/bin/env bash
# Запуск тестов из файлов tests/tests_*.txt (Windows: Git Bash или MSYS2; нужен bash 4.3+).
#   ./run_tests.sh                - все файлы tests/tests_*.txt
#   ./run_tests.sh --memcheck     - то же, но каждая программа запускается под Dr. Memory;
#                                   тест не пройден, если Dr. Memory нашёл ошибку памяти или утечку
# Перед запуском собрать программы: make (они появятся в папке build/)

cd "$(dirname "$0")" || exit 1
TIMEOUT=60   # секунд на один тест
MAX_LINES=8  # сколько строк ответа программы показывать под PASS
# не превращать аргументы вида "/s" в пути "C:/Program Files/Git/s"
export MSYS_NO_PATHCONV=1

if [ -t 1 ]; then
    GREEN=$'\e[32m'; RED=$'\e[31m'; YELLOW=$'\e[33m'; CYAN=$'\e[36m'; GRAY=$'\e[90m'; NC=$'\e[0m'
else
    GREEN=""; RED=""; YELLOW=""; CYAN=""; GRAY=""; NC=""
fi

MEMCHECK=0
if [ "$1" = "--memcheck" ]; then
    MEMCHECK=1
    shift
    # где искать Dr. Memory: переменная DRMEMORY, PATH, папка tools пользователя, Program Files
    DRM=""
    for k in "$DRMEMORY" "$(command -v drmemory 2>/dev/null)" \
             "$USERPROFILE/tools/drmemory/bin64/drmemory.exe" \
             "/c/Program Files (x86)/Dr. Memory/bin64/drmemory.exe" \
             "/c/Program Files/Dr. Memory/bin64/drmemory.exe"; do
        if [ -n "$k" ] && [ -f "$k" ]; then DRM="$k"; break; fi
    done
    if [ -z "$DRM" ]; then
        echo "${RED}Error: Dr. Memory not found (install it from https://drmemory.org)${NC}"
        exit 1
    fi
    TIMEOUT=600   # под Dr. Memory программы работают в десятки раз медленнее
    # код 99 программа сама никогда не возвращает - так отличаем ошибку памяти;
    # drmemory.supp - ложные срабатывания в стартовом коде (до main), см. комментарии в нём
    DRM_CMD=("$DRM" -batch -quiet -exit_code_if_errors 99
             -suppress "$(cygpath -w "$PWD/drmemory.supp")")
fi

files=($(ls tests/tests_*.txt 2>/dev/null | sort -V))
if [ ${#files[@]} -eq 0 ]; then
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
ITOGI=()   # итог проверки памяти по каждому файлу тестов

# ответ программы под PASS: строки из файлов-аргументов (stderr, stdout),
# первые MAX_LINES строк серым (длинные строки обрезаны), остальное - одной строкой "..."
show_answer() {
    local f s i lines=()
    for f in "$@"; do
        [ -f "$f" ] || continue
        while IFS= read -r s || [ -n "$s" ]; do lines+=("${s%$'\r'}"); done <"$f"
    done
    # пустые строки в конце не показываем
    while [ ${#lines[@]} -gt 0 ] && [ -z "${lines[-1]// /}" ]; do unset 'lines[-1]'; done
    if [ ${#lines[@]} -eq 0 ]; then
        echo "     ${GRAY}| (no output)${NC}"
        return 0
    fi
    for ((i = 0; i < ${#lines[@]} && i < MAX_LINES; i++)); do
        s="${lines[$i]}"
        [ ${#s} -gt 100 ] && s="${s:0:100}... (${#s} chars)"
        echo "     ${GRAY}| $s${NC}"
    done
    if [ ${#lines[@]} -gt $MAX_LINES ]; then
        echo "     ${GRAY}| ... ($(( ${#lines[@]} - MAX_LINES )) more lines)${NC}"
    fi
    return 0
}

run_test() {
    vsego=$((vsego + 1))

    # команда запуска: сама программа или программа под Dr. Memory
    local cmd=("$EXE" "${ARGS[@]}")
    if [ $MEMCHECK -eq 1 ]; then
        cmd=("${DRM_CMD[@]}" -- "${cmd[@]}")
    fi

    # запуск: stdout и stderr отдельно; на вход - строки input, файл infile или ничего
    if [ -n "$INFILE" ]; then
        timeout $TIMEOUT "${cmd[@]}" <"$INFILE" >"$TMP/out" 2>"$TMP/err"
    elif [ ${#INPUT[@]} -gt 0 ]; then
        printf '%s\n' "${INPUT[@]}" | timeout $TIMEOUT "${cmd[@]}" >"$TMP/out" 2>"$TMP/err"
    else
        timeout $TIMEOUT "${cmd[@]}" </dev/null >"$TMP/out" 2>"$TMP/err"
    fi
    local kod=$?

    local ok=1 okOut=1 okErr=1 okCode=1 okMem=1
    # ошибку памяти определяем по коду 99
    if [ $MEMCHECK -eq 1 ] && [ $kod -eq 99 ]; then
        okMem=0
        mem_oshibok=$((mem_oshibok + 1))
    fi
    # вывод сравнивается либо со строками out/outre, либо с файлом outfile
    if [ -n "$OUTFILE" ]; then
        [ "$(normalize "$TMP/out")" = "$(normalize "$OUTFILE")" ] || okOut=0
    else
        same_lines "$TMP/out" OUT || okOut=0
    fi
    same_lines "$TMP/err" ERR || okErr=0
    [ -z "$CODE" ] || [ $okMem -eq 0 ] || [ "$kod" -eq "$CODE" ] || okCode=0
    [ $okOut -eq 1 ] && [ $okErr -eq 1 ] && [ $okCode -eq 1 ] && [ $okMem -eq 1 ] || ok=0

    local komanda="$PROGRAM ${ARGS[*]}"
    [ ${#INPUT[@]} -gt 0 ] && komanda="$komanda  < ${#INPUT[@]} input lines"
    [ -n "$INFILE" ] && komanda="$komanda  < $INFILE"
    if [ $ok -eq 1 ]; then
        proshlo=$((proshlo + 1))
        # тест на ошибку (программа завершилась с кодом не 0, как и ждали) или обычный запуск
        local vid="${CYAN}[normal run]${NC}"
        [ "$kod" -ne 0 ] && vid="${YELLOW}[error expected]${NC}"
        echo "${GREEN}PASS${NC} $vid $NAME  [$komanda]"
        show_answer "$TMP/err" "$TMP/out"
    else
        echo "${RED}FAIL${NC} $NAME  [$komanda]"
        [ $kod -eq 124 ] && echo "     ${YELLOW}program did not finish in $TIMEOUT s${NC}"
        if [ $okMem -eq 0 ]; then
            echo "     ${YELLOW}Dr. Memory found memory errors:${NC}"
            awk '{ print "       " $0 }' "$TMP/err"
        fi
        if [ $okOut -eq 0 ]; then
            if [ -n "$OUTFILE" ]; then
                echo "     ${YELLOW}output differs from file $OUTFILE${NC}"
            else
                show_diff "output" OUT "$TMP/out"
            fi
        fi
        [ $okErr -eq 0 ] && show_diff "errors (stderr)" ERR "$TMP/err"
        [ $okCode -eq 0 ] && echo "     ${YELLOW}exit code expected $CODE, got $kod${NC}"
    fi
}

# тест закончился: запустить (или посчитать непройденным, если нет программы)
finish_test() {
    [ $EST -eq 1 ] || return
    zapuskov=$((zapuskov + 1))
    if [ $zapuskat -eq 1 ]; then run_test; else vsego=$((vsego + 1)); fi
}

new_test() {
    NAME=""; ARGS=(); INPUT=(); CODE=""; INFILE=""; OUTFILE=""; EST=0
    OUT_KIND=(); OUT_TEXT=(); ERR_KIND=(); ERR_TEXT=()
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
            # программа лежит в build/ (ее туда кладет make)
            # или в ../windows/build/ (туда ее кладет windows/build.bat)
            EXE="./build/$PROGRAM.exe"
            [ -f "$EXE" ] || EXE="../windows/build/$PROGRAM.exe"
            echo "${CYAN}===== $f  ($PROGRAM) =====${NC}"
            if [ ! -f "$EXE" ]; then
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
            name)    NAME="$val" ;;
            args)    split_args "$val" ;;
            input)   INPUT+=("$val") ;;
            infile)  INFILE="$val" ;;
            out)     OUT_KIND+=("eq"); OUT_TEXT+=("$val") ;;
            outre)   OUT_KIND+=("re"); OUT_TEXT+=("$val") ;;
            outfile) OUTFILE="$val" ;;
            err)     ERR_KIND+=("eq"); ERR_TEXT+=("$val") ;;
            code)    CODE="$val" ;;
            *)       echo "${YELLOW}Warning: unknown field '$key' in $f${NC}" ;;
        esac
    done < "$f"
    finish_test
    if [ $MEMCHECK -eq 1 ]; then
        if [ $zapuskat -eq 0 ]; then
            ITOGI+=("$f: not checked (program not found)")
        elif [ $mem_oshibok -eq 0 ]; then
            ITOGI+=("$f: $zapuskov runs, no memory errors")
        else
            ITOGI+=("$f: $zapuskov runs, memory errors in $mem_oshibok")
        fi
    fi
done

if [ $MEMCHECK -eq 1 ]; then
    echo
    echo "${CYAN}Dr. Memory summary:${NC}"
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
