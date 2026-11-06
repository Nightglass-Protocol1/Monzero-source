<?php
declare(strict_types=1);
// Exact XMZ supply from the chain (see v1Supply in ../lib.php).
require dirname(__DIR__) . '/lib.php';
v1Begin(300);
try { $s = v1Supply(); } catch (Throwable $e) { v1Unavailable(); }
$v = $s['value'];
v1Reply([
    'circulating_supply' => v1Xmz($v['circulating_atomic']),
    'total_supply' => v1Xmz($v['total_atomic']),
    'max_supply' => null,
    'circulating_atomic' => $v['circulating_atomic'],
    'total_atomic' => $v['total_atomic'],
    'height' => $v['height'],
    'method' => 'Sum of every block\'s coinbase minus fees, checked against the emission rule. Circulating excludes the newest 60 blocks\' rewards, which are still locked. No coins are burned or locked by the protocol; there is no maximum because of the 3.26 XMZ tail emission.',
], $s['observed_at'], V1_SOURCE);
