<?php
declare(strict_types=1);
// Blocks found outside the official pool (solo miners or other pools), and, with ?hashrate=,
// what a solo miner can expect at the current difficulty. See v1SoloMining in ../../lib.php.
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(60, ['hashrate']);
$rate = null;
if (isset($_GET['hashrate'])) {
    if (!preg_match('/\A\d{1,12}(\.\d{1,3})?\z/', $_GET['hashrate']) || (float)$_GET['hashrate'] <= 0)
        v1Error('invalid_parameter', 'hashrate must be a positive number of hashes per second.', 400);
    $rate = (float)$_GET['hashrate'];
}
try { $o = v1SoloMining(); }
catch (V1PoolUnavailable $e) { v1Error('pool_unavailable', 'The official pool\'s block list is temporarily unavailable. Please retry.', 503, 30); }
catch (Throwable $e) {
    // Why, in a header (short, letters and digits only), so a failure on the server can be told apart
    header('X-Monzero-Reason: ' . substr(preg_replace('/[^A-Za-z0-9 .,:_-]/', '', get_class($e) . ': ' . $e->getMessage()), 0, 160));
    v1Unavailable();
}
$s = $o['value'];
$total = $s['to'] - $s['from'] + 1;
$outside = count($s['outside']);
$difficulty = (float)$s['difficulty'];
$networkRate = $difficulty / V1_TARGET_SECONDS;
$share = static fn(int $n): float => round($n / $total * 100, 2);
$data = [
    'window' => ['from_height' => $s['from'], 'to_height' => $s['to'], 'blocks' => $total,
        'complete_window' => $total === V1_SOLO_WINDOW,
        'note' => 'Covers the newest blocks the API could match against the official pool\'s block list, at most ' . V1_SOLO_WINDOW . ' (about a day).'],
    'official_pool' => ['blocks' => $s['pool_blocks'], 'share_percent' => $share($s['pool_blocks']),
        'reported_hashrate' => $s['pool_hashrate'] === null ? null : round($s['pool_hashrate'], 2)],
    'outside_pool' => ['blocks' => $outside, 'share_percent' => $share($outside),
        'hashrate_estimate' => round($networkRate * $outside / $total, 2),
        'note' => 'Blocks the official pool did not find: solo miners or other pools. The chain does not say which.'],
    'network_hashrate_estimate' => round($networkRate, 2),
    'recent_outside_blocks' => array_slice(array_reverse($s['outside']), 0, 10),
    'method' => 'Each block in the window is compared by hash with the official pool\'s found blocks (pool.monzero.org/api/stats). Hashrates are estimates: network = next-block difficulty / 120 s; outside the pool = network x the outside share of the window.',
];
if ($rate !== null) {
    $seconds = $difficulty / $rate;
    $reward = (int)$s['reward_atomic'];
    $data['solo_estimate'] = [
        'hashrate' => $rate,
        'share_of_network_percent' => round($rate / $networkRate * 100, 4),
        'expected_seconds_per_block' => round($seconds),
        'expected_blocks_per_day' => round(86400 / $seconds, 4),
        'expected_xmz_per_day' => v1Xmz((string)(int)round($reward * 86400 / $seconds)),
        'block_reward_xmz' => v1Xmz((string)$reward),
        'note' => 'An average over many blocks at today\'s difficulty and reward. A solo miner is paid only when it finds a whole block, so real results vary a lot; difficulty and reward change.',
    ];
}
v1Reply($data, $o['observed_at'], 'node.monzero.org public node and pool.monzero.org');
