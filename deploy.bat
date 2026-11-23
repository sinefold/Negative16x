@echo off
set DEST=C:\Program Files (x86)\VstPlugins
set FXDEST=F:\OneDrive\ReaperPortableInstall\Effects\delay
call "%~dp0build.bat" || exit /b 1
copy /Y "%~dp0NegativeDelay.dll" "%DEST%\NegativeDelay.dll" || exit /b 1
echo Deployed DLL to %DEST%
copy /Y "%~dp0negdelay_min.jsfx" "%FXDEST%\negdelay_min.jsfx" || exit /b 1
echo Deployed JSFX to %FXDEST%
echo In REAPER: FX browser -^> scan for new plugins
