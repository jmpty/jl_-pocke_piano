@echo off
setlocal
cd /d %~dp0

set GCC=E:\ProgramFiles\riscv_gcc\w64devkit\bin\gcc.exe
if not exist "%GCC%" (
    echo gcc not found: %GCC%
    exit /b 1
)

set SRC=..\dir_midi_src
set OUT=..\dir_midi
set BAK=..\dir_midi.bak

if not exist "%SRC%" mkdir "%SRC%"

if not exist "%SRC%\m8.mid" (
    if exist "%BAK%" (
        "%GCC%" -O2 -o extract_midi.exe extract_midi.c
        extract_midi.exe "%BAK%" "%SRC%\m8.mid"
    )
)

"%GCC%" -O2 -o gen_perf_midis.exe gen_perf_midis.c
if errorlevel 1 exit /b 1
del /q "%SRC%\s*.mid" 2>nul
gen_perf_midis.exe "%SRC%"
if errorlevel 1 exit /b 1

"%GCC%" -O2 -o pack_dir_midi.exe pack_dir_midi.c
if errorlevel 1 exit /b 1

pack_dir_midi.exe "%SRC%" "%OUT%"
if errorlevel 1 exit /b 1

echo.
echo dir_midi updated: %OUT%
endlocal
