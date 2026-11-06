<?php
declare(strict_types=1);
// Content editing for /admin: the reviewed catalogs, announcements, roadmap updates and the faucet
// panel. Every save is validated (lib/catalogs.php), backs up the previous file, is written
// atomically, checks for concurrent edits and is recorded in the audit log.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once dirname(__DIR__) . '/lib/catalogs.php';
require_once dirname(__DIR__) . '/faucet/lib.php';
require_once dirname(__DIR__) . '/lib/voting.php';
require_once dirname(__DIR__) . '/lib/telegram.php';

const CONTENT_HISTORY_KEEP = 50;
const CONTENT_MODULES = [
    'announcements' => ['label' => 'Announcements', 'perm' => 'announcements', 'file' => 'community/announcements.json', 'schema' => 'monzero-announcements-v1', 'posts' => true,
        'public' => '/community/', 'help' => 'Short dated news shown on the community page, newest first.'],
    'roadmap-updates' => ['label' => 'Roadmap updates', 'perm' => 'roadmap', 'file' => 'roadmap/updates.json', 'schema' => 'monzero-roadmap-updates-v1', 'posts' => true,
        'public' => '/roadmap/', 'help' => 'Dated progress notes shown at the top of the roadmap. Do not promise release dates.'],
    'exchanges' => ['label' => 'Exchange listings', 'perm' => 'exchanges', 'file' => 'exchanges/catalog.json', 'schema' => 'monzero-exchanges-v1', 'title' => 'exchange',
        'public' => '/exchanges/', 'help' => 'Status: planned, application-submitted, under-review, approved, live or suspended. Venue fields (pair, trading_url, deposits, withdrawals, trading, last_verified, verification_url) stay null until live. Never record a listing you have not verified.'],
    'ecosystem' => ['label' => 'Ecosystem directory', 'perm' => 'ecosystem', 'file' => 'ecosystem/catalog.json', 'schema' => 'monzero-ecosystem-v1', 'title' => 'name',
        'public' => '/ecosystem/', 'help' => 'Category: Mining pools, Wallet applications, Block explorers, Community projects, Developer libraries, Merchants or Educational resources. Operator: project or third-party (third-party needs an https link).'],
    'bounties' => ['label' => 'Bounty board', 'perm' => 'bounties', 'file' => 'community/bounties/catalog.json', 'schema' => 'monzero-bounties-v1', 'title' => 'title',
        'public' => '/community/bounties/', 'help' => 'Only mark a reward funded with funding evidence. Status/review pairs: open/awaiting-submission, in-review/under-review, completed/accepted, cancelled/closed. reward_atomic is in atomic units (1 XMZ = 100000000000).'],
    'fund-goals' => ['label' => 'Fundraising goals', 'perm' => 'fundraising', 'file' => 'fund/goals.json', 'schema' => 'monzero-fund-goals-v1', 'title' => 'title',
        'public' => '/fund/', 'help' => 'Goals fill in this order. target_usd is whole US dollars as a string. Set status to reached when a goal is funded, closed to hide it. Moving money between goals must be reported.'],
    'fund-spending' => ['label' => 'Spending records', 'perm' => 'fundraising', 'file' => 'fund/spending.json', 'schema' => 'monzero-fund-spending-v1', 'title' => 'description',
        'public' => '/fund/', 'help' => 'One entry per payment. goal is a goal id (or general). amount is in the coin paid; amount_usd at the rate on the day. Spends over $100 need evidence_url (invoice or receipt).'],
    'founder-disclosures' => ['label' => 'Founder disclosures', 'perm' => 'founder', 'file' => 'founder/disclosures.json', 'schema' => 'monzero-founder-disclosures-v1', 'title' => 'date',
        'public' => '/founder/', 'help' => 'Publish only an address you intend to disclose. A view key is optional; reserve proof fields must be supplied together and proof files must already exist under /founder/proofs/.'],
    'benchmarks' => ['label' => 'CPU benchmarks', 'perm' => 'benchmarks', 'file' => 'mining/benchmarks/catalog.json', 'schema' => 'monzero-randomx-benchmarks-v1', 'title' => 'model',
        'public' => '/mining/benchmarks/', 'help' => 'Moderate submissions here: keep community results as status community; mark verified only with a separate verification_url and verified_by. power_basis is unknown exactly when power_watts is null.'],
];
const CONTENT_TEMPLATES = [
    'exchanges' => ['id' => 'new-exchange', 'exchange' => 'Exchange name', 'status' => 'planned', 'note' => 'What is known so far, with no unverified claims.', 'updated_at' => 'TODAY',
        'application_date' => null, 'listing_fee' => null, 'funding' => null, 'announcements' => [], 'pair' => null, 'trading_url' => null, 'deposits' => null,
        'withdrawals' => null, 'trading' => null, 'last_verified' => null, 'verification_url' => null],
    'ecosystem' => ['id' => 'new-entry', 'name' => 'Name', 'category' => 'Community projects', 'operator' => 'third-party', 'description' => 'One or two sentences.', 'url' => 'https://example.org/', 'verified_at' => 'TODAY'],
    'bounties' => ['id' => 'new-bounty', 'title' => 'Short task title', 'description' => 'What needs doing and why.', 'category' => 'Website features', 'difficulty' => 'intermediate',
        'acceptance_criteria' => ['One testable condition.'], 'submission_instructions' => 'How to submit work for review.', 'discussion_url' => 'https://github.com/Nightglass-Protocol1/Monzero-source/issues',
        'status' => 'open', 'review_status' => 'awaiting-submission', 'updated_at' => 'TODAY', 'maintainer' => 'Monzero maintainers', 'reviewed_by' => 'Monzero maintainers',
        'reward_currency' => 'XMZ', 'reward_atomic' => '0', 'funding_status' => 'none', 'funding_evidence_url' => null, 'funding_checked_at' => null,
        'completion_evidence_url' => null, 'completion_checked_at' => null, 'payment_evidence_url' => null, 'payment_checked_at' => null],
    'fund-goals' => ['id' => 'new-goal', 'title' => 'Goal title', 'description' => 'What the money is for and how it is spent.', 'target_usd' => '1000', 'status' => 'open'],
    'fund-spending' => ['id' => 'payment-1', 'date' => 'TODAY', 'goal' => 'bug-bounties', 'description' => 'What was paid for', 'paid_to' => 'Who was paid', 'amount' => '100', 'currency' => 'USDT',
        'amount_usd' => '100.00', 'evidence_url' => null],
    'founder-disclosures' => ['id' => '2026-10-20-founder', 'date' => 'TODAY', 'height' => 1, 'address' => 'FRJ2nMKxwd94nsgTLnjpJ77d3gZ2X5NxdUkTcFXuoNaWSQBNp5syt7dcwVsGPYP6vE1hyuLQcVgTtHKkNBUhpJoENAhsryR', 'view_key' => null, 'reserve_proof_url' => null,
        'reserve_proof_sha256' => null, 'proof_message' => null, 'proven_xmz' => null, 'note' => null],
    'benchmarks' => ['id' => 'vendor-model-os', 'manufacturer' => 'AMD', 'model' => 'CPU model', 'os' => 'Linux', 'miner' => 'XMRig', 'miner_version' => '6.x', 'methodology' => 'How it was measured.',
        'cores' => 8, 'threads' => 16, 'hashrate_hps' => 1000, 'power_watts' => null, 'power_basis' => 'unknown', 'date' => 'TODAY', 'status' => 'community',
        'source_url' => 'https://example.org/result', 'verification_url' => null, 'verified_by' => null],
];

