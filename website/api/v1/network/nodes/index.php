<?php
declare(strict_types=1);
// Country-level counts of observed Monzero nodes. No IP addresses, regions or coordinates exist here.
require dirname(__DIR__, 2) . '/lib.php';
require dirname(__DIR__, 4) . '/lib/node-census.php';
v1Begin(60, ['history']);
$latest = censusRead('latest.json');
if (!$latest || censusProblem($latest, (int)($latest['observed_at'] ?? 0)) !== null) v1Error('unavailable', 'No node census has been collected yet.', 503, 300);
$data = [
    'stale' => time() - $latest['observed_at'] > CENSUS_STALE_SECONDS,
    'window_seconds' => $latest['window_seconds'],
    'totals' => $latest['totals'],
    'countries' => (object)$latest['countries'],
    'geo_database' => $latest['geo_database'],
    'geo_attribution' => 'IP geolocation by DB-IP (https://db-ip.com), CC BY 4.0',
    'vantage' => 'Peers known to ' . implode(' and ', array_map(fn($v) => CENSUS_VANTAGES[$v], $latest['vantages'] ?? ['eu'])) . '; not every node on the network.',
    'vantages' => $latest['vantages'] ?? ['eu'],
];
if (($_GET['history'] ?? '') === 'daily') {
    $data['history'] = [];
    foreach (censusRead('daily.json') ?? [] as $day => $entry)
        if (preg_match('/\A\d{4}-\d{2}-\d{2}\z/', (string)$day) && censusCountsValid($entry['totals'] ?? null))
            $data['history'][] = ['date' => $day, 'totals' => $entry['totals'], 'countries' => count($entry['countries'] ?? [])];
} elseif (isset($_GET['history'])) {
    v1Error('invalid_parameter', 'history must be "daily".', 400);
}
v1Reply($data, $latest['observed_at'], 'node.monzero.org peer list, aggregated by country on the node');
