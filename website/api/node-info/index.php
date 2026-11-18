<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    header('Allow: POST');
    echo json_encode(['status' => 'METHOD_NOT_ALLOWED']);
    exit;
}

const NODE_RPC = 'http://node.monzero.org:6175';
const MAX_TIP_AGE_SECONDS = 900;
const MAX_FUTURE_SKEW_SECONDS = 300;
const GENESIS_HASH = '84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4';

function nodeRequest(string $path, string $body): array
{
    $url = NODE_RPC . $path;
    if (function_exists('curl_init')) {
        $curl = curl_init($url);
        curl_setopt_array($curl, [
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => $body,
            CURLOPT_HTTPHEADER => ['Content-Type: application/json'],
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_CONNECTTIMEOUT => 2,
            CURLOPT_TIMEOUT => 5,
        ]);
        $response = curl_exec($curl);
        $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        $error = curl_error($curl);
        curl_close($curl);
        return [$response, $status, $error];
    }

    $context = stream_context_create(['http' => [
        'method' => 'POST',
        'header' => "Content-Type: application/json\r\nConnection: close\r\n",
        'content' => $body,
        'timeout' => 5,
        'ignore_errors' => true,
    ]]);
    $response = @file_get_contents($url, false, $context);
    $statusLine = $http_response_header[0] ?? '';
    preg_match('/\s(\d{3})\s/', $statusLine, $match);
    $status = isset($match[1]) ? (int)$match[1] : 0;
    return [$response, $status, 'HTTP ' . $status];
}

[$response, $status, $error] = nodeRequest('/get_info', '{}');
$decoded = is_string($response) ? json_decode($response, true) : null;
if ($response === false || $status !== 200 || !is_array($decoded)) {
    http_response_code(502);
    echo json_encode(['status' => 'NODE_UNAVAILABLE']);
    error_log('Monzero node status proxy failed: ' . $error);
    exit;
}

$peerCount = (int)($decoded['incoming_connections_count'] ?? 0)
    + (int)($decoded['outgoing_connections_count'] ?? 0);
$decoded['connections_hidden'] = !empty($decoded['restricted']) && $peerCount === 0;
$decoded['peer_ready'] = !$decoded['connections_hidden'] && $peerCount >= 2;

$headerBody = json_encode([
    'jsonrpc' => '2.0',
    'id' => 'website-readiness',
    'method' => 'get_last_block_header',
]);
[$headerResponse, $headerStatus, $headerError] = nodeRequest('/json_rpc', $headerBody);
$headerDecoded = is_string($headerResponse) ? json_decode($headerResponse, true) : null;
$header = is_array($headerDecoded) ? ($headerDecoded['result']['block_header'] ?? null) : null;
$tipTimestamp = is_array($header) ? ($header['timestamp'] ?? null) : null;
$tipAge = is_int($tipTimestamp) ? time() - $tipTimestamp : null;
$genesisOnly = (int)($decoded['height'] ?? 0) === 1
    && hash_equals(GENESIS_HASH, (string)($decoded['top_block_hash'] ?? ''));
$tipFresh = $genesisOnly || (is_int($tipAge)
    && $tipAge <= MAX_TIP_AGE_SECONDS
    && $tipAge >= -MAX_FUTURE_SKEW_SECONDS);

$decoded['tip_timestamp'] = $tipTimestamp;
$decoded['tip_age_seconds'] = $tipAge;
$decoded['tip_fresh'] = $tipFresh;
$decoded['network_ready'] = ($decoded['status'] ?? '') === 'OK'
    && ($decoded['synchronized'] ?? false) === true
    && $tipFresh
    && $decoded['peer_ready'];
if ($headerStatus !== 200 || !is_array($header)) {
    error_log('Monzero last-block status proxy failed: ' . $headerError);
}
echo json_encode($decoded, JSON_UNESCAPED_SLASHES);
