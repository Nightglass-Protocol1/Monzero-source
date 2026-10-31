"""UTXO chain profiles for the live LTC/XMZ and BTC/XMZ drivers.

Both chains run the same contract, journal and light-wallet paths; a profile only
names the chain identities those paths check (quote network, Core chain name,
light-helper network and schema tag, genesis, address prefix).
"""
import socket
import subprocess
import time
import urllib.error

from test_litecoin_settlement import Node, RpcError, require

PROFILES = {
    ("ltc", "regtest"): dict(tag="ltc", coin="Litecoin", quote="ltc-regtest", app_stage="regtest",
        core_chain="regtest", light="regtest", cookie_dir="regtest", hrp="rltc1",
        genesis="530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9"),
    ("ltc", "testnet"): dict(tag="ltc", coin="Litecoin", quote="ltc-testnet", app_stage="testnet",
        core_chain="test", light="test", cookie_dir="testnet4", hrp="tltc1",
        genesis="4966625a4b2851d9fdee139e56211a0d88575f59ed816ff5e6a63deb4e3e29a0"),
    ("btc", "regtest"): dict(tag="btc", coin="Bitcoin", quote="btc-regtest", app_stage="btc-regtest",
        core_chain="regtest", light="regtest", cookie_dir="regtest", hrp="bcrt1",
        genesis="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"),
    ("btc", "testnet"): dict(tag="btc", coin="Bitcoin", quote="btc-testnet4", app_stage="btc-testnet4",
        core_chain="testnet4", light="testnet4", cookie_dir="testnet4", hrp="tb1",
        genesis="00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043"),
    # Signet: same "tb1" addresses as testnet4, but no minimum-difficulty blocks, so
    # transactions keep confirming when testnet4 only produces empty blocks.
    ("btc", "signet"): dict(tag="btc", coin="Bitcoin", quote="btc-signet", app_stage="btc-signet",
        core_chain="signet", light="signet", cookie_dir="signet", hrp="tb1",
        genesis="00000008819873e925422c1ff0f99f7cc9bbb232af63a077a480a3633bee1ef6"),
}


def profile(chain, stage):
    return PROFILES[(chain, stage)]


class BitcoinNode(Node):
    """Owned Bitcoin Core regtest node with the same interface as the Litecoin fixture node."""
    def start(self):
        self.log = open(self.directory / "daemon.log", "ab")
        self.process = subprocess.Popen([
            str(self.executable), f"-datadir={self.directory}", "-regtest", "-server=1",
            "-listen=0", "-connect=0", "-dnsseed=0", "-discover=0", "-natpmp=0",
            "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1", f"-rpcport={self.port}",
            "-txindex=1", "-fallbackfee=0.0001", "-printtoconsole=1",
        ], stdout=self.log, stderr=subprocess.STDOUT)
        genesis = PROFILES[("btc", "regtest")]["genesis"]
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            require(self.process.poll() is None, "Owned Bitcoin daemon exited during startup")
            try:
                info = self.rpc("getblockchaininfo")
                require(info["chain"] == "regtest" and self.rpc("getblockhash", 0) == genesis,
                        "Unexpected Bitcoin chain identity")
                require(self.rpc("getconnectioncount") == 0, "Fixture node has external peers")
                return
            except (FileNotFoundError, urllib.error.URLError, ConnectionError, RpcError):
                time.sleep(0.1)
        raise RuntimeError("Owned Bitcoin node startup timed out")


def regtest_node(chain, executable, directory):
    return (BitcoinNode if chain == "btc" else Node)(executable, directory)
