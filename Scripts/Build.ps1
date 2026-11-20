<#
.SYNOPSIS
    Configures, builds and tests Aegis with the MSVC toolchain.

.DESCRIPTION
    Windows does not place the MSVC toolchain on the PATH by default, so this
    script imports the vcvars environment first and then drives CMake/Ninja.
    It is the canonical build entry point for local development and CI.

.EXAMPLE
    ./Scripts/Build.ps1
    ./Scripts/Build.ps1 -Configuration Release -Test
    ./Scripts/Build.ps1 -Clean -Configuration Debug
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'Dist')]
    [string]$Configuration = 'Debug',

    [string]$Target = '',

    [switch]$Clean,
    [switch]$Test,
    [switch]$NoConfigure,
    [string]$BuildDirectory = '',
    [string]$CMakeArgs = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root

try
{
    $preset = "windows-" + $Configuration.ToLowerInvariant()
    if (-not $BuildDirectory)
    {
        $BuildDirectory = Join-Path $root ("build/" + $preset)
    }

    if ($Clean -and (Test-Path $BuildDirectory))
    {
        Write-Host "Removing $BuildDirectory" -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDirectory
    }

    # --- Locate the Visual C++ toolchain -------------------------------------
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path $vswhere))
    {
        throw "vswhere.exe not found. Install Visual Studio 2022+ with the C++ workload."
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath)
    {
        throw "No Visual Studio installation with the C++ toolchain was found."
    }

    $vcvars = Join-Path $vsPath 'VC/Auxiliary/Build/vcvars64.bat'
    if (-not (Test-Path $vcvars))
    {
        throw "vcvars64.bat not found at $vcvars"
    }

    # Import the environment that vcvars produces into this PowerShell process.
    $batPath = Join-Path ([System.IO.Path]::GetTempPath()) 'aegis-import-vcvars.bat'
    $batLines = @('@echo off', ('call "' + $vcvars + '"'), 'set')
    Set-Content -Path $batPath -Value $batLines -Encoding ASCII
    $envLines = cmd /c ('"' + $batPath + '"')
    foreach ($line in $envLines)
    {
        if ($line -match '^([^=]+)=(.*)$')
        {
            Set-Item -Path ('env:' + $matches[1]) -Value $matches[2]
        }
    }

    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmakeCommand)
    {
        $cmake = $cmakeCommand.Source
    }
    else
    {
        $cmake = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        if (-not (Test-Path $cmake))
        {
            throw "cmake.exe was not found on PATH or inside the Visual Studio installation."
        }
    }

    $ninjaPath = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    if ((Test-Path $ninjaPath) -and -not (($env:PATH -split ';') -contains $ninjaPath))
    {
        $env:PATH = $ninjaPath + ';' + $env:PATH
    }

    # --- Configure -----------------------------------------------------------
    if (-not $NoConfigure)
    {
        Write-Host "Configuring $Configuration -> $BuildDirectory" -ForegroundColor Cyan
        $configureArgs = @('-S', $root, '-B', $BuildDirectory, '-G', 'Ninja', ('-DCMAKE_BUILD_TYPE=' + $Configuration))
        if ($CMakeArgs)
        {
            $configureArgs += $CMakeArgs.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
        }
        & $cmake @configureArgs
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
    }

    # --- Build ---------------------------------------------------------------
    Write-Host "Building $Configuration" -ForegroundColor Cyan
    $buildArgs = @('--build', $BuildDirectory, '--config', $Configuration)
    if ($Target)
    {
        $buildArgs += @('--target', $Target)
    }
    & $cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

    # --- Test ----------------------------------------------------------------
    if ($Test)
    {
        Write-Host "Running tests" -ForegroundColor Cyan
        $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
        if (-not (Test-Path $ctest))
        {
            $ctest = 'ctest'
        }
        & $ctest --test-dir $BuildDirectory --output-on-failure -C $Configuration
        if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)." }
    }
}
finally
{
    Pop-Location
}
