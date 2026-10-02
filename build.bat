@echo off
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
rc /nologo /utf-8 /fo app.res app.rc || exit /b 1
cl /nologo /std:c17 /MT /O2 /W4 /permissive- /guard:cf /Tc main.c app.res /Fe:HiddenPictures.exe /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /CETCOMPAT || exit /b 1
del main.obj 2>nul
echo BUILD OK
