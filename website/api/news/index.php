<?php
declare(strict_types=1);
// Ingest for the daily crypto headlines (utils/news/news_collector.py on the EU node): gzip JSON,
// HMAC-signed over "timestamp\nnonce\nbody". Publishes /news/data/latest.json, one file per UTC day
// (the day's last ranking, kept 30 days) and /news/data/index.json listing the available days.
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

const NEWS_SCHEMA = 'monzero-news-v1';
const NEWS_KEEP_DAYS = 30;

function reply(int $status, array $body): never { http_response_code($status); echo json_encode($body); exit; }

function privateDir(): string { $c = getenv('MONZERO_PRIVATE_DIR'); return is_string($c) && $c !== '' ? $c : dirname(__DIR__, 3) . '/monzero-private'; }
function dataDir(): string { $c = getenv('MONZERO_NEWS_DATA_DIR'); return is_string($c) && $c !== '' ? $c : dirname(__DIR__, 2) . '/news/data'; }

function newsText(mixed $v, int $max): bool {
    return is_string($v) && mb_check_encoding($v, 'UTF-8') && trim($v) === $v && $v !== '' && mb_strlen($v) <= $max && !preg_match('/[\p{Cc}\p{Cf}\x{2028}\x{2029}]/u', $v);
}

function newsUrl(mixed $v): bool {
    return is_string($v) && strlen($v) <= 1000 && preg_match('~\Ahttps://[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?)+(?::\d{1,5})?(?:[/?][\x21-\x7E]*)?\z~', $v) === 1;
}

function newsProblem(mixed $p, int $now): ?string {
    if (!is_array($p) || array_keys($p) !== ['schema', 'generated_at', 'date', 'window_hours', 'stories', 'sources']) return 'shape';
    if ($p['schema'] !== NEWS_SCHEMA || $p['window_hours'] !== 24) return 'schema';
    if (!is_int($p['generated_at']) || abs($now - $p['generated_at']) > 900 || $p['date'] !== gmdate('Y-m-d', $p['generated_at'])) return 'time';
    if (!is_array($p['sources']) || !array_is_list($p['sources']) || count($p['sources']) < 1 || count($p['sources']) > 60) return 'sources';
    $names = [];
    foreach ($p['sources'] as $s) {
        if (!is_array($s) || array_keys($s) !== ['name', 'home', 'region', 'ok'] || !newsText($s['name'], 60) || !newsUrl($s['home']) || !newsText($s['region'], 20) || !is_bool($s['ok'])) return 'source';
        $names[$s['name']] = true;
    }
    if (!is_array($p['stories']) || !array_is_list($p['stories']) || count($p['stories']) > 30) return 'stories';
    foreach ($p['stories'] as $s) {
        if (!is_array($s) || array_keys($s) !== ['title', 'url', 'source', 'published_at', 'outlets', 'also']) return 'story';
        if (!newsText($s['title'], 300) || !newsUrl($s['url']) || !isset($names[$s['source']]) || !is_int($s['published_at']) || $s['published_at'] > $p['generated_at']
            || $p['generated_at'] - $s['published_at'] > 86400 + 3600 || !is_int($s['outlets']) || $s['outlets'] < 1 || $s['outlets'] > 60) return 'story';
        if (!is_array($s['also']) || !array_is_list($s['also']) || count($s['also']) > 10 || count($s['also']) > $s['outlets'] - 1) return 'also';  // the lead outlet is not repeated
        foreach ($s['also'] as $a) if (!is_array($a) || array_keys($a) !== ['source', 'url'] || !isset($names[$a['source']]) || !newsUrl($a['url'])) return 'also';
    }
    return null;
}

function writeJson(string $path, array $data): void {
    $tmp = dirname($path) . '/.news-' . bin2hex(random_bytes(6));
    if (file_put_contents($tmp, json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE)) === false || !chmod($tmp, 0644) || !rename($tmp, $path)) {
        @unlink($tmp); reply(503, ['error' => 'Storage unavailable']);
    }
}

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') { header('Allow: POST'); reply(405, ['error' => 'POST only']); }
$path = privateDir() . '/news-config.php';
$config = is_file($path) && !is_link($path) ? (static fn() => require $path)() : [];
$secret = is_array($config) && is_string($config['secret'] ?? null) && preg_match('/\A[0-9a-f]{64}\z/', $config['secret']) ? $config['secret'] : '';
if ($secret === '') reply(503, ['error' => 'Not configured']);
$body = (string)file_get_contents('php://input', false, null, 0, 1048577);
if (strlen($body) > 1048576) reply(413, ['error' => 'Too large']);
$ts = (string)($_SERVER['HTTP_X_NEWS_TIMESTAMP'] ?? '');
$nonce = (string)($_SERVER['HTTP_X_NEWS_NONCE'] ?? '');
$signature = (string)($_SERVER['HTTP_X_NEWS_SIGNATURE'] ?? '');
$now = time();
if (!preg_match('/\A\d{1,12}\z/', $ts) || abs($now - (int)$ts) > 300 || !preg_match('/\A[0-9a-f]{32}\z/', $nonce)
    || !hash_equals(hash_hmac('sha256', "$ts\n$nonce\n" . $body, $secret), $signature)) reply(401, ['error' => 'Bad signature']);

$state = privateDir() . '/news';
if (!is_dir($state)) @mkdir($state, 0700, true);
$lock = @fopen("$state/.lock", 'c');
if (!$lock || !flock($lock, LOCK_EX)) reply(503, ['error' => 'Storage unavailable']);
$nonces = json_decode((string)@file_get_contents("$state/nonces.json"), true);
$nonces = array_filter(is_array($nonces) ? $nonces : [], fn($at) => is_int($at) && $at > $now - 900);
if (isset($nonces[$nonce])) reply(409, ['error' => 'Replayed']);
$nonces[$nonce] = $now;
file_put_contents("$state/nonces.json", json_encode($nonces), LOCK_EX);

$json = @gzdecode($body, 8388608);
$payload = is_string($json) ? json_decode($json, true) : null;
if ($problem = newsProblem($payload, $now)) reply(400, ['error' => "Invalid news ($problem)"]);
$dir = dataDir();
if (!is_dir($dir)) @mkdir($dir, 0755, true);
if (!is_dir($dir) || is_link($dir)) reply(503, ['error' => 'Storage unavailable']);
$latest = json_decode((string)@file_get_contents("$dir/latest.json"), true);
if (is_array($latest) && is_int($latest['generated_at'] ?? null) && $payload['generated_at'] <= $latest['generated_at']) reply(409, ['error' => 'Older than the published news']);

writeJson("$dir/latest.json", $payload);
writeJson("$dir/{$payload['date']}.json", $payload);
$days = [];
foreach (glob("$dir/????-??-??.json") ?: [] as $file) {
    $day = basename($file, '.json');
    if ($day < gmdate('Y-m-d', $now - NEWS_KEEP_DAYS * 86400)) { @unlink($file); continue; }
    $days[] = $day;
}
rsort($days);
writeJson("$dir/index.json", ['schema' => 'monzero-news-index-v1', 'updated_at' => $now, 'days' => $days]);
// Major stories also go to the community Telegram group when the bot's group feed is set up.
try { require_once dirname(__DIR__, 2) . '/lib/telegram.php'; if ($tg = tgConfig()) tgFeedNews($tg, $payload, $now); }
catch (Throwable $e) { error_log('Monzero news telegram: ' . $e->getMessage()); }
reply(200, ['ok' => true, 'stories' => count($payload['stories'])]);
