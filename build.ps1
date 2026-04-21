#-------------------------------------------------------------------------------------------------------
# Copyright (C) ChakraCore Project Contributors. All rights reserved.
# Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
#-------------------------------------------------------------------------------------------------------

[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$TestsConfiguration = "Test",
    [int]$MaxCpuCount = 4,
    [switch]$SkipTests,
    [switch]$SkipRepl
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSCommandPath
$SingleFileTestsFailed = $false
$FullTestSuiteFailed = $false
$NativeTestsFailed = $false

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Normalize-ConfigurationName {
    param([Parameter(Mandatory = $true)][string]$Name)

    switch ($Name.ToLowerInvariant()) {
        "debug" { return "Debug" }
        "release" { return "Release" }
        "test" { return "Test" }
        default { throw "Unsupported configuration '$Name'. Use Debug, Release, or Test." }
    }
}

function Normalize-PlatformName {
    param([Parameter(Mandatory = $true)][string]$Name)

    switch ($Name.ToLowerInvariant()) {
        "x86" { return "x86" }
        "win32" { return "x86" }
        "x64" { return "x64" }
        "amd64" { return "x64" }
        "arm" { return "arm" }
        "arm64" { return "arm64" }
        default { throw "Unsupported platform '$Name'. Use x86, x64, arm, or arm64." }
    }
}

function Resolve-CargoPath {
    $cargo = Get-Command cargo.exe -ErrorAction SilentlyContinue
    if (-not $cargo) {
        $cargo = Get-Command cargo -ErrorAction SilentlyContinue
    }

    if (-not $cargo) {
        throw "cargo was not found on PATH. Install Rust and ensure cargo is available before running build.ps1."
    }

    return $cargo.Path
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

    $cargoPath = Resolve-CargoPath

    Push-Location $rustProjectPath
    try {
        & $cargoPath build --release
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

function Invoke-RustRuntimeBuild {
    param([Parameter(Mandatory = $true)][string]$BuildConfiguration)

    $runtimeProjectPath = Join-Path $RepoRoot "rust\runtime"
    $runtimeCargoTomlPath = Join-Path $runtimeProjectPath "Cargo.toml"
    if (-not (Test-Path -Path $runtimeCargoTomlPath)) {
        throw "Rust runtime crate was not found at $runtimeProjectPath."
    }

    $cargoPath = Resolve-CargoPath
    $runtimeBuildFolder = "debug"
    $cargoArgs = @("build", "--manifest-path", $runtimeCargoTomlPath)

    if ($BuildConfiguration -ne "Debug") {
        $cargoArgs += "--release"
        $runtimeBuildFolder = "release"
    }

    Push-Location $runtimeProjectPath
    try {
        & $cargoPath @cargoArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Rust runtime build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }

    return (Join-Path $runtimeProjectPath ("target\{0}\chakra_runtime.exe" -f $runtimeBuildFolder))
}

function Invoke-ChakraSolutionBuild {
    param(
        [Parameter(Mandatory = $true)][string]$MsBuildPath,
        [Parameter(Mandatory = $true)][string]$BuildConfiguration,
        [Parameter(Mandatory = $true)][string]$BuildPlatform
    )

    $solutionPath = Join-Path $RepoRoot "Build\Chakra.Core.sln"
    if (-not (Test-Path -Path $solutionPath)) {
        throw "Solution file was not found at $solutionPath."
    }

    & $MsBuildPath $solutionPath "/p:Configuration=$BuildConfiguration" "/p:Platform=$BuildPlatform" "/m:$MaxCpuCount" "/v:m"
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE."
    }
}

function Resolve-BinOutputPath {
    param(
        [Parameter(Mandatory = $true)][string]$BuildConfiguration,
        [Parameter(Mandatory = $true)][string]$BuildPlatform
    )

    $configurationLower = $BuildConfiguration.ToLowerInvariant()
    $platformLower = $BuildPlatform.ToLowerInvariant()

    $candidatePaths = @(
        (Join-Path $RepoRoot "Build\VcBuild\bin\${platformLower}_${configurationLower}"),
        (Join-Path $RepoRoot "Build\VcBuild\bin\x64_release")
    )

    foreach ($candidate in $candidatePaths) {
        if (Test-Path -Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Resolve-ChExePath {
    param(
        [Parameter(Mandatory = $true)][string]$BuildConfiguration,
        [Parameter(Mandatory = $true)][string]$BuildPlatform
    )

    $binOutputPath = Resolve-BinOutputPath -BuildConfiguration $BuildConfiguration -BuildPlatform $BuildPlatform
    if (-not $binOutputPath) {
        return $null
    }

    $chExePath = Join-Path $binOutputPath "ch.exe"
    if (Test-Path -Path $chExePath) {
        return $chExePath
    }

    return $null
}

function Resolve-TestSuiteConfiguration {
    param(
        [Parameter(Mandatory = $true)][string]$PrimaryConfiguration,
        [Parameter(Mandatory = $true)][string]$RequestedTestConfiguration
    )

    if ($PrimaryConfiguration -eq "Debug" -or $PrimaryConfiguration -eq "Test") {
        return $PrimaryConfiguration
    }

    if ($RequestedTestConfiguration -eq "Release") {
        throw "TestsConfiguration cannot be Release. Use Debug or Test."
    }

    return $RequestedTestConfiguration
}

function Get-TestVariantSwitch {
    param(
        [Parameter(Mandatory = $true)][string]$BuildConfiguration,
        [Parameter(Mandatory = $true)][string]$BuildPlatform
    )

    $buildTypeSwitch = switch ($BuildConfiguration.ToLowerInvariant()) {
        "debug" { "debug" }
        "test" { "test" }
        default { throw "Unsupported test build configuration '$BuildConfiguration'. Use Debug or Test." }
    }

    $archSwitch = switch ($BuildPlatform.ToLowerInvariant()) {
        "x86" { "x86" }
        "x64" { "x64" }
        "arm" { "arm" }
        "arm64" { "arm64" }
        default { throw "Unsupported platform '$BuildPlatform' for test harness." }
    }

    return ("-{0}{1}" -f $archSwitch, $buildTypeSwitch)
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

function Invoke-FullTestSuites {
    param(
        [Parameter(Mandatory = $true)][string]$BuildConfiguration,
        [Parameter(Mandatory = $true)][string]$BuildPlatform
    )

    $testsRoot = Join-Path $RepoRoot "test"
    $runtestsCmd = Join-Path $testsRoot "runtests.cmd"
    $nativeTestsCmd = Join-Path $testsRoot "runnativetests.cmd"
    $binDirBase = Join-Path $RepoRoot "Build\VcBuild\bin"
    $testSwitch = Get-TestVariantSwitch -BuildConfiguration $BuildConfiguration -BuildPlatform $BuildPlatform

    if (-not (Test-Path -Path $runtestsCmd)) {
        throw "runtests.cmd was not found at $runtestsCmd."
    }

    if (-not (Test-Path -Path $nativeTestsCmd)) {
        throw "runnativetests.cmd was not found at $nativeTestsCmd."
    }

    Push-Location $testsRoot
    try {
        Write-Host ""
        Write-Host "[test/*] Running runtests.cmd $testSwitch"
        & $runtestsCmd $testSwitch "-bindir" $binDirBase
        if ($LASTEXITCODE -ne 0) {
            $script:FullTestSuiteFailed = $true
            Write-Warning "test/runtests.cmd failed with exit code $LASTEXITCODE."
        }

        $platformLower = $BuildPlatform.ToLowerInvariant()
        if ($platformLower -eq "x86" -or $platformLower -eq "x64") {
            Write-Host ""
            Write-Host "[test/native-tests] Running runnativetests.cmd $testSwitch"
            & $nativeTestsCmd $testSwitch "-binDir" $binDirBase
            if ($LASTEXITCODE -ne 0) {
                $script:NativeTestsFailed = $true
                Write-Warning "test/runnativetests.cmd failed with exit code $LASTEXITCODE."
            }
        }
        else {
            Write-Warning "Skipping native tests for platform '$BuildPlatform' because runnativetests.cmd supports only x86 and x64."
        }
    }
    finally {
        Pop-Location
    }
}

function Get-OsDistName {
    if ([System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT) {
        return "windows"
    }

    return "unknown"
}

function Get-ArchDistName {
    param([Parameter(Mandatory = $true)][string]$BuildPlatform)

    switch ($BuildPlatform.ToLowerInvariant()) {
        "x86" { return "x86" }
        "x64" { return "x64" }
        "arm" { return "arm" }
        "arm64" { return "arm64" }
        default { return $BuildPlatform.ToLowerInvariant() }
    }
}

function Copy-ArtifactIfExists {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$DestinationDirectory,
        [switch]$Required
    )

    if (Test-Path -Path $SourcePath) {
        Copy-Item -Path $SourcePath -Destination $DestinationDirectory -Force
        return
    }

    if ($Required) {
        throw "Required artifact was not found: $SourcePath"
    }

    Write-Host "Skipping optional artifact (not found): $SourcePath"
}

function Invoke-CreateDistLayout {
    param(
        [Parameter(Mandatory = $true)][string]$BuildConfiguration,
        [Parameter(Mandatory = $true)][string]$BuildPlatform,
        [Parameter(Mandatory = $true)][string]$RustRuntimeExePath
    )

    $binOutputPath = Resolve-BinOutputPath -BuildConfiguration $BuildConfiguration -BuildPlatform $BuildPlatform
    if (-not $binOutputPath) {
        throw "Build output directory was not found for $BuildPlatform/$BuildConfiguration."
    }

    $distRoot = Join-Path $RepoRoot "dist"
    $distName = "{0}-{1}" -f (Get-OsDistName), (Get-ArchDistName -BuildPlatform $BuildPlatform)
    $distPath = Join-Path $distRoot $distName

    if (Test-Path -Path $distPath) {
        Remove-Item -Path $distPath -Recurse -Force
    }

    New-Item -Path $distPath -ItemType Directory -Force | Out-Null

    foreach ($requiredBinFile in @("ch.exe", "ChakraCore.dll")) {
        Copy-ArtifactIfExists -SourcePath (Join-Path $binOutputPath $requiredBinFile) -DestinationDirectory $distPath -Required
    }

    foreach ($optionalBinFile in @("rl.exe", "nativetests.exe", "ChakraCore.lib", "ch.pdb", "ChakraCore.pdb")) {
        Copy-ArtifactIfExists -SourcePath (Join-Path $binOutputPath $optionalBinFile) -DestinationDirectory $distPath
    }

    Copy-ArtifactIfExists -SourcePath $RustRuntimeExePath -DestinationDirectory $distPath -Required
    Copy-ArtifactIfExists -SourcePath ([System.IO.Path]::ChangeExtension($RustRuntimeExePath, "pdb")) -DestinationDirectory $distPath

    $rustPackagesOutputPath = $env:CHAKRA_RUST_PACKAGES_PATH
    if ([string]::IsNullOrWhiteSpace($rustPackagesOutputPath)) {
        $rustPackagesOutputPath = Join-Path $RepoRoot "rust\chakra_packages\target\release"
    }

    foreach ($rustPackageArtifact in @("chakra_packages.dll", "chakra_packages.lib", "chakra_packages.pdb")) {
        Copy-ArtifactIfExists -SourcePath (Join-Path $rustPackagesOutputPath $rustPackageArtifact) -DestinationDirectory $distPath
    }

    Copy-ArtifactIfExists -SourcePath (Join-Path $RepoRoot "LICENSE.txt") -DestinationDirectory $distPath -Required
    Copy-ArtifactIfExists -SourcePath (Join-Path $RepoRoot "THIRD-PARTY-NOTICES.txt") -DestinationDirectory $distPath -Required
    Copy-ArtifactIfExists -SourcePath (Join-Path $RepoRoot "README.md") -DestinationDirectory $distPath

    $runtimeReadmePath = Join-Path $RepoRoot "rust\runtime\README.md"
    if (Test-Path -Path $runtimeReadmePath) {
        Copy-Item -Path $runtimeReadmePath -Destination (Join-Path $distPath "RUST-RUNTIME-README.md") -Force
    }

    $distInfoPath = Join-Path $distPath "DIST-INFO.txt"
    $distInfoLines = @(
        "ChakraCore distribution artifacts",
        "Configuration=$BuildConfiguration",
        "Platform=$BuildPlatform",
        "BinOutput=$binOutputPath",
        "RustRuntime=$RustRuntimeExePath",
        "RustPackages=$rustPackagesOutputPath"
    )
    Set-Content -Path $distInfoPath -Value $distInfoLines -Encoding Ascii

    return $distPath
}

try {
    Set-Location $RepoRoot

    $Configuration = Normalize-ConfigurationName -Name $Configuration
    $Platform = Normalize-PlatformName -Name $Platform
    $TestsConfiguration = Normalize-ConfigurationName -Name $TestsConfiguration
    if ($TestsConfiguration -eq "Release") {
        throw "TestsConfiguration cannot be Release. Use Debug or Test."
    }

    Write-Step "Building Rust package crate first"
    Invoke-RustPackageBuild

    Write-Step "Building Rust runtime host"
    $rustRuntimeExePath = Invoke-RustRuntimeBuild -BuildConfiguration $Configuration
    if (-not (Test-Path -Path $rustRuntimeExePath)) {
        throw "Rust runtime executable was not found at $rustRuntimeExePath after build."
    }
    Write-Host "Using Rust runtime: $rustRuntimeExePath"

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

    Write-Step "Building Build\\Chakra.Core.sln ($Configuration|$Platform)"
    Invoke-ChakraSolutionBuild -MsBuildPath $msbuildPath -BuildConfiguration $Configuration -BuildPlatform $Platform

    $chExePath = Resolve-ChExePath -BuildConfiguration $Configuration -BuildPlatform $Platform
    if (-not $chExePath) {
        throw "ch.exe was not found under Build\\VcBuild\\bin for $Platform/$Configuration after build."
    }
    Write-Host "Using ch.exe: $chExePath"

    Write-Step "Preparing dist artifacts"
    $distPath = Invoke-CreateDistLayout -BuildConfiguration $Configuration -BuildPlatform $Platform -RustRuntimeExePath $rustRuntimeExePath
    Write-Host "Distribution output: $distPath"

    if (-not $SkipTests) {
        Write-Step "Running single_file_tests"
        Invoke-SingleFileTests -ChExePath $chExePath

        $testSuiteConfiguration = Resolve-TestSuiteConfiguration -PrimaryConfiguration $Configuration -RequestedTestConfiguration $TestsConfiguration
        if ($testSuiteConfiguration -ne $Configuration) {
            Write-Step "Building Build\\Chakra.Core.sln ($testSuiteConfiguration|$Platform) for test/*"
            Invoke-ChakraSolutionBuild -MsBuildPath $msbuildPath -BuildConfiguration $testSuiteConfiguration -BuildPlatform $Platform
        }

        Write-Step "Running test/* suite (runtests + native tests)"
        Invoke-FullTestSuites -BuildConfiguration $testSuiteConfiguration -BuildPlatform $Platform
    }
    else {
        Write-Host "Skipping single_file_tests and test/* because -SkipTests was provided."
    }

    if (-not $SkipRepl) {
        Write-Step "Starting ch.exe REPL"
        & $chExePath
    }
    else {
        Write-Host "Skipping REPL because -SkipRepl was provided."
    }

    if ($SingleFileTestsFailed -or $FullTestSuiteFailed -or $NativeTestsFailed) {
        exit 2
    }

    exit 0
}
catch {
    Write-Error $_
    exit 1
}