function contentRoot(): string { $c = getenv('MONZERO_CONTENT_ROOT'); return is_string($c) && $c !== '' ? $c : dirname(__DIR__); }

function contentToday(): string { return gmdate('Y-m-d'); }

// Private storage next to the admin database (outside the document root).
function contentPrivateDir(): string { $d = dirname(adminDbPath()); if (!is_dir($d)) @mkdir($d, 0700, true); return $d; }

function contentEncode(mixed $data): string { return json_encode($data, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_PRESERVE_ZERO_FRACTION | JSON_THROW_ON_ERROR) . "\n"; }

// Returns [decoded stdClass catalog, version hash of the file on disk].
function contentLoad(string $kind): array {
    $module = CONTENT_MODULES[$kind];
    $path = contentRoot() . '/' . $module['file'];
    $bytes = is_file($path) && !is_link($path) ? (string)file_get_contents($path, false, null, 0, 8388608) : '';
    $data = $bytes !== '' ? json_decode($bytes, false, 32) : null;
    if (!$data instanceof stdClass) $data = (object)['schema' => $module['schema'], 'updated_at' => contentToday(), 'records' => []];
    return [$data, hash('sha256', $bytes)];
}

function contentSave(string $kind, stdClass $data, string $version, int $actor, string $action, array $detail): ?string {
    $module = CONTENT_MODULES[$kind];
    $path = contentRoot() . '/' . $module['file'];
    $lock = fopen(contentPrivateDir() . "/content-$kind.lock", 'c');
    if (!$lock || !flock($lock, LOCK_EX)) return 'The file is busy. Try again.';
    $current = is_file($path) ? (string)file_get_contents($path) : '';
    if (!hash_equals(hash('sha256', $current), $version)) return 'Someone else changed this list after you opened it. Reload and repeat your change.';
    $data->updated_at = contentToday();
    $json = contentEncode($data);
    if ($problem = catalogProblem($kind, $json, contentToday())) return $problem;
    if ($current !== '') {
        $history = contentPrivateDir() . "/content-history/$kind";
        if (!is_dir($history)) @mkdir($history, 0700, true);
        @file_put_contents("$history/" . gmdate('Ymd\THis\Z') . '-' . bin2hex(random_bytes(3)) . '.json', $current);
        $old = glob("$history/*.json") ?: [];
        sort($old);
        foreach (array_slice($old, 0, max(0, count($old) - CONTENT_HISTORY_KEEP)) as $f) @unlink($f);
    }
    $tmp = "$path." . bin2hex(random_bytes(6)) . '.tmp';
    if (file_put_contents($tmp, $json) === false || !chmod($tmp, 0644) || !rename($tmp, $path)) { @unlink($tmp); return 'The file could not be written.'; }
    adminAudit($actor, $action, ['module' => $kind] + $detail);
    return null;
}

