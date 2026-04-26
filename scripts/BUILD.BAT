@echo off
rem -----------------------------------------------------------------------
rem BUILD.BAT -- Build DOSu! with Turbo C 2.01 / Turbo C++ 3.0
rem
rem Run from the project root directory:
rem   BUILD.BAT
rem   or
rem   scripts\BUILD.BAT
rem
rem Set TC_DIR to your Turbo C installation (default C:\TC).
rem Expected layout:
rem   TC_DIR\BIN\TCC.EXE  (or BCC.EXE)
rem   TC_DIR\BGI\BGIOBJ.EXE
rem   TC_DIR\BGI\EGAVGA.BGI
rem   TC_DIR\INCLUDE\
rem   TC_DIR\LIB\
rem -----------------------------------------------------------------------

if "%TC_DIR%"=="" set TC_DIR=C:\TC

rem Detect compiler (prefer TCC, fall back to BCC)
set COMPILER=
if exist %TC_DIR%\BIN\TCC.EXE set COMPILER=TCC
if not "%COMPILER%"=="" goto have_compiler
if exist %TC_DIR%\BIN\BCC.EXE set COMPILER=BCC
if not "%COMPILER%"=="" goto have_compiler
echo Error: TCC.EXE / BCC.EXE not found under %TC_DIR%\BIN\
echo Set TC_DIR to your Turbo C installation directory.
goto end

:have_compiler
rem Find BGIOBJ (may live in BGI\ or BIN\ depending on TC version)
set BGIOBJ=
if exist %TC_DIR%\BGI\BGIOBJ.EXE set BGIOBJ=%TC_DIR%\BGI\BGIOBJ.EXE
if not "%BGIOBJ%"=="" goto have_bgiobj
if exist %TC_DIR%\BIN\BGIOBJ.EXE set BGIOBJ=%TC_DIR%\BIN\BGIOBJ.EXE
if not "%BGIOBJ%"=="" goto have_bgiobj
echo Error: BGIOBJ.EXE not found in %TC_DIR%\BGI\ or %TC_DIR%\BIN\
goto end

:have_bgiobj
if not exist %TC_DIR%\BGI\EGAVGA.BGI goto no_bgi

rem Prepare build directory
if not exist build mkdir build
copy src\Dosu.c build\Dosu.c > nul
copy %TC_DIR%\BGI\EGAVGA.BGI build\EGAVGA.BGI > nul

rem Convert EGAVGA.BGI -> EGAVGA.OBJ (run from build\ so output lands there)
cd build
%BGIOBJ% EGAVGA
if not exist EGAVGA.OBJ goto bgiobj_fail

rem Compile and link
%TC_DIR%\BIN\%COMPILER% -ml -I%TC_DIR%\INCLUDE -L%TC_DIR%\LIB Dosu.c EGAVGA.OBJ GRAPHICS.LIB
cd ..

if not exist build\DOSU.EXE goto compile_fail
if not exist bin mkdir bin
copy build\DOSU.EXE bin\DOSU.EXE > nul
echo.
echo Build OK: bin\DOSU.EXE
goto end

:no_bgi
echo Error: EGAVGA.BGI not found at %TC_DIR%\BGI\EGAVGA.BGI
goto end

:bgiobj_fail
cd ..
echo Error: BGIOBJ failed to produce EGAVGA.OBJ
goto end

:compile_fail
echo Error: Compilation failed - no EXE produced in build\
goto end

:end
