<?php
declare(strict_types=1);
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(15);
try { $o = v1NetworkObservation(); } catch (Throwable $e) { v1Unavailable(); }
$n = $o['value'];
v1Reply([
    'height' => $n['height'],
    'top_block_height' => $n['tip']['height'],
    'top_block_hash' => $n['tip']['hash'],
    'top_block_timestamp' => $n['tip']['timestamp'],
    'top_block_reward_atomic' => $n['tip']['reward_atomic'],
    'difficulty' => (string)$n['difficulty'],
    'hashrate_estimate' => round($n['difficulty'] / V1_TARGET_SECONDS, 2),
    'target_block_seconds' => V1_TARGET_SECONDS,
    'synchronized' => $n['synchronized'],
    'tx_pool_size' => $n['tx_pool_size'],
], $o['observed_at'], V1_SOURCE);
