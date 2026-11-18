[CmdletBinding()]
param(
    [string]$ArtifactPath = "$env:USERPROFILE\Downloads\monzero-genesis-pre8-windows-x64.zip",
    [string]$EvidencePath = "$env:USERPROFILE\Documents\monzero-pre8-windows-network-evidence.json",
    [string]$MiningAddress = "FVtn1gEMEHA2FGvjJLYyrsEbWEtcheVujLSHfRgR6tA95V93io63jC1gKTQrfqS81oAvJee5EJ8CEQ2bkaHVSWfcS4ry354"
)

$ErrorActionPreference = "Stop"
$expectedHash = "049b4db553d52f592e0a0b05956fbafbec1e37c2672de50d0cde2bfbdffce284"
$expectedRevision = "38914bf13"
$programDataDir = Join-Path $env:ProgramData "monzero"
$stamp = [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
$savedDataDir = "$programDataDir.before-pre8-test-$stamp"
$testedDataDir = "$programDataDir.pre8-clean-sync-evidence-$stamp"
$workDir = Join-Path $env:TEMP "MonzeroPre7Exact-$stamp"
$nodeProcess = $null
$originalMoved = $false
$testDataCreated = $false
$passed = $false
$wrapperClosedByHarness = $false

function Invoke-NodePost([string]$Path, [string]$Body = "{}") {
    Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:6175/$Path" `
        -ContentType "application/json" -Body $Body -TimeoutSec 10
}

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw "This test must run on native Windows"
    }
    if (Get-Process monzerod -ErrorAction SilentlyContinue) {
        throw "A monzerod process is already running"
    }

    $zipPath = (Resolve-Path $ArtifactPath).Path
    $archiveHash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLowerInvariant()
    if ($archiveHash -ne $expectedHash) {
        throw "Archive SHA-256 mismatch: $archiveHash"
    }

    New-Item -ItemType Directory -Path $workDir | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $workDir
    $daemonPath = (Get-ChildItem $workDir -Filter monzerod.exe -File -Recurse | Select-Object -First 1).FullName
    if (-not $daemonPath) { throw "monzerod.exe is missing" }
    $packageDir = Split-Path $daemonPath -Parent

    $required = @(
        "monzerod.exe", "monzero-wallet-cli.exe", "monzero-wallet-rpc.exe",
        "start-node.bat", "start-mining.bat", "stop-mining.bat",
        "SHA256SUMS", "BUILD-MANIFEST.txt"
    )
    foreach ($name in $required) {
        if (-not (Test-Path (Join-Path $packageDir $name) -PathType Leaf)) {
            throw "Required package file is missing: $name"
        }
    }
    foreach ($line in Get-Content (Join-Path $packageDir "SHA256SUMS")) {
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Malformed SHA256SUMS line" }
        $file = Join-Path $packageDir $Matches[2].Replace('/', '\')
        $actual = (Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant()
        if ($actual -ne $Matches[1]) { throw "Inner SHA-256 mismatch: $($Matches[2])" }
    }

    $versions = [ordered]@{}
    foreach ($name in @("monzerod.exe", "monzero-wallet-cli.exe", "monzero-wallet-rpc.exe")) {
        $version = (& (Join-Path $packageDir $name) --version 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $version -notmatch $expectedRevision) {
            throw "$name did not report revision $expectedRevision"
        }
        $versions[$name] = $version
    }
    Write-Host "Archive, inner checksums, and executable versions passed"

    if (Test-Path $programDataDir) {
        Move-Item $programDataDir $savedDataDir
        $originalMoved = $true
    }

    $processInfo = New-Object System.Diagnostics.ProcessStartInfo
    $processInfo.FileName = "cmd.exe"
    $processInfo.Arguments = "/d /c start-node.bat"
    $processInfo.WorkingDirectory = $packageDir
    $processInfo.UseShellExecute = $false
    $processInfo.RedirectStandardInput = $true
    $nodeProcess = [System.Diagnostics.Process]::Start($processInfo)
    $nodeProcess.StandardInput.AutoFlush = $true

    $info = $null
    for ($attempt = 0; $attempt -lt 120; $attempt++) {
        if ($nodeProcess.HasExited) { throw "start-node.bat exited early with $($nodeProcess.ExitCode)" }
        try {
            $candidate = Invoke-NodePost "get_info"
            if ($candidate.status -eq "OK" -and $candidate.synchronized) {
                $info = $candidate
                break
            }
        } catch {}
        Start-Sleep -Seconds 1
    }
    if (-not $info) { throw "The exact start-node.bat did not reach synchronized state" }
    $testDataCreated = Test-Path $programDataDir
    Write-Host "Exact start-node.bat synchronized at height $($info.height)"

    $miningStdout = Join-Path $workDir "start-mining.stdout.log"
    $miningStderr = Join-Path $workDir "start-mining.stderr.log"
    $startMining = Start-Process cmd.exe -ArgumentList @('/d', '/c', "start-mining.bat $MiningAddress 1") `
        -WorkingDirectory $packageDir -RedirectStandardOutput $miningStdout `
        -RedirectStandardError $miningStderr -Wait -PassThru
    if ($startMining.ExitCode -ne 0) { throw "start-mining.bat exited with $($startMining.ExitCode)" }
    $mining = Invoke-NodePost "mining_status"
    if (-not $mining.active -or $mining.address -ne $MiningAddress -or $mining.threads_count -ne 1) {
        throw "Mining status did not match the launcher request"
    }
    Write-Host "Exact start-mining.bat activated one mining thread"

    $stopMining = Start-Process cmd.exe -ArgumentList @('/d', '/c', 'stop-mining.bat') `
        -WorkingDirectory $packageDir -Wait -PassThru
    if ($stopMining.ExitCode -ne 0) { throw "stop-mining.bat exited with $($stopMining.ExitCode)" }
    $stoppedMining = Invoke-NodePost "mining_status"
    if ($stoppedMining.active) { throw "Mining remained active after stop-mining.bat" }
    Write-Host "Exact stop-mining.bat stopped mining"

    $nodeProcess.StandardInput.WriteLine("exit")
    $nodeProcess.StandardInput.Close()
    if (-not $nodeProcess.WaitForExit(60000)) { throw "Node did not stop cleanly through its console within 60 seconds" }
    if ($nodeProcess.ExitCode -ne 0) { throw "start-node.bat returned $($nodeProcess.ExitCode)" }
    $nodeProcess.Dispose()
    $nodeProcess = $null
    Start-Sleep -Seconds 2
    Write-Host "Exact start-node.bat stopped cleanly"

    $evidence = [ordered]@{
        schema_version = 1
        project = "Monzero"
        test = "Genesis pre8 exact native Windows network and launcher smoke"
        tested_at_utc = [DateTime]::UtcNow.ToString("o")
        tester = "$env:USERNAME@$env:COMPUTERNAME"
        windows_version = [Environment]::OSVersion.VersionString
        powershell_version = $PSVersionTable.PSVersion.ToString()
        artifact_sha256 = $archiveHash
        source_revision = $expectedRevision
        executable_versions = $versions
        synchronized_node = [ordered]@{
            height = [int64]$info.height
            top_block_hash = [string]$info.top_block_hash
            outgoing_connections = [int]$info.outgoing_connections_count
        }
        exact_launchers = [ordered]@{
            start_node = "pass"
            start_mining = "pass"
            stop_mining = "pass"
            clean_console_shutdown = $true
            wrapper_closed_by_harness = $wrapperClosedByHarness
        }
        mining = [ordered]@{
            address = $MiningAddress
            threads = 1
            algorithm = [string]$mining.pow_algorithm
        }
        result = "pass"
    }
    $evidence | ConvertTo-Json -Depth 6 | Set-Content $EvidencePath -Encoding UTF8
    $passed = $true
    Write-Host "Native Windows pre8 network and launcher smoke passed"
    Write-Host "Evidence: $EvidencePath"
} finally {
    if ($nodeProcess -and -not $nodeProcess.HasExited) {
        try {
            $nodeProcess.StandardInput.WriteLine("exit")
            $nodeProcess.StandardInput.Close()
        } catch {
            try { Invoke-NodePost "stop_daemon" | Out-Null } catch {}
        }
        if (-not $nodeProcess.WaitForExit(60000)) { Stop-Process -Id $nodeProcess.Id -Force -ErrorAction SilentlyContinue }
    }
    if ($nodeProcess) {
        $nodeProcess.Dispose()
        $nodeProcess = $null
    }
    if (Test-Path $programDataDir) {
        & icacls.exe $programDataDir /grant "${env:COMPUTERNAME}\${env:USERNAME}:(OI)(CI)F" /T /C | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to grant cleanup access to the temporary data directory" }
        for ($attempt = 0; $attempt -lt 10; $attempt++) {
            try {
                [IO.Directory]::Move($programDataDir, $testedDataDir)
                $testDataCreated = $true
                break
            } catch {
                if ($attempt -eq 9) { throw }
                Start-Sleep -Seconds 1
            }
        }
    }
    if ($originalMoved -and (Test-Path $savedDataDir)) {
        [IO.Directory]::Move($savedDataDir, $programDataDir)
    }
    if (-not $passed) {
        Write-Warning "Failure diagnostics retained at $workDir"
    }
}
