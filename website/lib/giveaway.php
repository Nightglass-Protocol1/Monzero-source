<?php
declare(strict_types=1);
// Telegram giveaway: a fixed number of slots (e.g. 20 x 500 XMZ) for the first people who open the
// bot link t.me/BOT?start=gw-CODE, are members of the community group, have a public @handle and
// send a mainnet XMZ payout address. Each claim records the Telegram user ID, the @handle and the
// payout address; one claim per Telegram account, per @handle and per address. Nothing is paid here:
// the owner exports the claims as a CSV and pays them with utils/website/airdrop_payout.py.
// Config: ~/monzero-private/giveaway-config.php (written by utils/website/giveaway.sh). Without it, off.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once __DIR__ . '/xmz-address.php';

const GW_ATOMIC = 100000000000;
const GW_ADDRESS_MINUTES = 30;   // after opening the link, this long to send the address

function gwPrivateDir(): string { $d = getenv('MONZERO_PRIVATE_DIR'); return is_string($d) && $d !== '' ? $d : dirname(__DIR__, 2) . '/monzero-private'; }

function gwConfig(): ?array {
    $path = gwPrivateDir() . '/giveaway-config.php';
    $c = is_file($path) && !is_link($path) ? (static fn() => require $path)() : null;
    if (!is_array($c) || !is_string($c['code'] ?? null) || !preg_match('/\A[A-Za-z0-9]{6,32}\z/', $c['code'])) return null;
    $slots = $c['slots'] ?? 0; $amount = $c['amount_xmz'] ?? 0;
    if (!is_int($slots) || $slots < 1 || $slots > 1000 || !is_int($amount) || $amount < 1 || $amount > 100000) return null;
    return ['code' => $c['code'], 'slots' => $slots, 'amount_xmz' => $amount, 'enabled' => ($c['enabled'] ?? false) === true,
        'opens_at' => is_int($c['opens_at'] ?? null) ? $c['opens_at'] : 0, 'closes_at' => is_int($c['closes_at'] ?? null) ? $c['closes_at'] : 0,
        'title' => is_string($c['title'] ?? null) ? mb_substr($c['title'], 0, 80) : 'Monzero giveaway'];
}

