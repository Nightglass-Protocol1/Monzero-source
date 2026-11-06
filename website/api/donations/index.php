<?php
declare(strict_types=1);

// Public donation figures for /fund/. Read-only: every number comes from a
// public block explorer and can be checked there. Explorer data is cached
// outside the web root for CACHE_SECONDS so visitors never contact a third
// party and the free explorer limits are respected. A coin whose explorer
// fails keeps its last good figures, marked with when they were fetched.

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: public, max-age=60');
header('X-Content-Type-Options: nosniff');

if ($_SERVER['REQUEST_METHOD'] !== 'GET' && $_SERVER['REQUEST_METHOD'] !== 'HEAD') {
    http_response_code(405);
    header('Allow: GET, HEAD');
    echo json_encode(['status' => 'METHOD_NOT_ALLOWED']);
    exit;
}

const CACHE_SECONDS = 600;
const RECENT_PER_COIN = 10;
const USDT_SOLANA_MINT = 'Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB';

// Must match the addresses printed on /fund/; the page warns if they differ.
const WALLETS = [
    ['id' => 'btc', 'name' => 'Bitcoin', 'symbol' => 'BTC', 'decimals' => 8, 'price' => 'bitcoin',
     'address' => 'bc1pzsjv3e7mcst6khqr3msq85wlq67hnh48yjrm6eepycvze4htazyqyruvng',
     'addressUrl' => 'https://mempool.space/address/', 'txUrl' => 'https://mempool.space/tx/'],
    ['id' => 'ltc', 'name' => 'Litecoin', 'symbol' => 'LTC', 'decimals' => 8, 'price' => 'litecoin',
     'address' => 'ltc1qjsmmr8f92pt3k8ztmvg8tc0veu08whc8pklm5x',
     'addressUrl' => 'https://litecoinspace.org/address/', 'txUrl' => 'https://litecoinspace.org/tx/'],
    ['id' => 'bch', 'name' => 'Bitcoin Cash', 'symbol' => 'BCH', 'decimals' => 8, 'price' => 'bitcoin-cash',
     'address' => 'qq0je5u6qfkx56e9pl3y6mw26ma4qnwvav9evttxpe',
     'addressUrl' => 'https://blockchair.com/bitcoin-cash/address/', 'txUrl' => 'https://blockchair.com/bitcoin-cash/transaction/'],
    ['id' => 'doge', 'name' => 'Dogecoin', 'symbol' => 'DOGE', 'decimals' => 8, 'price' => 'dogecoin',
     'address' => 'DQGNxF1SEyh9cXH11mgns749frKwq7Xo1e',
     'addressUrl' => 'https://blockchair.com/dogecoin/address/', 'txUrl' => 'https://blockchair.com/dogecoin/transaction/'],
    ['id' => 'eth', 'name' => 'Ethereum', 'symbol' => 'ETH', 'decimals' => 18, 'price' => 'ethereum',
     'address' => '0xD0Eb7C50Db766c897B085A492387135123f0A6b9',
     'addressUrl' => 'https://etherscan.io/address/', 'txUrl' => 'https://etherscan.io/tx/'],
    ['id' => 'usdt-sol', 'name' => 'USDT on Solana', 'symbol' => 'USDT', 'decimals' => 6, 'price' => 'tether',
     'address' => '5LpU8TxNqWvs5ACyhxCXNVFXNMq6mnvvK1YvMGkBWXog',
     'addressUrl' => 'https://solscan.io/account/', 'txUrl' => 'https://solscan.io/tx/'],
];

function fetchJson(string $url, ?array $post = null): array
{
    $curl = curl_init($url);
    $options = [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_CONNECTTIMEOUT => 5,
        CURLOPT_TIMEOUT => 12,
        CURLOPT_USERAGENT => 'monzero-donations/1 (+https://monzero.org/fund/)',
        CURLOPT_PROTOCOLS => CURLPROTO_HTTPS,
    ];
    if ($post !== null) {
        $options[CURLOPT_POST] = true;
        $options[CURLOPT_POSTFIELDS] = json_encode($post);
        $options[CURLOPT_HTTPHEADER] = ['Content-Type: application/json'];
    }
    curl_setopt_array($curl, $options);
    $body = curl_exec($curl);
    $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
    curl_close($curl);
    if (!is_string($body) || $status !== 200 || strlen($body) > 2000000) {
        throw new RuntimeException("explorer returned HTTP {$status}");
    }
    $value = json_decode($body, true, 64, JSON_BIGINT_AS_STRING);
    if (!is_array($value)) throw new RuntimeException('explorer returned no JSON');
    return $value;
}

