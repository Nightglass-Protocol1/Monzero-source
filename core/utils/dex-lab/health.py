"""Read-only response validation for the isolated DEX lab; no signing or RPC writes."""
import re


def validate_node(asset, info, genesis, expected_genesis):
    """Return readiness blockers. Inputs are decoded node responses, not proof of funds."""
    blockers = []
    if not isinstance(info, dict):
        return ["Malformed node response"]
    if not isinstance(expected_genesis, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_genesis):
        blockers.append("Missing independently pinned genesis")
    elif genesis != expected_genesis:
        blockers.append("Genesis mismatch")
    if asset == "BTC":
        if info.get("chain") != "regtest":
            blockers.append("Bitcoin must use regtest")
        if info.get("initialblockdownload") is not False:
            blockers.append("Bitcoin synchronization unverified")
        height, target = info.get("blocks"), info.get("headers")
    elif asset == "XMZ":
        if info.get("nettype") != "fakechain":
            blockers.append("Monzero must use isolated fakechain")
        if info.get("status") != "OK" or info.get("synchronized") is not True:
            blockers.append("Monzero synchronization unverified")
        if info.get("untrusted") is not False:
            blockers.append("Untrusted or unspecified response provenance")
        height, target = info.get("height"), info.get("target_height")
    else:
        return ["Unsupported lab asset"]
    if type(height) is not int or type(target) is not int or height < 0 or target < 0:
        blockers.append("Malformed chain heights")
    elif target > height:
        blockers.append("Node behind target")
    return blockers
