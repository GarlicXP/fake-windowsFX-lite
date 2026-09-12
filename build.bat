@echo off
setlocal
set DIR=%~dp0

set ZIG=
for /d %%D in ("%DIR%tools\zig-win64-*") do if exist "%%D\zig.exe" set ZIG=%%D\zig.exe
if not defined ZIG for /d %%D in ("%DIR%tools\zig-x86_64-windows-*") do if exist "%%D\zig.exe" set ZIG=%%D\zig.exe
if not defined ZIG if exist "%DIR%tools\zig\zig.exe" set ZIG=%DIR%tools\zig\zig.exe
if not defined ZIG for /d %%D in ("%USERPROFILE%\WobbleDrag\tools\zig-x86_64-windows-*") do if exist "%%D\zig.exe" set ZIG=%%D\zig.exe
if not defined ZIG (
  echo [FAIL] 未找到 Zig。请先执行: powershell -ExecutionPolicy Bypass -File "%DIR%setup_tools.ps1"
  exit /b 1
)

echo [..] 编译 WindowsFX Lite ...
"%ZIG%" c++ -O2 -static -target x86_64-windows-gnu -Wl,--subsystem,windows ^
  "%DIR%wobbly.cpp" -lgdi32 -luser32 -lshell32 -lwinmm -o "%DIR%WindowsFXLite.exe"
if errorlevel 1 (
  echo [FAIL] 编译失败
  exit /b 1
)
echo [OK] 已生成 %DIR%WindowsFXLite.exe
exit /b 0
