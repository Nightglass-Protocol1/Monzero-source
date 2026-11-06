<?php
declare(strict_types=1);
require_once __DIR__.'/contract.php';
const STATS_MAX_RESPONSE_BYTES = 2097152;
const NODE_RPC = 'http://node.monzero.org:6175';
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: public, max-age=60');
header('X-Content-Type-Options: nosniff');
if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'GET') { header('Allow: GET'); respond(['error'=>'GET required'],405); }
// Private, deployment-scoped cache; a busy refresher never blocks another visitor.
$directory=sys_get_temp_dir().'/monzero-chain-charts-'.substr(hash('sha256',__DIR__),0,16);
if(!file_exists($directory))@mkdir($directory,0700);
if(!is_dir($directory)||is_link($directory)||(fileperms($directory)&0077)!==0
    ||(function_exists('posix_geteuid')&&fileowner($directory)!==posix_geteuid()))respond(['error'=>'Stats temporarily unavailable'],503);
$path=$directory.'/observation.json';
if(is_link($path))respond(['error'=>'Stats temporarily unavailable'],503);
$cache=@fopen($path,'c+');
if(!$cache||!flock($cache,LOCK_SH|LOCK_NB)||!@chmod($path,0600))respond(['error'=>'Stats temporarily unavailable'],503);
try {
    statsReplyFromCache($cache);
    flock($cache,LOCK_UN);
    if(!flock($cache,LOCK_EX|LOCK_NB)){header('Retry-After: 2');respond(['error'=>'Stats refresh in progress'],503);}
    rewind($cache);statsReplyFromCache($cache);
    $observedAt=time();
    $info = rpc('/get_info', []);
    $end=statsHeight($info)-1;
    $range = jsonRpc('get_block_headers_range', ['start_height'=>max(1,$end-719),'end_height'=>$end,'fill_pow_hash'=>false]);
    $data=statsObservation($info,$range,$observedAt);
    statsSave($cache,$data);
    respond($data);
} catch (Throwable $e) {
    error_log('Monzero charts: observation unavailable');
    try { statsSave($cache,['unavailable'=>true,'generated_at'=>time()]); } catch(Throwable $ignored) {}
    header('Retry-After: 5');
    header('Cache-Control: no-store');
    respond(['error'=>'Mainnet data is temporarily unavailable. Please retry.'],502);
}
function statsReplyFromCache($cache): void {
    $saved = json_decode(stream_get_contents($cache,STATS_MAX_RESPONSE_BYTES+1), true);
    if (is_array($saved) && is_int($saved['generated_at'] ?? null) && $saved['generated_at']<=time()) {
        if (($saved['unavailable'] ?? false)===true && time()-$saved['generated_at']<5) {
            header('Retry-After: 5');respond(['error'=>'Mainnet data is temporarily unavailable. Please retry.'],502);
        }
        if (statsCachedObservation($saved,time()))respond($saved);
    }
}
function statsSave($file,array $value): void {
    $bytes=json_encode($value,JSON_THROW_ON_ERROR);rewind($file);
    if(!ftruncate($file,0)||fwrite($file,$bytes)!==strlen($bytes)||!fflush($file))throw new RuntimeException('Cache write failed');
}
function jsonRpc(string $method, array $params): array {
    $response = rpc('/json_rpc', [
        'jsonrpc' => '2.0',
        'id' => '0',
        'method' => $method,
        'params' => $params,
    ]);
    if (isset($response['error'])) throw new RuntimeException('RPC error');
    return $response['result'] ?? [];
}

function rpc(string $path, array $payload): array {
    $json=$payload===[]?'{}':json_encode($payload,JSON_THROW_ON_ERROR);
    [$body,$status]=statsRpcTransport($path,$json);
    if($body===false||$status!==200)throw new RuntimeException('Node response unavailable');
    $decoded=json_decode($body,true,64,JSON_THROW_ON_ERROR);
    if(!is_array($decoded))throw new RuntimeException('Invalid node response');
    return $decoded;
}

function respond(array $data, int $status = 200): never {
    if($status>=400)header('Cache-Control: no-store');
    elseif(isset($data['generated_at']))header('Cache-Control: public, max-age='.max(0,60-(time()-$data['generated_at'])));
    http_response_code($status);
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);
    exit;
}

function statsRpcTransport(string $path, string $body): array
{
    $url = NODE_RPC . $path;
    if (function_exists('curl_init')) {
        $response = '';
        $curl = curl_init($url);
        curl_setopt_array($curl, [
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => $body,
            CURLOPT_HTTPHEADER => ['Content-Type: application/json'],
            CURLOPT_FOLLOWLOCATION => false,
            CURLOPT_WRITEFUNCTION => static function ($handle, string $chunk) use (&$response): int {
                if (strlen($response) + strlen($chunk) > STATS_MAX_RESPONSE_BYTES) return 0;
                $response .= $chunk;
                return strlen($chunk);
            },
            CURLOPT_CONNECTTIMEOUT => 2,
            CURLOPT_TIMEOUT => 5,
        ]);
        $ok = curl_exec($curl);
        $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        $error = curl_error($curl);
        curl_close($curl);
        return [$ok === false ? false : $response, $status, $error];
    }

    $context = stream_context_create(['http' => [
        'method' => 'POST',
        'header' => "Content-Type: application/json\r\nConnection: close\r\n",
        'content' => $body,
        'timeout' => 5,
        'ignore_errors' => true,
        'follow_location' => 0,
    ]]);
    $deadline = microtime(true) + 5;
    $stream = @fopen($url, 'rb', false, $context);
    $response = '';
    if (!$stream) $response = false;
    else {
        try {
            while (!feof($stream)) {
                $remaining = $deadline - microtime(true);
                if ($remaining <= 0) { $response = false; break; }
                $seconds = (int)$remaining;
                stream_set_timeout($stream, $seconds, max(1, (int)(($remaining - $seconds) * 1000000)));
                $chunk = fread($stream, 8192);
                if ($chunk === false || stream_get_meta_data($stream)['timed_out']
                    || strlen($response) + strlen($chunk) > STATS_MAX_RESPONSE_BYTES) {
                    $response = false; break;
                }
                $response .= $chunk;
            }
        } finally { fclose($stream); }
    }
    $statusLine = $http_response_header[0] ?? '';
    preg_match('/^HTTP\/\S+\s+(\d{3})\b/', $statusLine, $match);
    $status = isset($match[1]) ? (int)$match[1] : 0;
    return [$response, $status, 'HTTP ' . $status];
}