function contentAllowed(array $user, string $kind): bool { return isset(CONTENT_MODULES[$kind]) && adminCan($user, CONTENT_MODULES[$kind]['perm']); }

function contentKind(): string { $k = $_GET['kind'] ?? $_POST['kind'] ?? ''; return is_string($k) ? $k : ''; }

function contentSlug(string $title): string { return trim((string)preg_replace('/[^a-z0-9]+/', '-', strtolower($title)), '-') ?: 'post'; }

function contentBack(string $kind, string $message): never { header('Location: /admin/?' . http_build_query(['view' => 'content', 'kind' => $kind, 'msg' => $message]), true, 303); exit; }

// POST actions; returns false when $do is not a content action.
function contentAction(string $do, array $user): bool {
    $actor = (int)$user['id'];
    if ($do === 'faucet-enable' || $do === 'faucet-disable') {
        if (!adminCan($user, 'features')) adminDeny('You cannot change feature availability.');
        $on = $do === 'faucet-enable';
        if ($problem = faucetSetConfig('enabled', $on)) adminDeny($problem, 503);
        adminAudit($actor, $on ? 'faucet.resumed' : 'faucet.paused');
        header('Location: /admin/?view=faucet&msg=' . ($on ? 'faucet-on' : 'faucet-off'), true, 303);
        exit;
    }
    if (in_array($do, ['proposal-create', 'proposal-close', 'proposal-withdraw'], true)) {
        if (!adminCan($user, 'proposals')) adminDeny('You cannot moderate proposals.');
        if ($do === 'proposal-create') {
            $options = preg_split('/\R/', (string)($_POST['options'] ?? '')) ?: [];
            [$slug, $error] = voteCreate((string)($_POST['title'] ?? ''), (string)($_POST['description'] ?? ''), $options, (int)($_POST['days'] ?? 0), $user['email'], time());
            if ($error) { http_response_code(400); contentProposalsView($user, $error, $_POST); exit; }
            adminAudit($actor, 'proposal.created', ['slug' => $slug]);
        } else {
            $slug = (string)($_POST['slug'] ?? '');
            if (!voteSetStatus($slug, $do === 'proposal-close' ? 'closed' : 'withdrawn', time())) adminDeny('That proposal cannot be changed.', 409);
            adminAudit($actor, $do === 'proposal-close' ? 'proposal.closed' : 'proposal.withdrawn', ['slug' => $slug]);
        }
        header('Location: /admin/?view=proposals&msg=content-saved', true, 303);
        exit;
    }
    if (!in_array($do, ['content-save', 'content-delete', 'post-add', 'post-delete', 'post-telegram'], true)) return false;
    $kind = contentKind();
    if (!contentAllowed($user, $kind)) adminDeny('You cannot edit this content.');
    [$data, $version] = contentLoad($kind);
    $posted = (string)($_POST['version'] ?? '');
    $records = is_array($data->records ?? null) ? $data->records : [];
    if ($do === 'post-add') {
        $title = trim((string)($_POST['title'] ?? ''));
        $body = str_replace("\r\n", "\n", trim((string)($_POST['body'] ?? '')));
        $url = trim((string)($_POST['url'] ?? ''));
        $id = contentToday() . '-' . contentSlug($title);
        $taken = array_column(array_map(fn($r) => (array)$r, $records), 'id');
        for ($n = 2; in_array($id, $taken, true); $n++) $id = contentToday() . '-' . contentSlug($title) . "-$n";
        array_unshift($records, (object)['id' => $id, 'date' => contentToday(), 'title' => $title, 'body' => $body, 'url' => $url === '' ? null : $url]);
        $data->records = $records;
        $error = contentSave($kind, $data, $posted, $actor, 'content.post_added', ['id' => $id, 'title' => mb_substr($title, 0, 120)]);
        if ($error) { http_response_code(400); contentPostsView($user, $kind, $error, ['title' => $title, 'body' => $body, 'url' => $url]); exit; }
        if ($kind === 'announcements' && ($_POST['telegram'] ?? '') === '1' && ($tg = contentFeed())) {
            $sent = tgFeedAnnouncement($tg, $records[0]);
            adminAudit($actor, $sent ? 'telegram.posted' : 'telegram.failed', ['id' => $id]);
            contentBack($kind, $sent ? 'content-saved-telegram' : 'telegram-failed');
        }
        contentBack($kind, 'content-saved');
    }
    $id = (string)($_POST['id'] ?? '');
    $index = null;
    foreach ($records as $i => $r) if (($r->id ?? null) === $id) $index = $i;
    if ($do === 'post-telegram') {
        if ($kind !== 'announcements' || $index === null || !($tg = contentFeed())) adminDeny('This post cannot be sent to Telegram.', 409);
        if (tgFeedAnnouncementPosted((string)$id)) contentBack($kind, 'telegram-already');
        $sent = tgFeedAnnouncement($tg, $records[$index]);
        adminAudit($actor, $sent ? 'telegram.posted' : 'telegram.failed', ['id' => $id]);
        contentBack($kind, $sent ? 'telegram-sent' : 'telegram-failed');
    }
    if ($do === 'content-delete' || $do === 'post-delete') {
        if ($index === null) adminDeny('That entry no longer exists.', 409);
        array_splice($records, $index, 1);
        $data->records = $records;
        if ($error = contentSave($kind, $data, $posted, $actor, 'content.deleted', ['id' => $id])) adminDeny($error, 409);
        contentBack($kind, 'content-deleted');
    }
    // content-save: replace (or add) one record given as JSON.
    $text = (string)($_POST['record'] ?? '');
    try { $record = json_decode($text, false, 16, JSON_THROW_ON_ERROR); } catch (JsonException $e) { $record = null; }
    $error = null;
    if (!$record instanceof stdClass) $error = 'The entry must be one JSON object: { "id": ..., ... }.';
    else {
        if ($index === null && $id !== '') $error = 'That entry was removed by someone else. Reload and add it again.';
        elseif ($index === null) $records[] = $record;
        else $records[$index] = $record;
        if (!$error) {
            $data->records = array_values($records);
            $error = contentSave($kind, $data, $posted, $actor, $id === '' ? 'content.added' : 'content.edited', ['id' => (string)($record->id ?? '')]);
        }
    }
    if ($error) { http_response_code(400); contentRecordView($user, $kind, $id, $text, $error, $posted); exit; }
    contentBack($kind, 'content-saved');
}

