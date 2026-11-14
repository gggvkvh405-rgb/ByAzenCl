@echo off
chcp 65001 >nul
title ByAzen Overlay Launcher
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PY_CMD=py -3"
    set "PYW_CMD=pyw -3"
) else (
    where python >nul 2>nul
    if %errorlevel%==0 (
        set "PY_CMD=python"
        set "PYW_CMD=pythonw"
    ) else (
        echo [ОШИБКА] Python не найден! Установи Python 3.10+ с https://www.python.org/downloads/
        echo Обязательно поставь галочку "Add Python to PATH" при установке.
        pause
        exit /b 1
    )
)

%PY_CMD% -c "import PyQt6" >nul 2>nul
if not %errorlevel%==0 (
    echo [ByAzen] Устанавливаю библиотеку интерфейса PyQt6 (один раз)...
    %PY_CMD% -m pip install --upgrade pip
    %PY_CMD% -m pip install PyQt6
    if not %errorlevel%==0 (
        echo [ОШИБКА] Не удалось установить PyQt6. Проверь подключение к интернету.
        pause
        exit /b 1
    )
)

echo [ByAzen] Запускаю оверлей поверх Roblox...
start "" %PYW_CMD% "%~dp0byazen.py"
exit /b 0
