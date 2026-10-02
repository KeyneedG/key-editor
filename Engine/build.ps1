[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$ConfigFile = (Join-Path $PSScriptRoot 'engine.json'),
    [string]$ToolchainDirectory = 'C:\msys64\ucrt64',
    [switch]$Rebuild,
    [switch]$Clean,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
try {
    $engineDirectory = $PSScriptRoot
    $buildDirectory = Join-Path $PSScriptRoot "build\GCC\$Configuration"
    $configPath = (Resolve-Path -LiteralPath $ConfigFile).Path
    $toolchainPath = (Resolve-Path -LiteralPath $ToolchainDirectory).Path
    $toolchainBin = Join-Path $toolchainPath 'bin'
    $compiler = Join-Path $toolchainBin 'g++.exe'
    $cmake = Join-Path $toolchainBin 'cmake.exe'
    $ninja = Join-Path $toolchainBin 'ninja.exe'
    foreach ($tool in @($compiler, $cmake, $ninja)) {
        if (-not (Test-Path -LiteralPath $tool)) {
            throw "Missing tool: $tool. Install the MSYS2 UCRT64 GCC, CMake, and Ninja packages."
        }
    }

    # Change only this build process's environment, preserving other consoles.
    # Some parent shells expose both Path and PATH; normalize before launching tools.
    $taskBuildPath = "$toolchainBin;$env:PATH"
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    $env:Path = $taskBuildPath

    if ($Clean) {
        if (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt')) {
            & $cmake --build $buildDirectory --target clean
            if ($LASTEXITCODE -ne 0) { throw 'Clean failed.' }
        }
        return
    }
    $autoStart = if ($NoLaunch) { 'OFF' } else { 'ON' }
    & $cmake -S $engineDirectory -B $buildDirectory -G Ninja `
        "-DCMAKE_CXX_COMPILER=$compiler" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_BUILD_TYPE=$Configuration" `
        "-DTINY3D_CONFIG=$configPath" "-DTINY3D_AUTO_START=$autoStart"
    if ($LASTEXITCODE -ne 0) { throw 'Configuration failed. Check compiler reflection support, JSON settings, and the project manifest.' }
    $buildArguments = @('--build', $buildDirectory, '--parallel')
    if ($Rebuild) { $buildArguments += '--clean-first' }
    & $cmake @buildArguments
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

    # Visual Studio's built-in MIEngine can read GCC symbols through local GDB.
    $gdb = Join-Path $toolchainBin 'gdb.exe'
    $executable = Join-Path $buildDirectory 'tiny3d_project.exe'
    $settings = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    if ($settings.project -and (Test-Path -LiteralPath $gdb) -and (Test-Path -LiteralPath $executable)) {
        $debugDocument = New-Object System.Xml.XmlDocument
        $debugOptions = $debugDocument.CreateElement('LocalLaunchOptions',
            'http://schemas.microsoft.com/vstudio/MDDDebuggerOptions/2014')
        $debugOptions.SetAttribute('MIDebuggerPath', $gdb)
        $debugOptions.SetAttribute('ExePath', $executable)
        $debugOptions.SetAttribute('WorkingDirectory', $engineDirectory)
        $debugOptions.SetAttribute('TargetArchitecture', 'X64')
        $debugOptions.SetAttribute('MIMode', 'gdb')
        $debugOptions.SetAttribute('ExternalConsole', 'false')
        $debugDocument.AppendChild($debugOptions) | Out-Null
        $debugDocument.Save((Join-Path $buildDirectory 'gdb.xml'))
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
