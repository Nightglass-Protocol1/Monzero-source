<?php
declare(strict_types=1);
require __DIR__ . '/lib.php';
v1Begin(300);
v1Reply(['version' => 1, 'documentation' => '/developers/api/', 'endpoints' => [
    '/api/v1/network/stats/', '/api/v1/network/hashrate/', '/api/v1/network/difficulty/', '/api/v1/network/nodes/', '/api/v1/network/upgrade/', '/api/v1/supply/', '/api/v1/supply/circulating/', '/api/v1/supply/total/',
    '/api/v1/blocks/latest/?count=10', '/api/v1/blocks/?height=1',
    '/api/v1/mining/pools/', '/api/v1/mining/benchmarks/', '/api/v1/exchanges/', '/api/v1/ecosystem/',
]]);
