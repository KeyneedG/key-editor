[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$ConfigFile = (Join-Path $PSScriptRoot 'engine.json'),
    [switch]$Rebuild,
    [switch]$Clean,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
try {
    $engineDirectory = Split-Path -Parent $PSScriptRoot
    $buildDirectory = Join-Path $PSScriptRoot "build\$Configuration"
    $configPath = (Resolve-Path -LiteralPath $ConfigFile).Path

    # Some shells expose both Path and PATH. Normalize before invoking VS tools.
    $taskBuildPath = $env:PATH
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    $env:Path = $taskBuildPath

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Install Visual Studio 2022 with Desktop development with C++.'
    }
    $installation = & $vswhere -latest -version '[17.0,18.0)' -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        throw 'Visual Studio 2022 C++ tools were not found.'
    }
    $developerCommand = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    $environmentLines = & $env:ComSpec /d /s /c `
        ('"{0}" -no_logo -arch=x64 -host_arch=x64 >nul && set' -f $developerCommand)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the Visual Studio C++ environment.' }
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
    $cmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ninja = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    if (-not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath $ninja)) {
        throw 'Install the C++ CMake tools for Windows component in Visual Studio Installer.'
    }

    if ($Clean) {
        if (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt')) {
            & $cmake --build $buildDirectory --target clean
            if ($LASTEXITCODE -ne 0) { throw 'Clean failed.' }
        }
        return
    }
    $autoStart = if ($NoLaunch) { 'OFF' } else { 'ON' }
    & $cmake -S $engineDirectory -B $buildDirectory -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Configuration" `
        "-DTINY3D_CONFIG=$configPath" "-DTINY3D_AUTO_START=$autoStart" -DBUILD_TESTING=OFF
    if ($LASTEXITCODE -ne 0) { throw 'Configuration failed. Check the JSON settings and project manifest.' }
    $buildArguments = @('--build', $buildDirectory, '--parallel')
    if ($Rebuild) { $buildArguments += '--clean-first' }
    & $cmake @buildArguments
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
