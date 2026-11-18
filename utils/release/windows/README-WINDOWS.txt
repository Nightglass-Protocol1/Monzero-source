MONZERO WINDOWS X64 PRERELEASE
==============================

Experimental and unaudited software. Never reuse a Monero wallet, keys, or
recovery seed with Monzero. The executables are not code-signed; verify the
ZIP checksum and the included SHA256SUMS file before running them.

QUICK START
-----------

1. Extract the complete ZIP to a normal folder. Do not run it inside the ZIP.
2. Double-click start-node.bat and keep its window open.
3. Wait for the node to synchronize, then run start-wallet-cli.bat.
4. Create a new wallet and record its recovery seed offline.

The first Windows Firewall prompt should be allowed for monzerod.exe on the
appropriate network. Incoming P2P uses TCP port 6174. HTTP RPC uses 6175;
both HTTP and ZMQ RPC are explicitly restricted to 127.0.0.1.

MINING
------

After the node is synchronized, open Command Prompt in this folder and run:

  start-mining.bat YOUR_MONZERO_ADDRESS 2

The final argument is the CPU thread count. Run stop-mining.bat to stop mining
without stopping the node. Solo-mining rewards remain locked for 60 blocks.

Optional anonymous website reporting uses monzero-miner-reporter.ps1. It sends
only a random installation ID, hashrate, and block count; it never sends a
wallet address or machine name. The project operator must separately install
an ingest credential at %APPDATA%\Monzero\miner-stats-token.txt. That private
credential is deliberately absent from public archives. Mining works normally
without website reporting. When that credential exists, start-mining.bat
automatically starts the reporter; no Scheduled Task setup is required. Reporter
diagnostics are written to %APPDATA%\Monzero\miner-stats-reporter.log.
Mined block counts are read locally from %PROGRAMDATA%\monzero\monzero.log.

PACKAGE CONTENTS
----------------

monzerod.exe             Node and solo miner
monzero-wallet-cli.exe   Interactive command-line wallet
monzero-wallet-rpc.exe   Wallet automation server for advanced operators

The graphical wallet is not included in this command-line prerelease.
Review RELEASE_STATUS.md and UPGRADE.md before replacing an older installation.
