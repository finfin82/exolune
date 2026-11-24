# Clones AMD FidelityFX FSR 2.2.1 and builds the DX12 static API used by MyPlanetEngine.
# The SDK is placed in third_party/FidelityFX-FSR2/sdk/ (gitignored). The engine project
# stays on PlatformToolset v143; this script only builds the third-party libraries.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_fsr2.ps1
[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"
if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$Tag = "v2.2.1"
$Commit = "1680d1edd5c034f88ebbbb793d8b88f8842cf804"
$Sdk = Join-Path $RepoRoot "third_party\FidelityFX-FSR2\sdk"
$Lib = Join-Path $Sdk "src\ffx-fsr2-api\bin\ffx_fsr2_api\ffx_fsr2_api_x64.lib"
$DxLib = Join-Path $Sdk "src\ffx-fsr2-api\bin\ffx_fsr2_api\ffx_fsr2_api_dx12_x64.lib"
$Source = "https://github.com/GPUOpen-Effects/FidelityFX-FSR2.git"

function Find-Git {
    $cmd = Get-Command git -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $wsl = Get-Command wsl -ErrorAction SilentlyContinue
    if ($wsl) { return "wsl-git" }
    throw "git is required to clone FidelityFX-FSR2 ($Source)."
}

function Invoke-Git {
    param([string[]]$GitArgs, [string]$WorkDir = "")
    $git = Find-Git
    if ($git -eq "wsl-git") {
        $unix = $WorkDir
        if ($WorkDir) {
            $unix = (wsl -e wslpath -a $WorkDir).Trim()
            wsl -e git -C $unix @GitArgs
        } else {
            wsl -e git @GitArgs
        }
    } else {
        if ($WorkDir) {
            & $git -C $WorkDir @GitArgs
        } else {
            & $git @GitArgs
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "git $($GitArgs -join ' ') failed with exit $LASTEXITCODE"
    }
}

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
        if ($vs) {
            $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $cmake) { return $cmake }
        }
    }
    throw "cmake is required to build FSR 2. Run this script from a Visual Studio developer environment, or add cmake to PATH."
}

function Choose-Generator {
    if ($Generator) { return $Generator }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $line = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName
        if ($line -match "2022") { return "Visual Studio 17 2022" }
        if ($line -match "2026") { return "Visual Studio 18 2026" }
        if ($line -match "2019") { return "Visual Studio 16 2019" }
    }
    return "Visual Studio 17 2022"
}

if (-not (Test-Path (Join-Path $Sdk ".git"))) {
    New-Item -ItemType Directory -Force -Path (Split-Path $Sdk) | Out-Null
    if (Test-Path $Sdk) {
        throw "Unexpected existing folder at $Sdk (not a git clone). Move it aside and re-run."
    }
    Write-Host "Cloning FidelityFX-FSR2 $Tag into $Sdk"
    Invoke-Git @("clone", "--branch", $Tag, "--depth", "1", $Source, $Sdk)
}

$head = (Invoke-Git @("rev-parse", "HEAD") -WorkDir $Sdk | Out-String).Trim()
# When invoked through the function, stdout may already have been emitted; read it explicitly.
$git = Find-Git
if ($git -eq "wsl-git") {
    $unix = (wsl -e wslpath -a $Sdk).Trim()
    $head = (wsl -e git -C $unix rev-parse HEAD).Trim()
} else {
    $head = (& $git -C $Sdk rev-parse HEAD).Trim()
}
if ($head -ne $Commit) {
    Write-Warning "FSR2 SDK HEAD is $head (expected $Commit for $Tag). Continuing with the checkout that is present."
}

if ((Test-Path $Lib) -and (Test-Path $DxLib)) {
    Write-Host "FSR2 DX12 libraries already present:"
    Write-Host "  $Lib"
    Write-Host "  $DxLib"
    exit 0
}

$cmake = Find-CMake
$gen = Choose-Generator
$buildDir = Join-Path $Sdk "build\api-dx12"
$srcDir = Join-Path $Sdk "src\ffx-fsr2-api"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

Write-Host "Configuring FSR2 API with $gen (DX12 only, Vulkan off)"
& $cmake -S $srcDir -B $buildDir -G $gen -A x64 -DFFX_FSR2_API_DX12=ON -DFFX_FSR2_API_VK=OFF
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "Building FSR2 API (Release)"
& $cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

if (-not ((Test-Path $Lib) -and (Test-Path $DxLib))) {
    throw "Build finished but the expected libraries were not found:`n  $Lib`n  $DxLib"
}

Write-Host "FSR2 DX12 libraries ready:"
Write-Host "  $Lib"
Write-Host "  $DxLib"
