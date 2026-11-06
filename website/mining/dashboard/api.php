<?php
declare(strict_types=1);
require __DIR__.'/contract.php';
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: no-referrer');
function dashboardReply(array $value,int $code=200): never {http_response_code($code);echo json_encode($value,JSON_UNESCAPED_SLASHES|JSON_INVALID_UTF8_SUBSTITUTE);exit;}
if(($_SERVER['REQUEST_METHOD'] ?? '')!=='POST'){header('Allow: POST');dashboardReply(['error'=>'POST required'],405);}
if(!empty($_GET))dashboardReply(['error'=>'Query parameters are not accepted'],400);
if(($_SERVER['HTTP_SEC_FETCH_SITE'] ?? 'same-origin')==='cross-site')dashboardReply(['error'=>'Same-origin requests required'],403);
$raw=file_get_contents('php://input',false,null,0,2049);
if(strlen($raw)>2048)dashboardReply(['error'=>'Request too large'],413);
$input=json_decode($raw,true);unset($raw);
if(!is_array($input) || count($input)!==1 || !poolLookupShape($input['address'] ?? null))dashboardReply(['error'=>'Enter an XMZ public mining address'],400);
$address=$input['address'];unset($input);
if(!function_exists('curl_init'))dashboardReply(['error'=>'Pool lookup temporarily unavailable'],503);
// Private, short-lived operational state only. No plain addresses or client IPs.
$directory=sys_get_temp_dir().'/monzero-pool-dashboard-v1';
if(!file_exists($directory))@mkdir($directory,0700);
if(!is_dir($directory) || is_link($directory) || (fileperms($directory)&0077)!==0)dashboardReply(['error'=>'Pool lookup temporarily unavailable'],503);
$path=$directory.'/state.json';
if(is_link($path))dashboardReply(['error'=>'Pool lookup temporarily unavailable'],503);
$file=@fopen($path,'c+');
if(!$file || !flock($file,LOCK_EX|LOCK_NB))dashboardReply(['error'=>'Pool lookup busy; retry shortly'],503);
if(!@chmod($path,0600))dashboardReply(['error'=>'Pool lookup temporarily unavailable'],503);
$bytes=stream_get_contents($file,2097153);
if(strlen($bytes)>2097152)dashboardReply(['error'=>'Pool lookup temporarily unavailable'],503);
$state=json_decode($bytes,true);$now=time();
// Stable deployment secret comes from private configuration, never temp state.
$configuredKey=getenv('MONZERO_POOL_WORKER_LABEL_KEY');
$workerKey=is_string($configuredKey)&&preg_match('/\A[0-9a-f]{64}\z/',$configuredKey)?$configuredKey:null;
if(!is_array($state) || !is_string($state['salt'] ?? null) || ($state['window'] ?? 0)<$now-60)
    $state=['salt'=>bin2hex(random_bytes(32)),'window'=>$now,'total'=>0,'clients'=>[],'cache'=>[]];
unset($state['worker_key']); // remove temporary keys from earlier previews
$client=hash_hmac('sha256',(string)($_SERVER['REMOTE_ADDR'] ?? 'unknown'),$state['salt']);
function dashboardSave($file,array $state): void {
    $bytes=json_encode($state,JSON_THROW_ON_ERROR);rewind($file);
    if(!ftruncate($file,0) || fwrite($file,$bytes)!==strlen($bytes) || !fflush($file))throw new RuntimeException('State unavailable');
}
if(($state['clients'][$client] ?? 0)>=10 || $state['total']>=60){header('Retry-After: 60');dashboardReply(['error'=>'Lookup limit reached; retry in one minute'],429);}
$state['clients'][$client]=($state['clients'][$client] ?? 0)+1;$state['total']++;
try {
    dashboardSave($file,$state); // failures and cached responses count toward abuse limits
    $miner=dashboardFetch('/api/miner?address='.rawurlencode($address));
    $cached=$state['cache']['stats'] ?? null;
    if(is_array($cached) && ($cached['observed_at'] ?? 0)>$now-30) $stats=$cached['value'];
    else {
        $rawStats=dashboardFetch('/api/stats');
        $stats=['config'=>['coin'=>$rawStats['config']['coin'] ?? null,'coinUnits'=>$rawStats['config']['coinUnits'] ?? null],
            'pool'=>['hashrate'=>poolNonnegative($rawStats['pool']['hashrate'] ?? null)]];
        $state['cache']=['stats'=>['observed_at'=>time(),'value'=>$stats]];
    }
    $view=poolMinerView($miner,$stats,$address,time(),$workerKey);
    unset($address,$miner,$stats);
    dashboardSave($file,$state);dashboardReply($view);
} catch(Throwable $error) {
    // Never log request bodies, addresses or upstream response contents.
    dashboardReply(['error'=>'Pool lookup temporarily unavailable'],502);
}
function dashboardFetch(string $path): array {
    $body='';$curl=curl_init('https://pool.monzero.org'.$path);
    curl_setopt_array($curl,[CURLOPT_FOLLOWLOCATION=>false,CURLOPT_PROTOCOLS=>CURLPROTO_HTTPS,
        CURLOPT_CONNECTTIMEOUT=>3,CURLOPT_TIMEOUT=>7,CURLOPT_HTTPHEADER=>['Accept: application/json'],
        CURLOPT_WRITEFUNCTION=>static function($handle,string $chunk) use (&$body): int {
            if(strlen($body)+strlen($chunk)>262144)return 0;
            $body.=$chunk;return strlen($chunk);
        }]);
    $ok=curl_exec($curl);$status=curl_getinfo($curl,CURLINFO_RESPONSE_CODE);curl_close($curl);
    if($ok===false || $status!==200)throw new RuntimeException('Pool unavailable');
    $data=json_decode($body,true,64,JSON_THROW_ON_ERROR|JSON_BIGINT_AS_STRING);
    if(!is_array($data))throw new RuntimeException('Invalid pool response');
    return $data;
}
