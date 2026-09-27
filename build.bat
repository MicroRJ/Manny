@echo off
setlocal

if not exist blessed\manny.exe (
    echo Missing blessed\manny.exe.
    exit /b 1
)

blessed\manny.exe build.elf %*
exit /b %errorlevel%
