@echo off
chcp 65001 >nul
title ByAzen — Сборка Python-варианта
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
    set "PY_CMD=py -3"
) else (
    where python >nul 2>nul
    if %errorlevel%==0 (
        set "PY_CMD=python"
    ) else (
        echo [ОШИБКА] Python не найден! Установи Python 3.10+ с https://www.python.org/downloads/
        pause
        exit /b 1
    )
)

echo [1/2] Проверяю PyQt6 и PyInstaller...
%PY_CMD% -m pip install --quiet PyQt6 pyinstaller

echo [2/2] Собираю автономный Python-вариант ByAzen_Python.exe (подожди ~20-40 секунд)...
%PY_CMD% -m PyInstaller --noconsole --onefile --clean --name ByAzen_Python --distpath "%~dp0" --workpath "%TEMP%\byazen_build" --specpath "%TEMP%\byazen_build" "%~dp0byazen.py"

if exist "%~dp0ByAzen_Python.exe" (
    echo.
    echo ========================================================
    echo  ГОТОВО! Файл ByAzen_Python.exe успешно создан в этой папке!
    echo ========================================================
) else (
    echo [ОШИБКА] Сборка не завершилась. Проверь вывод выше.
)
pause
