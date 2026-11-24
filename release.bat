@echo off
set "GAME_EXE=MVP06"
cd /d "%~dp0"
rem In the paired workspace, refuse a release with divergent authored source.
set "SYNC_PEER=..\MVP06-Base\MVP06-XBERecomp"
if exist "..\..\MVP06-XBERecomp\tools\sync-check.ps1" set "SYNC_PEER=..\..\MVP06-XBERecomp"
if exist "%SYNC_PEER%\tools\sync-check.ps1" (
    where pwsh >nul 2>nul || (echo PowerShell 7 is required for the BASE/DEV sync check & exit /b 1)
    pwsh -NoProfile -File tools\sync-check.ps1 || (echo BASE/DEV source differs; review and synchronize before packaging & exit /b 1)
)
set "OUT=out\build\release"
rem Never package a stale executable with developer tools, even from DEV.
cmake --preset release -DMVP06_DEV_TOOLS=OFF -DMVP06_UE_EMBED=OFF || (echo configuring player capabilities failed & exit /b 1)
cmake --build "%OUT%" --target MVP06 || (echo building player executable failed & exit /b 1)
rem Verify and stage the combined Overhaul built from its source project.
pwsh -NoProfile -File tools\build-bundled-mods.ps1 || (echo staging the bundled Overhaul failed & exit /b 1)
set /p VER=<VERSION
set "ZIP=MVP-27-College-Baseball-v%VER%.zip"
rem setup and the launcher are launcher\ here; they use MVP Workshop's install code from the pinned submodule
if not exist "external\MVP-Workshop\src\MVPWorkshop.Core\MVPWorkshop.Core.csproj" git submodule update --init external/MVP-Workshop || (echo fetching the MVP-Workshop submodule failed & exit /b 1)
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
rd /s /q "%OUT%\release" 2>nul
rd /s /q "%PKG%" 2>nul
mkdir "%PORT%"
mkdir "%PORT%\bundled-mods"
for %%m in (mvp27-overhaul) do copy /y "%OUT%\bundled-mods\%%m.mvpmod" "%PORT%\bundled-mods\" >nul || exit /b 1
copy /y "%OUT%\bundled-mods\defaults.json" "%PORT%\bundled-mods\" >nul || exit /b 1
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
dotnet publish launcher -c Release -r win-x64 -o "%PKG%" -p:DebugType=none -nologo -v q || (echo building Setup.exe failed & exit /b 1)
rem the launcher's updater (PortUpdater in MVP Workshop) installs "Port Files" from this zip: Launcher.exe (setup's own
rem exe) so the launcher updates itself, the version it compares with the release tag (v<VERSION>), and the list of
rem every port file with its size and SHA-256, which the update checks and uses to remove files a release dropped
copy /y "%PKG%\Setup.exe" "%PORT%\Launcher.exe" >nul || exit /b 1
>"%PORT%\version.txt" echo %VER%
powershell -NoProfile -Command "$ErrorActionPreference = 'Stop'; Import-Module '%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1' -Force; $root = (Resolve-Path '%PORT%').Path; $files = Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object Name -ne 'port-manifest.json' | ForEach-Object { [ordered]@{ path = $_.FullName.Substring($root.Length + 1).Replace('\','/'); size = $_.Length; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLower() } }; [ordered]@{ format = 1; version = '%VER%'; modFormat = 1; layout = 1; workshopReload = 1; workshopLiveTextures = 1; workshopLaunch = 1; workshopReloadMode = 'live-textures'; files = @($files) } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $root 'port-manifest.json')" || (echo writing port-manifest.json failed & exit /b 1)
"%SEVENZIP%" a "%ZIP%" ".\%PKG%\*" >nul || exit /b 1
powershell -NoProfile -Command "$ErrorActionPreference = 'Stop'; Import-Module '%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1' -Force; '{0} *{1}' -f (Get-FileHash -Algorithm SHA256 -LiteralPath '%ZIP%').Hash.ToLower(), '%ZIP%' | Set-Content -Encoding ASCII -LiteralPath '%ZIP%.sha256'" || exit /b 1
echo %ZIP%
