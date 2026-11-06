<?php
declare(strict_types=1);

// Legacy listing-site supply estimate, not a verified circulating-supply total.
// The saved anchor is extrapolated using unpenalized emission. Later reward
// penalties can make it read high; lost, burned or locked coins are not measured.
// Keep legacy numeric fields for existing consumers, with explicit JSON metadata.
// Anchor provenance must be reverified before using this as a homepage metric.

header('Cache-Control: public, max-age=300');
header('X-Content-Type-Options: nosniff');
header('X-Monzero-Supply-Status: estimate-unverified-anchor');

const NODE_RPC = 'http://node.monzero.org:6175/json_rpc';
const ANCHOR_BLOCKS = 10000;
const ANCHOR_EMISSION = '188927562217236362';
const MONEY_SUPPLY = '10000000000000000000';
const SPEED_SHIFT = 19;              // emission speed factor 20, 2-minute blocks
const TAIL_REWARD = 326000000000;    // 3.26 XMZ per block
const ATOMIC_DIGITS = 11;

function height(): int
{
    $curl = curl_init(NODE_RPC);
    curl_setopt_array($curl, [CURLOPT_POST => true, CURLOPT_RETURNTRANSFER => true,
        CURLOPT_POSTFIELDS => '{"jsonrpc":"2.0","id":0,"method":"get_info"}',
        CURLOPT_HTTPHEADER => ['Content-Type: application/json'], CURLOPT_CONNECTTIMEOUT => 3, CURLOPT_TIMEOUT => 8]);
    $body = curl_exec($curl);
    curl_close($curl);
    $height = is_string($body) ? (json_decode($body, true)['result']['height'] ?? null) : null;
    if (!is_int($height) || $height < ANCHOR_BLOCKS) throw new RuntimeException('node unavailable');
    return $height;
}

// The supply after `blocks` blocks (heights 0..blocks-1), in atomic units.
// gmp keeps the 64-bit-plus arithmetic exact.
function supply(int $blocks): GMP
{
    $generated = gmp_init(ANCHOR_EMISSION);
    $cap = gmp_init(MONEY_SUPPLY);
    for ($height = ANCHOR_BLOCKS; $height < $blocks; ++$height) {
        $reward = gmp_div_q(gmp_sub($cap, $generated), gmp_pow(2, SPEED_SHIFT));
        $generated = gmp_add($generated, gmp_cmp($reward, TAIL_REWARD) < 0 ? TAIL_REWARD : $reward);
    }
    return $generated;
}

function coins(GMP $atomic): string
{
    $digits = str_pad(gmp_strval($atomic), ATOMIC_DIGITS + 1, '0', STR_PAD_LEFT);
    return substr($digits, 0, -ATOMIC_DIGITS) . '.' . substr($digits, -ATOMIC_DIGITS);
}

try {
    $blocks = height();
    $amount = coins(supply($blocks));
} catch (Throwable $error) {
    http_response_code(503);
    header('Content-Type: text/plain; charset=utf-8');
    echo "unavailable\n";
    exit;
}

if (($_GET['format'] ?? '') === 'json') {
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['ticker' => 'XMZ', 'circulating_supply' => $amount, 'total_supply' => $amount,
        'max_supply' => null, 'height' => $blocks, 'is_estimate' => true, 'method' => 'saved-anchor-plus-unpenalized-emission',
        'anchor_blocks' => ANCHOR_BLOCKS, 'anchor_emission_atomic' => ANCHOR_EMISSION,
        'anchor_verification' => 'not-reverified',
        'note' => 'Legacy supply fields contain an emission estimate, not independently verified circulating supply. Reward penalties after the saved anchor are not accounted for; lost, burned and locked coins are not measured. Tail emission is 3.26 XMZ per block.']);
} else {
    header('Content-Type: text/plain; charset=utf-8');
    echo $amount;
}
