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

rem "build.cmd dist" monta a pasta portatil; "build.cmd installer" tambem gera build\installer\Pauta.exe
if /i "%1"=="dist" goto dist
if /i "%1"=="installer" goto dist
goto done

:dist
if not exist build\dist mkdir build\dist
copy /y build\Pauta.exe build\dist\ >nul
"%QTDIR%\bin\windeployqt.exe" --release --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --no-translations build\dist\Pauta.exe || exit /b 1
if not exist build\dist\translations mkdir build\dist\translations
copy /y "%QTDIR%\translations\qtbase_pt_BR.qm" build\dist\translations\ >nul
rem Runtime do Visual C++ junto do programa (dispensa instalar o vc_redist).
for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do copy /y "%%d\*.dll" build\dist\ >nul
copy /y README.md build\dist\ >nul
copy /y resources\fonts\OFL.txt build\dist\ >nul
if /i not "%1"=="installer" goto done

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (echo Inno Setup 6 nao encontrado. & exit /b 1)
for /f "tokens=4 delims=() " %%v in ('findstr /r /c:"project(Pauta VERSION" CMakeLists.txt') do set "PVER=%%v"
"%ISCC%" /Qp "/DAppVersion=%PVER%" packaging\windows\pauta.iss || exit /b 1

:done
echo OK
