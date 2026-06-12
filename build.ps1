<#
.SYNOPSIS
    Build one or all Clipboard++ executables.

.PARAMETER Target
    Which target to build: all | clipboardpp | sqlite_editor | json_viewer
    Defaults to "all".

.PARAMETER Config
    Build configuration: Release | Debug
    Defaults to "Release".

.PARAMETER Clean
    Delete the build directory before configuring (full rebuild).

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Target sqlite_editor
    .\build.ps1 -Target json_viewer -Config Debug
    .\build.ps1 -Clean
#>

param(
    [ValidateSet("all", "clipboardpp", "sqlite_editor", "json_viewer")]
    [string]$Target = "all",

    [ValidateSet("Release", "Debug", "Both")]
    [string]$Config = "Release",

    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root      = $PSScriptRoot
$Generator = "Visual Studio 17 2022"

# ── helpers ──────────────────────────────────────────────────────────────────

function Write-Step([string]$msg) {
    Write-Host "`n>>> $msg" -ForegroundColor Cyan
}

function Write-Ok([string]$msg) {
    Write-Host "    $msg" -ForegroundColor Green
}

function Write-Fail([string]$msg) {
    Write-Host "    ERROR: $msg" -ForegroundColor Red
}

function Build-Project {
    param(
        [string]   $Name,
        [string]   $SourceDir,
        [string]   $BuildDir,
        [string[]] $Configs
    )

    Write-Step "[$Name]  configs=$($Configs -join '+')"

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "    Cleaning $BuildDir" -ForegroundColor DarkYellow
        $procName = [System.IO.Path]::GetFileNameWithoutExtension($Name)
        Get-Process -Name $procName -ErrorAction SilentlyContinue |
            Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 300
        Remove-Item $BuildDir -Recurse -Force
    }

    # Centralised output dirs — all exes land in <root>\build\<Config>\<Name>\
    $outRelease = "$Root\build\Release\$Name"
    $outDebug   = "$Root\build\Debug\$Name"

    # Configure once — VS generator supports all configs from one configure.
    # RUNTIME_OUTPUT_DIRECTORY_* redirects the final exe for each config.
    cmake -S $SourceDir -B $BuildDir -G $Generator -A x64 `
          "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=$outRelease" `
          "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG=$outDebug"
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "cmake configure failed for $Name"
        exit $LASTEXITCODE
    }

    # Build each requested config
    foreach ($cfg in $Configs) {
        Write-Host "    Building $cfg..." -ForegroundColor DarkCyan
        cmake --build $BuildDir --config $cfg
        if ($LASTEXITCODE -ne 0) {
            Write-Fail "cmake build failed for $Name ($cfg)"
            exit $LASTEXITCODE
        }

        $exe = Join-Path "$Root\build\$cfg\$Name" "$Name.exe"
        if (Test-Path $exe) {
            Write-Ok "Output [$cfg]: $exe"
        } else {
            Write-Host "    (exe not found at expected path)" -ForegroundColor DarkYellow
        }
    }
}

# ── project table ─────────────────────────────────────────────────────────────

$Projects = @{
    clipboardpp    = @{ Source = $Root;                Build = "$Root\build\.cmake\clipboardpp"   }
    sqlite_editor  = @{ Source = "$Root\dbviewer";     Build = "$Root\build\.cmake\sqlite_editor" }
    json_viewer    = @{ Source = "$Root\jsonviewer";   Build = "$Root\build\.cmake\json_viewer"   }
}

# ── dispatch ──────────────────────────────────────────────────────────────────

$toBuild   = if ($Target -eq "all") { $Projects.Keys | Sort-Object } else { @($Target) }
$toConfigs = if ($Config -eq "Both") { @("Release", "Debug") } else { @($Config) }

$start = Get-Date

foreach ($name in $toBuild) {
    $p = $Projects[$name]
    Build-Project -Name $name -SourceDir $p.Source -BuildDir $p.Build -Configs $toConfigs
}

$elapsed = (Get-Date) - $start
Write-Host ("`nDone — {0} target(s) built in {1:mm\:ss}" -f @($toBuild).Count, $elapsed) `
    -ForegroundColor White
