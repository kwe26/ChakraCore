#-------------------------------------------------------------------------------------------------------
# Copyright (C) ChakraCore Project Contributors. All rights reserved.
# Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
#-------------------------------------------------------------------------------------------------------

[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [int]$MaxCpuCount = 4,
    [switch]$SkipTests,
    [switch]$SkipRepl
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSCommandPath
$SingleFileTestsFailed = $false

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Resolve-VsWherePath {
    $candidatePaths = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\Installer\vswhere.exe")
    )

    foreach ($candidate in $candidatePaths) {
        if (Test-Path -Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Resolve-VisualStudioPath {
    $vswherePath = Resolve-VsWherePath
    if ($vswherePath) {
        # Prefer VS2022 (17.x)
        $vs2022 = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -version "[17.0,18.0)" -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($vs2022)) {
            return $vs2022.Trim()
        }

        # Fallback to latest version that includes MSBuild
        $latest = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($latest)) {
            return $latest.Trim()
        }
    }

    $fallbackCandidates = @(
        (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Enterprise"),
        (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Professional"),
        (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Community"),
        (Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\BuildTools"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Enterprise"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Professional"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Community"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\BuildTools")
    )

    foreach ($candidate in $fallbackCandidates) {
        if (Test-Path -Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Resolve-MsBuildPath {
    param([Parameter(Mandatory = $true)][string]$VisualStudioPath)

    $msbuildCandidates = @(
        (Join-Path $VisualStudioPath "MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $VisualStudioPath "MSBuild\15.0\Bin\MSBuild.exe")
    )

    foreach ($candidate in $msbuildCandidates) {
        if (Test-Path -Path $candidate) {
            return $candidate
        }
    }

    $pathMsBuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($pathMsBuild) {
        return $pathMsBuild.Path
    }

    return $null
}

function Invoke-RustPackageBuild {
    $rustProjectPath = Join-Path $RepoRoot "rust\chakra_packages"
    if (-not (Test-Path -Path (Join-Path $rustProjectPath "Cargo.toml"))) {
        $legacyRustProjectPath = Join-Path $RepoRoot "bin\ch\rust\chakra_packages"
        if (Test-Path -Path (Join-Path $legacyRustProjectPath "Cargo.toml")) {
            $rustProjectPath = $legacyRustProjectPath
        }
    }

    $cargoTomlPath = Join-Path $rustProjectPath "Cargo.toml"

    if (-not (Test-Path -Path $cargoTomlPath)) {
        throw "Rust package crate was not found at $rustProjectPath."
    }

    $cargo = Get-Command cargo.exe -ErrorAction SilentlyContinue
    if (-not $cargo) {
        throw "cargo.exe was not found on PATH. Install Rust and ensure cargo is available before running build.ps1."
    }

    Push-Location $rustProjectPath
    try {
        & $cargo.Path build --release
        if ($LASTEXITCODE -ne 0) {
            throw "Rust package build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }

    $env:CHAKRA_RUST_PACKAGES_PATH = Join-Path $rustProjectPath "target\release"
    Write-Host "Using CHAKRA_RUST_PACKAGES_PATH=$env:CHAKRA_RUST_PACKAGES_PATH"
}

function Invoke-ChakraSolutionBuild {
    param([Parameter(Mandatory = $true)][string]$MsBuildPath)

    $solutionPath = Join-Path $RepoRoot "Build\Chakra.Core.sln"
    if (-not (Test-Path -Path $solutionPath)) {
        throw "Solution file was not found at $solutionPath."
    }

    & $MsBuildPath $solutionPath "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/m:$MaxCpuCount" "/v:m"
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
}

function Resolve-ChExePath {
    $configurationLower = $Configuration.ToLowerInvariant()
    $platformLower = $Platform.ToLowerInvariant()

    $candidatePaths = @(
        (Join-Path $RepoRoot "Build\VcBuild\bin\${platformLower}_${configurationLower}\ch.exe"),
        (Join-Path $RepoRoot "Build\VcBuild\bin\x64_release\ch.exe")
    )

    foreach ($candidate in $candidatePaths) {
        if (Test-Path -Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Invoke-SingleFileTests {
    param([Parameter(Mandatory = $true)][string]$ChExePath)

    $testsPath = Join-Path $RepoRoot "single_file_tests"
    if (-not (Test-Path -Path $testsPath)) {
        throw "Test directory was not found at $testsPath."
    }

    $testFiles = Get-ChildItem -Path $testsPath -Filter "*.js" -File | Sort-Object -Property Name
    if ($testFiles.Count -eq 0) {
        Write-Host "No *.js tests found under $testsPath"
        return
    }

    $failedTests = @()
    foreach ($testFile in $testFiles) {
        Write-Host ""
        Write-Host "[single_file_tests] Running $($testFile.Name)"
        & $ChExePath $testFile.FullName
        if ($LASTEXITCODE -ne 0) {
            $failedTests += $testFile.FullName
            Write-Warning "Test failed: $($testFile.FullName) (exit code $LASTEXITCODE)"
        }
    }

    if ($failedTests.Count -gt 0) {
        $script:SingleFileTestsFailed = $true
        Write-Warning "$($failedTests.Count) single_file_tests script(s) failed."
        foreach ($failed in $failedTests) {
            Write-Warning "  $failed"
        }
    }
    else {
        Write-Host ""
        Write-Host "All single_file_tests scripts completed successfully."
    }
}

try {
    Set-Location $RepoRoot

    Write-Step "Building Rust package crate first"
    Invoke-RustPackageBuild

    Write-Step "Locating Visual Studio installation (prefers VS2022)"
    $vsPath = Resolve-VisualStudioPath
    if (-not $vsPath) {
        throw "Unable to locate a Visual Studio installation with MSBuild."
    }
    Write-Host "Using Visual Studio: $vsPath"

    $msbuildPath = Resolve-MsBuildPath -VisualStudioPath $vsPath
    if (-not $msbuildPath) {
        throw "Unable to locate MSBuild.exe in Visual Studio installation '$vsPath'."
    }
    Write-Host "Using MSBuild: $msbuildPath"

    Write-Step "Building Build\\Chakra.Core.sln"
    Invoke-ChakraSolutionBuild -MsBuildPath $msbuildPath

    $chExePath = Resolve-ChExePath
    if (-not $chExePath) {
        throw "ch.exe was not found under Build\\VcBuild\\bin after build."
    }
    Write-Host "Using ch.exe: $chExePath"

    if (-not $SkipTests) {
        Write-Step "Running single_file_tests"
        Invoke-SingleFileTests -ChExePath $chExePath
    }
    else {
        Write-Host "Skipping single_file_tests because -SkipTests was provided."
    }

    if (-not $SkipRepl) {
        Write-Step "Starting ch.exe REPL"
        & $chExePath
    }
    else {
        Write-Host "Skipping REPL because -SkipRepl was provided."
    }

    if ($SingleFileTestsFailed) {
        exit 2
    }

    exit 0
}
catch {
    Write-Error $_
    exit 1
}
