@echo off
set "GAME_EXE=MVP06"
cd /d "%~dp0"
set "OUT=out\build\release"
set /p VER=<VERSION
set "ZIP=MVP-27-College-Baseball-v%VER%.zip"
rem setup comes from MVP Workshop (src/MVPSetup), which shares the install code with the editor
if not defined WORKSHOP set "WORKSHOP=E:\Tool Source\MVP-Tool"
set "SEVENZIP=7z"
where 7z >nul 2>nul || set "SEVENZIP=%ProgramFiles%\7-Zip\7z.exe"
rem the Visual C++ runtime beside the executable, so a fresh Windows install needs nothing more
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
for /d %%v in ("%VSDIR%\VC\Redist\MSVC\1*") do for /d %%c in ("%%v\x64\Microsoft.VC*.CRT") do set "VCRUNTIME=%%c"
if not exist "%VCRUNTIME%\vcruntime140.dll" (echo Visual C++ runtime not found: install Visual Studio with the C++ workload & exit /b 1)
if not exist "%OUT%\%GAME_EXE%.exe" call build.bat || exit /b 1
rem the configure step writes where the SDK is
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
if not exist "%OUT%\sdk_dir.txt" echo %OUT%\sdk_dir.txt was not written, update the SDK. && exit /b 1
set /p SDK=<out\build\release\sdk_dir.txt
del /q "%ZIP%" 2>nul
rem the zip holds Setup.exe and "Port Files": setup copies those to the chosen folder, then installs the game from
rem the player's own disc image beside them
set "PKG=%OUT%\package"
set "PORT=%PKG%\Port Files"
rd /s /q "%OUT%elease" 2>nul
rd /s /q "%PKG%" 2>nul
mkdir "%PORT%"
rem only the runtime DLLs the executables import; xbe_recomp.exe needs its own copy, Windows only looks beside the
rem exe it starts
del /q "%OUT%\compiler\*.dll" 2>nul
for %%f in (msvcp140 vcruntime140) do copy /y "%VCRUNTIME%\%%f.dll" "%OUT%\compiler\" >nul
copy /y "%OUT%\%GAME_EXE%.exe" "%PORT%\" >nul
copy /y README.md "%PORT%\" >nul
copy /y "%SDK%\THIRD_PARTY.txt" "%PORT%\" >nul
for %%f in (msvcp140 vcruntime140 vcruntime140_1) do copy /y "%VCRUNTIME%\%%f.dll" "%PORT%\" >nul
rem the compiler folder without its work folder: no game code goes in the zip, the first start builds it
robocopy "%OUT%\compiler" "%PORT%\compiler" /e /xd work /njh /njs /nfl /ndl >nul
if errorlevel 8 (echo copying the compiler failed & exit /b 1)
dotnet publish "%WORKSHOP%\src\MVPSetup" -c Release -r win-x64 -o "%PKG%" -p:DebugType=none -nologo -v q || (echo building Setup.exe failed: set WORKSHOP to the MVP Workshop folder & exit /b 1)
"%SEVENZIP%" a "%ZIP%" ".\%PKG%\*" >nul || exit /b 1
echo %ZIP%
