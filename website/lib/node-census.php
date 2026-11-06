<?php
declare(strict_types=1);
// Country-level node census storage. The collector on the node sends per-country counts only;
// this site never receives IP addresses, so it cannot store or reveal them.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }

const CENSUS_SCHEMA = 'monzero-node-census-v1';
const CENSUS_HISTORY_DAYS = 400;
const CENSUS_STALE_SECONDS = 7200;
const CENSUS_MAX_COUNTRIES = 300;
const CENSUS_MAX_COUNT = 1000000;
const CENSUS_TAGGED_SCHEMA = 'monzero-node-census-v2';
const CENSUS_VANTAGES = ['eu' => 'node.monzero.org', 'us' => 'node2.monzero.org'];
const CENSUS_MERGE_SECONDS = 2700;   // vantage reports older than this (relative to the newest) are left out
const CENSUS_MAX_TAGS = 20000;

function censusDir(): string {
    $configured = getenv('MONZERO_NODE_CENSUS_DIR');
    return is_string($configured) && $configured !== '' ? $configured : dirname(__DIR__, 2) . '/monzero-private/node-census';
}

function censusSecret(): string {
    $path = dirname(censusDir()) . '/node-census-config.php';
    $config = is_file($path) && !is_link($path) ? (static fn() => require $path)() : [];
    $secret = is_array($config) && is_string($config['secret'] ?? null) ? $config['secret'] : '';
    return preg_match('/\A[0-9a-f]{64}\z/', $secret) ? $secret : '';
}

function censusRead(string $name): ?array {
    $path = censusDir() . '/' . $name;
    if (!is_file($path) || is_link($path)) return null;
    $data = json_decode((string)@file_get_contents($path, false, null, 0, 4194304), true);
    return is_array($data) ? $data : null;
}

function censusWrite(string $name, array $data): void {
    $directory = censusDir();
    if (!is_dir($directory)) @mkdir($directory, 0700, true);
    if (!is_dir($directory) || is_link($directory)) throw new RuntimeException('census storage unavailable');
    $tmp = $directory . '/.' . $name . '.' . bin2hex(random_bytes(6));
    $old = umask(077);
    try {
        if (file_put_contents($tmp, json_encode($data, JSON_UNESCAPED_SLASHES | JSON_THROW_ON_ERROR)) === false || !rename($tmp, "$directory/$name"))
            throw new RuntimeException('census write failed');
    } finally { umask($old); @unlink($tmp); }
}

function censusCountsValid(mixed $c): bool {
    if (!is_array($c) || array_keys($c) !== ['observed', 'reachable', 'connected']) return false;
    foreach ($c as $v) if (!is_int($v) || $v < 0 || $v > CENSUS_MAX_COUNT) return false;
    return $c['reachable'] <= $c['observed'] && $c['connected'] <= $c['observed'];
}

// Returns null when the payload is acceptable, otherwise a short reason.
function censusProblem(mixed $p, int $now): ?string {
    $keys = ['schema', 'observed_at', 'window_seconds', 'totals', 'countries', 'geo_database'];
    if (!is_array($p) || (array_keys($p) !== $keys && array_keys($p) !== [...$keys, 'vantages'])) return 'shape';
    if (isset($p['vantages']) && (!is_array($p['vantages']) || !$p['vantages'] || array_diff($p['vantages'], array_keys(CENSUS_VANTAGES)))) return 'vantages';
    if ($p['schema'] !== CENSUS_SCHEMA || $p['window_seconds'] !== 86400) return 'schema';
    if (!is_int($p['observed_at']) || abs($now - $p['observed_at']) > 600) return 'time';
    if (!is_string($p['geo_database']) || !preg_match('/\A[A-Za-z0-9._-]{1,80}\z/', $p['geo_database'])) return 'geo_database';
    if (!censusCountsValid($p['totals']) || !is_array($p['countries']) || count($p['countries']) > CENSUS_MAX_COUNTRIES) return 'totals';
    $sum = ['observed' => 0, 'reachable' => 0, 'connected' => 0];
    foreach ($p['countries'] as $code => $counts) {
        if (!is_string($code) || !preg_match('/\A[A-Z]{2}\z/', $code) || !censusCountsValid($counts) || $counts['observed'] === 0) return 'country';
        foreach ($sum as $k => $_) $sum[$k] += $counts[$k];
    }
    return $sum === $p['totals'] ? null : 'sum';
}

