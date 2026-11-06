<?php
declare(strict_types=1);
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(15);
try { $o = v1NetworkObservation(); } catch (Throwable $e) { v1Unavailable(); }
$n = $o['value'];
v1Reply(['hashrate_estimate' => round($n['difficulty'] / V1_TARGET_SECONDS, 2), 'unit' => 'H/s',
    'method' => 'next-block difficulty divided by the 120-second target; an estimate, not a measurement',
    'height' => $n['height']], $o['observed_at'], V1_SOURCE);
