@echo off
setlocal
cd /d "%~dp0"

if not exist build mkdir build

where g++ >nul 2>nul
if %errorlevel%==0 goto build_gpp

where cl >nul 2>nul
if %errorlevel%==0 goto build_msvc

echo [ERROR] No C++ compiler found.
echo Install MinGW-w64 ^(g++^) or Visual Studio Build Tools, then run this again.
pause
exit /b 1

:build_gpp
echo [BUILD] g++ backend\server.cpp ...
g++ -std=c++14 -O2 -Wall backend\server.cpp -o build\calculator-server.exe -lws2_32
if %errorlevel% neq 0 goto fail
goto run

:build_msvc
echo [BUILD] cl backend\server.cpp ...
cl /nologo /EHsc /O2 backend\server.cpp /Fe:build\calculator-server.exe ws2_32.lib
if %errorlevel% neq 0 goto fail
goto run

:run
echo [RUN] Starting server - your browser will open automatically...
build\calculator-server.exe
goto :eof

:fail
echo [ERROR] Build failed.
pause
exit /b 1