// Telegram feed configuration, or null when the bot or its group feed is not set up.
function contentFeed(): ?array { $c = tgConfig(); return $c && $c['feed_chat'] ? $c : null; }

function contentCsrf(array $user): string { return '<input type="hidden" name="csrf" value="' . adminH($user['csrf']) . '">'; }

function contentMessage(): string {
    $messages = ['content-saved' => 'Saved and published.', 'content-saved-telegram' => 'Published on the website and posted to the Telegram group.',
        'telegram-sent' => 'Posted to the Telegram group.', 'telegram-already' => 'That post is already in the Telegram group, so it was not sent again.', 'telegram-failed' => 'The Telegram group did not accept the post. Check that the bot is still an admin there (details below).', 'content-deleted' => 'Removed and published.', 'faucet-on' => 'Faucet claims resumed.', 'faucet-off' => 'Faucet claims paused.'];
    $m = $messages[$_GET['msg'] ?? ''] ?? '';
    return $m ? '<p class="notice" role="status">' . adminH($m) . '</p>' : '';
}

function contentPostsView(array $user, string $kind, string $error = '', array $draft = []): void {
    $module = CONTENT_MODULES[$kind];
    [$data, $version] = contentLoad($kind);
    $hidden = contentCsrf($user) . '<input type="hidden" name="kind" value="' . adminH($kind) . '"><input type="hidden" name="version" value="' . adminH($version) . '">';
    $feed = $kind === 'announcements' ? contentFeed() : null;
    $feedNote = '';
    if ($feed) {
        $feedError = tgMeta('feed_error');
        $feedNote = '<p>Telegram group: <code>' . adminH($feed['feed_chat']) . '</code>' . ($feed['feed_pin'] ? ' (announcements are pinned)' : '') . '.' . ($feedError ? ' Last problem: ' . adminH($feedError) : '') . '</p>';
    }
    $list = '';
    foreach ($data->records ?? [] as $p) {
        $list .= '<article><h3>' . adminH($p->title ?? '') . '</h3><p><small>' . adminH($p->date ?? '') . '</small></p><p>' . nl2br(adminH($p->body ?? '')) . '</p>'
            . (($p->url ?? null) ? '<p><a href="' . adminH($p->url) . '">' . adminH($p->url) . '</a></p>' : '')
            . ($kind === 'announcements' && $feed && tgFeedAnnouncementPosted((string)($p->id ?? '')) ? '<p><small>Posted to the Telegram group.</small></p>' : '')
            . ($kind === 'announcements' && $feed && !tgFeedAnnouncementPosted((string)($p->id ?? '')) ? '<form method="post" action="/admin/?do=post-telegram">' . $hidden . '<input type="hidden" name="id" value="' . adminH($p->id ?? '') . '"><button type="submit">Post to the Telegram group</button></form>' : '')
            . '<details><summary>Remove</summary><form method="post" action="/admin/?do=post-delete">' . $hidden . '<input type="hidden" name="id" value="' . adminH($p->id ?? '') . '"><button type="submit">Remove this post from the site</button></form></details></article>';
    }
    adminPage($module['label'], contentMessage() . ($error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '')
        . '<p>' . adminH($module['help']) . ' Published on <a href="' . adminH($module['public']) . '">' . adminH($module['public']) . '</a>.</p>'
        . '<section><h2>New post</h2><form method="post" action="/admin/?do=post-add" class="stack">' . $hidden
        . '<label>Title<input name="title" maxlength="120" required value="' . adminH($draft['title'] ?? '') . '"></label>'
        . '<label>Text<textarea name="body" rows="6" maxlength="1500" required>' . adminH($draft['body'] ?? '') . '</textarea></label>'
        . '<label>Link (optional)<input name="url" placeholder="https://… or /roadmap/" value="' . adminH($draft['url'] ?? '') . '"></label>'
        . ($feed ? '<label class="check"><input type="checkbox" name="telegram" value="1" checked> Also post to the Telegram group</label>' : '')
        . '<button type="submit">Publish</button></form></section>' . $feedNote
        . '<section><h2>Published</h2>' . ($list ?: '<p>Nothing published yet.</p>') . '</section>', $user);
}

