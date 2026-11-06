<?php
declare(strict_types=1);
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(15);
try { $o = v1NetworkObservation(); } catch (Throwable $e) { v1Unavailable(); }
$n = $o['value'];
v1Reply(['difficulty' => (string)$n['difficulty'], 'height' => $n['height'],
    'top_block_difficulty' => $n['tip']['difficulty']], $o['observed_at'], V1_SOURCE);
