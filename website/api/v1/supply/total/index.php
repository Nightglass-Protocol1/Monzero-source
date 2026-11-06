<?php
declare(strict_types=1);
// Plain number for listing sites (CoinGecko, CoinMarketCap): total XMZ supply.
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(300);
header('Content-Type: text/plain; charset=utf-8');
try { $s = v1Supply(); } catch (Throwable $e) { http_response_code(502); header('Cache-Control: no-store'); echo "unavailable\n"; exit; }
echo v1Xmz($s['value']['total_atomic']), "\n";
