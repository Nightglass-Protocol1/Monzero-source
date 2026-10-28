[CmdletBinding()]
param(
    [string]$ArtifactPath = "$env:USERPROFILE\Downloads\monzero-genesis-pre8-windows-x64.zip",
    [string]$EvidencePath = "$env:USERPROFILE\Documents\monzero-pre8-windows-wallet-evidence.json",
    [int]$RpcPort = 6285,
    [string]$DaemonAddress = "node.monzero.org:6175"
)

$ErrorActionPreference = "Stop"
$expectedHash = "049b4db553d52f592e0a0b05956fbafbec1e37c2672de50d0cde2bfbdffce284"
$expectedRevision = "38914bf13"
$stamp = [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
$workDir = Join-Path $env:TEMP "MonzeroPre7Wallet-$stamp"
$walletDir = Join-Path $workDir "wallets"
$rpcProcess = $null
$passed = $false

function Invoke-WalletRpc([string]$Method, [hashtable]$Params = @{}) {
    $request = @{
        jsonrpc = "2.0"
        id = "pre8-wallet-smoke"
        method = $Method
        params = $Params
    } | ConvertTo-Json -Depth 8
    $response = Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:$RpcPort/json_rpc" `
        -ContentType "application/json" -Body $request -TimeoutSec 30
    if ($response.error) { throw "$Method failed: $($response.error.message)" }
    return $response.result
}

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw "This test must run on native Windows"
    }
    if (Get-Process monzero-wallet-rpc -ErrorAction SilentlyContinue) {
        throw "A monzero-wallet-rpc process is already running"
    }

    $zipPath = (Resolve-Path $ArtifactPath).Path
    $archiveHash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLowerInvariant()
    if ($archiveHash -ne $expectedHash) { throw "Archive SHA-256 mismatch: $archiveHash" }

    New-Item -ItemType Directory -Path $workDir, $walletDir | Out-Null
    Expand-Archive -LiteralPath $zipPath -DestinationPath $workDir
    $rpcPath = (Get-ChildItem $workDir -Filter monzero-wallet-rpc.exe -File -Recurse |
        Select-Object -First 1).FullName
    if (-not $rpcPath) { throw "monzero-wallet-rpc.exe is missing" }
    $version = (& $rpcPath --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -notmatch $expectedRevision) {
        throw "monzero-wallet-rpc.exe did not report revision $expectedRevision"
    }

    $stdoutPath = Join-Path $workDir "wallet-rpc.stdout.log"
    $stderrPath = Join-Path $workDir "wallet-rpc.stderr.log"
    $arguments = @(
        "--wallet-dir", $walletDir,
        "--rpc-bind-ip", "127.0.0.1",
        "--rpc-bind-port", "$RpcPort",
        "--disable-rpc-login",
        "--daemon-address", $DaemonAddress,
        "--log-file", (Join-Path $workDir "wallet-rpc.log")
    )
    $rpcProcess = Start-Process $rpcPath -ArgumentList $arguments -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

    $ready = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        if ($rpcProcess.HasExited) { throw "monzero-wallet-rpc exited with $($rpcProcess.ExitCode)" }
        try {
            Invoke-WalletRpc "get_version" | Out-Null
            $ready = $true
            break
        } catch {}
        Start-Sleep -Milliseconds 500
    }
    if (-not $ready) { throw "Wallet RPC did not become ready" }

    Invoke-WalletRpc "create_wallet" @{
        filename = "native-created"
        password = "pre8-temporary-test"
        language = "English"
    } | Out-Null
    $createdAddress = [string](Invoke-WalletRpc "get_address").address
    $seed = [string](Invoke-WalletRpc "query_key" @{ key_type = "mnemonic" }).key
    if ($createdAddress -notmatch '^F[1-9A-HJ-NP-Za-km-z]{94}$') {
        throw "Created wallet returned an invalid Monzero address"
    }
    if ([string]::IsNullOrWhiteSpace($seed)) { throw "Created wallet returned no recovery seed" }
    Invoke-WalletRpc "close_wallet" | Out-Null

    Invoke-WalletRpc "restore_deterministic_wallet" @{
        filename = "native-restored"
        password = "pre8-temporary-test"
        seed = $seed
        restore_height = 0
    } | Out-Null
    $restoredAddress = [string](Invoke-WalletRpc "get_address").address
    if ($restoredAddress -ne $createdAddress) {
        throw "Seed restoration did not reproduce the original address"
    }
    $refresh = Invoke-WalletRpc "refresh"
    $balance = Invoke-WalletRpc "get_balance"
    Invoke-WalletRpc "store" | Out-Null
    Invoke-WalletRpc "close_wallet" | Out-Null

    $seed = $null
    $evidence = [ordered]@{
        schema_version = 1
        project = "Monzero"
        test = "Genesis pre8 exact native Windows wallet lifecycle smoke"
        tested_at_utc = [DateTime]::UtcNow.ToString("o")
        tester = "$env:USERNAME@$env:COMPUTERNAME"
        windows_version = [Environment]::OSVersion.VersionString
        powershell_version = $PSVersionTable.PSVersion.ToString()
        artifact_sha256 = $archiveHash
        source_revision = $expectedRevision
        executable_version = $version
        daemon_address = $DaemonAddress
        wallet = [ordered]@{
            address = $createdAddress
            creation = "pass"
            seed_export = "pass"
            deterministic_restore = "pass"
            refresh = "pass"
            blocks_fetched = [int64]($refresh.blocks_fetched)
            balance = [uint64]($balance.balance)
            unlocked_balance = [uint64]($balance.unlocked_balance)
            store_and_close = "pass"
        }
        result = "pass"
    }
    $evidence | ConvertTo-Json -Depth 6 | Set-Content $EvidencePath -Encoding UTF8
    $passed = $true
    Write-Host "Native Windows pre8 wallet lifecycle smoke passed"
    Write-Host "Evidence: $EvidencePath"
} finally {
    $seed = $null
    if ($rpcProcess -and -not $rpcProcess.HasExited) {
        try { Invoke-WalletRpc "stop_wallet" | Out-Null } catch {}
        if (-not $rpcProcess.WaitForExit(30000)) {
            Stop-Process -Id $rpcProcess.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($rpcProcess) { $rpcProcess.Dispose() }
    if ($passed -and (Test-Path $workDir)) {
        Remove-Item -LiteralPath $workDir -Recurse -Force
    } elseif (-not $passed) {
        Write-Warning "Failure diagnostics retained at $workDir"
    }
}
