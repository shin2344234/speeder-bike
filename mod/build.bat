@echo off
rem Speeder Bike build: MSVC Build Tools 2022 + the CMake and Ninja they bundle.
rem   build.bat          configure (first run) and build Release into build\, stage dist\
rem   build.bat clean    wipe build\ first
rem   build.bat chartcheck IN OUT   build the speeder's charts offline (tests\chartcheck.cpp)
rem   build.bat stackcheck IN [alone]   the speeder's tables, package list and charts on top of
rem                      Broomy's (tests\stackcheck.cpp)
rem   build.bat soundcheck   a fake ride through the speeder's engine sounds (tests\soundcheck.cpp)
rem   build.bat riderfind CHARTS [SPLICED]   the plugin's search for Kliff's chart words on the shipped
rem                      charts, against speeder\research\rider_words.py (tests\riderfind.cpp)
rem Call it by full quoted path from PowerShell; the space in the repo path
rem breaks a bare `cmd /c build.bat`.
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "HERE=%~dp0"

if "%1"=="clean" if exist "%HERE%build" rmdir /s /q "%HERE%build"

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo vcvars64.bat not found at "%VCVARS%"
  exit /b 1
)

if not exist "%HERE%build\build.ninja" (
  "%CMAKE%" -S "%HERE%." -B "%HERE%build" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release
  if errorlevel 1 exit /b 1
)
if "%1"=="stackcheck" goto check
if "%1"=="riderfind" goto check
if "%1"=="soundcheck" goto check
"%CMAKE%" --build "%HERE%build"
if errorlevel 1 exit /b 1
echo.
echo staged in "%HERE%dist"
endlocal
exit /b 0

rem The offline checks. Their exit code goes out with an explicit
rem `exit /b %errorlevel%` outside any block: a bare `exit /b`, or one inside
rem parentheses, hands PowerShell's `cmd /c` a 0 whatever the check returned.
:check
"%CMAKE%" --build "%HERE%build" --target %1
if errorlevel 1 exit /b 1
"%HERE%build\%1.exe" %2 %3
exit /b %errorlevel%
