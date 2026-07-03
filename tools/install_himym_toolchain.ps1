param(
    [switch]$SkipCMake,
    [switch]$PrepareLocalBuild,
    [switch]$BuildRelease
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Ensure-Winget {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget is required. Install App Installer from Microsoft Store, then re-run this script."
    }
}

function Install-CMakeIfMissing {
    if ($SkipCMake) {
        Write-Host "[Info] Skipping CMake install by request." -ForegroundColor Yellow
        return
    }

    if (Get-Command cmake -ErrorAction SilentlyContinue) {
        Write-Host "[OK] CMake already available." -ForegroundColor Green
        return
    }

    Write-Host "[Step] Installing CMake..." -ForegroundColor Cyan
    winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements --silent
}

function Install-BuildTools {
    Write-Host "[Step] Installing Visual Studio 2022 Build Tools workload..." -ForegroundColor Cyan

    $override = @(
        "--quiet",
        "--wait",
        "--norestart",
        "--add", "Microsoft.VisualStudio.Workload.VCTools",
        "--add", "Microsoft.Component.MSBuild",
        "--add", "Microsoft.VisualStudio.Component.VC.CMake.Project",
        "--add", "Microsoft.VisualStudio.Component.Windows11SDK.22621"
    ) -join " "

    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --override $override
}

function Clear-StaleSdkEnv {
    # Prevent stale user overrides from forcing invalid SDK locations.
    [Environment]::SetEnvironmentVariable("WindowsSdkDir", $null, "User")
    [Environment]::SetEnvironmentVariable("WindowsSDKVersion", $null, "User")
    Write-Host "[OK] Cleared user WindowsSdkDir/WindowsSDKVersion overrides." -ForegroundColor Green
}

function Verify-Install {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere not found. Visual Studio Installer appears incomplete."
    }

    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $vsPath) {
        throw "Build Tools installation not detected with MSBuild component."
    }

    $clGlob = Join-Path $vsPath "VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe"
    $clFound = Get-ChildItem -Path $clGlob -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $clFound) {
        throw "MSVC compiler (cl.exe) not found under $vsPath."
    }

    $sdkLibRoot = "C:\Program Files (x86)\Windows Kits\10\Lib"
    if (-not (Test-Path $sdkLibRoot)) {
        throw "Windows SDK libraries not found at $sdkLibRoot."
    }

    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake not found in PATH. Install CMake and retry."
    }

    Write-Host "[OK] Toolchain verified." -ForegroundColor Green
    Write-Host "[Info] Detected Build Tools at: $vsPath" -ForegroundColor Gray
}

function Prepare-LocalBuild {
    $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
    $cmakeLists = Join-Path $repoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakeLists)) {
        Write-Host "[Info] No CMakeLists.txt found in repo root. Skipping local build preparation." -ForegroundColor Yellow
        return
    }

    $buildDir = Join-Path $repoRoot "build"
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    $cmakeFilesDir = Join-Path $buildDir "CMakeFiles"

    if (Test-Path $cacheFile) {
        Remove-Item $cacheFile -Force
        Write-Host "[OK] Removed stale CMakeCache.txt" -ForegroundColor Green
    }
    if (Test-Path $cmakeFilesDir) {
        Remove-Item $cmakeFilesDir -Recurse -Force
        Write-Host "[OK] Removed stale CMakeFiles directory" -ForegroundColor Green
    }

    Write-Host "[Step] Configuring fresh local build folder..." -ForegroundColor Cyan
    cmake -S $repoRoot -B $buildDir -G "Visual Studio 17 2022"

    if ($BuildRelease) {
        Write-Host "[Step] Building Release..." -ForegroundColor Cyan
        cmake --build $buildDir --config Release
    }
}

if (-not (Test-IsAdministrator)) {
    Write-Host "[Info] Re-launching with Administrator privileges..." -ForegroundColor Yellow
    $argList = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", ('"' + $PSCommandPath + '"')
    )
    if ($SkipCMake) {
        $argList += "-SkipCMake"
    }
    if ($PrepareLocalBuild) {
        $argList += "-PrepareLocalBuild"
    }
    if ($BuildRelease) {
        $argList += "-BuildRelease"
    }
    Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList $argList
    exit 0
}

Write-Host "=== HiMYM Toolchain Installer ===" -ForegroundColor Magenta
Ensure-Winget
Install-CMakeIfMissing
Install-BuildTools
Clear-StaleSdkEnv
Verify-Install

if ($PrepareLocalBuild) {
    Prepare-LocalBuild
}

Write-Host "";
Write-Host "Done. Recommended next steps:" -ForegroundColor Magenta
Write-Host "1) Open a NEW terminal" -ForegroundColor White
if ($PrepareLocalBuild -and $BuildRelease) {
    Write-Host "2) Toolchain + local configure + release build already completed." -ForegroundColor White
} elseif ($PrepareLocalBuild) {
    Write-Host "2) Toolchain + local configure already completed." -ForegroundColor White
    Write-Host "3) Build: cmake --build build --config Release" -ForegroundColor White
} else {
    Write-Host "2) Configure: cmake -S . -B build -G \"Visual Studio 17 2022\"" -ForegroundColor White
    Write-Host "3) Build:     cmake --build build --config Release" -ForegroundColor White
}
