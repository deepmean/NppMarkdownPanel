<#
.SYNOPSIS
    Builds the native ARM64 package of NppMarkdownPanel.

.DESCRIPTION
    Produces plugins\NppMarkdownPanel\ content for Windows on ARM64:

      1. the managed plugin (AnyCPU .NET Framework IL, JITed natively by the
         ARM64 CLR),
      2. the native ARM64 shim that Notepad++ loads and that hosts the CLR,
      3. the dependency set and WebView2 native loader.

    No .NET 8 or any other runtime is required: .NET Framework 4.8.1 ships
    natively with Windows 11 on ARM64.

    The x86/x64 release path (build.ps1 + makerelease.ps1) is not touched.
#>
[CmdletBinding()]
param(
    [string] $Configuration = 'Release',
    [string] $Version       = '0.9.3.0'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$outRoot = Join-Path $root 'Release-arm64'
$plugin  = Join-Path $outRoot 'NppMarkdownPanel'

function Write-Step($text) {
    Write-Host ''
    Write-Host "=== $text ===" -ForegroundColor Cyan
}

# ---------------------------------------------------------------------------
# 0. toolchain discovery
# ---------------------------------------------------------------------------
Write-Step '0. Locating MSBuild and the ARM64 native toolchain'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere" }

$vsPath = & $vswhere -latest -prerelease -products * `
    -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { throw 'No Visual Studio installation with MSBuild was found.' }
Write-Host "Visual Studio : $vsPath"

$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $msbuild)) {
    $msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
}
if (-not (Test-Path $msbuild)) { throw "MSBuild.exe not found under $vsPath" }
Write-Host "MSBuild       : $msbuild"

# ---------------------------------------------------------------------------
# 1. managed plugin (AnyCPU .NET Framework)
# ---------------------------------------------------------------------------
Write-Step '1. Restoring NuGet packages'

# The dependency projects (MarkdigWrapper, PanelCommon, Webview2Viewer) still
# use packages.config, so they are restored through the existing solution
# rather than through the ARM64 project.
& $msbuild (Join-Path $root 'NppMarkdownPanel.sln') `
    /target:Restore /p:RestorePackagesConfig=true `
    /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "package restore failed with exit code $LASTEXITCODE" }

Write-Step '2. Building the managed plugin (AnyCPU)'

$proj = Join-Path $root 'NppMarkdownPanel\NppMarkdownPanel.Arm64.csproj'
& $msbuild $proj `
    /target:Clean /target:Build `
    /p:Configuration=$Configuration `
    /p:Platform=AnyCPU `
    /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "managed build failed with exit code $LASTEXITCODE" }

$managedOut = Join-Path $root "NppMarkdownPanel\bin\$Configuration-arm64"
$managedDll = Join-Path $managedOut 'NppMarkdownPanel.Managed.dll'
if (-not (Test-Path $managedDll)) { throw "managed output missing: $managedDll" }
Write-Host "managed       : $managedDll"

# ---------------------------------------------------------------------------
# 3. native ARM64 shim
# ---------------------------------------------------------------------------
Write-Step '3. Building the native ARM64 shim'

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path $vcvars)) { throw "vcvarsall.bat not found at $vcvars" }

$shimSrc = Join-Path $root 'arm64\NppShim.cpp'
$shimDll = Join-Path $managedOut 'NppMarkdownPanel.dll'

$nativeCmd = Join-Path $env:TEMP ("npp-arm64-native-{0}.cmd" -f ([guid]::NewGuid().ToString('N')))
@"
@echo off
call "$vcvars" arm64
if errorlevel 1 exit /b 1
cl /nologo /LD /TP /EHsc /O2 /MT /GS /DWIN32_LEAN_AND_MEAN "$shimSrc" /link mscoree.lib user32.lib advapi32.lib /OUT:"$shimDll"
exit /b %ERRORLEVEL%
"@ | Set-Content -Path $nativeCmd -Encoding ASCII

try {
    & cmd.exe /c $nativeCmd
    if ($LASTEXITCODE -ne 0) { throw "native shim build failed with exit code $LASTEXITCODE" }
}
finally {
    Remove-Item $nativeCmd -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path $shimDll)) { throw "native shim output missing: $shimDll" }
Write-Host "shim          : $shimDll"

# ---------------------------------------------------------------------------
# 4. assemble the plugin folder
# ---------------------------------------------------------------------------
Write-Step '4. Assembling the plugin folder'

if (Test-Path $plugin) { Remove-Item $plugin -Recurse -Force }
New-Item -ItemType Directory -Path $plugin -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $plugin 'lib') -Force | Out-Null

# Managed entry assembly and the shim Notepad++ loads.
Copy-Item $managedDll (Join-Path $plugin 'NppMarkdownPanel.Managed.dll') -Force
Copy-Item $shimDll    (Join-Path $plugin 'NppMarkdownPanel.dll') -Force
Copy-Item (Join-Path $root 'NppMarkdownPanel\style.css')       $plugin -Force
Copy-Item (Join-Path $root 'NppMarkdownPanel\style-dark.css')  $plugin -Force
Copy-Item (Join-Path $root 'README.md')                        $plugin -Force
Copy-Item (Join-Path $root 'License.txt')                      $plugin -Force
Copy-Item (Join-Path $root 'help')                             $plugin -Recurse -Force

# Dependencies: the satellite assemblies plus their transitive packages.
$depProjects = @('MarkdigWrapper', 'PanelCommon', 'Webview2Viewer')
foreach ($p in $depProjects) {
    $bin = Join-Path $root "$p\bin\$Configuration"
    if (-not (Test-Path $bin)) { throw "dependency output missing: $bin" }
    Get-ChildItem $bin -Filter *.dll -File | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $plugin 'lib') -Force
    }
}

# WebView2 ships a per-architecture native loader. Only the ARM64 one is
# included: this package targets Windows on ARM64 only, and the user's disk
# budget matters.
#
# Placement: the managed Microsoft.Web.WebView2.Core.dll P/Invokes
# "WebView2Loader.dll". Under .NET Framework that resolves via the altered
# search path, which starts at the directory of the calling assembly (lib\).
# The shim's own folder is also searched because Notepad++ loads the plugin
# with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR. A copy is placed in both locations so
# the Edge renderer initialises regardless of which path the loader takes.
$wv2Native = Join-Path $root "Webview2Viewer\bin\$Configuration\runtimes\win-arm64\native\WebView2Loader.dll"
if (Test-Path $wv2Native) {
    Copy-Item $wv2Native (Join-Path $plugin 'lib\WebView2Loader.dll') -Force
    Copy-Item $wv2Native (Join-Path $plugin 'WebView2Loader.dll') -Force

    $rtDir = Join-Path $plugin 'lib\runtimes\win-arm64\native'
    New-Item -ItemType Directory -Path $rtDir -Force | Out-Null
    Copy-Item $wv2Native (Join-Path $rtDir 'WebView2Loader.dll') -Force
} else {
    Write-Warning "WebView2 arm64 native loader not found at $wv2Native - the Edge renderer may fail to initialise."
}

Write-Host "plugin folder : $plugin"

# ---------------------------------------------------------------------------
# 5. package
# ---------------------------------------------------------------------------
Write-Step '5. Creating the ARM64 zip'

$zipName = "NppMarkdownPanel-$Version-arm64.zip"
$zipPath = Join-Path $outRoot $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Compress-Archive -Path (Join-Path $plugin '*') -DestinationPath $zipPath -Force
Write-Host "package       : $zipPath"

Write-Step '6. Verifying the produced binaries'
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($file in @(
        (Join-Path $plugin 'NppMarkdownPanel.dll'),
        (Join-Path $plugin 'lib\WebView2Loader.dll'))) {
    if (-not (Test-Path $file)) { Write-Host "  MISSING: $file" -ForegroundColor Yellow; continue }
    $bytes = [System.IO.File]::ReadAllBytes($file)
    $pe    = [BitConverter]::ToInt32($bytes, 0x3C)
    $mach  = [BitConverter]::ToUInt16($bytes, $pe + 4)
    $name  = switch ($mach) {
        0xAA64 { 'ARM64' } 0x8664 { 'AMD64' } 0x14C { 'I386' } default { '0x{0:X}' -f $mach }
    }
    Write-Host ("  {0,-52} {1}" -f (Split-Path $file -Leaf), $name)
}

# ---------------------------------------------------------------------------
# 6b. smoke test: load the shim like Notepad++ and cross into managed code
# ---------------------------------------------------------------------------
Write-Step '6b. Running the ARM64 smoke test'

$smokeSrc = Join-Path $root 'arm64\NppSmokeTest.cpp'
$smokeExe = Join-Path $outRoot 'NppArm64SmokeTest.exe'
$report   = Join-Path $outRoot 'managed-selftest.txt'

$smokeCmd = Join-Path $env:TEMP ("npp-arm64-smoke-{0}.cmd" -f ([guid]::NewGuid().ToString('N')))
@"
@echo off
call "$vcvars" arm64
if errorlevel 1 exit /b 1
cl /nologo /TP /EHsc /O2 /MT /DWIN32_LEAN_AND_MEAN "$smokeSrc" /link /OUT:"$smokeExe"
if errorlevel 1 exit /b 1
"$smokeExe" "$plugin" "$report"
exit /b %ERRORLEVEL%
"@ | Set-Content -Path $smokeCmd -Encoding ASCII

$smokeRc = 0
try {
    & cmd.exe /c $smokeCmd
    $smokeRc = $LASTEXITCODE
}
finally {
    Remove-Item $smokeCmd -Force -ErrorAction SilentlyContinue
}

if (Test-Path $report) {
    Write-Host ''
    Write-Host '--- managed self-test report ---'
    Get-Content $report | ForEach-Object { Write-Host "  $_" }
}

if ($smokeRc -ne 0) {
    throw "ARM64 smoke test failed (exit code $smokeRc). The package was built but not verified."
}
Write-Host 'smoke test    : PASS' -ForegroundColor Green

Write-Host ''
Write-Host "DONE: $zipPath" -ForegroundColor Green