function contentListView(array $user, string $kind): void {
    $module = CONTENT_MODULES[$kind];
    [$data, $version] = contentLoad($kind);
    $hidden = contentCsrf($user) . '<input type="hidden" name="kind" value="' . adminH($kind) . '"><input type="hidden" name="version" value="' . adminH($version) . '">';
    $rows = '';
    foreach ($data->records ?? [] as $r) {
        $edit = '/admin/?' . http_build_query(['view' => 'content', 'kind' => $kind, 'record' => $r->id ?? '']);
        $rows .= '<tr><td><a href="' . adminH($edit) . '">' . adminH($r->{$module['title']} ?? $r->id ?? '') . '</a></td><td><code>' . adminH($r->id ?? '') . '</code></td><td>' . adminH($r->status ?? $r->category ?? '') . '</td>'
            . '<td><details><summary>Remove</summary><form method="post" action="/admin/?do=content-delete">' . $hidden . '<input type="hidden" name="id" value="' . adminH($r->id ?? '') . '"><button type="submit">Remove from the public page</button></form></details></td></tr>';
    }
    $add = '/admin/?' . http_build_query(['view' => 'content', 'kind' => $kind, 'record' => '']);
    adminPage($module['label'], contentMessage() . '<p>' . adminH($module['help']) . '</p><p>Changes are published to <a href="' . adminH($module['public']) . '">' . adminH($module['public']) . '</a> immediately after they pass the same checks the page uses. The previous version is kept.</p>'
        . '<p><a class="button" href="' . adminH($add) . '">Add an entry</a></p><div class="table-scroll"><table><thead><tr><th scope="col">Entry</th><th scope="col">ID</th><th scope="col">Status</th><th scope="col"></th></tr></thead><tbody>'
        . ($rows ?: '<tr><td colspan="4">No entries yet.</td></tr>') . '</tbody></table></div>', $user);
}

