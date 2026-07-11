<#
.SYNOPSIS
    Increment the beta version, build all projects, and publish a GitHub release.
    Executables are bundled into release-x64.zip and debug-x64.zip (includes PDBs).
    The Android companion debug APK is built and uploaded as a separate asset.

.PARAMETER Notes
    Release notes body. Supports multi-line strings. Required unless -DryRun is set.

.PARAMETER DryRun
    Build and stage everything but do not create the tag, release, or push anything.

.EXAMPLE
    .\release.ps1 -Notes "Fixed icon theming, added title bar color pickers."

    .\release.ps1 -Notes @"
    ## What's new
    - Theme-driven icon in all windows and system tray
    - Title bar button base colors
    "@

    .\release.ps1 -DryRun
#>

param(
    [string]$Notes  = "",
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot

function Write-Step([string]$msg) { Write-Host "`n>>> $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host "    OK  $msg" -ForegroundColor Green }
function Write-Info([string]$msg) { Write-Host "    $msg" -ForegroundColor DarkCyan }
function Write-Warn([string]$msg) { Write-Host "    WARN $msg" -ForegroundColor Yellow }
function Write-Fail([string]$msg) { Write-Host "    FAIL $msg" -ForegroundColor Red; exit 1 }

# ── 1. Read and increment VERSION ─────────────────────────────────────────────

Write-Step "Version"

$versionFile = Join-Path $Root "VERSION"
if (-not (Test-Path $versionFile)) { Write-Fail "VERSION file not found at $versionFile" }

$raw = (Get-Content $versionFile -Raw).Trim()

if ($raw -notmatch '^(\d+\.\d+\.\d+)-(\w+)\.(\d+)$') {
    Write-Fail "Unexpected VERSION format '$raw'. Expected e.g. '0.1.0-beta.3'"
}

$semver     = $Matches[1]
$stage      = $Matches[2]
$build      = [int]$Matches[3]
$newBuild   = $build + 1
$newVersion = "$semver-$stage.$newBuild"
$tagName    = "v$newVersion"
$displayVer = "Version $semver  (Beta $newBuild)"

Write-Info "Old: $raw"
Write-Info "New: $newVersion  (tag: $tagName)"

if (-not $DryRun) {
    Set-Content $versionFile "$newVersion`n"
    Write-Ok "Updated VERSION"
} else {
    Write-Warn "DryRun — VERSION not written"
}

# ── 2. Patch version string in MainWindow.cpp ─────────────────────────────────

Write-Step "Patch source version string"

$mainWindowPath = Join-Path $Root "src\ui\MainWindow.cpp"
if (-not (Test-Path $mainWindowPath)) { Write-Fail "MainWindow.cpp not found" }

$src     = Get-Content $mainWindowPath -Raw
$patched = $src -replace 'Version \d+\.\d+\.\d+\s+\(Beta \d+\)', $displayVer

if ($src -eq $patched) {
    Write-Warn "No version string matched in MainWindow.cpp — check the pattern"
} elseif (-not $DryRun) {
    Set-Content $mainWindowPath $patched -NoNewline
    Write-Ok "Patched MainWindow.cpp  ->  $displayVer"
} else {
    Write-Warn "DryRun — MainWindow.cpp not patched"
}

# ── 3. Build all projects (Release + Debug) ───────────────────────────────────

Write-Step "Build all projects  (Release + Debug)"

& "$Root\build.ps1" -Target all -Config Both
if ($LASTEXITCODE -ne 0) { Write-Fail "build.ps1 failed" }

# ── 4. Build Android companion APK ────────────────────────────────────────────

Write-Step "Build Android companion APK"

$androidRoot = Join-Path $Root "android\clipboardpp-android-api"
$androidApkSource = Join-Path $androidRoot "app\build\outputs\apk\debug\app-debug.apk"
if (-not (Test-Path $androidRoot)) {
    Write-Fail "Android project not found: $androidRoot"
}

$gradleCmd = Get-Command gradle -ErrorAction SilentlyContinue
$gradleExe = if ($gradleCmd) { $gradleCmd.Source } else { $null }
if (-not $gradleExe) {
    $localGradle = "C:\Gradle\gradle-8.10\bin\gradle.bat"
    if (Test-Path $localGradle) {
        $gradleExe = $localGradle
    }
}
if (-not $gradleExe) {
    Write-Fail "Gradle not found on PATH and C:\Gradle\gradle-8.10\bin\gradle.bat was not found"
}

if (-not $env:JAVA_HOME) {
    $defaultJavaHome = "C:\Program Files\Eclipse Adoptium\jdk-17.0.19.10-hotspot"
    if (Test-Path $defaultJavaHome) {
        $env:JAVA_HOME = $defaultJavaHome
        $env:Path = "$env:JAVA_HOME\bin;$env:Path"
        Write-Info "JAVA_HOME set to $env:JAVA_HOME for this release run"
    }
}
if (-not $env:ANDROID_HOME) {
    $defaultAndroidHome = Join-Path $env:LOCALAPPDATA "Android\Sdk"
    if (Test-Path $defaultAndroidHome) {
        $env:ANDROID_HOME = $defaultAndroidHome
        $env:ANDROID_SDK_ROOT = $defaultAndroidHome
        $env:Path = "$defaultAndroidHome\platform-tools;$defaultAndroidHome\cmdline-tools\latest\bin;$env:Path"
        Write-Info "ANDROID_HOME set to $env:ANDROID_HOME for this release run"
    }
}

Push-Location $androidRoot
try {
    & $gradleExe ":app:assembleDebug"
    if ($LASTEXITCODE -ne 0) { Write-Fail "Android Gradle build failed" }
} finally {
    Pop-Location
}
if (-not (Test-Path $androidApkSource)) {
    Write-Fail "Android debug APK not found: $androidApkSource"
}
Write-Ok "Android APK: app-debug.apk"

# ── 5. Bundle into release-x64.zip and debug-x64.zip ─────────────────────────

Write-Step "Bundle assets"

$Projects = @("clipboardpp", "clipboardpp_ide", "sqlite_editor", "json_viewer")

$stagingDir  = Join-Path $Root "build\release-staging\$tagName"
$relDir      = Join-Path $stagingDir "release-x64"
$dbgDir      = Join-Path $stagingDir "debug-x64"

foreach ($d in @($stagingDir, $relDir, $dbgDir)) {
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item $d -ItemType Directory | Out-Null
}

foreach ($name in $Projects) {
    # Release exe
    $relExe = "$Root\build\Release\$name\$name.exe"
    if (-not (Test-Path $relExe)) { Write-Fail "Release exe not found: $relExe" }
    Copy-Item $relExe (Join-Path $relDir "$name.exe")
    Write-Ok "Release: $name.exe"

    # Debug exe (renamed with d suffix) + PDB
    $dbgExe = "$Root\build\Debug\$name\$name.exe"
    if (Test-Path $dbgExe) {
        Copy-Item $dbgExe (Join-Path $dbgDir "${name}d.exe")
        Write-Ok "Debug:   ${name}d.exe"
    } else {
        Write-Warn "Debug exe not found: $dbgExe (skipping)"
    }

    $dbgPdb = "$Root\build\Debug\$name\$name.pdb"
    if (Test-Path $dbgPdb) {
        Copy-Item $dbgPdb (Join-Path $dbgDir "$name.pdb")
        Write-Ok "PDB:     $name.pdb"
    } else {
        Write-Warn "PDB not found: $dbgPdb (skipping)"
    }
}

# Zip both folders
$relZip = Join-Path $stagingDir "release-x64.zip"
$dbgZip = Join-Path $stagingDir "debug-x64.zip"
$androidApkOut = Join-Path $stagingDir "clipboardpp-android-api-debug.apk"

Compress-Archive -Path "$relDir\*" -DestinationPath $relZip -Force
Write-Ok "Zipped: release-x64.zip"

Compress-Archive -Path "$dbgDir\*" -DestinationPath $dbgZip -Force
Write-Ok "Zipped: debug-x64.zip"

Copy-Item $androidApkSource $androidApkOut -Force
Write-Ok "Staged: clipboardpp-android-api-debug.apk"

# Bundle fonts.zip + install-fonts.ps1 into fonts-installer.zip
$fontsDir    = Join-Path $stagingDir "fonts-installer"
$fontsZipSrc = Join-Path $Root "fonts.zip"
$fontsScript = Join-Path $Root "install-fonts.ps1"
$fontsZipOut = Join-Path $stagingDir "fonts-installer.zip"

New-Item $fontsDir -ItemType Directory | Out-Null
if (Test-Path $fontsZipSrc) { Copy-Item $fontsZipSrc (Join-Path $fontsDir "fonts.zip") }
else { Write-Warn "fonts.zip not found in repo root — fonts-installer.zip will be empty" }
if (Test-Path $fontsScript) { Copy-Item $fontsScript (Join-Path $fontsDir "install-fonts.ps1") }
else { Write-Warn "install-fonts.ps1 not found — fonts-installer.zip will be incomplete" }

Compress-Archive -Path "$fontsDir\*" -DestinationPath $fontsZipOut -Force
Write-Ok "Zipped: fonts-installer.zip  (fonts.zip + install-fonts.ps1)"

$assets = @($relZip, $dbgZip, $androidApkOut, $fontsZipOut)

Write-Info "Staged in: $stagingDir"

# ── 6. Build release notes ────────────────────────────────────────────────────

Write-Step "Release notes"

if ([string]::IsNullOrWhiteSpace($Notes)) {
    $Notes = "## Clipboard++ $displayVer`n`nSee [commits](../../commits) for details."
    Write-Warn "No -Notes provided — using placeholder body"
}

$fullNotes = @"
$Notes

---

### Downloads

| File | Contents |
|---|---|
| ``release-x64.zip`` | Release builds — clipboardpp.exe, clipboardpp_ide.exe, sqlite_editor.exe, json_viewer.exe |
| ``debug-x64.zip`` | Debug builds — *d.exe executables + .pdb symbol files |
| ``clipboardpp-android-api-debug.apk`` | Debug APK for testing the experimental Android clipboard bridge |
| ``fonts-installer.zip`` | fonts.zip + install-fonts.ps1 — run the script to install fonts |

No installer required. Unzip and run.
"@

Write-Info "Notes preview (first 3 lines):"
$fullNotes -split "`n" | Select-Object -First 3 | ForEach-Object { Write-Host "    $_" }

# ── 7. Git tag ────────────────────────────────────────────────────────────────

Write-Step "Git tag  $tagName"

if (-not $DryRun) {
    git -C $Root add VERSION "src/ui/MainWindow.cpp"
    git -C $Root commit -m "Release $tagName"
    git -C $Root tag -a $tagName -m "Release $tagName"
    git -C $Root push origin HEAD
    git -C $Root push origin $tagName
    Write-Ok "Tagged and pushed $tagName"
} else {
    Write-Warn "DryRun — no tag, no push"
}

# ── 8. GitHub release ─────────────────────────────────────────────────────────

Write-Step "GitHub release  $tagName"

if (-not $DryRun) {
    $ghArgs = @(
        "release", "create", $tagName,
        "--title", "Clipboard++ $displayVer",
        "--notes", $fullNotes,
        "--prerelease"
    ) + $assets

    & gh @ghArgs
    if ($LASTEXITCODE -ne 0) { Write-Fail "gh release create failed" }

    Write-Ok "GitHub release published: $tagName"
    Write-Ok "View at: https://github.com/james28909/clipboard-plus-plus/releases"
} else {
    Write-Warn "DryRun — GitHub release not created"
    Write-Info "Would upload $($assets.Count) asset(s):"
    $assets | ForEach-Object { Write-Info "  $_" }
}

Write-Host ("`nRelease $tagName complete.") -ForegroundColor White
