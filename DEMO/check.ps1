[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$engineDirectory = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
foreach ($mode in @('editor', 'game')) {
    & (Join-Path $engineDirectory 'Editor\build.ps1') -Configuration $Configuration `
        -ConfigFile (Join-Path $PSScriptRoot "$mode.json") -NoLaunch
    if ($LASTEXITCODE -ne 0) { throw "$mode build failed." }
    $executable = Join-Path $engineDirectory "Editor\build\$Configuration\tiny3d_project.exe"
    & $executable --render (Join-Path $outputDirectory "$mode.ppm")
    if ($LASTEXITCODE -ne 0) { throw "$mode render failed." }
    & $executable --frames 3
    if ($LASTEXITCODE -ne 0) { throw "$mode window smoke test failed." }
}
if ((Get-FileHash (Join-Path $outputDirectory 'editor.ppm')).Hash -ne
    (Get-FileHash (Join-Path $outputDirectory 'game.ppm')).Hash) {
    throw 'The editor and game modes produced different images.'
}
Write-Host 'Both demo modes built, rendered identically, and passed the window smoke test.'
