# Genesis pre6 native Windows smoke test

This gate must run on a clean, native 64-bit Windows installation. Wine,
compatibility layers, Linux containers, and a Windows build performed on the
release operator's existing development host do not satisfy the requirement.
The tester should be independent of the release build operator where possible.

Download these two files from `https://monzero.org/downloads/`:

```text
monzero-genesis-pre6-windows-x64.zip
monzero-genesis-pre6-windows-smoke.ps1
```

Before running anything, open PowerShell and verify both published hashes:

```powershell
(Get-FileHash -Algorithm SHA256 .\monzero-genesis-pre6-windows-x64.zip).Hash
(Get-FileHash -Algorithm SHA256 .\monzero-genesis-pre6-windows-smoke.ps1).Hash
```

Expected values:

```text
ZIP:    9d6fb20055a1e1e89625f8565af13be6f9140275de943c5a41a886d8aea87405
SCRIPT: 7fec2c0940b13e5b9e30cc40805ddd2bb62cf0034fb999f80d23426de9bc74ae
```

Read the script before executing it. It creates a temporary directory,
validates the ZIP and all inner package hashes, launches the daemon, CLI wallet,
and wallet RPC with `--version`, starts a fresh daemon bound only to random
loopback ports in offline mode, checks its unrestricted local RPC, requests a
clean RPC shutdown, and deletes temporary data after success. On failure it
retains the temporary directory and logs for diagnosis.

Run it without changing the downloaded archive:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\monzero-genesis-pre6-windows-smoke.ps1 `
  -ArtifactPath .\monzero-genesis-pre6-windows-x64.zip
```

Successful execution writes
`monzero-pre6-windows-smoke-evidence.json`. The tester must inspect that file,
confirm `result` is `pass`, and return it through an authenticated channel with
their name, Windows edition/build, VM or physical-machine description, and the
SHA-256 of the evidence file. Do not send passwords, wallet material, private
keys, or unrelated system information.

This harness proves native loading, version binding, clean-database daemon
startup, local RPC, and clean shutdown for the exact archive. It does not prove
independent reproduction, code signing, GUI behavior, network synchronization,
mining, wallet recovery, or security audit completion; those remain separate
release gates.
