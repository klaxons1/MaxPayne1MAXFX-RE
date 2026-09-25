@echo off
setlocal
rem Build a drop-in ldb-viewer.exe with CMake + Visual Studio.
rem Run from a "x64 Native Tools Command Prompt for VS" or any shell
rem that has cmake and cl.exe on PATH.
rem
rem Output: build\Release\ldb-viewer.exe
rem Place it next to the game data folder:
rem   ldb-viewer.exe
rem   data\database\levels\*.ldb

cd /d "%~dp0\.."

if not exist build mkdir build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
  echo.
  echo CMake configure failed. Install Visual Studio 2022 with "Desktop development with C++"
  echo and CMake, then retry from the x64 Native Tools prompt.
  exit /b 1
)

cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1

echo.
echo Built: build\Release\ldb-viewer.exe
echo Copy that exe next to the Max Payne data folder and run it.
echo Left/Right arrows cycle every .ldb in data\database\levels.
endlocal
