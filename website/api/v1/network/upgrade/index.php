<?php
declare(strict_types=1);
// The scheduled v1.0.0 network upgrade (hard fork 17 at block 21,000): current height, blocks left,
// a time estimate at the 120-second target, and whether the public node already signals version 17.
require dirname(__DIR__, 2) . '/lib.php';
const UPGRADE_HEIGHT = 21000;
const UPGRADE_VERSION = 17;
v1Begin(30);
try {
    $o = v1NetworkObservation();
    $fork = v1Cached('hard-fork', 60, static fn(): array => v1Rpc('hard_fork_info'));
} catch (Throwable $e) { v1Unavailable(); }
$height = $o['value']['height'];
$left = max(0, UPGRADE_HEIGHT - $height);
v1Reply([
    'release' => 'v1.0.0',
    'fork_height' => UPGRADE_HEIGHT,
    'fork_version' => UPGRADE_VERSION,
    'height' => $height,
    'blocks_left' => $left,
    'active' => $height >= UPGRADE_HEIGHT,
    'estimated_at' => $left > 0 ? (int)$o['value']['tip']['timestamp'] + $left * V1_TARGET_SECONDS : null,
    'public_node_ready' => is_int($fork['value']['voting'] ?? null) && $fork['value']['voting'] >= UPGRADE_VERSION,
    'download' => 'https://monzero.org/#download',
    'guide' => 'https://monzero.org/upgrade/',
], $o['observed_at'], V1_SOURCE);
