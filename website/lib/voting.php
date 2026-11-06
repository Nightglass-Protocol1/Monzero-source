<?php
declare(strict_types=1);
// Non-binding community polls. Voters are faucet members with a confirmed email and a fully
// signed-in session (password + authenticator code). Only accounts created before a poll opened
// may vote on it, each member votes once, and at most VOTE_PER_NETWORK votes per poll come from
// one network. Polls never change the Monzero protocol.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once dirname(__DIR__) . '/faucet/lib.php';

const VOTE_PER_NETWORK = 3;
const VOTE_MAX_OPTIONS = 6;
const VOTE_MAX_DAYS = 30;

function voteDb(): PDO {
    static $db = null;
    if ($db) return $db;
    $configured = getenv('MONZERO_VOTING_DB');
    $path = is_string($configured) && $configured !== '' ? $configured : faucetPrivateDir() . '/voting.sqlite';
    if (!is_dir(dirname($path))) @mkdir(dirname($path), 0700, true);
    if (is_link($path)) throw new RuntimeException('Voting storage unavailable');
    $new = !file_exists($path);
    $db = new PDO('sqlite:' . $path, null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
    if ($new) @chmod($path, 0600);
    $db->exec('PRAGMA busy_timeout = 5000; PRAGMA journal_mode = WAL;');
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 1) {
        $db->exec(<<<'SQL'
            BEGIN;
            CREATE TABLE proposals (
                id INTEGER PRIMARY KEY,
                slug TEXT NOT NULL UNIQUE,
                title TEXT NOT NULL,
                description TEXT NOT NULL,
                options TEXT NOT NULL,
                opens_at INTEGER NOT NULL,
                closes_at INTEGER NOT NULL,
                status TEXT NOT NULL CHECK (status IN ('open', 'closed', 'withdrawn')),
                created_by TEXT NOT NULL,
                created_at INTEGER NOT NULL
            );
            CREATE TABLE votes (
                proposal_id INTEGER NOT NULL REFERENCES proposals(id),
                member_id INTEGER NOT NULL,
                option_index INTEGER NOT NULL,
                client_hash TEXT NOT NULL,
                voted_at INTEGER NOT NULL,
                PRIMARY KEY (proposal_id, member_id)
            );
            CREATE INDEX votes_client ON votes(proposal_id, client_hash);
            CREATE TRIGGER votes_no_update BEFORE UPDATE ON votes BEGIN SELECT RAISE(ABORT, 'votes are final'); END;
            PRAGMA user_version = 1;
            COMMIT;
        SQL);
    }
    return $db;
}

function voteOpen(array $p, int $now): bool { return $p['status'] === 'open' && $now >= (int)$p['opens_at'] && $now < (int)$p['closes_at']; }

function voteState(array $p, int $now): string {
    if ($p['status'] === 'withdrawn') return 'Withdrawn';
    return voteOpen($p, $now) ? 'Open' : 'Closed';
}

function voteProposals(bool $includeWithdrawn = false): array {
    return voteDb()->query('SELECT * FROM proposals' . ($includeWithdrawn ? '' : " WHERE status != 'withdrawn'") . ' ORDER BY opens_at DESC, id DESC')->fetchAll();
}

function voteProposal(string $slug): ?array {
    $stmt = voteDb()->prepare('SELECT * FROM proposals WHERE slug = ?');
    $stmt->execute([$slug]);
    return $stmt->fetch() ?: null;
}

function voteResults(array $p): array {
    $options = json_decode($p['options'], true);
    $counts = array_fill(0, count($options), 0);
    $stmt = voteDb()->prepare('SELECT option_index, COUNT(*) AS n FROM votes WHERE proposal_id = ? GROUP BY option_index');
    $stmt->execute([$p['id']]);
    foreach ($stmt as $row) if (isset($counts[(int)$row['option_index']])) $counts[(int)$row['option_index']] = (int)$row['n'];
    return ['options' => $options, 'counts' => $counts, 'total' => array_sum($counts)];
}

function voteOf(array $p, int $memberId): ?int {
    $stmt = voteDb()->prepare('SELECT option_index FROM votes WHERE proposal_id = ? AND member_id = ?');
    $stmt->execute([$p['id'], $memberId]);
    $v = $stmt->fetchColumn();
    return $v === false ? null : (int)$v;
}

