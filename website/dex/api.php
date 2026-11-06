<?php
declare(strict_types=1);
// Public DEX bootstrap config. Read-only. The Monzero app fetches this to learn the
// DEX's live status, the pairs/fees to display, and the rendezvous points where it can
// discover trading peers. It then connects PEER-TO-PEER and to blockchain nodes to
// trade — the website is never a trading backend and holds no funds.
//
// TRUST MODEL (important): nothing here is fund-authoritative.
//  - `live`/`launch_at`  : presentation gate only; real trading is gated by the app's
//                          signed operator control document, never by this flag.
//  - `pairs`/`fees`      : display only; the real fee is enforced in the maker's signed
//                          quote that the app verifies, not taken from here.
//  - `rendezvous`        : peer-discovery hints; a hostile value can at worst point the
//                          app at bad makers — atomicity still prevents theft (swaps
//                          fail/refund, never steal).
//  - EVM CONTRACT ADDRESSES ARE NOT SERVED HERE. They are pinned in the app build, so a
//    compromised website can never redirect funds to a malicious contract.
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function dexPrivateDir(): string {
    $d = getenv('MONZERO_PRIVATE_DIR');
    return is_string($d) && $d !== '' ? $d : dirname(__DIR__, 2) . '/monzero-private';
}

// Baked defaults (kept in sync with /dex/dex.js). The owner overrides live/launch_at/
// rendezvous/min_app_version via monzero-private/dex-status.php.
$cfg = [
    'live' => false,
    'launch_at' => 1796342400, // 2026-12-04T00:00:00Z
    'min_app_version' => null,  // e.g. "1.0.0" to require a recent app; null = no floor
    'rendezvous' => [],         // libp2p multiaddrs where makers register (owner-set)
    'pairs' => [
        ['pair' => 'XMZ/BTC',  'status' => 'beta', 'fee' => '0.1%-0.5%'],
        ['pair' => 'XMZ/LTC',  'status' => 'soon', 'fee' => 'free'],
        ['pair' => 'XMZ/BNB',  'status' => 'soon', 'fee' => 'tbd'],
        ['pair' => 'XMZ/ETH',  'status' => 'soon', 'fee' => 'tbd'],
        ['pair' => 'XMZ/SOL',  'status' => 'soon', 'fee' => '0.1%-0.5%'],
    ],
];

$path = dexPrivateDir() . '/dex-status.php';
if (is_file($path) && !is_link($path)) {
    $o = (static fn() => require $path)();
    if (is_array($o)) {
        if (isset($o['live'])) $cfg['live'] = (bool)$o['live'];
        if (isset($o['launch_at'])) $cfg['launch_at'] = (int)$o['launch_at'];
        if (isset($o['min_app_version']) && is_string($o['min_app_version'])) $cfg['min_app_version'] = $o['min_app_version'];
        if (isset($o['rendezvous']) && is_array($o['rendezvous'])) {
            // Only pass through well-formed libp2p multiaddr strings.
            $cfg['rendezvous'] = array_values(array_filter($o['rendezvous'], static fn($m) =>
                is_string($m) && $m !== '' && strlen($m) <= 512 && preg_match('#^/(ip4|ip6|dns4|dns6|dnsaddr)/#', $m)));
        }
        if (isset($o['pairs']) && is_array($o['pairs'])) $cfg['pairs'] = $o['pairs'];
    }
}

echo json_encode([
    'schema' => 'monzero-dex-bootstrap-v1',
    'live' => $cfg['live'],
    'launch_at' => $cfg['launch_at'],
    'min_app_version' => $cfg['min_app_version'],
    'rendezvous' => $cfg['rendezvous'],
    'pairs' => $cfg['pairs'],
    'trust' => 'Discovery/display only. Trading is peer-to-peer and non-custodial; contract addresses are pinned in the app, not served here.',
], JSON_UNESCAPED_SLASHES);
