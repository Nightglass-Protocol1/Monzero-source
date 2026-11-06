<?php
declare(strict_types=1);
require dirname(__DIR__) . '/lib.php';
v1Begin(30, ['height']);
$raw = $_GET['height'] ?? null;
if (!is_string($raw) || !preg_match('/\A(0|[1-9][0-9]{0,9})\z/', $raw)) v1Error('invalid_parameter', 'height must be a non-negative integer.', 400);
$height = (int)$raw;
try {
    $o = v1NetworkObservation();
    if ($height > $o['value']['tip']['height']) v1Error('not_found', 'No block at this height yet.', 404);
    $header = v1Header(v1Rpc('get_block_header_by_height', ['height' => $height])['block_header'] ?? null);
    if ($header['height'] !== $height) throw new RuntimeException('Height mismatch');
} catch (Throwable $e) { v1Unavailable(); }
// Recent blocks can still be reorganised; only cache deep ones for long.
if ($header['depth'] < 60) header('Cache-Control: public, max-age=15');
else header('Cache-Control: public, max-age=3600');
v1Reply(['block' => $header], time(), V1_SOURCE);