function contentRecordView(array $user, string $kind, string $id, ?string $text = null, string $error = '', ?string $version = null): void {
    $module = CONTENT_MODULES[$kind];
    [$data, $current] = contentLoad($kind);
    if ($text === null) {
        $record = null;
        foreach ($data->records ?? [] as $r) if (($r->id ?? null) === $id) $record = $r;
        if ($id !== '' && !$record) adminDeny('That entry no longer exists.', 404);
        $record ??= json_decode(str_replace('"TODAY"', '"' . contentToday() . '"', json_encode(CONTENT_TEMPLATES[$kind])));
        $text = contentEncode($record);
    }
    adminPage(($id === '' ? 'Add to ' : 'Edit in ') . $module['label'], ($error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '')
        . '<p>' . adminH($module['help']) . '</p><p>Edit the fields between the quotes. Dates are YYYY-MM-DD; use <code>null</code> for an empty field. Today is ' . contentToday() . ' (UTC).</p>'
        . '<form method="post" action="/admin/?do=content-save" class="stack">' . contentCsrf($user) . '<input type="hidden" name="kind" value="' . adminH($kind) . '">'
        . '<input type="hidden" name="version" value="' . adminH($version ?? $current) . '"><input type="hidden" name="id" value="' . adminH($id) . '">'
        . '<label>Entry (JSON)<textarea name="record" rows="24" spellcheck="false" class="code">' . adminH($text) . '</textarea></label>'
        . '<button type="submit">Check and publish</button></form><p><a href="/admin/?view=content&amp;kind=' . adminH($kind) . '">Back to the list</a></p>', $user);
}

