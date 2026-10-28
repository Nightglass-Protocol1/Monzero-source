param(
    [string]$Endpoint = 'https://monzero.org/api/miner-stats/',
    [string]$Rpc = 'http://127.0.0.1:6175',
    [string]$TokenFile = "$env:APPDATA\Monzero\miner-stats-token.txt",
    [string]$IdFile = "$env:APPDATA\Monzero\telemetry-id.txt",
    [string]$ReporterLogFile = "$env:APPDATA\Monzero\miner-stats-reporter.log",
    [string]$DaemonLogFile = "$env:PROGRAMDATA\monzero\monzero.log",
    [ValidateRange(30, 3600)][int]$Interval = 60
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Write-ReporterLog([string]$Message) {
    $directory = Split-Path -Parent $ReporterLogFile
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    if ((Test-Path $ReporterLogFile) -and (Get-Item $ReporterLogFile).Length -gt 1048576) {
        Move-Item -Force $ReporterLogFile "$ReporterLogFile.previous"
    }
    Add-Content -Path $ReporterLogFile -Value "$(Get-Date -Format o) $Message"
}

function Get-BlocksFound {
    if (-not (Test-Path $DaemonLogFile)) {
        return 0
    }
    return @(Select-String -Path $DaemonLogFile -Pattern 'Found block <?[0-9a-f]+>? at height ' -AllMatches).Count
}

if (-not (Test-Path $TokenFile)) {
    throw "Miner-stat token file is missing: $TokenFile"
}
$token = (Get-Content -Raw $TokenFile).Trim()
if ($token.Length -lt 24) {
    throw 'Miner-stat token is invalid.'
}

$idDirectory = Split-Path -Parent $IdFile
New-Item -ItemType Directory -Force -Path $idDirectory | Out-Null
if (Test-Path $IdFile) {
    $installationId = (Get-Content -Raw $IdFile).Trim()
} else {
    $installationId = [guid]::NewGuid().ToString()
    Set-Content -NoNewline -Encoding Ascii -Path $IdFile -Value $installationId
}
if ($installationId -notmatch '^[a-f0-9-]{32,64}$') {
    throw 'Miner-stat installation ID is invalid.'
}

$createdNew = $false
$mutex = [Threading.Mutex]::new($true, 'Local\MonzeroMinerStatsReporter', [ref]$createdNew)
if (-not $createdNew) {
    exit 0
}

try {
    Write-ReporterLog 'reporter started'
    while ($true) {
        try {
            $status = Invoke-RestMethod -Method Post -Uri "$($Rpc.TrimEnd('/'))/mining_status" `
                -ContentType 'application/json' -Body '{}' -TimeoutSec 8
            if ($status.status -ne 'OK') {
                throw "daemon returned status $($status.status)"
            }
            if ($status.active) {
                $payload = @{
                    installation_id = $installationId
                    hashrate = [math]::Max(0, [int64]$status.speed)
                    blocks_found = Get-BlocksFound
                } | ConvertTo-Json -Compress
                $headers = @{ Authorization = "Bearer $token" }
                $result = Invoke-RestMethod -Method Post -Uri $Endpoint -Headers $headers `
                    -ContentType 'application/json' -Body $payload -TimeoutSec 8
                if ($result.status -ne 'OK') {
                    throw "website returned status $($result.status)"
                }
                Write-ReporterLog "reported $($status.speed) H/s"
            }
        } catch {
            Write-ReporterLog "report failed: $($_.Exception.Message)"
        }
        Start-Sleep -Seconds $Interval
    }
} finally {
    if ($createdNew) {
        $mutex.ReleaseMutex()
    }
    $mutex.Dispose()
}
