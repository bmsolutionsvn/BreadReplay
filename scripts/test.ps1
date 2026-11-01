param(
  [ValidateSet('release', 'debug')][string]$Preset = 'release',
  [string]$Filter = ''
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
$exe = Join-Path $repo "build\$Preset\br_tests.exe"
if (-not (Test-Path $exe)) { throw "br_tests.exe not built yet; run scripts/build.ps1 first." }
if ($Filter) { & $exe $Filter } else { & $exe }
exit $LASTEXITCODE