function contentFaucetView(array $user): void {
    $config = faucetConfig();
    $beat = is_file(faucetHeartbeatPath()) ? json_decode((string)file_get_contents(faucetHeartbeatPath()), true) : null;
    $db = faucetDb();
    $counts = [];
    foreach ($db->query("SELECT status, COUNT(*) AS n FROM claims WHERE created_at > " . (time() - 86400) . " GROUP BY status") as $row) $counts[$row['status']] = (int)$row['n'];
    $pending = (int)$db->query("SELECT COUNT(*) FROM claims WHERE status IN ('pending', 'processing')")->fetchColumn();
    $rows = '';
    foreach ($db->query('SELECT id, created_at, finished_at, status, address, txid, reason FROM claims ORDER BY id DESC LIMIT 50') as $c) {
        $address = substr((string)$c['address'], 0, 8) . '…' . substr((string)$c['address'], -6);
        $result = $c['txid'] ? '<code>' . adminH(substr($c['txid'], 0, 16)) . '…</code>' : adminH($c['reason'] ?? '');
        $rows .= '<tr><td>' . (int)$c['id'] . '</td><td>' . adminH(gmdate('Y-m-d H:i', (int)$c['created_at'])) . '</td><td>' . adminH($c['status']) . '</td><td><code>' . adminH($address) . '</code></td><td>' . $result . '</td></tr>';
    }
    $worker = is_array($beat) && is_int($beat['at'] ?? null)
        ? sprintf('Last heartbeat %s UTC; balance %.4f XMZ (%.4f unlocked)%s.', gmdate('Y-m-d H:i', $beat['at']), (int)$beat['balance_atomic'] / 1e11, (int)$beat['unlocked_atomic'] / 1e11, !empty($beat['halted']) ? '; HALTED: ' . $beat['halted'] : '')
        : 'No heartbeat from the payout worker yet.';
    $toggle = adminCan($user, 'features')
        ? '<form method="post" action="/admin/?do=' . ($config['enabled'] ? 'faucet-disable' : 'faucet-enable') . '">' . contentCsrf($user) . '<button type="submit">' . ($config['enabled'] ? 'Pause claims now' : 'Resume claims') . '</button></form>'
        : '';
    adminPage('Faucet', contentMessage() . '<section><h2>State</h2><p>Claims are <strong>' . ($config['enabled'] ? 'open' : 'paused') . '</strong>. ' . adminH($worker) . '</p>'
        . '<p>Last 24 hours: ' . (int)($counts['paid'] ?? 0) . ' paid, ' . (int)($counts['failed'] ?? 0) . ' failed; ' . $pending . ' waiting now. Daily maximum ' . sprintf('%.2f', (int)$config['daily_cap_atomic'] / 1e11) . ' XMZ.</p>' . $toggle
        . '<p>Pausing stops new claims immediately; claims already queued are still paid. Refill the wallet, then run <code>utils/faucet/split-outputs.sh</code>.</p></section>'
        . '<section><h2>Recent claims</h2><div class="table-scroll"><table><thead><tr><th scope="col">#</th><th scope="col">Claimed (UTC)</th><th scope="col">Status</th><th scope="col">Address</th><th scope="col">Transaction or reason</th></tr></thead><tbody>'
        . ($rows ?: '<tr><td colspan="5">No claims yet.</td></tr>') . '</tbody></table></div></section>', $user);
}

