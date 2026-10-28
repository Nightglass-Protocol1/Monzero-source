[CmdletBinding()]
param(
    [string]$ArtifactPath = "",
    [string]$EvidencePath = (Join-Path (Get-Location) "monzero-pre6-windows-smoke-evidence.json")
)

$ErrorActionPreference = "Stop"
$ExpectedSha256 = "9d6fb20055a1e1e89625f8565af13be6f9140275de943c5a41a886d8aea87405"
$ExpectedRevision = "d4cac3627"
$ArtifactUrl = "https://monzero.org/downloads/monzero-genesis-pre6-windows-x64.zip"
$workDir = Join-Path ([IO.Path]::GetTempPath()) ("MonzeroPre6Smoke-" + [guid]::NewGuid().ToString("N"))
$daemon = $null
$passed = $false

function Get-VersionOutput([string]$Path) {
    $output = (& $Path --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "$(Split-Path $Path -Leaf) --version exited with code $LASTEXITCODE"
    }
    if ($output -notmatch [regex]::Escape($ExpectedRevision)) {
        throw "$(Split-Path $Path -Leaf) did not report revision $ExpectedRevision"
    }
    return $output
}

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw "This gate must run on a native Windows system"
    }
    New-Item -ItemType Directory -Path $workDir | Out-Null

    if ($ArtifactPath) {
        $zipPath = (Resolve-Path $ArtifactPath).Path
    } else {
        $zipPath = Join-Path $workDir "monzero-genesis-pre6-windows-x64.zip"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $ArtifactUrl -OutFile $zipPath
    }

    $actualHash = (Get-FileHash -Algorithm SHA256 -Path $zipPath).Hash.ToLowerInvariant()
    if ($actualHash -ne $ExpectedSha256) {
        throw "Archive SHA-256 mismatch: expected $ExpectedSha256, got $actualHash"
    }

    $extractDir = Join-Path $workDir "extracted"
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir
    $daemonPath = (Get-ChildItem -Path $extractDir -Filter monzerod.exe -File -Recurse |
        Select-Object -First 1).FullName
    if (-not $daemonPath) {
        throw "monzerod.exe is missing from the verified archive"
    }
    $packageDir = Split-Path $daemonPath -Parent

    $required = @(
        "monzerod.exe", "monzero-wallet-cli.exe", "monzero-wallet-rpc.exe",
        "start-node.bat", "start-wallet-cli.bat", "start-mining.bat", "stop-mining.bat",
        "SHA256SUMS", "BUILD-MANIFEST.txt"
    )
    foreach ($name in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $packageDir $name) -PathType Leaf)) {
            throw "Required package file is missing: $name"
        }
    }

    foreach ($line in Get-Content -LiteralPath (Join-Path $packageDir "SHA256SUMS")) {
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') {
            throw "Malformed SHA256SUMS line"
        }
        $expected = $Matches[1]
        $relative = $Matches[2].Replace('/', [IO.Path]::DirectorySeparatorChar)
        $file = Join-Path $packageDir $relative
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Checksummed package file is missing: $relative"
        }
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant()
        if ($actual -ne $expected) {
            throw "Inner SHA-256 mismatch: $relative"
        }
    }

    $versions = [ordered]@{}
    foreach ($name in @("monzerod.exe", "monzero-wallet-cli.exe", "monzero-wallet-rpc.exe")) {
        $versions[$name] = Get-VersionOutput (Join-Path $packageDir $name)
    }

    $p2pPort = Get-Random -Minimum 40000 -Maximum 44999
    $rpcPort = Get-Random -Minimum 45000 -Maximum 49999
    $dataDir = Join-Path $workDir "daemon-data"
    $stdoutLog = Join-Path $workDir "monzerod.stdout.log"
    $stderrLog = Join-Path $workDir "monzerod.stderr.log"
    $arguments = @(
        "--data-dir=$dataDir", "--log-file=$(Join-Path $workDir 'monzerod.log')",
        "--p2p-bind-ip=127.0.0.1", "--p2p-bind-port=$p2pPort",
        "--rpc-bind-ip=127.0.0.1", "--rpc-bind-port=$rpcPort",
        "--offline", "--no-igd", "--non-interactive"
    )
    # Start-Process on Windows PowerShell 5.1 can lose ExitCode when output is
    # redirected. A background job owns the native process and returns
    # $LASTEXITCODE as its only pipeline value after the process exits.
    $daemonConfig = [pscustomobject]@{
        Path = $daemonPath
        Arguments = $arguments
        StdoutLog = $stdoutLog
        StderrLog = $stderrLog
    }
    $daemon = Start-Job -ArgumentList $daemonConfig -ScriptBlock {
        param($config)
        & $config.Path @($config.Arguments) 1> $config.StdoutLog 2> $config.StderrLog
        [int]$LASTEXITCODE
    }

    $info = $null
    $rpcUrl = "http://127.0.0.1:$rpcPort/get_info"
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        if ($daemon.State -ne "Running") {
            $earlyOutput = @(Receive-Job -Job $daemon -ErrorAction SilentlyContinue)
            throw "monzerod.exe exited before RPC became ready (job state $($daemon.State), output: $earlyOutput)"
        }
        try {
            $info = Invoke-RestMethod -Method Post -Uri $rpcUrl -ContentType "application/json" `
                -Body "{}" -TimeoutSec 2
            break
        } catch {
            Start-Sleep -Seconds 1
        }
    }
    if (-not $info -or $info.status -ne "OK" -or -not $info.mainnet -or -not $info.offline) {
        throw "Fresh offline daemon RPC did not report the expected mainnet OK state"
    }

    Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$rpcPort/stop_daemon" `
        -ContentType "application/json" -Body "{}" -TimeoutSec 5 | Out-Null
    $completedJob = Wait-Job -Job $daemon -Timeout 10
    if (-not $completedJob) {
        throw "monzerod.exe did not stop cleanly through RPC"
    }
    $jobOutput = @(Receive-Job -Job $daemon)
    if ($daemon.State -ne "Completed") {
        throw "monzerod.exe job ended in unexpected state $($daemon.State)"
    }
    $daemonExitCode = 0
    if ($jobOutput.Count -ne 1 -or
        -not [int]::TryParse([string]$jobOutput[0], [ref]$daemonExitCode) -or
        $daemonExitCode -ne 0) {
        throw "monzerod.exe did not return a successful numeric exit code: $jobOutput"
    }

    $evidence = [ordered]@{
        schema_version = 1
        project = "Monzero"
        test = "Genesis pre6 native Windows smoke"
        tested_at_utc = [DateTime]::UtcNow.ToString("o")
        tester = "$env:USERNAME@$env:COMPUTERNAME"
        windows_version = [Environment]::OSVersion.VersionString
        powershell_version = $PSVersionTable.PSVersion.ToString()
        artifact_url = $ArtifactUrl
        artifact_sha256 = $actualHash
        source_revision = $ExpectedRevision
        executable_versions = $versions
        fresh_daemon = [ordered]@{
            status = $info.status
            mainnet = [bool]$info.mainnet
            offline = [bool]$info.offline
            height = [int64]$info.height
            clean_rpc_shutdown = $true
        }
        result = "pass"
    }
    $evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $EvidencePath -Encoding UTF8
    $passed = $true
    Write-Host "Native Windows smoke test passed"
    Write-Host "Evidence: $EvidencePath"
} finally {
    if ($daemon) {
        if ($daemon.State -eq "Running") {
            Stop-Job -Job $daemon -ErrorAction SilentlyContinue
        }
        Remove-Job -Job $daemon -Force -ErrorAction SilentlyContinue
    }
    if ($passed) {
        Remove-Item -LiteralPath $workDir -Recurse -Force
    } elseif (Test-Path -LiteralPath $workDir) {
        Write-Warning "Failure diagnostics retained at $workDir"
    }
}
