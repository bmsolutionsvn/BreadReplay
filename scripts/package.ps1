#Requires -Version 5.1
# package.ps1 - build the release (UI + C++), then compile the Inno Setup installer into dist\.
param([string]$Version = '0.1.0')
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')

# build.ps1 builds the UI, the binaries, and stages ui\dist beside the exe.
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') -Preset release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if (-not (Test-Path (Join-Path $repo 'build\release\ui\index.html'))) { throw 'build\release\ui\index.html is missing.' }

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$redist = Get-ChildItem "$vs\VC\Redist\MSVC\*\vc_redist.x64.exe" | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $redist) { throw 'vc_redist.x64.exe not found in the Visual Studio install' }

$iscc = @(
  "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
  "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
  "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) { throw 'ISCC.exe (Inno Setup 6) not found; install it with: winget install --id JRSoftware.InnoSetup -e' }

& $iscc "/DAppVersion=$Version" "/DVcRedist=$($redist.FullName)" (Join-Path $repo 'installer\BreadReplay.iss')
exit $LASTEXITCODE