function gwDb(): PDO {
    static $db = null;
    if ($db) return $db;
    $path = gwPrivateDir() . '/giveaway.sqlite';
    if (!is_dir(dirname($path))) @mkdir(dirname($path), 0700, true);
    $new = !file_exists($path);
    $db = new PDO('sqlite:' . $path, null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
    if ($new) @chmod($path, 0600);
    $db->exec('PRAGMA busy_timeout = 5000;');
    $db->exec(<<<'SQL'
        CREATE TABLE IF NOT EXISTS claims (code TEXT NOT NULL, slot INTEGER NOT NULL, user_id INTEGER NOT NULL, handle TEXT NOT NULL,
            first_name TEXT NOT NULL, address TEXT NOT NULL, amount_xmz INTEGER NOT NULL, claimed_at INTEGER NOT NULL,
            PRIMARY KEY (code, slot), UNIQUE (code, user_id), UNIQUE (code, handle), UNIQUE (code, address));
        CREATE TABLE IF NOT EXISTS sessions (user_id INTEGER PRIMARY KEY, code TEXT NOT NULL, started_at INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS finished (code TEXT PRIMARY KEY, finished_at INTEGER NOT NULL, note TEXT NOT NULL);
    SQL);
    return $db;
}

function gwClaimed(string $code): int {
    $s = gwDb()->prepare('SELECT COUNT(*) FROM claims WHERE code = ?');
    $s->execute([$code]);
    return (int)$s->fetchColumn();
}

// A finished giveaway (paid out) never takes a claim again, whatever its config says. Permanent:
// the only way back is editing the database by hand.
function gwFinished(string $code): bool {
    $s = gwDb()->prepare('SELECT 1 FROM finished WHERE code = ?');
    $s->execute([$code]);
    return $s->fetchColumn() !== false;
}

function gwFinish(string $code, string $note, int $now): void {
    $db = gwDb();
    $db->prepare('INSERT OR IGNORE INTO finished (code, finished_at, note) VALUES (?, ?, ?)')->execute([$code, $now, mb_substr($note, 0, 200)]);
    $db->prepare('DELETE FROM sessions WHERE code = ?')->execute([$code]);
}

const GW_FINISHED_TEXT = 'This giveaway has finished and been paid out. Follow https://t.me/MonzeroOfficial for the next one.';

function gwClaimOf(string $code, int $user): ?array {
    $s = gwDb()->prepare('SELECT * FROM claims WHERE code = ? AND user_id = ?');
    $s->execute([$code, $user]);
    return $s->fetch() ?: null;
}

// Why the giveaway is not taking claims right now, or null when it is.
function gwClosedReason(?array $c, int $now): ?string {
    if ($c && gwFinished($c['code'])) return GW_FINISHED_TEXT;
    if (!$c || !$c['enabled']) return 'This giveaway is not running. Follow https://t.me/MonzeroOfficial for the next one.';
    if ($c['opens_at'] && $now < $c['opens_at']) return 'The giveaway opens at ' . gmdate('H:i', $c['opens_at']) . ' UTC on ' . gmdate('j M Y', $c['opens_at']) . '. Open the link again then.';
    if ($c['closes_at'] && $now >= $c['closes_at']) return 'This giveaway has ended.';
    if (gwClaimed($c['code']) >= $c['slots']) return 'All ' . $c['slots'] . ' places have been claimed. Thanks for joining in — watch https://t.me/MonzeroOfficial for the next one.';
    return null;
}

function gwHandle(array $from): string { return is_string($from['username'] ?? null) && preg_match('/\A[A-Za-z0-9_]{4,32}\z/', $from['username']) ? '@' . $from['username'] : ''; }

// $isMember(int $userId): bool checks the community group. Returns the bot's reply.
function gwStart(string $code, array $from, callable $isMember, int $now): string {
    $c = gwConfig();
    if (gwFinished($code)) return GW_FINISHED_TEXT;
    if (!$c || !hash_equals($c['code'], $code)) return 'That giveaway link is not valid. Follow https://t.me/MonzeroOfficial for the next one.';
    $user = (int)$from['id'];
    if ($mine = gwClaimOf($c['code'], $user)) return gwClaimedText($mine, $c);
    if ($why = gwClosedReason($c, $now)) return $why;
    if (gwHandle($from) === '') return "You need a public Telegram username (@handle) to take part, so we can link your claim to you.\nSet one in Telegram: Settings → Username. Then open the giveaway link again.";
    if (!$isMember($user)) return "Join https://t.me/MonzeroOfficial first, then open the giveaway link again.";
    gwDb()->prepare('INSERT INTO sessions (user_id, code, started_at) VALUES (?, ?, ?) ON CONFLICT (user_id) DO UPDATE SET code = excluded.code, started_at = excluded.started_at')
        ->execute([$user, $c['code'], $now]);
    $left = $c['slots'] - gwClaimed($c['code']);
    return "🎁 {$c['title']}: {$c['amount_xmz']} XMZ each for the first {$c['slots']} people. $left place" . ($left === 1 ? '' : 's') . " left.\n\n"
        . 'Reply now with your Monzero (XMZ) payout address — your main wallet address starting with F. Your place is only taken when your address arrives, so be quick.'
        . "\n\nYour claim will be linked to " . gwHandle($from) . '. One claim per person and per address.';
}

function gwHasSession(int $user, int $now): bool {
    $s = gwDb()->prepare('SELECT started_at FROM sessions WHERE user_id = ?');
    $s->execute([$user]);
    $t = $s->fetchColumn();
    return $t !== false && $now - (int)$t <= GW_ADDRESS_MINUTES * 60;
}

// The address reply after gwStart. Returns [reply, filledUp] where filledUp is true for the claim that took the last place.
function gwClaim(string $text, array $from, callable $isMember, int $now): array {
    $user = (int)$from['id'];
    $db = gwDb();
    $s = $db->prepare('SELECT code FROM sessions WHERE user_id = ?');
    $s->execute([$user]);
    $c = gwConfig();
    if ($c && gwFinished($c['code'])) return [GW_FINISHED_TEXT, false];
    if (!$c || $s->fetchColumn() !== $c['code']) return ['Open the giveaway link again to take part.', false];
    if (!gwHasSession($user, $now)) return ['Your ' . GW_ADDRESS_MINUTES . ' minutes to send an address ran out. Open the giveaway link again.', false];
    if ($mine = gwClaimOf($c['code'], $user)) return [gwClaimedText($mine, $c), false];
    $handle = gwHandle($from);
    if ($handle === '') return ['You need a public Telegram username (@handle). Set one in Settings → Username, then open the giveaway link again.', false];
    $parsed = xmzParseAddress($text);
    if (isset($parsed['error'])) return [$parsed['error'] . ' Reply with your XMZ payout address.', false];
    if ($parsed['network'] !== 'mainnet') return ['That is not a mainnet address. Reply with your mainnet XMZ address (it starts with F).', false];
    if ($parsed['kind'] === 'integrated') return ['Please use your main wallet address, not an integrated (payment ID) address. Exchange deposit addresses are not accepted.', false];
    if ($why = gwClosedReason($c, $now)) return [$why, false];
    if (!$isMember($user)) return ['Join https://t.me/MonzeroOfficial first, then send your address again.', false];
    $db->exec('BEGIN IMMEDIATE');   // the count and the insert as one step: never more than the slots
    try {
        $claimed = gwClaimed($c['code']);
        if ($claimed >= $c['slots']) { $db->exec('ROLLBACK'); return ['Sorry — all ' . $c['slots'] . ' places were claimed just before your address arrived.', false]; }
        $dup = $db->prepare('SELECT user_id = ? AS me, handle = ? COLLATE NOCASE AS h, address = ? AS a FROM claims WHERE code = ? AND (user_id = ? OR handle = ? COLLATE NOCASE OR address = ?)');
        $dup->execute([$user, $handle, $parsed['address'], $c['code'], $user, $handle, $parsed['address']]);
        if ($d = $dup->fetch()) {
            $db->exec('ROLLBACK');
            return [$d['a'] ? 'That address has already been used for a claim. One claim per address.' : 'This Telegram account has already claimed. One claim per person.', false];
        }
        $slot = $claimed + 1;
        $db->prepare('INSERT INTO claims (code, slot, user_id, handle, first_name, address, amount_xmz, claimed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)')
            ->execute([$c['code'], $slot, $user, $handle, mb_substr(trim((string)preg_replace('/[\p{Cc}\p{Cf}]/u', '', (string)($from['first_name'] ?? ''))), 0, 64),
                $parsed['address'], $c['amount_xmz'], $now]);
        $db->prepare('DELETE FROM sessions WHERE user_id = ?')->execute([$user]);
        $db->exec('COMMIT');
    } catch (Throwable $e) { if ($db->inTransaction()) $db->exec('ROLLBACK'); throw $e; }
    return [gwClaimedText(gwClaimOf($c['code'], $user), $c), $slot === $c['slots']];
}

function gwShort(string $a): string { return substr($a, 0, 8) . '…' . substr($a, -6); }

function gwClaimedText(array $claim, array $c): string {
    return "✅ You're in! Place {$claim['slot']} of {$c['slots']}.\n{$claim['amount_xmz']} XMZ will be sent to " . gwShort($claim['address'])
        . " (linked to {$claim['handle']}). Payouts go out after the giveaway fills; we'll announce it in https://t.me/MonzeroOfficial.";
}

// Owner tools (run with php on the webspace by utils/website/giveaway.sh).
function gwExportCsv(string $code): string {
    $s = gwDb()->prepare('SELECT slot, handle, user_id, address, amount_xmz, claimed_at FROM claims WHERE code = ? ORDER BY slot');
    $s->execute([$code]);
    // The username without its @: a cell starting with @ can be read as a formula by spreadsheets.
    $out = "kind,address,atomic,xmz,slot,telegram_username,telegram_id,claimed_utc\n";
    foreach ($s->fetchAll() as $r)
        $out .= implode(',', ['giveaway', $r['address'], (string)((int)$r['amount_xmz'] * GW_ATOMIC), (string)$r['amount_xmz'], (string)$r['slot'],
            ltrim($r['handle'], '@'), (string)$r['user_id'], gmdate('Y-m-d H:i:s', (int)$r['claimed_at'])]) . "\n";
    return $out;
}
