<?php
declare(strict_types=1);
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(15, ['count']);
$raw = $_GET['count'] ?? '10';
if (!preg_match('/\A[1-9][0-9]?\z/', $raw) || (int)$raw > 30) v1Error('invalid_parameter', 'count must be an integer from 1 to 30.', 400);
$count = (int)$raw;
try {
    // One cached window of 30 serves every count, so callers cannot multiply node requests.
    $o = v1Cached('latest-blocks', 15, static function (): array {
        $tip = v1Header(v1Rpc('get_last_block_header')['block_header'] ?? null);
        $start = max(0, $tip['height'] - 29);
        $range = v1Rpc('get_block_headers_range', ['start_height' => $start, 'end_height' => $tip['height'], 'fill_pow_hash' => false]);
        $headers = array_map('v1Header', is_array($range['headers'] ?? null) ? $range['headers'] : []);
        if (count($headers) !== $tip['height'] - $start + 1) throw new RuntimeException('Incomplete range');
        foreach ($headers as $i => $h) {
            if ($h['height'] !== $start + $i || ($i > 0 && !hash_equals($headers[$i - 1]['hash'], $h['prev_hash'])))
                throw new RuntimeException('Unlinked range');
        }
        if (!hash_equals($tip['hash'], end($headers)['hash'])) throw new RuntimeException('Tip changed during range');
        return ['blocks' => array_reverse($headers)];
    });
} catch (Throwable $e) { v1Unavailable(); }
v1Reply(['blocks' => array_slice($o['value']['blocks'], 0, $count)], $o['observed_at'], V1_SOURCE);
