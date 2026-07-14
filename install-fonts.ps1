<#
.SYNOPSIS
    Extract bundled fonts into %APPDATA%\Clipboard++\fonts\
    No dependencies - requires only Windows 10/11 built-in ZIP support.
    Rename fonts.7z to fonts.zip before using this script.
#>

$ErrorActionPreference = "Stop"

$archive = Join-Path $PSScriptRoot "fonts.zip"
$dest    = Join-Path $env:APPDATA "Clipboard++\fonts"

if (-not (Test-Path $archive)) {
    Write-Host "ERROR: fonts.zip not found next to this script." -ForegroundColor Red
    Write-Host "       Expected: $archive"
    exit 1
}

if (-not (Test-Path $dest)) {
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
}

Write-Host "Installing fonts to: $dest" -ForegroundColor Cyan

# Extract - overwrite existing files silently
Expand-Archive -Path $archive -DestinationPath $dest -Force

$count = (Get-ChildItem $dest -File).Count
Write-Host "Done - $count font file(s) in $dest" -ForegroundColor Green
