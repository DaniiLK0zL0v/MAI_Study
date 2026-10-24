@echo off
chcp 65001 >nul
echo ============================================
echo   Сборка лабораторной работы №1 по ОС
echo   Вариант 11 (Windows)
echo ============================================
echo.

REM Используем Visual Studio 2022 Build Tools (MSVC)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Не удалось инициализировать MSVC Build Tools.
    goto :try_gcc
)

echo [INFO] Используется MSVC (Visual Studio 2022 Build Tools)
echo.

echo Компиляция child1.c ...
cl /W3 /Fe:child1.exe child1.c /Fo:child1.obj >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Ошибка компиляции child1.c
    goto :end
)
echo [OK] child1.exe

echo Компиляция child2.c ...
cl /W3 /Fe:child2.exe child2.c /Fo:child2.obj >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Ошибка компиляции child2.c
    goto :end
)
echo [OK] child2.exe

echo Компиляция parent.c ...
cl /W3 /Fe:parent.exe parent.c /Fo:parent.obj >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Ошибка компиляции parent.c
    goto :end
)
echo [OK] parent.exe

echo.
echo ============================================
echo   Сборка завершена успешно!
echo   Запуск: parent.exe
echo ============================================
goto :end

:try_gcc
REM Попытка компиляции через gcc (MinGW)
where gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Компилятор не найден!
    goto :end
)

echo [INFO] Используется GCC (MinGW)
echo.

gcc -Wall -o child1.exe child1.c
gcc -Wall -o child2.exe child2.c
gcc -Wall -o parent.exe parent.c

echo.
echo ============================================
echo   Сборка завершена!
echo   Запуск: parent.exe
echo ============================================

:end
echo.
pause