function contentProposalsView(array $user, string $error = '', array $draft = []): void {
    $now = time();
    $list = '';
    foreach (voteProposals(true) as $p) {
        $results = voteResults($p);
        $state = voteState($p, $now);
        $hidden = contentCsrf($user) . '<input type="hidden" name="slug" value="' . adminH($p['slug']) . '">';
        $counts = implode(', ', array_map(fn($o, $n) => adminH($o) . ': ' . $n, $results['options'], $results['counts']));
        $list .= '<article><h3>' . adminH($p['title']) . '</h3><p><small>' . adminH($state) . ' · ' . $results['total'] . ' votes · closes ' . gmdate('Y-m-d H:i', (int)$p['closes_at']) . ' UTC · by ' . adminH($p['created_by']) . '</small></p><p>' . $counts . '</p>'
            . ($p['status'] !== 'withdrawn' ? '<p><a href="/community/voting/?p=' . adminH($p['slug']) . '">Public page</a></p>' : '')
            . (voteOpen($p, $now) ? '<details><summary>Close voting now</summary><form method="post" action="/admin/?do=proposal-close">' . $hidden . '<button type="submit">Close and show final results</button></form></details>' : '')
            . ($p['status'] !== 'withdrawn' ? '<details><summary>Withdraw</summary><form method="post" action="/admin/?do=proposal-withdraw">' . $hidden . '<button type="submit">Withdraw and hide from the public list</button></form></details>' : '') . '</article>';
    }
    adminPage('Community proposals', contentMessage() . ($error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '')
        . '<p>Polls are non-binding feedback. Faucet members whose account existed before a poll opened vote once each; results show after voting or at the close.</p>'
        . '<section><h2>New proposal</h2><form method="post" action="/admin/?do=proposal-create" class="stack">' . contentCsrf($user)
        . '<label>Title<input name="title" minlength="5" maxlength="140" required value="' . adminH($draft['title'] ?? '') . '"></label>'
        . '<label>Description<textarea name="description" rows="6" minlength="10" maxlength="4000" required>' . adminH($draft['description'] ?? '') . '</textarea></label>'
        . '<label>Options, one per line (2-' . VOTE_MAX_OPTIONS . ')<textarea name="options" rows="4" required>' . adminH($draft['options'] ?? "Yes\nNo") . '</textarea></label>'
        . '<label>Voting lasts (days)<input name="days" type="number" min="1" max="' . VOTE_MAX_DAYS . '" value="' . adminH($draft['days'] ?? '7') . '" required></label>'
        . '<button type="submit">Open the poll</button></form></section><section><h2>Proposals</h2>' . ($list ?: '<p>None yet.</p>') . '</section>', $user);
}

// GET views; returns false when the view is not a content view.
function contentView(string $view, array $user): bool {
    if ($view === 'proposals') {
        if (!adminCan($user, 'proposals')) adminDeny('You cannot moderate proposals.');
        contentProposalsView($user);
        return true;
    }
    if ($view === 'faucet') {
        if (!adminCan($user, 'faucet') && !adminCan($user, 'features')) adminDeny('You cannot view the faucet.');
        contentFaucetView($user);
        return true;
    }
    if ($view !== 'content') return false;
    $kind = contentKind();
    if (!contentAllowed($user, $kind)) adminDeny('You cannot edit this content.');
    if (!empty(CONTENT_MODULES[$kind]['posts'])) contentPostsView($user, $kind);
    elseif (array_key_exists('record', $_GET)) contentRecordView($user, $kind, is_string($_GET['record']) ? $_GET['record'] : '');
    else contentListView($user, $kind);
    return true;
}

// Links for the overview page.
function contentLinks(array $user): string {
    $links = [];
    foreach (CONTENT_MODULES as $kind => $m) if (adminCan($user, $m['perm'])) $links[] = '<li><a href="/admin/?view=content&amp;kind=' . $kind . '">' . adminH($m['label']) . '</a></li>';
    if (adminCan($user, 'proposals')) $links[] = '<li><a href="/admin/?view=proposals">Community proposals</a></li>';
    if (adminCan($user, 'faucet') || adminCan($user, 'features')) $links[] = '<li><a href="/admin/?view=faucet">Faucet</a></li>';
    return $links ? '<section><h2>Content</h2><ul>' . implode('', $links) . '</ul></section>' : '';
}
