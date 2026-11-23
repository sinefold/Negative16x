@echo off
set GCC=C:\Users\pauls\AppData\Local\Temp\opencode\w64\w64devkit\bin
"%GCC%\g++.exe" -O2 -s -shared -static-libgcc -static-libstdc++ -o NegativeDelay.dll negdelay.cpp negdelay.def -Wall -Wextra
if errorlevel 1 exit /b 1
"%GCC%\g++.exe" -O2 -s -static-libgcc -static-libstdc++ -o test_host.exe test_host.cpp -Wall -Wextra