// Atomic amounts are kept as decimal strings (wei do not fit a float).
function atomic(mixed $value): string
{
    $text = is_int($value) ? (string)$value : (is_string($value) ? $value : '');
    if (!preg_match('/^[0-9]{1,40}$/', $text)) throw new RuntimeException('bad amount');
    return ltrim($text, '0') === '' ? '0' : ltrim($text, '0');
}

function received(string $coin, string $txid, string $amount, ?int $time, bool $confirmed): array
{
    return ['coin' => $coin, 'txid' => $txid, 'amount' => $amount, 'time' => $time, 'confirmed' => $confirmed];
}

function bitcoin(array $wallet): array
{
    $base = 'https://mempool.space/api/address/' . $wallet['address'];
    $summary = fetchJson($base);
    $chain = $summary['chain_stats'];
    $recent = [];
    foreach (array_slice(fetchJson($base . '/txs'), 0, RECENT_PER_COIN) as $tx) {
        $amount = 0;
        foreach ($tx['vout'] as $out) {
            if (($out['scriptpubkey_address'] ?? '') === $wallet['address']) $amount += (int)$out['value'];
        }
        if ($amount > 0) {
            $recent[] = received('btc', $tx['txid'], (string)$amount, $tx['status']['block_time'] ?? null,
                (bool)$tx['status']['confirmed']);
        }
    }
    return ['received' => atomic($chain['funded_txo_sum']),
        'balance' => atomic((int)$chain['funded_txo_sum'] - (int)$chain['spent_txo_sum']),
        'transactions' => (int)$chain['tx_count'], 'recent' => $recent];
}

function blockcypher(array $wallet, string $chain): array
{
    $address = $chain === 'eth' ? substr($wallet['address'], 2) : $wallet['address'];
    $data = fetchJson("https://api.blockcypher.com/v1/{$chain}/main/addrs/{$address}?limit=50");
    $recent = [];
    foreach (array_merge($data['unconfirmed_txrefs'] ?? [], $data['txrefs'] ?? []) as $ref) {
        if (($ref['tx_input_n'] ?? 0) !== -1 || count($recent) >= RECENT_PER_COIN) continue;
        $when = $ref['confirmed'] ?? $ref['received'] ?? null;
        $recent[] = received($wallet['id'], ($chain === 'eth' ? '0x' : '') . $ref['tx_hash'], atomic($ref['value']),
            $when ? strtotime($when) : null, isset($ref['confirmed']));
    }
    return ['received' => atomic($data['total_received']), 'balance' => atomic($data['balance']),
        'transactions' => (int)$data['n_tx'], 'recent' => $recent];
}

function bitcoinCash(array $wallet): array
{
    $base = 'https://api.haskoin.com/bch/address/' . $wallet['address'];
    $balance = fetchJson($base . '/balance');
    $recent = [];
    foreach (fetchJson($base . '/transactions/full?limit=' . RECENT_PER_COIN) as $tx) {
        $amount = 0;
        foreach ($tx['outputs'] as $out) {
            // Haskoin reports outputs in prefixed CashAddr form.
            if (($out['address'] ?? '') === 'bitcoincash:' . $wallet['address']) $amount += (int)$out['value'];
        }
        if ($amount > 0) {
            $recent[] = received('bch', $tx['txid'], (string)$amount, $tx['time'] ?? null,
                isset($tx['block']['height']));
        }
    }
    return ['received' => atomic($balance['received']),
        'balance' => atomic($balance['confirmed'] + $balance['unconfirmed']),
        'transactions' => (int)$balance['txs'], 'recent' => $recent];
}

function solanaUsdt(array $wallet): array
{
    $rpc = fn(string $method, array $params) => fetchJson('https://api.mainnet-beta.solana.com',
        ['jsonrpc' => '2.0', 'id' => 1, 'method' => $method, 'params' => $params])['result']
        ?? throw new RuntimeException('Solana RPC error');
    $accounts = $rpc('getTokenAccountsByOwner', [$wallet['address'], ['mint' => USDT_SOLANA_MINT],
        ['encoding' => 'jsonParsed']])['value'];
    $balance = 0; $received = 0; $count = 0; $recent = [];
    foreach ($accounts as $account) {
        $tokenAccount = $account['pubkey'];
        $balance += (int)$account['account']['data']['parsed']['info']['tokenAmount']['amount'];
        foreach ($rpc('getSignaturesForAddress', [$tokenAccount, ['limit' => 25]]) as $signature) {
            $count++;
            if ($signature['err'] !== null || count($recent) >= RECENT_PER_COIN) continue;
            $tx = $rpc('getTransaction', [$signature['signature'],
                ['encoding' => 'jsonParsed', 'maxSupportedTransactionVersion' => 0]]);
            $change = 0;
            foreach (['postTokenBalances' => 1, 'preTokenBalances' => -1] as $key => $sign) {
                foreach ($tx['meta'][$key] ?? [] as $entry) {
                    if (($entry['owner'] ?? '') === $wallet['address'] && $entry['mint'] === USDT_SOLANA_MINT) {
                        $change += $sign * (int)$entry['uiTokenAmount']['amount'];
                    }
                }
            }
            if ($change > 0) {
                $received += $change;
                $recent[] = received('usdt-sol', $signature['signature'], (string)$change,
                    $signature['blockTime'] ?? null, true);
            }
        }
    }
    // Solana explorers do not total incoming transfers; this sums the newest 25
    // transfers, so it can understate a long history (the balance is exact).
    return ['received' => atomic(max($received, $balance)), 'balance' => atomic($balance),
        'transactions' => $count, 'recent' => $recent];
}

