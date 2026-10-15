@echo off
rem Builds the Hand Poser GML plugin and its in-game test harness.
rem   build.bat            build into build\
rem   build.bat install    also copy the plugin into the game (refuses while the game runs)
rem Needs the GML SDK (..\ModLoader\include, or set GML_SDK) and Visual Studio 2022.
setlocal
set ROOT=%~dp0
set OUT=%ROOT%build
set SDK=%ROOT%external\gml\include
if not exist "%SDK%\GML\GML.hpp" set SDK=%ROOT%..\ModLoader\include
if defined GML_SDK set SDK=%GML_SDK%
set GAME=C:\Program Files (x86)\Steam\steamapps\common\GERONIMO\Geronimo\Binaries\Win64
if defined GML_GAME_DIR set GAME=%GML_GAME_DIR%
for %%D in ("%OUT%\obj" "%OUT%\HandPoser" "%OUT%\tests\HandPoserDev") do if not exist "%%~D" mkdir "%%~D"
if not exist "%SDK%\GML\GML.hpp" (
  echo GML SDK not found at %SDK% - set GML_SDK
  exit /b 1
)
if not defined VCToolsInstallDir (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
set CFLAGS=/nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%"
set LFLAGS=/DEBUG /INCREMENTAL:NO
echo === plugin
cl %CFLAGS% /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%src\HandPoser.cpp" /Fe"%OUT%\HandPoser\HandPoser.dll" /link %LFLAGS% || exit /b 1
echo === test harness (never installed)
cl %CFLAGS% /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%tests\HandPoserDev\HandPoserDev.cpp" /Fe"%OUT%\tests\HandPoserDev\HandPoserDev.dll" /link %LFLAGS% || exit /b 1
for /r "%OUT%" %%F in (*.exp *.lib *.ilk) do del /q "%%F"
if /i "%1"=="install" (
  tasklist /FI "IMAGENAME eq Geronimo-Win64-Shipping.exe" | find /I "Geronimo-Win64" >nul && (
    echo Geronimo is running - close it first.
    exit /b 1
  )
  echo === install to "%GAME%\GML\plugins\HandPoser"
  if not exist "%GAME%\GML\plugins\HandPoser" mkdir "%GAME%\GML\plugins\HandPoser"
  copy /y "%OUT%\HandPoser\HandPoser.dll" "%GAME%\GML\plugins\HandPoser\" >nul || exit /b 1
)
echo === done
exit /b 0
