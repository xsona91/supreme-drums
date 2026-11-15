@echo off
setlocal
title Supreme Drums - compilazione del plugin VST3
echo.
echo  SUPREME DRUMS - (c) 2026 Giulia Casu
echo  Compilo il plugin VST3 a 64 bit su questo computer.
echo.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto novs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto novs
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
where cmake >nul 2>nul || goto nocmake
cd /d "%~dp0"
echo  1/2  Preparo il progetto (la prima volta scarica JUCE, circa 100 MB)...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto fail
echo  2/2  Compilo (qualche minuto)...
cmake --build build --config Release --target SupremeDrums_VST3 || goto fail
set "OUT=%~dp0build\SupremeDrums_artefacts\Release\VST3\Supreme Drums.vst3"
if not exist "%OUT%" goto fail
echo.
echo  Fatto! Il plugin e' qui:
echo  %OUT%
echo.
echo  Lo copio nella cartella dei plugin VST3 di Windows (serve il permesso di amministratore)...
powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait cmd -ArgumentList '/c xcopy /E /I /Y \"%OUT%\" \"%CommonProgramFiles%\VST3\Supreme Drums.vst3\"'"
echo.
echo  In Ableton: Preferenze ^> Plug-in ^> attiva "Usa cartelle di sistema VST3" e premi "Ripeti scansione".
echo  Poi cerca "Supreme Drums" nel Browser, sotto Plug-in.
pause
exit /b 0
:novs
echo  Manca Visual Studio 2022 con il C++.
echo  Installa gratis "Visual Studio 2022 Community" da https://visualstudio.microsoft.com/it/
echo  e nell'installazione spunta "Sviluppo di applicazioni desktop con C++". Poi rilancia questo file.
pause
exit /b 1
:nocmake
echo  CMake non trovato: in Visual Studio Installer aggiungi il componente "Strumenti CMake C++ per Windows".
pause
exit /b 1
:fail
echo.
echo  La compilazione non e' riuscita. Copia le righe in rosso qui sopra e mandamele.
pause
exit /b 1
