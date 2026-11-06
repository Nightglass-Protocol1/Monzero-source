<?php
declare(strict_types=1);

const NODE_RPC = 'http://node.monzero.org:6175';
const MAX_RANGE = 25;

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    respond(['error' => 'POST required'], 405);
}

$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) respond(['error' => 'Invalid JSON'], 400);
$action = $input['action'] ?? '';

try {
    switch ($action) {
        case 'info':
            respond(rpc('/get_info', []));

        case 'blocks':
            $info = rpc('/get_info', []);
            $height = max(1, (int)($info['height'] ?? 1));
            $limit = min(MAX_RANGE, max(1, (int)($input['limit'] ?? 15)));
            $end = max(0, $height - 1);
            $start = max(0, $end - $limit + 1);
            $range = jsonRpc('get_block_headers_range', [
                'start_height' => $start,
                'end_height' => $end,
                'fill_pow_hash' => false,
            ]);
            respond(['info' => $info, 'headers' => array_reverse($range['headers'] ?? [])]);

        case 'block':
            $params = ['fill_pow_hash' => true];
            $height = $input['height'] ?? null;
            if (is_int($height) && $height >= 0 || is_string($height) && ctype_digit($height)) {
                $params['height'] = (int)$height;
            } elseif (validHash($input['hash'] ?? '')) {
                $params['hash'] = strtolower($input['hash']);
            } else {
                respond(['error' => 'Valid height or block hash required'], 400);
            }
            respond(jsonRpc('get_block', $params));

        case 'transaction':
            $hash = strtolower((string)($input['hash'] ?? ''));
            if (!validHash($hash)) respond(['error' => 'Valid transaction hash required'], 400);
            $result = rpc('/get_transactions', [
                'txs_hashes' => [$hash],
                'decode_as_json' => true,
                'prune' => false,
                'split' => true,
            ]);
            if (empty($result['txs']) && empty($result['txs_as_json'])) respond(['error' => 'Transaction not found'], 404);
            respond($result);

        case 'assets':
            $offset = max(0, (int)($input['offset'] ?? 0));
            $count = min(100, max(1, (int)($input['count'] ?? 25)));
            $assets = jsonRpc('get_assets', ['offset' => $offset, 'count' => $count]);
            validateAssetPage($assets, 'assets', $offset, $count);
            respond(['supported' => true] + $assets);

        case 'asset':
            $assetId = strtolower((string)($input['asset_id'] ?? ''));
            if (!validHash($assetId)) respond(['error' => 'Valid asset ID required'], 400);
            $assets = jsonRpc('get_assets', ['offset' => 0, 'count' => 1000]);
            validateAssetPage($assets, 'assets', 0, 1000);
            $match = null;
            foreach (($assets['assets'] ?? []) as $asset) {
                if (hash_equals($assetId, strtolower((string)($asset['asset_id'] ?? '')))) {
                    $match = $asset;
                    break;
                }
            }
            if ($match === null) {
                if ($assets['total'] > count($assets['assets'])) respond(['error' => 'Asset lookup window exhausted; existence is unknown'], 503);
                respond(['error' => 'Asset not found in this node registry'], 404);
            }
            $outputs = jsonRpc('get_asset_outputs', [
                'asset_id' => $assetId,
                'offset' => 0,
                'count' => 100,
            ]);
            validateAssetPage($outputs, 'outputs', 0, 100);
            if ($outputs['active'] !== $assets['active']) throw new RuntimeException('Inconsistent asset activation observations');
            respond([
                'supported' => true,
                'active' => $assets['active'],
                'asset' => $match,
                'outputs' => $outputs['outputs'],
                'output_total' => $outputs['total'],
            ]);

        case 'pool':
            respond(rpc('/get_transaction_pool', []));

        default:
            respond(['error' => 'Unknown action'], 400);
    }
} catch (Throwable $error) {
    error_log('Monzero explorer API: ' . $error->getMessage());
    respond(['error' => 'Node request failed'], 502);
}

function validateAssetPage(array &$page, string $field, int $offset, int $count): void {
    // The daemon omits empty lists from its replies, so an empty page arrives without the field.
    if (!array_key_exists($field, $page) && is_int($page['total'] ?? null) && min($count, max(0, $page['total'] - $offset)) === 0) {
        $page[$field] = [];
    }
    if (($page['status'] ?? null) !== 'OK' || !is_bool($page['active'] ?? null)
        || !is_int($page['total'] ?? null) || $page['total'] < 0 || $page['total'] > 9007199254740991
        || !isset($page[$field]) || !is_array($page[$field]) || !array_is_list($page[$field])
        || count($page[$field]) !== min($count, max(0, $page['total'] - $offset))) {
        throw new RuntimeException('Invalid asset page observation');
    }
    foreach ($page[$field] as $record) {
        if (!is_array($record) || !validHash($record['asset_id'] ?? null)) throw new RuntimeException('Invalid asset record identity');
    }
}

function validHash(mixed $value): bool {
    return is_string($value) && preg_match('/^[0-9a-fA-F]{64}$/', $value) === 1;
}

function jsonRpc(string $method, array $params): array {
    $response = rpc('/json_rpc', [
        'jsonrpc' => '2.0',
        'id' => '0',
        'method' => $method,
        'params' => $params,
    ]);
    if (isset($response['error'])) throw new RuntimeException($response['error']['message'] ?? 'RPC error');
    return $response['result'] ?? [];
}

function rpc(string $path, array $payload): array {
    // Daemon endpoints expect an empty JSON object, not an empty JSON array.
    $json = $payload === [] ? '{}' : json_encode($payload, JSON_THROW_ON_ERROR);
    if (function_exists('curl_init')) {
        $curl = curl_init(NODE_RPC . $path);
        curl_setopt_array($curl, [
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => $json,
            CURLOPT_HTTPHEADER => ['Content-Type: application/json'],
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_CONNECTTIMEOUT => 3,
            CURLOPT_TIMEOUT => 12,
        ]);
        $body = curl_exec($curl);
        $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        $message = curl_error($curl);
        curl_close($curl);
        if ($body === false || $status !== 200) throw new RuntimeException($message ?: 'HTTP ' . $status);
    } else {
        $context = stream_context_create(['http' => [
            'method' => 'POST',
            'header' => "Content-Type: application/json\r\nConnection: close\r\n",
            'content' => $json,
            'timeout' => 12,
            'ignore_errors' => true,
        ]]);
        $body = @file_get_contents(NODE_RPC . $path, false, $context);
        $statusLine = $http_response_header[0] ?? '';
        preg_match('/\s(\d{3})\s/', $statusLine, $match);
        $status = isset($match[1]) ? (int)$match[1] : 0;
        if ($body === false || $status !== 200) throw new RuntimeException('HTTP ' . $status);
    }
    $decoded = json_decode($body, true, 512, JSON_THROW_ON_ERROR);
    return is_array($decoded) ? $decoded : [];
}

function respond(array $data, int $status = 200): never {
    http_response_code($status);
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);
    exit;
}
