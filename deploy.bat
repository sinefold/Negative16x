@echo off
set DEST=C:\Program Files (x86)\VstPlugins
call "%~dp0build.bat" || exit /b 1
copy /Y "%~dp0NegativeDelay.dll" "%DEST%\NegativeDelay.dll" || exit /b 1
echo Deployed to %DEST%
echo In REAPER: FX browser -^> scan for new plugins
