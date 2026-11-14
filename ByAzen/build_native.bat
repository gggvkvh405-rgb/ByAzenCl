@echo off
setlocal
pushd "%~dp0src"

where x86_64-w64-mingw32-g++ >nul 2>&1
if errorlevel 1 (
  echo MinGW-w64 x86_64 tools not found in PATH.
  echo Install the MSYS2 MinGW-w64 toolchain, then run this file again.
  popd
  exit /b 1
)

x86_64-w64-mingw32-windres resource.rc -O coff -o "%TEMP%\byazen_v35_resource.o"
if errorlevel 1 goto :fail

x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -Werror -finput-charset=UTF-8 -municode -mwindows -static -static-libgcc -static-libstdc++ byazen_native.cpp "%TEMP%\byazen_v35_resource.o" -lgdiplus -lgdi32 -luser32 -lshell32 -lwinmm -lole32 -lcomdlg32 -ladvapi32 -o "..\ByAzen.exe"
if errorlevel 1 goto :fail

del "%TEMP%\byazen_v35_resource.o" >nul 2>&1
echo.
echo Build succeeded: %~dp0ByAzen.exe
echo SHA-256:
certutil -hashfile "..\ByAzen.exe" SHA256
popd
exit /b 0

:fail
del "%TEMP%\byazen_v35_resource.o" >nul 2>&1
echo Native build failed.
popd
exit /b 1
