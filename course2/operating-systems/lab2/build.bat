@echo off
chcp 65001 >nul
echo ============================================
echo   Сборка лабораторной работы №2 по ОС
echo   Вариант 11 — Медианный фильтр (Windows)
echo ============================================
echo.

REM Попытка использовать MSVC (Visual Studio 2022 Build Tools)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [INFO] MSVC Build Tools не найдены, пробуем GCC...
    goto :try_gcc
)

echo [INFO] Используется MSVC (Visual Studio 2022 Build Tools)
echo.

echo Компиляция median_filter.c ...
cl /W3 /O2 /Fe:median_filter.exe median_filter.c /Fo:median_filter.obj >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Ошибка компиляции median_filter.c
    goto :end
)
echo [OK] median_filter.exe
goto :done

:try_gcc
REM Попытка компиляции через gcc (MinGW)
where gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Компилятор не найден! Установите MSVC Build Tools или MinGW.
    goto :end
)

echo [INFO] Используется GCC (MinGW)
echo.

echo Компиляция median_filter.c ...
gcc -Wall -O2 -o median_filter.exe median_filter.c
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Ошибка компиляции median_filter.c
    goto :end
)
echo [OK] median_filter.exe

:done
echo.
echo ============================================
echo   Сборка завершена успешно!
echo.
echo   Примеры запуска:
echo     median_filter.exe -r 500 -c 500 -w 3 -k 3 -t 4
echo     median_filter.exe -r 800 -c 800 -w 5 -k 3 -t 8 -b
echo     median_filter.exe -h
echo ============================================

:end
echo.
pause