// Tagged report from one vantage node: per country, sorted lists of one-way 16-hex tags.
function censusTaggedProblem(mixed $p, int $now): ?string {
    if (!is_array($p) || array_keys($p) !== ['schema', 'vantage', 'tag_day', 'observed_at', 'window_seconds', 'countries', 'geo_database']) return 'shape';
    if ($p['schema'] !== CENSUS_TAGGED_SCHEMA || $p['window_seconds'] !== 86400 || !is_string($p['vantage']) || !isset(CENSUS_VANTAGES[$p['vantage']])) return 'schema';
    if (!is_int($p['observed_at']) || abs($now - $p['observed_at']) > 600) return 'time';
    if (!is_string($p['tag_day']) || $p['tag_day'] !== gmdate('Y-m-d', $p['observed_at'])) return 'tag_day';
    if (!is_string($p['geo_database']) || !preg_match('/\A[A-Za-z0-9._-]{1,80}\z/', $p['geo_database'])) return 'geo_database';
    if (!is_array($p['countries']) || count($p['countries']) > CENSUS_MAX_COUNTRIES) return 'countries';
    $total = 0;
    foreach ($p['countries'] as $code => $lists) {
        if (!is_string($code) || !preg_match('/\A[A-Z]{2}\z/', $code) || !is_array($lists) || array_keys($lists) !== ['reachable', 'connected']) return 'country';
        foreach ($lists as $list) {
            if (!is_array($list) || !array_is_list($list)) return 'tags';
            foreach ($list as $t) if (!is_string($t) || !preg_match('/\A[0-9a-f]{16}\z/', $t)) return 'tags';
            $total += count($list);
        }
        if (!$lists['reachable'] && !$lists['connected']) return 'country';
    }
    return $total > CENSUS_MAX_TAGS ? 'too many' : null;
}

// Merge the newest tagged report of each vantage (same tag day, within CENSUS_MERGE_SECONDS) into
// country counts. A node seen by several vantages, or placed in two countries, is counted once.
function censusMerge(array $reports): array {
    usort($reports, fn($a, $b) => $b['observed_at'] <=> $a['observed_at']);
    $newest = $reports[0];
    $used = array_values(array_filter($reports, fn($r) => $r['tag_day'] === $newest['tag_day'] && $newest['observed_at'] - $r['observed_at'] <= CENSUS_MERGE_SECONDS));
    $placed = [];   // tag => country where it is first seen (newest report first, then country code order)
    $reach = []; $conn = [];
    foreach ($used as $r) {
        ksort($r['countries']);
        foreach ($r['countries'] as $code => $lists) {
            foreach (['reachable', 'connected'] as $kind) foreach ($lists[$kind] as $t) {
                $placed[$t] ??= $code;
                if ($kind === 'reachable') $reach[$t] = true; else $conn[$t] = true;
            }
        }
    }
    $countries = [];
    foreach ($placed as $t => $code) {
        $countries[$code] ??= ['observed' => 0, 'reachable' => 0, 'connected' => 0];
        $countries[$code]['observed']++;
        $countries[$code]['reachable'] += isset($reach[$t]) ? 1 : 0;
        $countries[$code]['connected'] += isset($conn[$t]) ? 1 : 0;
    }
    ksort($countries);
    $vantages = array_column($used, 'vantage');
    sort($vantages);
    return ['schema' => CENSUS_SCHEMA, 'observed_at' => $newest['observed_at'], 'window_seconds' => 86400,
        'totals' => ['observed' => count($placed), 'reachable' => count($reach), 'connected' => count($conn)],
        'countries' => $countries, 'geo_database' => $newest['geo_database'], 'vantages' => $vantages];
}
