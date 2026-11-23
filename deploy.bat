@echo off
set DEST=C:\Program Files (x86)\VstPlugins
call "%~dp0build.bat" || exit /b 1
copy /Y "%~dp0NegativeDelay16.dll" "%DEST%\NegativeDelay16.dll" || exit /b 1
echo Deployed DLL to %DEST%
echo In REAPER: FX browser -^> scan for new plugins
