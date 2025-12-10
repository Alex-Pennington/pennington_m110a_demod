@echo off
REM Build script for M110A Test GUI
REM Run from the test/test_gui directory

echo Building M110A Test GUI...

REM Check for MinGW
where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: g++ not found. Please install MinGW or add to PATH.
    exit /b 1
)

REM Build
g++ -std=c++17 -O2 ^
    -I../.. ^
    -I../../src ^
    -I../../src/common ^
    -D_USE_MATH_DEFINES ^
    -o test_gui.exe ^
    main.cpp ^
    -lws2_32 -lshell32

if %ERRORLEVEL% EQU 0 (
    echo Build successful: test_gui.exe
) else (
    echo Build failed!
    exit /b 1
)
