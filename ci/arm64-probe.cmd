@echo off
setlocal enabledelayedexpansion
echo ============ ARM64 CLR host probe ============

set "WS=%GITHUB_WORKSPACE%"
if "%WS%"=="" set "WS=%~dp0.."
echo workspace: %WS%

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe not found
  exit /b 1
)

set "VS="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath`) do set "VS=%%i"
if "%VS%"=="" (
  echo ERROR: no VS install with Microsoft.VisualStudio.Component.VC.Tools.ARM64
  exit /b 1
)
echo VS install: %VS%

call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" arm64
if errorlevel 1 (
  echo ERROR: vcvarsall.bat arm64 failed
  exit /b 1
)

echo.
echo ---- cl.exe ----
where cl
echo ---- LIB (arm64 dirs) ----
echo %LIB%

if exist "%WS%\out" rmdir /s /q "%WS%\out"
mkdir "%WS%\out"
mkdir "%WS%\out\managed"

echo.
echo ============ [1/3] compile managed assembly (IL) ============
set "CSC="
for %%p in (
  "%WINDIR%\Microsoft.NET\FrameworkArm64\v4.0.30319\csc.exe"
  "%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
) do if exist %%p if not defined CSC set "CSC=%%~p"

if not defined CSC (
  echo ERROR: csc.exe not found
  exit /b 1
)
echo csc: !CSC!
"!CSC!" /nologo /target:library /out:"%WS%\out\managed\NppArm64Probe.Managed.dll" /r:System.Runtime.InteropServices.dll "%WS%\ci\Managed.cs"
if errorlevel 1 (
  echo RESULT: MANAGED BUILD FAILED
  exit /b 1
)
echo managed build OK

echo.
echo ============ [2/3] compile native ARM64 probe DLL (links mscoree.lib) ============
cl /nologo /LD /TP /EHsc /O2 /DWIN32_LEAN_AND_MEAN "%WS%\ci\probe.cpp" /link mscoree.lib /OUT:"%WS%\out\NppArm64Probe.dll"
if errorlevel 1 (
  echo RESULT: NATIVE BUILD FAILED ^(mscoree.lib missing for arm64?^)
  exit /b 1
)
echo native probe build OK

echo.
echo ============ [3/3] compile native ARM64 harness exe ============
cl /nologo /TP /EHsc /O2 /DWIN32_LEAN_AND_MEAN "%WS%\ci\host.cpp" /link /OUT:"%WS%\out\NppArm64Host.exe"
if errorlevel 1 (
  echo RESULT: HARNESS BUILD FAILED
  exit /b 1
)
echo harness build OK

echo.
echo ============ arch check ============
powershell -NoProfile -Command "$ErrorActionPreference='SilentlyContinue'; foreach($f in @('%WS%\out\NppArm64Probe.dll','%WS%\out\NppArm64Host.exe')){ $b=[IO.File]::ReadAllBytes($f); $p=[BitConverter]::ToInt32($b,0x3C); $m=[BitConverter]::ToUInt16($b,$p+4); '{0} -> 0x{1:X} ({2})' -f (Split-Path $f -Leaf), $m, $(if($m -eq 0xAA64){'ARM64'}elseif($m -eq 0x8664){'AMD64'}elseif($m -eq 0x14c){'I386'}else{'other'}) }"

echo.
echo ============ run probe ============
"%WS%\out\NppArm64Host.exe" "%WS%\out\NppArm64Probe.dll"
set "RC=%ERRORLEVEL%"
echo probe exit code: %RC%

echo.
echo ============ managed report ============
if exist "%WS%\out\managed-report.txt" (
  type "%WS%\out\managed-report.txt"
) else (
  echo NO REPORT FILE - managed code did not run
)

echo.
if "%RC%"=="0" (
  echo RESULT: ARM64 NETFX HOSTING OK
) else (
  echo RESULT: ARM64 NETFX HOSTING FAILED ^(rc=%RC%^)
)
exit /b %RC%