function refresh(array $previous): array
{
    $coins = []; $recent = [];
    $old = [];
    foreach ($previous['coins'] ?? [] as $coin) $old[$coin['id']] = $coin;
    foreach (WALLETS as $wallet) {
        $entry = ['id' => $wallet['id'], 'name' => $wallet['name'], 'symbol' => $wallet['symbol'],
            'decimals' => $wallet['decimals'], 'address' => $wallet['address'],
            'addressUrl' => $wallet['addressUrl'] . $wallet['address'], 'txUrl' => $wallet['txUrl']];
        try {
            $data = match ($wallet['id']) {
                'btc' => bitcoin($wallet),
                'ltc' => blockcypher($wallet, 'ltc'),
                'doge' => blockcypher($wallet, 'doge'),
                'eth' => blockcypher($wallet, 'eth'),
                'bch' => bitcoinCash($wallet),
                'usdt-sol' => solanaUsdt($wallet),
            };
            $entry += ['received' => $data['received'], 'balance' => $data['balance'],
                'transactions' => $data['transactions'], 'fetchedAt' => time(), 'current' => true];
            $coinRecent = $data['recent'];
        } catch (Throwable $error) {
            $last = $old[$wallet['id']] ?? null;
            $entry += ['received' => $last['received'] ?? null, 'balance' => $last['balance'] ?? null,
                'transactions' => $last['transactions'] ?? null, 'fetchedAt' => $last['fetchedAt'] ?? null,
                'current' => false];
            $coinRecent = array_values(array_filter($previous['recent'] ?? [],
                fn($item) => $item['coin'] === $wallet['id']));
        }
        $coins[] = $entry;
        $recent = array_merge($recent, $coinRecent);
    }
    usort($recent, fn($a, $b) => ($b['time'] ?? PHP_INT_MAX) <=> ($a['time'] ?? PHP_INT_MAX));
    try {
        $ids = implode(',', array_unique(array_column(WALLETS, 'price')));
        $quotes = fetchJson("https://api.coingecko.com/api/v3/simple/price?ids={$ids}&vs_currencies=usd");
        $prices = [];
        foreach (WALLETS as $wallet) $prices[$wallet['id']] = (float)($quotes[$wallet['price']]['usd'] ?? 0);
        $pricedAt = time();
    } catch (Throwable $error) {
        $prices = $previous['pricesUsd'] ?? []; $pricedAt = $previous['pricedAt'] ?? null;
    }
    return ['schema' => 'monzero-donations-v1', 'updatedAt' => time(), 'pricedAt' => $pricedAt,
        'pricesUsd' => $prices, 'coins' => $coins, 'recent' => array_slice($recent, 0, 30)];
}

$directory = dirname(__DIR__, 3) . '/monzero-data';
if (!is_dir($directory)) @mkdir($directory, 0700, true);
$path = $directory . '/donations-cache.json';
$cached = is_file($path) ? json_decode((string)file_get_contents($path), true) : null;

if (!is_array($cached) || time() - (int)($cached['updatedAt'] ?? 0) >= CACHE_SECONDS) {
    $lock = @fopen($path . '.lock', 'c');
    // One refresh at a time; other visitors get the previous figures meanwhile.
    if ($lock !== false && flock($lock, LOCK_EX | LOCK_NB)) {
        $fresh = refresh(is_array($cached) ? $cached : []);
        $temporary = $path . '.tmp';
        if (file_put_contents($temporary, json_encode($fresh)) !== false) rename($temporary, $path);
        $cached = $fresh;
        flock($lock, LOCK_UN);
    }
    if ($lock !== false) fclose($lock);
}

if (!is_array($cached)) {
    http_response_code(503);
    echo json_encode(['status' => 'REFRESHING']);
    exit;
}
echo json_encode($cached);
