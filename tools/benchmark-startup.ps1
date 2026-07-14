param(
    [ValidateSet("Warm", "Cold")]
    [string]$Mode = "Warm",
    [ValidateRange(1, 50)]
    [int]$Runs = 5,
    [string]$ExePath = "$PSScriptRoot\..\build\Debug\clipboardpp\clipboardpp.exe",
    [string]$OutputPath = "$PSScriptRoot\..\startup-benchmark.csv"
)

$ErrorActionPreference = "Stop"
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$report = Join-Path $env:APPDATA "Clipboard++\startup_profile.log"
$results = @()

Write-Host "Clipboard++ $Mode startup benchmark ($Runs runs)"
Write-Host "Close Clipboard++ before continuing. Each benchmark process is stopped after its report completes."
if ($Mode -eq "Cold") {
    Write-Host "Before each run, clear the Windows standby list with an approved tool or perform the run after a reboot."
}

for ($run = 1; $run -le $Runs; $run++) {
    if (Get-Process clipboardpp -ErrorAction SilentlyContinue) {
        throw "clipboardpp.exe is already running. Close it before benchmarking."
    }
    if ($Mode -eq "Cold") {
        Read-Host "Prepare cold-cache conditions for run $run, then press Enter"
    }

    $previousWrite = if (Test-Path -LiteralPath $report) {
        (Get-Item -LiteralPath $report).LastWriteTimeUtc
    } else { [datetime]::MinValue }
    $process = Start-Process -FilePath $exe -PassThru
    $deadline = [datetime]::UtcNow.AddSeconds(60)
    $content = ""
    do {
        Start-Sleep -Milliseconds 100
        if ($process.HasExited) { throw "Clipboard++ exited before producing a complete startup report." }
        if (Test-Path -LiteralPath $report) {
            $item = Get-Item -LiteralPath $report
            if ($item.LastWriteTimeUtc -gt $previousWrite) {
                $content = Get-Content -LiteralPath $report -Raw
            }
        }
    } until ($content -match "active vault item count" -or [datetime]::UtcNow -ge $deadline)

    if ($content -notmatch "active vault item count") {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Timed out waiting for deferred startup to finish."
    }

    $timings = @{}
    $metrics = @{}
    $inMetrics = $false
    foreach ($line in ($content -split "`r?`n")) {
        if ($line -eq "metric`tvalue") { $inMetrics = $true; continue }
        $parts = $line -split "`t"
        if ($inMetrics -and $parts.Count -ge 2) { $metrics[$parts[0]] = $parts[1] }
        elseif (-not $inMetrics -and $parts.Count -eq 3 -and $parts[1] -as [double]) {
            $timings[$parts[0]] = [pscustomobject]@{
                Duration = [double]$parts[1]
                Completed = [double]$parts[2]
            }
        }
    }

    $results += [pscustomobject]@{
        Mode = $Mode
        Run = $run
        FirstFrameMs = $timings["total to first rendered frame"].Duration
        ActiveHistoryReadyMs = $timings["deferred active profile hydration"].Completed
        ActiveDeserializeMs = $timings["active history deserialization"].Duration
        Profiles = $metrics["profile count"]
        ActiveItems = $metrics["active history item count"]
        Images = $metrics["image count"]
        VaultItems = $metrics["active vault item count"]
        ClipboardDbBytes = $metrics["clipboard.db bytes"]
        ImagesDbBytes = $metrics["images.db bytes"]
    }

    Stop-Process -Id $process.Id
    $process.WaitForExit()
    Start-Sleep -Milliseconds 500
    Write-Host "Completed run $run of $Runs"
}

$results | Export-Csv -LiteralPath $OutputPath -NoTypeInformation
$results | Format-Table -AutoSize
Write-Host "Saved benchmark evidence to $OutputPath"
