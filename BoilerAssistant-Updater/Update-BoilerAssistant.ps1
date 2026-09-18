$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cli = Join-Path $root 'tools\arduino-cli.exe'
$firmwareDir = Join-Path $root 'firmware'
$fqbn = 'arduino:renesas_uno:unor4wifi'

# Edit these to point at the repo folder that holds only the current .hex release.
$repoOwner  = 'YOUR_GITHUB_USERNAME'
$repoName   = 'YOUR_REPO_NAME'
$repoBranch = 'main'
$repoFolder = 'firmware-releases'

Write-Host ''
Write-Host 'Boiler Assistant updater' -ForegroundColor Cyan
Write-Host 'Connect the UNO R4 WiFi with a USB cable.'
Write-Host 'Close Arduino IDE and any serial monitor, then press Enter.'
Read-Host | Out-Null

function Get-LatestFirmwareFromGitHub {
    $apiUrl = "https://api.github.com/repos/$repoOwner/$repoName/contents/$repoFolder`?ref=$repoBranch"
    Write-Host 'Checking GitHub for the newest firmware...' -ForegroundColor Yellow
    try {
        $headers = @{ 'User-Agent' = 'BoilerAssistant-Updater' }
        $items = Invoke-RestMethod -Uri $apiUrl -Headers $headers -TimeoutSec 15
    } catch {
        Write-Host "Could not reach GitHub: $($_.Exception.Message)" -ForegroundColor DarkYellow
        return $null
    }

    $hexFiles = @($items | Where-Object { $_.name -like '*.hex' })
    if ($hexFiles.Count -eq 0) {
        Write-Host 'No .hex file was found in the GitHub folder.' -ForegroundColor DarkYellow
        return $null
    }

    $latest = $hexFiles | Sort-Object name -Descending | Select-Object -First 1
    $destination = Join-Path $firmwareDir $latest.name

    Write-Host "Downloading $($latest.name) ..." -ForegroundColor Yellow
    Invoke-WebRequest -Uri $latest.download_url -OutFile $destination -Headers $headers -TimeoutSec 60
    return $destination
}

if (-not (Test-Path $firmwareDir)) { New-Item -ItemType Directory -Path $firmwareDir | Out-Null }

$firmware = Get-LatestFirmwareFromGitHub
if (-not $firmware) {
    Write-Host 'Falling back to the firmware bundled in this updater package.' -ForegroundColor DarkYellow
    $firmware = Get-ChildItem $firmwareDir -Filter '*.hex' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $firmware) { throw 'No firmware .hex file is available locally or on GitHub.' }

Write-Host "Using firmware: $firmware" -ForegroundColor Cyan

$ports = @(& $cli board list --format json 2>$null | ConvertFrom-Json)
$port = $null
if ($ports -and $ports.detected_ports) {
    $port = $ports.detected_ports | Where-Object {
        $_.matching_boards -and ($_.matching_boards | Where-Object { $_.fqbn -eq $fqbn })
    } | Select-Object -First 1 -ExpandProperty port
}

if (-not $port) {
    $port = Read-Host 'Board not detected automatically. Enter the COM port, for example COM5'
}
if (-not $port) { throw 'No serial port was provided.' }

Write-Host "Uploading Boiler Assistant to $port ..." -ForegroundColor Yellow
& $cli upload --fqbn $fqbn --port $port $firmware
if ($LASTEXITCODE -ne 0) { throw "Upload failed with exit code $LASTEXITCODE." }

Write-Host ''
Write-Host 'Update completed successfully.' -ForegroundColor Green
Write-Host 'The controller will restart with the new firmware.'
exit 0