// Why this member cannot vote on this proposal, or null if they can.
function voteBlock(array $p, ?array $member, int $now): ?string {
    if (!voteOpen($p, $now)) return 'Voting on this proposal has ended.';
    if (!$member) return 'Sign in with your faucet account to vote.';
    if ($member['level'] !== 'full' || !$member['totp_secret'] || !$member['email_verified_at']) return 'Finish signing in (password and authenticator code) to vote.';
    if ((int)$member['created_at'] >= (int)$p['opens_at']) return 'Only accounts created before this proposal opened can vote on it.';
    if (voteOf($p, (int)$member['id']) !== null) return 'You have voted on this proposal.';
    $stmt = voteDb()->prepare('SELECT COUNT(*) FROM votes WHERE proposal_id = ? AND client_hash = ?');
    $stmt->execute([$p['id'], faucetClientHash()]);
    if ((int)$stmt->fetchColumn() >= VOTE_PER_NETWORK) return 'This network has already cast the maximum number of votes on this proposal.';
    return null;
}

function voteCast(array $p, array $member, int $option, int $now): ?string {
    $options = json_decode($p['options'], true);
    if ($option < 0 || $option >= count($options)) return 'Choose one of the options.';
    $db = voteDb();
    $db->exec('BEGIN IMMEDIATE');
    if ($block = voteBlock($p, $member, $now)) { $db->exec('ROLLBACK'); return $block; }
    $db->prepare('INSERT INTO votes (proposal_id, member_id, option_index, client_hash, voted_at) VALUES (?, ?, ?, ?, ?)')
        ->execute([$p['id'], $member['id'], $option, faucetClientHash(), $now]);
    $db->exec('COMMIT');
    faucetEvent((int)$member['id'], 'vote.cast', ['proposal' => $p['slug']]);
    return null;
}

// Admin: create a proposal. Returns [slug, null] or [null, error].
function voteCreate(string $title, string $description, array $options, int $days, string $by, int $now): array {
    $title = trim($title); $description = trim(str_replace("\r\n", "\n", $description));
    $options = array_values(array_filter(array_map('trim', $options), fn($o) => $o !== ''));
    if (mb_strlen($title) < 5 || mb_strlen($title) > 140) return [null, 'The title must be 5-140 characters.'];
    if (mb_strlen($description) < 10 || mb_strlen($description) > 4000) return [null, 'The description must be 10-4000 characters.'];
    if (count($options) < 2 || count($options) > VOTE_MAX_OPTIONS || count(array_unique($options)) !== count($options)) return [null, 'Give 2-' . VOTE_MAX_OPTIONS . ' different options, one per line.'];
    foreach ($options as $o) if (mb_strlen($o) > 120) return [null, 'Each option must be at most 120 characters.'];
    if ($days < 1 || $days > VOTE_MAX_DAYS) return [null, 'Voting must last 1-' . VOTE_MAX_DAYS . ' days.'];
    foreach (array_merge([$title, $description], $options) as $t) if (preg_match('/[\p{Cc}\p{Cf}]/u', str_replace("\n", '', $t))) return [null, 'Remove control characters from the text.'];
    $base = gmdate('Y-m', $now) . '-' . (trim((string)preg_replace('/[^a-z0-9]+/', '-', strtolower($title)), '-') ?: 'proposal');
    $slug = substr($base, 0, 60);
    for ($n = 2; voteProposal($slug); $n++) $slug = substr($base, 0, 56) . "-$n";
    voteDb()->prepare("INSERT INTO proposals (slug, title, description, options, opens_at, closes_at, status, created_by, created_at) VALUES (?, ?, ?, ?, ?, ?, 'open', ?, ?)")
        ->execute([$slug, $title, $description, json_encode($options, JSON_UNESCAPED_UNICODE), $now, $now + $days * 86400, $by, $now]);
    return [$slug, null];
}

function voteSetStatus(string $slug, string $status, int $now): bool {
    $sql = $status === 'closed' ? "UPDATE proposals SET status = 'closed', closes_at = MIN(closes_at, ?) WHERE slug = ? AND status = 'open'"
        : "UPDATE proposals SET status = 'withdrawn', closes_at = MIN(closes_at, ?) WHERE slug = ? AND status != 'withdrawn'";
    $stmt = voteDb()->prepare($sql);
    $stmt->execute([$now, $slug]);
    return $stmt->rowCount() === 1;
}
