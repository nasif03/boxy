@echo off
setlocal enabledelayedexpansion

echo Building boxy...

set LIBS=-lglfw3 -lopengl32 -lgdi32
set OUTPUT=boxy.exe

gcc -Iinclude -c src/glad.c -o glad.o
if %ERRORLEVEL% NEQ 0 (
    echo Error: Failed to compile src/glad.c
    exit /b %ERRORLEVEL%
)

echo Compiling src/main.cpp...
g++ -std=c++17 -Wall -Iinclude -c src/main.cpp -o main.o
if %ERRORLEVEL% NEQ 0 (
    echo Error: Failed to compile src/main.cpp
    exit /b %ERRORLEVEL%
)

echo Linking %OUTPUT%...
g++ -Wall main.o glad.o -o %OUTPUT% %LIBS%
if %ERRORLEVEL% NEQ 0 (
    echo Error: Linking failed.
    exit /b %ERRORLEVEL%
)

if exist main.o del main.o
if exist glad.o del glad.o

echo.
echo Build successful! Output: %OUTPUT%
endlocal