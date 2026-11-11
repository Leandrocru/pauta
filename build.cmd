@echo off
rem Compila o Pauta (Release) e monta uma pasta portatil em build\dist.
setlocal
set "QTDIR=C:\Qt\6.11.3\msvc2022_64"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio Build Tools nao encontrado. & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QTDIR%" || exit /b 1
cmake --build build || exit /b 1

if /i "%1"=="dist" (
  if not exist build\dist mkdir build\dist
  copy /y build\Pauta.exe build\dist\ >nul
  "%QTDIR%\bin\windeployqt.exe" --release --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler build\dist\Pauta.exe || exit /b 1
)
echo OK
