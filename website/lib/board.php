<?php
declare(strict_types=1);
// Community discussion board. Members are faucet accounts with a confirmed email and a fully
// signed-in session (password + authenticator code) who have chosen a public username; email
// addresses are never shown. Posts are plain text (paragraphs, links, ``` code blocks), so no
// member-supplied HTML ever reaches a page. Moderation lives in /admin/ (permission "board").
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once dirname(__DIR__) . '/faucet/lib.php';
require_once __DIR__ . '/registration.php';

const BOARD_THREADS_PER_PAGE = 30;
const BOARD_POSTS_PER_PAGE = 25;
const BOARD_TITLE_MIN = 5;
const BOARD_TITLE_MAX = 120;
const BOARD_BODY_MIN = 2;
const BOARD_BODY_MAX = 10000;
const BOARD_REPORT_HIDE = 3;            // open reports from different members that hide a post until reviewed
const BOARD_NEW_MEMBER = 259200;        // accounts younger than 3 days: tighter limits and no links
const BOARD_POST_INTERVAL = 20;         // seconds between two posts by one member
const BOARD_LIMITS = [                  // [new member, established member] per rolling 24 hours
    'threads' => [3, 10],
    'posts' => [20, 200],
];
const BOARD_NETWORK_DAILY = 300;        // posts per network per 24 hours, all members together
const BOARD_LINKS_MAX = 5;
const BOARD_DEFAULT_CATEGORIES = [
    ['general', 'General discussion', 'Anything about Monzero that does not fit elsewhere.'],
    ['mining', 'Mining', 'CPU mining, the pool, hardware and RandomX tuning.'],
    ['wallets-nodes', 'Wallets & nodes', 'Using the GUI and CLI wallets, running a node, sync and connection help.'],
    ['development', 'Development', 'Code, protocol, tooling and integrations.'],
    ['ideas', 'Ideas & feedback', 'Suggestions for the project, the website and the community.'],
    ['off-topic', 'Off-topic', 'Friendly conversation that is not about Monzero.'],
];
// Forums are grouped under section headings on the board index, in order of their first forum.
const BOARD_DEFAULT_SECTIONS = ['general' => 'Monzero', 'ideas' => 'Monzero', 'mining' => 'Technical', 'wallets-nodes' => 'Technical', 'development' => 'Technical', 'off-topic' => 'Community'];
// Ten levels earned by visible posts (hidden or deleted posts do not count).
const BOARD_LEVELS = [[0, 'Newcomer'], [5, 'Initiate'], [15, 'Member'], [30, 'Contributor'], [60, 'Regular'], [100, 'Established'], [200, 'Veteran'], [400, 'Elite'], [750, 'Legend'], [1200, 'Monzero Master']];
// New members introduce themselves here before posting anywhere else.
const BOARD_WELCOME_SLUG = 'welcome';
const BOARD_INTRO_MIN = 30;
// Three layers of member moderation. The owner's /admin console sits above all of them.
const BOARD_ROLES = ['member' => [0, 'Member'], 'moderator' => [1, 'Moderator'], 'global_moderator' => [2, 'Global moderator'], 'administrator' => [3, 'Administrator']];
const BOARD_MOD_ACTIONS = [   // action => lowest role rank allowed
    'hide_post' => 1, 'restore_post' => 1, 'dismiss_reports' => 1, 'lock' => 1, 'unlock' => 1,
    'pin' => 2, 'unpin' => 2, 'move' => 2, 'hide_topic' => 2, 'show_topic' => 2, 'suspend' => 2, 'unsuspend' => 2,
    'clear_signature' => 1, 'remove_avatar' => 1, 'hide_message' => 1, 'dismiss_message_report' => 1,
    'set_role' => 3,
];
const BOARD_SUSPEND_MAX_DAYS = [2 => 30, 3 => 3650];   // global moderators: up to 30 days; administrators: any, or until lifted
// Badges: automatic ones are checked after posting and on page views; manual ones are awarded in /admin.
const BOARD_BADGES = [
    'introduced' => ['👋', 'Said hello', 'Introduced yourself in New user welcome.'],
    'first_post' => ['✍️', 'First post', 'Wrote your first post.'],
    'conversation_starter' => ['💬', 'Conversation starter', 'Started 5 topics.'],
    'prolific' => ['📝', 'Prolific', 'Wrote 100 posts.'],
    'hot_topic' => ['🔥', 'Hot topic', 'Started a topic that got 10 replies from other members.'],
    'explorer' => ['🧭', 'Explorer', 'Posted in 5 different forums.'],
    'quotable' => ['🗨️', 'Quotable', 'Another member quoted one of your posts.'],
    'code_sharer' => ['💻', 'Code sharer', 'Shared code or configuration in a [code] block.'],
    'show_and_tell' => ['🖼️', 'Show and tell', 'Posted an image.'],
    'regular' => ['📅', 'Regular', 'Posted on 7 different days.'],
    'welcomer' => ['🤝', 'Welcomer', 'Replied to 5 new members\' introductions.'],
    'good_citizen' => ['🛡️', 'Good citizen', 'Reported a post that moderators then removed.'],
    'early_adopter' => ['🌱', 'Early adopter', 'One of the first 100 members of the forum.'],
    'anniversary' => ['🎂', 'One-year member', 'A member of the forum for a year.'],
    'appreciated' => ['💖', 'Appreciated', 'Received your first like.'],
    'popular_post' => ['🌟', 'Popular post', 'Wrote a post that got 10 likes.'],
    'helpful' => ['🙌', 'Helpful', 'Reached 50 reputation (likes received).'],
    'respected' => ['🏅', 'Respected', 'Reached 250 reputation (likes received).'],
    'generous' => ['🎁', 'Generous', 'Gave 25 likes to other members.'],
    'level_5' => ['⭐', 'Rising star', 'Reached level 5.'],
    'level_10' => ['🏆', 'Monzero Master', 'Reached level 10, the highest level.'],
    'bug_hunter' => ['🐞', 'Bug hunter', 'Found and reported a real bug. Awarded by the team.'],
    'contributor' => ['🛠️', 'Project contributor', 'Contributed code, docs, translations or design. Awarded by the team.'],
    'donator' => ['💰', 'XMZ donator', 'Donated to the Monzero project fund. Awarded by the team after checking the donation.'],
    'top_donator' => ['👑', 'Top XMZ donator', 'The project\'s largest donor. Only one member holds this at a time. Awarded by the team.'],
];
const BOARD_MANUAL_BADGES = ['bug_hunter', 'contributor', 'donator', 'top_donator'];
const BOARD_EXCLUSIVE_BADGES = ['top_donator'];   // awarding moves the badge from its previous holder
// Likes: one per member per post, never your own. Daily limits [new member, established], and a cap on
// likes from one network to one member, so reputation cannot be farmed with extra accounts.
const BOARD_LIKES_DAILY = [10, 100];
const BOARD_LIKES_PER_NETWORK_TO_MEMBER = 10;
// Signatures unlock at level 2 and allow only simple inline formatting.
const BOARD_SIGNATURE_LEVEL = 2;
const BOARD_SIGNATURE_MAX = 300;
const BOARD_SIGNATURE_LINES = 4;
const BOARD_SIGNATURE_LINKS = 2;
const BOARD_SIGNATURE_TAGS = ['b', 'i', 'u', 's', 'color', 'url'];
// Avatars are decoded and re-drawn as a fresh square image, which drops metadata (such as GPS
// positions from phones) and anything else hidden in the upload.
const BOARD_AVATAR_MAX_BYTES = 2097152;
const BOARD_AVATAR_MAX_SIDE = 3000;
const BOARD_AVATAR_SIZE = 160;
const BOARD_AVATAR_DAILY = 10;
// Private messages: conversations of up to 1 + BOARD_PM_MAX_RECIPIENTS members. Starting one needs
// level BOARD_PM_LEVEL unless every recipient is staff; replying is always allowed.
const BOARD_PM_MAX_RECIPIENTS = 5;
const BOARD_PM_LEVEL = 2;
const BOARD_PM_SUBJECT_MAX = 100;
const BOARD_PM_LIMITS = ['conversations' => [3, 20], 'messages' => [30, 300]];   // per 24 hours [new member, established]
// Email notices: one per conversation until the member reads it, at most this many a day per member.
const BOARD_PM_MAIL_DAILY = 10;
// Mentions (@username) and quotes notify members; one post notifies at most this many people.
const BOARD_NOTIFY_PER_POST = 10;
const BOARD_NOTIFY_MAIL_DAILY = 10;
const BOARD_NOTIFY_KEEP_DAYS = 180;
const BOARD_MENTION_PATTERN = '/(?<![A-Za-z0-9_@\/.])@([A-Za-z0-9][A-Za-z0-9_-]{2,23})\b/';
const BOARD_ONLINE_WINDOW = 900;
const BOARD_USERNAME_HOLD = 7 * 86400; // sign-up to email confirmation (links can be re-sent)

function boardDb(): PDO {
    static $db = null;
    if ($db) return $db;
    $configured = getenv('MONZERO_BOARD_DB');
    $path = is_string($configured) && $configured !== '' ? $configured : faucetPrivateDir() . '/board.sqlite';
    if (!is_dir(dirname($path))) @mkdir(dirname($path), 0700, true);
    if (is_link($path)) throw new RuntimeException('Board storage unavailable');
    $new = !file_exists($path);
    $db = new PDO('sqlite:' . $path, null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
    if ($new) @chmod($path, 0600);
    $db->exec('PRAGMA foreign_keys = ON; PRAGMA busy_timeout = 5000; PRAGMA journal_mode = WAL;');
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 1) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 1) {
            $db->exec(<<<'SQL'
                CREATE TABLE profiles (
                    member_id INTEGER PRIMARY KEY,
                    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
                    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'suspended')),
                    suspended_until INTEGER,
                    suspend_reason TEXT,
                    created_at INTEGER NOT NULL
                );
                CREATE TABLE categories (
                    id INTEGER PRIMARY KEY,
                    slug TEXT NOT NULL UNIQUE,
                    name TEXT NOT NULL,
                    description TEXT NOT NULL,
                    position INTEGER NOT NULL DEFAULT 0,
                    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'locked', 'archived')),
                    created_at INTEGER NOT NULL
                );
                CREATE TABLE threads (
                    id INTEGER PRIMARY KEY,
                    category_id INTEGER NOT NULL REFERENCES categories(id),
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    title TEXT NOT NULL,
                    pinned INTEGER NOT NULL DEFAULT 0,
                    locked INTEGER NOT NULL DEFAULT 0,
                    status TEXT NOT NULL DEFAULT 'visible' CHECK (status IN ('visible', 'hidden')),
                    created_at INTEGER NOT NULL,
                    last_post_at INTEGER NOT NULL
                );
                CREATE INDEX threads_category ON threads(category_id, status, pinned, last_post_at);
                CREATE TABLE posts (
                    id INTEGER PRIMARY KEY,
                    thread_id INTEGER NOT NULL REFERENCES threads(id),
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    body TEXT NOT NULL,
                    client_hash TEXT NOT NULL,
                    status TEXT NOT NULL DEFAULT 'visible' CHECK (status IN ('visible', 'hidden', 'deleted')),
                    created_at INTEGER NOT NULL,
                    edited_at INTEGER
                );
                CREATE INDEX posts_thread ON posts(thread_id, id);
                CREATE INDEX posts_member ON posts(member_id, created_at);
                CREATE INDEX posts_client ON posts(client_hash, created_at);
                CREATE TABLE reports (
                    id INTEGER PRIMARY KEY,
                    post_id INTEGER NOT NULL REFERENCES posts(id),
                    member_id INTEGER NOT NULL,
                    reason TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    resolved_at INTEGER,
                    outcome TEXT CHECK (outcome IN ('hidden', 'dismissed')),
                    UNIQUE (post_id, member_id)
                );
                CREATE INDEX reports_open ON reports(resolved_at, post_id);
                PRAGMA user_version = 1;
            SQL);
            $add = $db->prepare('INSERT INTO categories (slug, name, description, position, created_at) VALUES (?, ?, ?, ?, ?)');
            foreach (BOARD_DEFAULT_CATEGORIES as $i => [$slug, $name, $description]) $add->execute([$slug, $name, $description, ($i + 1) * 10, time()]);
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 2) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 2) {
            $db->exec("ALTER TABLE categories ADD COLUMN section TEXT NOT NULL DEFAULT 'Community'; ALTER TABLE threads ADD COLUMN views INTEGER NOT NULL DEFAULT 0;");
            $set = $db->prepare('UPDATE categories SET section = ? WHERE slug = ?');
            foreach (BOARD_DEFAULT_SECTIONS as $slug => $section) $set->execute([$section, $slug]);
            $db->exec('PRAGMA user_version = 2');
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 3) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 3) {
            $db->exec('ALTER TABLE posts ADD COLUMN bbcode INTEGER NOT NULL DEFAULT 1; ALTER TABLE posts ADD COLUMN smilies INTEGER NOT NULL DEFAULT 1; ALTER TABLE posts ADD COLUMN autolink INTEGER NOT NULL DEFAULT 1; PRAGMA user_version = 3;');
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 4) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 4) {
            $db->exec(<<<'SQL'
                ALTER TABLE profiles ADD COLUMN role TEXT NOT NULL DEFAULT 'member';
                ALTER TABLE profiles ADD COLUMN introduced_at INTEGER;
                UPDATE profiles SET introduced_at = created_at;
                CREATE TABLE badges (
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    badge TEXT NOT NULL,
                    awarded_at INTEGER NOT NULL,
                    awarded_by TEXT,
                    seen INTEGER NOT NULL DEFAULT 0,
                    PRIMARY KEY (member_id, badge)
                );
                CREATE TABLE mod_log (
                    id INTEGER PRIMARY KEY,
                    at INTEGER NOT NULL,
                    actor TEXT NOT NULL,
                    action TEXT NOT NULL,
                    target TEXT NOT NULL,
                    detail TEXT NOT NULL DEFAULT ''
                );
                CREATE TRIGGER mod_log_no_update BEFORE UPDATE ON mod_log BEGIN SELECT RAISE(ABORT, 'the moderation log is append-only'); END;
                CREATE TRIGGER mod_log_no_delete BEFORE DELETE ON mod_log BEGIN SELECT RAISE(ABORT, 'the moderation log is append-only'); END;
                PRAGMA user_version = 4;
            SQL);
            $db->prepare("INSERT OR IGNORE INTO categories (slug, name, description, position, section, created_at) VALUES (?, 'New user welcome', ?, 5, 'Monzero', ?)")
                ->execute([BOARD_WELCOME_SLUG, 'New here? Introduce yourself before posting anywhere else, and say hello to other new members.', time()]);
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 5) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 5) {
            $db->exec(<<<'SQL'
                CREATE TABLE likes (
                    post_id INTEGER NOT NULL REFERENCES posts(id),
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    author_id INTEGER NOT NULL,
                    client_hash TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    PRIMARY KEY (post_id, member_id)
                );
                CREATE INDEX likes_author ON likes(author_id);
                CREATE INDEX likes_member ON likes(member_id, created_at);
                CREATE INDEX likes_client ON likes(client_hash, author_id, created_at);
                PRAGMA user_version = 5;
            SQL);
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 6) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 6) {
            $db->exec("ALTER TABLE profiles ADD COLUMN signature TEXT NOT NULL DEFAULT ''; ALTER TABLE profiles ADD COLUMN avatar TEXT; PRAGMA user_version = 6;");
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 7) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 7) {
            $db->exec(<<<'SQL'
                ALTER TABLE profiles ADD COLUMN allow_pms INTEGER NOT NULL DEFAULT 1;
                CREATE TABLE conversations (
                    id INTEGER PRIMARY KEY,
                    subject TEXT NOT NULL,
                    created_by INTEGER NOT NULL,
                    created_at INTEGER NOT NULL,
                    last_message_at INTEGER NOT NULL
                );
                CREATE TABLE conversation_members (
                    conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    last_read_id INTEGER NOT NULL DEFAULT 0,
                    left_at INTEGER,
                    PRIMARY KEY (conversation_id, member_id)
                );
                CREATE INDEX conversation_members_member ON conversation_members(member_id, left_at);
                CREATE TABLE messages (
                    id INTEGER PRIMARY KEY,
                    conversation_id INTEGER NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    body TEXT NOT NULL,
                    bbcode INTEGER NOT NULL DEFAULT 1,
                    smilies INTEGER NOT NULL DEFAULT 1,
                    autolink INTEGER NOT NULL DEFAULT 1,
                    status TEXT NOT NULL DEFAULT 'visible' CHECK (status IN ('visible', 'hidden')),
                    client_hash TEXT NOT NULL,
                    created_at INTEGER NOT NULL
                );
                CREATE INDEX messages_conversation ON messages(conversation_id, id);
                CREATE INDEX messages_member ON messages(member_id, created_at);
                CREATE TABLE message_reports (
                    id INTEGER PRIMARY KEY,
                    message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
                    member_id INTEGER NOT NULL,
                    reason TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    resolved_at INTEGER,
                    outcome TEXT,
                    UNIQUE (message_id, member_id)
                );
                CREATE TABLE blocks (
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    blocked_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    created_at INTEGER NOT NULL,
                    PRIMARY KEY (member_id, blocked_id)
                );
                PRAGMA user_version = 7;
            SQL);
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 8) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 8) {
            $db->exec('ALTER TABLE profiles ADD COLUMN notify_pm INTEGER NOT NULL DEFAULT 1; ALTER TABLE conversation_members ADD COLUMN notified_id INTEGER NOT NULL DEFAULT 0; PRAGMA user_version = 8;');
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 9) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 9) {
            $db->exec(<<<'SQL'
                ALTER TABLE profiles ADD COLUMN notify_mentions_email INTEGER NOT NULL DEFAULT 0;
                CREATE TABLE notifications (
                    id INTEGER PRIMARY KEY,
                    member_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    type TEXT NOT NULL CHECK (type IN ('mention', 'quote')),
                    actor_id INTEGER NOT NULL REFERENCES profiles(member_id),
                    post_id INTEGER NOT NULL REFERENCES posts(id),
                    created_at INTEGER NOT NULL,
                    read_at INTEGER,
                    UNIQUE (member_id, type, post_id)
                );
                CREATE INDEX notifications_member ON notifications(member_id, read_at, id);
                PRAGMA user_version = 9;
            SQL);
        }
        $db->exec('COMMIT');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 10) {
        $db->exec('BEGIN IMMEDIATE');
        if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 10) {
            // Usernames chosen at sign-up, held until the email is confirmed.
            $db->exec('CREATE TABLE username_holds (member_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE COLLATE NOCASE, expires_at INTEGER NOT NULL); PRAGMA user_version = 10;');
        }
        $db->exec('COMMIT');
    }
    return $db;
}

function boardOne(string $sql, array $args = []): ?array {
    $stmt = boardDb()->prepare($sql);
    $stmt->execute($args);
    return $stmt->fetch() ?: null;
}

function boardAll(string $sql, array $args = []): array {
    $stmt = boardDb()->prepare($sql);
    $stmt->execute($args);
    return $stmt->fetchAll();
}

// --- Members ---

function boardProfile(int $memberId): ?array { return boardOne('SELECT * FROM profiles WHERE member_id = ?', [$memberId]); }
function boardProfileByName(string $username): ?array { return boardOne('SELECT * FROM profiles WHERE username = ?', [$username]); }

function boardSuspended(array $profile, int $now): bool {
    return $profile['status'] === 'suspended' && ($profile['suspended_until'] === null || (int)$profile['suspended_until'] > $now);
}

// Why this faucet member cannot take part yet (sign-in level, email), or null.
function boardMemberBlock(?array $member): ?string {
    if (!$member) return 'Sign in with your Monzero account to take part.';
    if ($member['level'] !== 'full' || !$member['totp_secret'] || !$member['email_verified_at']) return 'Finish signing in (password and authenticator code) to take part.';
    return null;
}

function boardUsernameProblem(string $username): ?string {
    if (!preg_match('/\A[A-Za-z0-9][A-Za-z0-9_-]{2,23}\z/', $username)) return 'Use 3-24 letters, digits, - or _, starting with a letter or digit.';
    if (preg_match('/admin|moderator|monzero|official|support|staff|founder|nightglass/i', $username)) return 'That name could be mistaken for the project or its staff. Choose another.';
    return null;
}

// Usernames are permanent: they identify who wrote what.
function boardCreateProfile(array $member, string $username, int $now): ?string {
    $username = trim($username);
    if ($problem = boardMemberBlock($member)) return $problem;
    if (boardProfile((int)$member['id'])) return 'You already have a username.';
    if ($problem = boardUsernameProblem($username)) return $problem;
    if (boardUsernameTaken($username, (int)$member['id'], $now)) return 'That username is taken.';
    return boardInsertProfile((int)$member['id'], $username, $now);
}

function boardInsertProfile(int $memberId, string $username, int $now): ?string {
    try {
        boardDb()->prepare('INSERT INTO profiles (member_id, username, created_at) VALUES (?, ?, ?)')->execute([$memberId, $username, $now]);
    } catch (PDOException $e) {
        return 'That username is taken.';
    }
    boardDb()->prepare('DELETE FROM username_holds WHERE member_id = ?')->execute([$memberId]);
    faucetEvent($memberId, 'board.profile', ['username' => $username]);
    return null;
}

// Taken by a profile, or held for another account that has not confirmed its email yet.
function boardUsernameTaken(string $username, int $memberId, int $now): bool {
    return boardProfileByName($username) !== null
        || boardOne('SELECT 1 FROM username_holds WHERE username = ? AND member_id != ? AND expires_at > ?', [$username, $memberId, $now]) !== null;
}

// Sign-up picks the username; the profile is created when the email is confirmed.
function boardHoldUsername(int $memberId, string $username, int $now): ?string {
    $username = trim($username);
    if ($problem = boardUsernameProblem($username)) return $problem;
    if (boardUsernameTaken($username, $memberId, $now)) return 'That username is taken.';
    $db = boardDb();
    $db->prepare('DELETE FROM username_holds WHERE expires_at <= ?')->execute([$now]);
    try {
        $db->prepare('INSERT OR REPLACE INTO username_holds (member_id, username, expires_at) VALUES (?, ?, ?)')->execute([$memberId, $username, $now + BOARD_USERNAME_HOLD]);
    } catch (PDOException $e) {
        return 'That username is taken.';
    }
    return null;
}

// Called once the email is confirmed. If the name was lost meanwhile, the member picks another later.
function boardClaimHeldUsername(int $memberId, int $now): void {
    $hold = boardOne('SELECT username FROM username_holds WHERE member_id = ? AND expires_at > ?', [$memberId, $now]);
    if (!$hold || boardProfile($memberId) || boardProfileByName($hold['username'])) return;
    boardInsertProfile($memberId, $hold['username'], $now);
}

// --- Categories and threads ---

function boardCategories(bool $includeArchived = false): array {
    return boardAll('SELECT c.*, (SELECT COUNT(*) FROM threads t WHERE t.category_id = c.id AND t.status = \'visible\') AS threads,'
        . ' (SELECT MAX(t.last_post_at) FROM threads t WHERE t.category_id = c.id AND t.status = \'visible\') AS last_post_at,'
        . ' (SELECT COUNT(*) FROM posts x JOIN threads t ON t.id = x.thread_id WHERE t.category_id = c.id AND t.status = \'visible\' AND x.status != \'deleted\') AS posts,'
        . ' (SELECT MAX(x.id) FROM posts x JOIN threads t ON t.id = x.thread_id WHERE t.category_id = c.id AND t.status = \'visible\' AND x.status != \'deleted\') AS last_post_id FROM categories c'
        . ($includeArchived ? '' : " WHERE c.status != 'archived'") . ' ORDER BY c.position, c.id');
}

function boardCategory(string $slug): ?array { return boardOne('SELECT * FROM categories WHERE slug = ?', [$slug]); }

function boardThreads(int $categoryId, int $page): array {
    return boardAll("SELECT t.*, p.username, MAX(0, (SELECT COUNT(*) FROM posts x WHERE x.thread_id = t.id AND x.status != 'deleted') - 1) AS replies,"
        . " (SELECT COUNT(*) FROM posts x WHERE x.thread_id = t.id) AS total_posts, (SELECT MAX(x.id) FROM posts x WHERE x.thread_id = t.id AND x.status != 'deleted') AS last_post_id"
        . " FROM threads t JOIN profiles p ON p.member_id = t.member_id WHERE t.category_id = ? AND t.status = 'visible'"
        . ' ORDER BY t.pinned DESC, t.last_post_at DESC, t.id DESC LIMIT ? OFFSET ?', [$categoryId, BOARD_THREADS_PER_PAGE, ($page - 1) * BOARD_THREADS_PER_PAGE]);
}

function boardThreadCount(int $categoryId): int {
    return (int)boardOne("SELECT COUNT(*) AS n FROM threads WHERE category_id = ? AND status = 'visible'", [$categoryId])['n'];
}

function boardThread(int $id): ?array {
    return boardOne('SELECT t.*, c.slug AS category_slug, c.name AS category_name, c.status AS category_status, p.username FROM threads t'
        . ' JOIN categories c ON c.id = t.category_id JOIN profiles p ON p.member_id = t.member_id WHERE t.id = ?', [$id]);
}

function boardPosts(int $threadId, int $page): array {
    return boardAll("SELECT x.*, p.username, p.role, p.avatar, p.signature, p.created_at AS joined_at, p.status AS profile_status, p.suspended_until,"
        . " (SELECT COUNT(*) FROM posts y WHERE y.member_id = x.member_id AND y.status = 'visible') AS author_posts"
        . ' FROM posts x JOIN profiles p ON p.member_id = x.member_id WHERE x.thread_id = ? ORDER BY x.id LIMIT ? OFFSET ?',
        [$threadId, BOARD_POSTS_PER_PAGE, ($page - 1) * BOARD_POSTS_PER_PAGE]);
}

function boardPostCount(int $threadId): int { return (int)boardOne('SELECT COUNT(*) AS n FROM posts WHERE thread_id = ?', [$threadId])['n']; }

function boardPost(int $id): ?array {
    return boardOne('SELECT x.*, p.username, t.title, t.locked, t.status AS thread_status, (SELECT MIN(id) FROM posts f WHERE f.thread_id = x.thread_id) AS first_id'
        . ' FROM posts x JOIN profiles p ON p.member_id = x.member_id JOIN threads t ON t.id = x.thread_id WHERE x.id = ?', [$id]);
}

// Page of the thread that holds this post.
function boardPostPage(array $post): int {
    $before = (int)boardOne('SELECT COUNT(*) AS n FROM posts WHERE thread_id = ? AND id < ?', [$post['thread_id'], $post['id']])['n'];
    return intdiv($before, BOARD_POSTS_PER_PAGE) + 1;
}

function boardMemberPosts(int $memberId, int $limit = 30): array {
    return boardAll("SELECT x.id, x.thread_id, x.created_at, t.title FROM posts x JOIN threads t ON t.id = x.thread_id JOIN categories c ON c.id = t.category_id"
        . " WHERE x.member_id = ? AND x.status = 'visible' AND t.status = 'visible' AND c.status != 'archived' ORDER BY x.id DESC LIMIT ?", [$memberId, $limit]);
}

function boardRecentThreads(int $limit = 10): array {
    return boardAll("SELECT t.*, c.name AS category_name, c.slug AS category_slug, p.username, MAX(0, (SELECT COUNT(*) FROM posts x WHERE x.thread_id = t.id AND x.status != 'deleted') - 1) AS replies,"
        . " (SELECT COUNT(*) FROM posts x WHERE x.thread_id = t.id) AS total_posts, (SELECT MAX(x.id) FROM posts x WHERE x.thread_id = t.id AND x.status != 'deleted') AS last_post_id"
        . " FROM threads t JOIN categories c ON c.id = t.category_id JOIN profiles p ON p.member_id = t.member_id"
        . " WHERE t.status = 'visible' AND c.status != 'archived' ORDER BY t.last_post_at DESC, t.id DESC LIMIT ?", [$limit]);
}

// Author, time and thread of one post, for "Last post" columns.
function boardPostBrief(?int $postId): ?array {
    if (!$postId) return null;
    return boardOne('SELECT x.id, x.thread_id, x.created_at, p.username, p.avatar, t.title FROM posts x JOIN profiles p ON p.member_id = x.member_id JOIN threads t ON t.id = x.thread_id WHERE x.id = ?', [$postId]);
}

// [level 1-10, name, posts needed for this level, posts needed for the next level or null].
function boardLevel(int $posts): array {
    $level = 1;
    foreach (BOARD_LEVELS as $i => [$min]) if ($posts >= $min) $level = $i + 1;
    return [$level, BOARD_LEVELS[$level - 1][1], BOARD_LEVELS[$level - 1][0], BOARD_LEVELS[$level][0] ?? null];
}

function boardMemberPostCount(int $memberId): int {
    return (int)boardOne("SELECT COUNT(*) AS n FROM posts WHERE member_id = ? AND status = 'visible'", [$memberId])['n'];
}

function boardStats(): array {
    $row = boardOne("SELECT (SELECT COUNT(*) FROM posts x JOIN threads t ON t.id = x.thread_id JOIN categories c ON c.id = t.category_id WHERE x.status != 'deleted' AND t.status = 'visible' AND c.status != 'archived') AS posts,"
        . " (SELECT COUNT(*) FROM threads t JOIN categories c ON c.id = t.category_id WHERE t.status = 'visible' AND c.status != 'archived') AS topics, (SELECT COUNT(*) FROM profiles) AS members");
    $row['newest'] = boardOne('SELECT username FROM profiles ORDER BY created_at DESC, member_id DESC LIMIT 1')['username'] ?? null;
    return $row;
}

// Signed-in members (with a username) seen in the last 15 minutes. Only the count is shown.
function boardOnlineCount(int $now): int {
    $stmt = faucetDb()->prepare('SELECT DISTINCT member_id FROM sessions WHERE seen_at > ?');
    $stmt->execute([$now - BOARD_ONLINE_WINDOW]);
    $ids = array_map('intval', $stmt->fetchAll(PDO::FETCH_COLUMN));
    if (!$ids) return 0;
    return (int)boardOne('SELECT COUNT(*) AS n FROM profiles WHERE member_id IN (' . implode(',', $ids) . ')')['n'];
}

function boardAddView(int $threadId): void { boardDb()->prepare('UPDATE threads SET views = views + 1 WHERE id = ?')->execute([$threadId]); }

// Visible posts whose text or topic title contains every word of the query.
function boardSearch(string $query, int $limit = 50): array {
    $words = array_slice(array_values(array_filter(preg_split('/\s+/u', trim($query)) ?: [], fn($w) => mb_strlen($w) >= 2)), 0, 6);
    if (!$words) return [];
    $where = []; $args = [];
    foreach ($words as $w) {
        $like = '%' . strtr($w, ['\\' => '\\\\', '%' => '\\%', '_' => '\\_']) . '%';
        $where[] = "(x.body LIKE ? ESCAPE '\\' OR t.title LIKE ? ESCAPE '\\')";
        array_push($args, $like, $like);
    }
    $args[] = $limit;
    return boardAll("SELECT x.id, x.thread_id, x.body, x.created_at, p.username, t.title, c.name AS category_name FROM posts x JOIN threads t ON t.id = x.thread_id JOIN categories c ON c.id = t.category_id"
        . " JOIN profiles p ON p.member_id = x.member_id WHERE x.status = 'visible' AND t.status = 'visible' AND c.status != 'archived' AND " . implode(' AND ', $where) . ' ORDER BY x.id DESC LIMIT ?', $args);
}

// Reply text that quotes a post.
// Code fences are dropped (quotes are plain text) and runs of blank lines collapse to one.
function boardQuote(array $post): string {
    $lines = array_values(array_filter(explode("\n", $post['body']), fn($l) => !preg_match('/^```/', $l)));
    $text = preg_replace("/\n{3,}/", "\n\n", implode("\n", $lines));
    return '> ' . $post['username'] . " wrote:\n" . implode("\n", array_map(fn($l) => rtrim('> ' . $l), explode("\n", trim((string)$text)))) . "\n\n";
}

// --- Writing ---

// Per-post formatting switches (phpBB-style "Disable BBCode / smilies / URL parsing").
function boardOptions(array $source): array {
    return ['bbcode' => empty($source['disable_bbcode']), 'smilies' => empty($source['disable_smilies']), 'autolink' => empty($source['disable_autolink'])];
}

function boardPostOptions(array $post): array {
    return ['bbcode' => (bool)($post['bbcode'] ?? 1), 'smilies' => (bool)($post['smilies'] ?? 1), 'autolink' => (bool)($post['autolink'] ?? 1)];
}

function boardTextProblem(string $text, int $min, int $max, string $what): ?string {
    $length = mb_strlen($text);
    if ($length < $min || $length > $max) return "The $what must be $min-" . number_format($max) . ' characters.';
    if (!mb_check_encoding($text, 'UTF-8')) return "Remove control characters from the $what.";
    $checked = preg_replace('/[\x{200C}\x{200D}\x{E0020}-\x{E007F}]/u', '', str_replace(["\n", "\t"], '', $text));   // emoji joiners and flag tags
    if (preg_match('/[\p{Cc}\p{Cf}]/u', (string)$checked)) return "Remove control characters from the $what.";
    return null;
}

function boardClean(string $text): string { return trim(str_replace(["\r\n", "\r"], "\n", $text)); }

function boardLinkCount(string $body): int { return preg_match_all('#https?://#i', $body); }

// Why this member cannot post right now, or null. $forumSlug is the forum being posted in.
function boardPostBlock(?array $member, ?array $profile, string $kind, int $now, ?string $forumSlug = null): ?string {
    if ($problem = boardMemberBlock($member)) return $problem;
    if (!$profile) return 'Choose a username before posting.';
    if (!$profile['introduced_at'] && $forumSlug !== null && $forumSlug !== BOARD_WELCOME_SLUG) return 'Please introduce yourself in New user welcome first. After that you can post in every forum.';
    if (boardSuspended($profile, $now)) return 'Your posting access is suspended' . ($profile['suspended_until'] ? ' until ' . gmdate('Y-m-d H:i', (int)$profile['suspended_until']) . ' UTC' : '') . '.';
    $id = (int)$member['id'];
    $last = boardOne('SELECT MAX(created_at) AS at FROM posts WHERE member_id = ?', [$id])['at'];
    if ($last && (int)$last > $now - BOARD_POST_INTERVAL) return 'You are posting too quickly. Wait a few seconds and try again.';
    $tier = (int)$member['created_at'] > $now - BOARD_NEW_MEMBER ? 0 : 1;
    if ($kind === 'thread') {
        $n = (int)boardOne('SELECT COUNT(*) AS n FROM threads WHERE member_id = ? AND created_at > ?', [$id, $now - 86400])['n'];
        if ($n >= BOARD_LIMITS['threads'][$tier]) return 'You have started the maximum number of topics for today. Try again tomorrow.';
    }
    $n = (int)boardOne('SELECT COUNT(*) AS n FROM posts WHERE member_id = ? AND created_at > ?', [$id, $now - 86400])['n'];
    if ($n >= BOARD_LIMITS['posts'][$tier]) return 'You have reached today\'s posting limit. Try again tomorrow.';
    $n = (int)boardOne('SELECT COUNT(*) AS n FROM posts WHERE client_hash = ? AND created_at > ?', [faucetClientHash(), $now - 86400])['n'];
    if ($n >= BOARD_NETWORK_DAILY) return 'Too many posts from this network today. Try again tomorrow.';
    return null;
}

function boardBodyProblem(array $member, string $body, int $now): ?string {
    if ($problem = boardTextProblem($body, BOARD_BODY_MIN, BOARD_BODY_MAX, 'post')) return $problem;
    $links = boardLinkCount($body);
    if ($links > 0 && (int)$member['created_at'] > $now - BOARD_NEW_MEMBER) return 'New accounts cannot post links for their first 3 days. Remove the link and try again.';
    if ($links > BOARD_LINKS_MAX) return 'Use at most ' . BOARD_LINKS_MAX . ' links in one post.';
    return null;
}

// Returns [thread id, null] or [null, error].
function boardCreateThread(array $member, array $category, string $title, string $body, int $now, array $opts = ['bbcode' => true, 'smilies' => true, 'autolink' => true]): array {
    $title = trim(preg_replace('/\s+/u', ' ', $title) ?? ''); $body = boardClean($body);
    if ($category['status'] !== 'open') return [null, 'New topics cannot be started in this forum.'];
    if ($problem = boardTextProblem($title, BOARD_TITLE_MIN, BOARD_TITLE_MAX, 'title') ?? boardBodyProblem($member, $body, $now)) return [null, $problem];
    $db = boardDb();
    $db->exec('BEGIN IMMEDIATE');
    $profile = boardProfile((int)$member['id']);
    if ($problem = boardPostBlock($member, $profile, 'thread', $now, $category['slug'])) { $db->exec('ROLLBACK'); return [null, $problem]; }
    $intro = $category['slug'] === BOARD_WELCOME_SLUG && $profile && !$profile['introduced_at'];
    if ($intro && mb_strlen(trim(boardPlain($body))) < BOARD_INTRO_MIN) { $db->exec('ROLLBACK'); return [null, 'Tell us a little more about yourself (at least ' . BOARD_INTRO_MIN . ' characters).']; }
    $db->prepare('INSERT INTO threads (category_id, member_id, title, created_at, last_post_at) VALUES (?, ?, ?, ?, ?)')->execute([$category['id'], $member['id'], $title, $now, $now]);
    $thread = (int)$db->lastInsertId();
    $db->prepare('INSERT INTO posts (thread_id, member_id, body, client_hash, created_at, bbcode, smilies, autolink) VALUES (?, ?, ?, ?, ?, ?, ?, ?)')
        ->execute([$thread, $member['id'], $body, faucetClientHash(), $now, (int)$opts['bbcode'], (int)$opts['smilies'], (int)$opts['autolink']]);
    if ($intro) $db->prepare('UPDATE profiles SET introduced_at = ? WHERE member_id = ?')->execute([$now, $member['id']]);
    $db->exec('COMMIT');
    faucetEvent((int)$member['id'], 'board.thread', ['thread' => $thread]);
    boardAfterPost((int)$member['id'], $body, $opts, $now);
    boardNotifyPost((int)$db->query('SELECT MIN(id) FROM posts WHERE thread_id = ' . $thread)->fetchColumn(), (int)$member['id'], $body, $opts, $now);
    return [$thread, null];
}

// Returns [post id, null] or [null, error].
function boardReply(array $member, array $thread, string $body, int $now, array $opts = ['bbcode' => true, 'smilies' => true, 'autolink' => true]): array {
    $body = boardClean($body);
    if ($thread['status'] !== 'visible' || $thread['locked'] || $thread['category_status'] === 'archived') return [null, 'This topic is locked. No new replies can be posted.'];
    if ($problem = boardBodyProblem($member, $body, $now)) return [null, $problem];
    $db = boardDb();
    $db->exec('BEGIN IMMEDIATE');
    if ($problem = boardPostBlock($member, boardProfile((int)$member['id']), 'reply', $now, $thread['category_slug'] ?? null)) { $db->exec('ROLLBACK'); return [null, $problem]; }
    $db->prepare('INSERT INTO posts (thread_id, member_id, body, client_hash, created_at, bbcode, smilies, autolink) VALUES (?, ?, ?, ?, ?, ?, ?, ?)')
        ->execute([$thread['id'], $member['id'], $body, faucetClientHash(), $now, (int)$opts['bbcode'], (int)$opts['smilies'], (int)$opts['autolink']]);
    $post = (int)$db->lastInsertId();
    $db->prepare('UPDATE threads SET last_post_at = ? WHERE id = ?')->execute([$now, $thread['id']]);
    $db->exec('COMMIT');
    faucetEvent((int)$member['id'], 'board.reply', ['post' => $post]);
    boardAfterPost((int)$member['id'], $body, $opts, $now);
    boardNotifyPost($post, (int)$member['id'], $body, $opts, $now);
    boardAwardBadges((int)$thread['member_id'], $now);   // the topic starter may now have a hot topic
    return [$post, null];
}

// Authors may edit or delete their own visible posts while the thread is open.
function boardOwnPostBlock(?array $member, ?array $post, int $now): ?string {
    if ($problem = boardMemberBlock($member)) return $problem;
    if (!$post || (int)$post['member_id'] !== (int)$member['id']) return 'You can only change your own posts.';
    if ($post['status'] !== 'visible') return 'This post can no longer be changed.';
    if ($post['locked'] || $post['thread_status'] !== 'visible') return 'This topic is locked.';
    $profile = boardProfile((int)$member['id']);
    if (!$profile || boardSuspended($profile, $now)) return 'Your posting access is suspended.';
    return null;
}

function boardEdit(array $member, array $post, string $body, int $now, ?array $opts = null): ?string {
    $opts ??= boardPostOptions($post);
    $body = boardClean($body);
    if ($problem = boardOwnPostBlock($member, $post, $now) ?? boardBodyProblem($member, $body, $now)) return $problem;
    boardDb()->prepare("UPDATE posts SET body = ?, edited_at = ?, bbcode = ?, smilies = ?, autolink = ? WHERE id = ? AND status = 'visible'")
        ->execute([$body, $now, (int)$opts['bbcode'], (int)$opts['smilies'], (int)$opts['autolink'], $post['id']]);
    faucetEvent((int)$member['id'], 'board.edit', ['post' => (int)$post['id']]);
    boardNotifyPost((int)$post['id'], (int)$member['id'], $body, $opts, $now);   // only newly mentioned or quoted members
    return null;
}

function boardDelete(array $member, array $post, int $now): ?string {
    if ($problem = boardOwnPostBlock($member, $post, $now)) return $problem;
    boardDb()->prepare("UPDATE posts SET status = 'deleted', body = '', edited_at = ? WHERE id = ?")->execute([$now, $post['id']]);
    faucetEvent((int)$member['id'], 'board.delete', ['post' => (int)$post['id']]);
    return null;
}

function boardReport(array $member, array $post, string $reason, int $now): ?string {
    $reason = trim(preg_replace('/\s+/u', ' ', $reason) ?? '');
    if ($problem = boardMemberBlock($member)) return $problem;
    if ($post['status'] !== 'visible') return 'This post is already removed.';
    if ((int)$post['member_id'] === (int)$member['id']) return 'You cannot report your own post.';
    if ($problem = boardTextProblem($reason, 3, 500, 'reason')) return $problem;
    $db = boardDb();
    $db->exec('BEGIN IMMEDIATE');
    if (boardOne('SELECT 1 AS x FROM reports WHERE post_id = ? AND member_id = ?', [$post['id'], $member['id']])) { $db->exec('ROLLBACK'); return 'You have already reported this post.'; }
    $db->prepare('INSERT INTO reports (post_id, member_id, reason, created_at) VALUES (?, ?, ?, ?)')->execute([$post['id'], $member['id'], $reason, $now]);
    // Several members agreeing hides the post until a moderator looks at it.
    $agree = (int)boardOne('SELECT COUNT(*) AS n FROM reports WHERE post_id = ? AND resolved_at IS NULL', [$post['id']])['n'];
    if ($agree >= BOARD_REPORT_HIDE) $db->prepare("UPDATE posts SET status = 'hidden' WHERE id = ? AND status = 'visible'")->execute([$post['id']]);
    $db->exec('COMMIT');
    faucetEvent((int)$member['id'], 'board.report', ['post' => (int)$post['id']]);
    return null;
}

// --- Moderation (called from /admin/) ---

function boardOpenReports(): array {
    return boardAll('SELECT x.id AS post_id, x.body, x.status, x.thread_id, x.created_at, p.username, t.title, COUNT(r.id) AS reports, GROUP_CONCAT(r.reason, \' | \') AS reasons'
        . ' FROM reports r JOIN posts x ON x.id = r.post_id JOIN profiles p ON p.member_id = x.member_id JOIN threads t ON t.id = x.thread_id'
        . ' WHERE r.resolved_at IS NULL GROUP BY x.id ORDER BY MIN(r.created_at)');
}

function boardRecentPosts(int $limit = 50): array {
    return boardAll('SELECT x.id AS post_id, x.body, x.status, x.thread_id, x.created_at, p.username, t.title, t.locked, t.pinned, t.status AS thread_status'
        . ' FROM posts x JOIN profiles p ON p.member_id = x.member_id JOIN threads t ON t.id = x.thread_id ORDER BY x.id DESC LIMIT ?', [$limit]);
}

function boardSetPostStatus(int $postId, string $status, int $now): bool {
    $from = $status === 'hidden' ? 'visible' : 'hidden';
    $stmt = boardDb()->prepare('UPDATE posts SET status = ? WHERE id = ? AND status = ?');
    $stmt->execute([$status, $postId, $from]);
    $reporters = boardAll('SELECT member_id FROM reports WHERE post_id = ? AND resolved_at IS NULL', [$postId]);
    boardDb()->prepare('UPDATE reports SET resolved_at = ?, outcome = ? WHERE post_id = ? AND resolved_at IS NULL')->execute([$now, $status === 'hidden' ? 'hidden' : 'dismissed', $postId]);
    if ($status === 'hidden') foreach ($reporters as $r) boardAwardBadges((int)$r['member_id'], $now);
    return $stmt->rowCount() === 1;
}

function boardDismissReports(int $postId, int $now): bool {
    $stmt = boardDb()->prepare("UPDATE reports SET resolved_at = ?, outcome = 'dismissed' WHERE post_id = ? AND resolved_at IS NULL");
    $stmt->execute([$now, $postId]);
    boardDb()->prepare("UPDATE posts SET status = 'visible' WHERE id = ? AND status = 'hidden'")->execute([$postId]);
    return $stmt->rowCount() > 0;
}

function boardSetThreadFlag(int $threadId, string $flag, int $value): bool {
    $column = ['locked' => 'locked', 'pinned' => 'pinned'][$flag] ?? null;
    if ($flag === 'hidden') {
        $stmt = boardDb()->prepare('UPDATE threads SET status = ? WHERE id = ?');
        $stmt->execute([$value ? 'hidden' : 'visible', $threadId]);
        return $stmt->rowCount() === 1;
    }
    if (!$column) return false;
    $stmt = boardDb()->prepare("UPDATE threads SET $column = ? WHERE id = ?");
    $stmt->execute([$value ? 1 : 0, $threadId]);
    return $stmt->rowCount() === 1;
}

function boardMoveThread(int $threadId, string $categorySlug): bool {
    $category = boardCategory($categorySlug);
    if (!$category) return false;
    $stmt = boardDb()->prepare('UPDATE threads SET category_id = ? WHERE id = ?');
    $stmt->execute([$category['id'], $threadId]);
    return $stmt->rowCount() === 1;
}

// $days 0 = until lifted. Returns an error or null.
function boardSuspend(string $username, int $days, string $reason, int $now): ?string {
    $profile = boardProfileByName($username);
    if (!$profile) return 'No member has that username.';
    if ($days < 0 || $days > 3650) return 'Suspend for 0 (until lifted) to 3650 days.';
    $reason = trim($reason);
    if ($problem = boardTextProblem($reason, 3, 500, 'reason')) return $problem;
    boardDb()->prepare("UPDATE profiles SET status = 'suspended', suspended_until = ?, suspend_reason = ? WHERE member_id = ?")
        ->execute([$days ? $now + $days * 86400 : null, $reason, $profile['member_id']]);
    return null;
}

function boardUnsuspend(string $username): bool {
    $stmt = boardDb()->prepare("UPDATE profiles SET status = 'active', suspended_until = NULL, suspend_reason = NULL WHERE username = ? AND status = 'suspended'");
    $stmt->execute([$username]);
    return $stmt->rowCount() === 1;
}

function boardSuspendedMembers(int $now): array {
    return array_values(array_filter(boardAll("SELECT * FROM profiles WHERE status = 'suspended' ORDER BY username"), fn($p) => boardSuspended($p, $now)));
}

// Create (empty $slug) or update a category. Returns an error or null.
function boardSaveCategory(string $slug, string $name, string $description, int $position, string $status, int $now, string $section = 'Community'): ?string {
    $name = trim($name); $description = trim($description); $section = trim($section);
    if ($problem = boardTextProblem($name, 2, 60, 'name') ?? boardTextProblem($description, 5, 300, 'description') ?? boardTextProblem($section, 2, 40, 'section')) return $problem;
    if (!in_array($status, ['open', 'locked', 'archived'], true)) return 'Choose open, locked or archived.';
    if ($position < 0 || $position > 10000) return 'Position must be 0-10000.';
    if ($slug === '') {
        $slug = substr(trim((string)preg_replace('/[^a-z0-9]+/', '-', strtolower($name)), '-'), 0, 40) ?: 'category';
        if (boardCategory($slug)) return 'A forum with that name already exists.';
        boardDb()->prepare('INSERT INTO categories (slug, name, description, position, status, section, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)')->execute([$slug, $name, $description, $position, $status, $section, $now]);
        return null;
    }
    $stmt = boardDb()->prepare('UPDATE categories SET name = ?, description = ?, position = ?, status = ?, section = ? WHERE slug = ?');
    $stmt->execute([$name, $description, $position, $status, $section, $slug]);
    return $stmt->rowCount() === 1 ? null : 'That forum does not exist.';
}

// --- Badges ---

function boardGrantBadge(int $memberId, string $badge, int $now, ?string $by = null): bool {
    if (!isset(BOARD_BADGES[$badge]) || !boardProfile($memberId)) return false;
    $db = boardDb();
    $exclusive = in_array($badge, BOARD_EXCLUSIVE_BADGES, true);
    if ($exclusive) $db->exec('BEGIN IMMEDIATE');
    $stmt = $db->prepare('INSERT OR IGNORE INTO badges (member_id, badge, awarded_at, awarded_by) VALUES (?, ?, ?, ?)');
    $stmt->execute([$memberId, $badge, $now, $by]);
    $granted = $stmt->rowCount() === 1;
    if ($exclusive) {
        if ($granted) $db->prepare('DELETE FROM badges WHERE badge = ? AND member_id != ?')->execute([$badge, $memberId]);
        $db->exec('COMMIT');
    }
    return $granted;
}

function boardBadgeHolder(string $badge): ?array {
    return boardOne('SELECT p.* FROM badges b JOIN profiles p ON p.member_id = b.member_id WHERE b.badge = ? ORDER BY b.awarded_at DESC LIMIT 1', [$badge]);
}

function boardRevokeBadge(int $memberId, string $badge): bool {
    $stmt = boardDb()->prepare('DELETE FROM badges WHERE member_id = ? AND badge = ?');
    $stmt->execute([$memberId, $badge]);
    return $stmt->rowCount() === 1;
}

function boardBadgesOf(int $memberId): array {
    $rows = boardAll('SELECT badge, awarded_at, awarded_by FROM badges WHERE member_id = ? ORDER BY awarded_at, badge', [$memberId]);
    return array_values(array_filter($rows, fn($r) => isset(BOARD_BADGES[$r['badge']])));
}

// Badges for many members at once (post author columns): member id => [badge keys].
function boardBadgesFor(array $memberIds): array {
    $ids = array_values(array_unique(array_map('intval', $memberIds)));
    if (!$ids) return [];
    $out = [];
    $team = "'" . implode("','", BOARD_MANUAL_BADGES) . "'";   // team-awarded badges (donors, contributors) first
    foreach (boardAll('SELECT member_id, badge FROM badges WHERE member_id IN (' . implode(',', $ids) . ") ORDER BY CASE WHEN badge = 'top_donator' THEN 0 WHEN badge IN ($team) THEN 1 ELSE 2 END, awarded_at DESC") as $r) {
        if (isset(BOARD_BADGES[$r['badge']])) $out[(int)$r['member_id']][] = $r['badge'];
    }
    return $out;
}

// Newly earned badges the member has not been told about yet; marks them as seen.
function boardTakeNewBadges(int $memberId): array {
    $rows = boardAll('SELECT badge FROM badges WHERE member_id = ? AND seen = 0', [$memberId]);
    if ($rows) boardDb()->prepare('UPDATE badges SET seen = 1 WHERE member_id = ? AND seen = 0')->execute([$memberId]);
    return array_values(array_filter(array_column($rows, 'badge'), fn($b) => isset(BOARD_BADGES[$b])));
}

function boardBadgeHolders(): array {
    $out = [];
    foreach (boardAll('SELECT badge, COUNT(*) AS n FROM badges GROUP BY badge') as $r) $out[$r['badge']] = (int)$r['n'];
    return $out;
}

// Checks every automatic badge for one member and awards the ones newly earned.
function boardAwardBadges(int $memberId, int $now): array {
    $profile = boardProfile($memberId);
    if (!$profile) return [];
    $visible = "x.member_id = ? AND x.status = 'visible' AND t.status = 'visible'";
    $stats = boardOne("SELECT COUNT(*) AS posts, COUNT(DISTINCT t.category_id) AS forums, COUNT(DISTINCT x.created_at / 86400) AS days,"
        . " SUM(x.body LIKE '%[code]%' OR x.body LIKE '%```%') AS code, SUM(x.body LIKE '%[img]https://%') AS images FROM posts x JOIN threads t ON t.id = x.thread_id WHERE $visible", [$memberId]);
    $topics = (int)boardOne("SELECT COUNT(*) AS n FROM threads WHERE member_id = ? AND status = 'visible'", [$memberId])['n'];
    $hot = boardOne("SELECT 1 AS x FROM threads t WHERE t.member_id = ? AND t.status = 'visible' AND (SELECT COUNT(*) FROM posts x WHERE x.thread_id = t.id AND x.member_id != t.member_id AND x.status = 'visible') >= 10 LIMIT 1", [$memberId]);
    $welcomed = (int)boardOne("SELECT COUNT(DISTINCT t.id) AS n FROM posts x JOIN threads t ON t.id = x.thread_id JOIN categories c ON c.id = t.category_id"
        . " WHERE x.member_id = ? AND x.status = 'visible' AND c.slug = ? AND t.member_id != x.member_id", [$memberId, BOARD_WELCOME_SLUG])['n'];
    $upheld = (int)boardOne("SELECT COUNT(*) AS n FROM reports WHERE member_id = ? AND outcome = 'hidden'", [$memberId])['n'];
    $order = (int)boardOne('SELECT COUNT(*) AS n FROM profiles WHERE created_at < ? OR (created_at = ? AND member_id <= ?)', [$profile['created_at'], $profile['created_at'], $memberId])['n'];
    $posts = (int)$stats['posts'];
    [$level] = boardLevel($posts);
    $reputation = boardReputation($memberId);
    $topLiked = (int)(boardOne("SELECT MAX(n) AS n FROM (SELECT COUNT(*) AS n FROM likes l JOIN posts x ON x.id = l.post_id WHERE l.author_id = ? AND x.status = 'visible' GROUP BY l.post_id)", [$memberId])['n'] ?? 0);
    $given = (int)boardOne('SELECT COUNT(*) AS n FROM likes WHERE member_id = ?', [$memberId])['n'];
    $earned = [
        'introduced' => (bool)$profile['introduced_at'] && boardOne("SELECT 1 AS x FROM threads t JOIN categories c ON c.id = t.category_id WHERE t.member_id = ? AND c.slug = ? LIMIT 1", [$memberId, BOARD_WELCOME_SLUG]) !== null,
        'first_post' => $posts >= 1,
        'conversation_starter' => $topics >= 5,
        'prolific' => $posts >= 100,
        'hot_topic' => $hot !== null,
        'explorer' => (int)$stats['forums'] >= 5,
        'code_sharer' => (int)$stats['code'] > 0,
        'show_and_tell' => (int)$stats['images'] > 0,
        'regular' => (int)$stats['days'] >= 7,
        'welcomer' => $welcomed >= 5,
        'good_citizen' => $upheld >= 1,
        'early_adopter' => $order <= 100,
        'anniversary' => (int)$profile['created_at'] <= $now - 365 * 86400,
        'appreciated' => $reputation >= 1,
        'popular_post' => $topLiked >= 10,
        'helpful' => $reputation >= 50,
        'respected' => $reputation >= 250,
        'generous' => $given >= 25,
        'level_5' => $level >= 5,
        'level_10' => $level >= 10,
    ];
    $new = [];
    foreach ($earned as $badge => $yes) if ($yes && boardGrantBadge($memberId, $badge, $now)) $new[] = $badge;
    return $new;
}

// After a post: badges for the author, and "Quotable" for anyone they quoted.
function boardAfterPost(int $memberId, string $body, array $opts, int $now): void {
    boardAwardBadges($memberId, $now);
    $names = [];
    if ($opts['bbcode'] ?? true) { preg_match_all('/\[quote=["\']?([A-Za-z0-9][A-Za-z0-9_-]{2,23})["\']?\]/i', $body, $m); $names = $m[1]; }
    preg_match_all('/^> ?([A-Za-z0-9][A-Za-z0-9_-]{2,23}) wrote:$/m', $body, $m);
    foreach (array_unique(array_merge($names, $m[1])) as $name) {
        $quoted = boardProfileByName($name);
        if ($quoted && (int)$quoted['member_id'] !== $memberId) boardGrantBadge((int)$quoted['member_id'], 'quotable', $now);
    }
}

// --- Mentions, quotes and notifications ---

// Members quoted in a post ([quote=name] with BBCode on, or the Quote button's "> name wrote:").
function boardQuotedNames(string $body, array $opts): array {
    $names = [];
    if ($opts['bbcode'] ?? true) { preg_match_all('/\[quote=["\']?([A-Za-z0-9][A-Za-z0-9_-]{2,23})["\']?\]/i', $body, $m); $names = $m[1]; }
    preg_match_all('/^> ?([A-Za-z0-9][A-Za-z0-9_-]{2,23}) wrote:$/m', $body, $m);
    return array_values(array_unique(array_map('strtolower', array_merge($names, $m[1]))));
}

// @names in the author's own words: code and quoted text are skipped.
function boardMentionedNames(string $body): array {
    $text = preg_replace(['/\[code\].*?\[\/code\]/is', '/^```.*?^```/ms', '/^>.*$/m'], '', $body) ?? '';
    for ($i = 0; $i < 5 && preg_match('/\[quote(=[^\]]*)?\]/i', $text); $i++) $text = preg_replace('/\[quote(=[^\]]*)?\](?:(?!\[quote).)*?\[\/quote\]/is', '', $text) ?? '';
    preg_match_all(BOARD_MENTION_PATTERN, $text, $m);
    return array_values(array_unique(array_map('strtolower', $m[1])));
}

// Notifies quoted and mentioned members about one post (a member who is both is told about the quote).
// Duplicates (for example after an edit) are ignored; blocked authors and the author themself never notify.
function boardNotifyPost(int $postId, int $authorId, string $body, array $opts, int $now): void {
    $db = boardDb();
    $db->prepare('DELETE FROM notifications WHERE created_at < ?')->execute([$now - BOARD_NOTIFY_KEEP_DAYS * 86400]);
    $quoted = boardQuotedNames($body, $opts);
    $targets = array_merge(array_map(fn($n) => [$n, 'quote'], $quoted), array_map(fn($n) => [$n, 'mention'], array_diff(boardMentionedNames($body), $quoted)));
    $done = 0;
    foreach ($targets as [$name, $type]) {
        if ($done >= BOARD_NOTIFY_PER_POST) break;
        $who = boardProfileByName($name);
        if (!$who || (int)$who['member_id'] === $authorId || boardBlocked((int)$who['member_id'], $authorId)) continue;
        $unreadBefore = (int)boardOne('SELECT COUNT(*) AS n FROM notifications WHERE member_id = ? AND read_at IS NULL', [$who['member_id']])['n'];
        $stmt = $db->prepare('INSERT OR IGNORE INTO notifications (member_id, type, actor_id, post_id, created_at) VALUES (?, ?, ?, ?, ?)');
        $stmt->execute([$who['member_id'], $type, $authorId, $postId, $now]);
        if (!$stmt->rowCount()) continue;
        $done++;
        if ($who['notify_mentions_email'] && $unreadBefore === 0) boardNotifyEmail($who, $authorId, $type, $now);
    }
}

// One email while notifications are waiting unread; no post text or link to the post leaves the server.
function boardNotifyEmail(array $who, int $authorId, string $type, int $now): void {
    $key = 'board-mention-mail:' . $who['member_id'];
    if (faucetLimitCount($key, 86400) >= BOARD_NOTIFY_MAIL_DAILY) return;
    $email = faucetDb()->prepare("SELECT email FROM members WHERE id = ? AND status = 'active' AND email_verified_at IS NOT NULL");
    $email->execute([$who['member_id']]);
    $to = $email->fetchColumn();
    if (!is_string($to)) return;
    $actor = boardProfile($authorId)['username'] ?? 'A member';
    $base = faucetConfig()['base_url'];
    $unsubscribe = $base . '/community/board/?unsubscribe_mentions=' . boardUnsubscribeToken((int)$who['member_id'], 'mentions');
    if (authMail($to, ($type === 'quote' ? 'You were quoted' : 'You were mentioned') . ' on the Monzero forum',
        "Hello {$who['username']},\n\n$actor " . ($type === 'quote' ? 'quoted' : 'mentioned') . " you on the Monzero community forum.\n\nSee your notifications (you will need to sign in):\n$base/community/board/?view=notifications\n\n"
        . "You get one email until you check your notifications. Monzero will never ask for your password, seed phrase or keys.\n\nTo stop these emails, open this link or untick the option in your forum profile settings:\n$unsubscribe\n",
        'MONZERO_FAUCET_MAIL_LOG', ['List-Unsubscribe' => "<$unsubscribe>", 'List-Unsubscribe-Post' => 'List-Unsubscribe=One-Click'])) faucetLimitHit($key);
}

function boardNotificationsUnread(int $memberId): int { return (int)boardOne('SELECT COUNT(*) AS n FROM notifications WHERE member_id = ? AND read_at IS NULL', [$memberId])['n']; }

function boardNotifications(int $memberId, int $limit = 50): array {
    return boardAll('SELECT n.*, a.username AS actor, a.role AS actor_role, a.avatar AS actor_avatar, x.status AS post_status, t.title, t.status AS thread_status FROM notifications n'
        . ' JOIN profiles a ON a.member_id = n.actor_id JOIN posts x ON x.id = n.post_id JOIN threads t ON t.id = x.thread_id WHERE n.member_id = ? ORDER BY n.id DESC LIMIT ?', [$memberId, $limit]);
}

// Marks one notification read and returns its post id (null if it is not this member's).
function boardOpenNotification(int $memberId, int $id, int $now): ?int {
    $row = boardOne('SELECT post_id FROM notifications WHERE id = ? AND member_id = ?', [$id, $memberId]);
    if (!$row) return null;
    boardDb()->prepare('UPDATE notifications SET read_at = COALESCE(read_at, ?) WHERE id = ?')->execute([$now, $id]);
    return (int)$row['post_id'];
}

function boardMarkNotificationsRead(int $memberId, int $now): void { boardDb()->prepare('UPDATE notifications SET read_at = ? WHERE member_id = ? AND read_at IS NULL')->execute([$now, $memberId]); }

// --- Likes and reputation ---

// Reputation: likes received on posts that are still visible.
function boardReputation(int $memberId): int {
    return (int)boardOne("SELECT COUNT(*) AS n FROM likes l JOIN posts x ON x.id = l.post_id JOIN threads t ON t.id = x.thread_id WHERE l.author_id = ? AND x.status = 'visible' AND t.status = 'visible'", [$memberId])['n'];
}

function boardReputationFor(array $memberIds): array {
    $ids = array_values(array_unique(array_map('intval', $memberIds)));
    if (!$ids) return [];
    $out = array_fill_keys($ids, 0);
    foreach (boardAll("SELECT l.author_id, COUNT(*) AS n FROM likes l JOIN posts x ON x.id = l.post_id JOIN threads t ON t.id = x.thread_id WHERE l.author_id IN (" . implode(',', $ids) . ") AND x.status = 'visible' AND t.status = 'visible' GROUP BY l.author_id") as $r) $out[(int)$r['author_id']] = (int)$r['n'];
    return $out;
}

// For posts on one page: post id => [count, liked by me, up to 3 liker names (newest first)].
function boardLikesFor(array $postIds, int $myId): array {
    $ids = array_values(array_unique(array_map('intval', $postIds)));
    if (!$ids) return [];
    $out = [];
    foreach (boardAll('SELECT l.post_id, l.member_id, p.username FROM likes l JOIN profiles p ON p.member_id = l.member_id WHERE l.post_id IN (' . implode(',', $ids) . ') ORDER BY l.created_at DESC') as $r) {
        $id = (int)$r['post_id'];
        $out[$id] ??= ['count' => 0, 'mine' => false, 'names' => []];
        $out[$id]['count']++;
        if ((int)$r['member_id'] === $myId) $out[$id]['mine'] = true;
        if (count($out[$id]['names']) < 3) $out[$id]['names'][] = $r['username'];
    }
    return $out;
}

function boardMostLiked(int $memberId, int $limit = 5): array {
    return boardAll("SELECT x.id, x.thread_id, t.title, COUNT(*) AS likes FROM likes l JOIN posts x ON x.id = l.post_id JOIN threads t ON t.id = x.thread_id JOIN categories c ON c.id = t.category_id"
        . " WHERE l.author_id = ? AND x.status = 'visible' AND t.status = 'visible' AND c.status != 'archived' GROUP BY x.id ORDER BY likes DESC, x.id DESC LIMIT ?", [$memberId, $limit]);
}

function boardLikesGiven(int $memberId): int { return (int)boardOne('SELECT COUNT(*) AS n FROM likes WHERE member_id = ?', [$memberId])['n']; }

// Why this member cannot like this post, or null.
function boardLikeBlock(?array $member, ?array $profile, ?array $post, int $now): ?string {
    if ($problem = boardMemberBlock($member)) return $problem;
    if (!$profile) return 'Choose a username first.';
    if (!$profile['introduced_at']) return 'Introduce yourself in New user welcome first.';
    if (boardSuspended($profile, $now)) return 'Your account is suspended.';
    if (!$post || $post['status'] !== 'visible' || $post['thread_status'] !== 'visible') return 'That post cannot be liked.';
    if ((int)$post['member_id'] === (int)$member['id']) return 'You cannot like your own post.';
    return null;
}

// Like or unlike. Returns [liked now?, like count, error or null].
function boardToggleLike(array $member, array $post, int $now): array {
    $profile = boardProfile((int)$member['id']);
    if ($problem = boardLikeBlock($member, $profile, $post, $now)) return [false, 0, $problem];
    $db = boardDb();
    $db->exec('BEGIN IMMEDIATE');
    $had = boardOne('SELECT 1 AS x FROM likes WHERE post_id = ? AND member_id = ?', [$post['id'], $member['id']]);
    if ($had) {
        $db->prepare('DELETE FROM likes WHERE post_id = ? AND member_id = ?')->execute([$post['id'], $member['id']]);
    } else {
        $tier = (int)$member['created_at'] > $now - BOARD_NEW_MEMBER ? 0 : 1;
        $today = (int)boardOne('SELECT COUNT(*) AS n FROM likes WHERE member_id = ? AND created_at > ?', [$member['id'], $now - 86400])['n'];
        $network = (int)boardOne('SELECT COUNT(*) AS n FROM likes WHERE client_hash = ? AND author_id = ? AND created_at > ?', [faucetClientHash(), $post['member_id'], $now - 86400])['n'];
        if ($today >= BOARD_LIKES_DAILY[$tier]) { $db->exec('ROLLBACK'); return [false, 0, 'You have used today\'s likes. Try again tomorrow.']; }
        if ($network >= BOARD_LIKES_PER_NETWORK_TO_MEMBER) { $db->exec('ROLLBACK'); return [false, 0, 'Too many likes for this member from your network today.']; }
        $db->prepare('INSERT INTO likes (post_id, member_id, author_id, client_hash, created_at) VALUES (?, ?, ?, ?, ?)')->execute([$post['id'], $member['id'], $post['member_id'], faucetClientHash(), $now]);
    }
    $count = (int)boardOne('SELECT COUNT(*) AS n FROM likes WHERE post_id = ?', [$post['id']])['n'];
    $db->exec('COMMIT');
    if (!$had) { boardAwardBadges((int)$post['member_id'], $now); boardAwardBadges((int)$member['id'], $now); }
    return [!$had, $count, null];
}

// --- Signatures and avatars ---

function boardRenderSignature(string $signature): string { return boardRender($signature, ['allow' => BOARD_SIGNATURE_TAGS]); }

// Saves (or clears, with an empty text) the member's signature. Returns an error or null.
function boardSetSignature(array $member, string $text, int $now): ?string {
    $profile = boardProfile((int)$member['id']);
    if ($problem = boardMemberBlock($member)) return $problem;
    if (!$profile || boardSuspended($profile, $now)) return 'Your account cannot change its signature right now.';
    $text = boardClean($text);
    if ($text !== '') {
        [$level] = boardLevel(boardMemberPostCount((int)$member['id']));
        if ($level < BOARD_SIGNATURE_LEVEL) return 'Signatures unlock at level ' . BOARD_SIGNATURE_LEVEL . ' (' . BOARD_LEVELS[BOARD_SIGNATURE_LEVEL - 1][0] . ' posts).';
        if ($problem = boardTextProblem($text, 1, BOARD_SIGNATURE_MAX, 'signature')) return $problem;
        if (substr_count($text, "\n") >= BOARD_SIGNATURE_LINES) return 'Use at most ' . BOARD_SIGNATURE_LINES . ' lines.';
        $links = boardLinkCount($text);
        if ($links > BOARD_SIGNATURE_LINKS) return 'Use at most ' . BOARD_SIGNATURE_LINKS . ' links in a signature.';
        if ($links && (int)$member['created_at'] > $now - BOARD_NEW_MEMBER) return 'New accounts cannot add links for their first 3 days.';
    }
    boardDb()->prepare('UPDATE profiles SET signature = ? WHERE member_id = ?')->execute([$text, $member['id']]);
    return null;
}

function boardAvatarDir(): string {
    $configured = getenv('MONZERO_BOARD_AVATARS');
    $dir = is_string($configured) && $configured !== '' ? $configured : faucetPrivateDir() . '/board-avatars';
    if (!is_dir($dir)) @mkdir($dir, 0700, true);
    return $dir;
}

// Stored avatar file name, or null when the name is not one we create (never a path).
function boardAvatarFile(?string $name): ?string {
    return is_string($name) && preg_match('/\A[0-9a-f]{32}\.(webp|png)\z/', $name) ? $name : null;
}

function boardRemoveAvatar(int $memberId): bool {
    $profile = boardProfile($memberId);
    if (!$profile || !($file = boardAvatarFile($profile['avatar']))) return false;
    boardDb()->prepare('UPDATE profiles SET avatar = NULL WHERE member_id = ?')->execute([$memberId]);
    @unlink(boardAvatarDir() . '/' . $file);
    return true;
}

// Validates an uploaded image, re-draws it as a BOARD_AVATAR_SIZE square and stores it. Returns an error or null.
function boardSetAvatar(array $member, array $upload, int $now): ?string {
    $profile = boardProfile((int)$member['id']);
    if ($problem = boardMemberBlock($member)) return $problem;
    if (!$profile || !$profile['introduced_at']) return 'Introduce yourself in New user welcome first.';
    if (boardSuspended($profile, $now)) return 'Your account cannot change its avatar right now.';
    if (!function_exists('imagecreatefromstring') || !function_exists('imagecopyresampled')) return 'Avatar uploads are not available on this server yet.';
    $error = $upload['error'] ?? UPLOAD_ERR_NO_FILE;
    if ($error === UPLOAD_ERR_INI_SIZE || $error === UPLOAD_ERR_FORM_SIZE) return 'That image is too large (2 MB at most).';
    if ($error !== UPLOAD_ERR_OK || !is_string($upload['tmp_name'] ?? null) || !is_uploaded_file($upload['tmp_name'])) return 'Choose an image to upload.';
    $tmp = $upload['tmp_name'];
    if (filesize($tmp) > BOARD_AVATAR_MAX_BYTES) return 'That image is too large (2 MB at most).';
    $key = 'board-avatar:' . $member['id'];
    if (faucetLimitCount($key, 86400) >= BOARD_AVATAR_DAILY) return 'You have changed your avatar too often today. Try again tomorrow.';
    // Read only the header first, so oversized images are refused before decoding them.
    $info = @getimagesize($tmp);
    if (!$info || !in_array($info[2], [IMAGETYPE_JPEG, IMAGETYPE_PNG, IMAGETYPE_GIF, IMAGETYPE_WEBP], true)) return 'Use a JPEG, PNG, WebP or GIF image.';
    [$w, $h] = $info;
    if ($w < 16 || $h < 16) return 'That image is too small (16 × 16 pixels at least).';
    if ($w > BOARD_AVATAR_MAX_SIDE || $h > BOARD_AVATAR_MAX_SIDE) return 'That image is too big (' . BOARD_AVATAR_MAX_SIDE . ' × ' . BOARD_AVATAR_MAX_SIDE . ' pixels at most).';
    $source = @imagecreatefromstring((string)file_get_contents($tmp));
    if (!$source) return 'That image could not be read.';
    $side = min($w, $h);
    $out = imagecreatetruecolor(BOARD_AVATAR_SIZE, BOARD_AVATAR_SIZE);
    imagealphablending($out, false); imagesavealpha($out, true);
    imagefill($out, 0, 0, imagecolorallocatealpha($out, 0, 0, 0, 127));
    imagecopyresampled($out, $source, 0, 0, intdiv($w - $side, 2), intdiv($h - $side, 2), BOARD_AVATAR_SIZE, BOARD_AVATAR_SIZE, $side, $side);
    $webp = function_exists('imagewebp');
    $name = bin2hex(random_bytes(16)) . ($webp ? '.webp' : '.png');
    $path = boardAvatarDir() . '/' . $name;
    $ok = $webp ? imagewebp($out, $path, 85) : imagepng($out, $path, 9);
    if (!$ok) { @unlink($path); return 'The avatar could not be saved. Try again later.'; }
    @chmod($path, 0600);
    faucetLimitHit($key);
    boardRemoveAvatar((int)$member['id']);
    boardDb()->prepare('UPDATE profiles SET avatar = ? WHERE member_id = ?')->execute([$name, $member['id']]);
    faucetEvent((int)$member['id'], 'board.avatar');
    return null;
}

// --- Private messages ---

function boardBlocked(int $memberId, int $otherId): bool { return boardOne('SELECT 1 AS x FROM blocks WHERE member_id = ? AND blocked_id = ?', [$memberId, $otherId]) !== null; }

function boardBlockList(int $memberId): array {
    return boardAll('SELECT p.username, b.created_at FROM blocks b JOIN profiles p ON p.member_id = b.blocked_id WHERE b.member_id = ? ORDER BY p.username', [$memberId]);
}

// Block or unblock a member by username. Returns an error or null.
function boardSetBlock(int $memberId, string $username, bool $block, int $now): ?string {
    $other = boardProfileByName(trim($username));
    if (!$other) return 'No member has that username.';
    if ((int)$other['member_id'] === $memberId) return 'You cannot block yourself.';
    if ($block && boardRoleRank($other) >= 1) return 'Forum staff cannot be blocked, so they can always reach you about moderation.';
    if ($block) boardDb()->prepare('INSERT OR IGNORE INTO blocks (member_id, blocked_id, created_at) VALUES (?, ?, ?)')->execute([$memberId, $other['member_id'], $now]);
    else boardDb()->prepare('DELETE FROM blocks WHERE member_id = ? AND blocked_id = ?')->execute([$memberId, $other['member_id']]);
    return null;
}

// Why this member cannot use private messages at all right now, or null.
function boardPmBlock(?array $member, ?array $profile, int $now): ?string {
    if ($problem = boardMemberBlock($member)) return $problem;
    if (!$profile) return 'Choose a username first.';
    if (!$profile['introduced_at']) return 'Introduce yourself in New user welcome first.';
    if (boardSuspended($profile, $now)) return 'Your account is suspended, so it cannot send messages.';
    return null;
}

function boardPmLimit(array $member, string $kind, int $now): ?string {
    $tier = (int)$member['created_at'] > $now - BOARD_NEW_MEMBER ? 0 : 1;
    $n = $kind === 'conversations'
        ? (int)boardOne('SELECT COUNT(*) AS n FROM conversations WHERE created_by = ? AND created_at > ?', [$member['id'], $now - 86400])['n']
        : (int)boardOne('SELECT COUNT(*) AS n FROM messages WHERE member_id = ? AND created_at > ?', [$member['id'], $now - 86400])['n'];
    return $n >= BOARD_PM_LIMITS[$kind][$tier] ? 'You have reached today\'s limit for ' . ($kind === 'conversations' ? 'new conversations' : 'messages') . '. Try again tomorrow.' : null;
}

function boardPmInsert(int $conversationId, array $member, string $body, array $opts, int $now): int {
    $db = boardDb();
    $db->prepare('INSERT INTO messages (conversation_id, member_id, body, bbcode, smilies, autolink, client_hash, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)')
        ->execute([$conversationId, $member['id'], $body, (int)$opts['bbcode'], (int)$opts['smilies'], (int)$opts['autolink'], faucetClientHash(), $now]);
    $id = (int)$db->lastInsertId();
    $db->prepare('UPDATE conversations SET last_message_at = ? WHERE id = ?')->execute([$now, $conversationId]);
    $db->prepare('UPDATE conversation_members SET last_read_id = ? WHERE conversation_id = ? AND member_id = ?')->execute([$id, $conversationId, $member['id']]);
    return $id;
}

// Start a conversation. $to: usernames. Returns [conversation id, null] or [null, error].
function boardPmStart(array $member, array $to, string $subject, string $body, array $opts, int $now): array {
    $profile = boardProfile((int)$member['id']);
    if ($problem = boardPmBlock($member, $profile, $now)) return [null, $problem];
    $names = array_values(array_unique(array_filter(array_map('trim', $to), fn($n) => $n !== ''), SORT_REGULAR));
    if (!$names) return [null, 'Choose at least one member to message.'];
    if (count($names) > BOARD_PM_MAX_RECIPIENTS) return [null, 'Message at most ' . BOARD_PM_MAX_RECIPIENTS . ' members at once.'];
    $recipients = [];
    foreach ($names as $name) {
        $r = boardProfileByName($name);
        if (!$r) return [null, 'No member is called “' . $name . '”.'];
        if ((int)$r['member_id'] === (int)$member['id']) return [null, 'You cannot send a message to yourself.'];
        if (boardBlocked((int)$r['member_id'], (int)$member['id'])) return [null, $r['username'] . ' is not accepting messages from you.'];
        if (!$r['allow_pms'] && boardRoleRank($profile) < 1) return [null, $r['username'] . ' only accepts messages from forum staff.'];
        $recipients[(int)$r['member_id']] = $r;
    }
    $allStaff = !array_filter($recipients, fn($r) => boardRoleRank($r) < 1);
    [$level] = boardLevel(boardMemberPostCount((int)$member['id']));
    if (!$allStaff && boardRoleRank($profile) < 1 && $level < BOARD_PM_LEVEL) return [null, 'Starting conversations unlocks at level ' . BOARD_PM_LEVEL . ' (' . BOARD_LEVELS[BOARD_PM_LEVEL - 1][0] . ' posts). You can always message forum staff, and reply to messages you receive.'];
    $subject = trim(preg_replace('/\s+/u', ' ', $subject) ?? ''); $body = boardClean($body);
    if ($problem = boardTextProblem($subject, 1, BOARD_PM_SUBJECT_MAX, 'subject') ?? boardBodyProblem($member, $body, $now)) return [null, $problem];
    $db = boardDb();
    $db->exec('BEGIN IMMEDIATE');
    if ($problem = boardPmLimit($member, 'conversations', $now) ?? boardPmLimit($member, 'messages', $now)) { $db->exec('ROLLBACK'); return [null, $problem]; }
    $db->prepare('INSERT INTO conversations (subject, created_by, created_at, last_message_at) VALUES (?, ?, ?, ?)')->execute([$subject, $member['id'], $now, $now]);
    $id = (int)$db->lastInsertId();
    $add = $db->prepare('INSERT INTO conversation_members (conversation_id, member_id) VALUES (?, ?)');
    foreach (array_merge([(int)$member['id']], array_keys($recipients)) as $m) $add->execute([$id, $m]);
    $message = boardPmInsert($id, $member, $body, $opts, $now);
    $db->exec('COMMIT');
    faucetEvent((int)$member['id'], 'board.pm_start', ['conversation' => $id]);
    boardPmNotify($id, (int)$member['id'], $message, $now);
    return [$id, null];
}

// The conversation if this member is an active participant, else null.
function boardPmConversation(int $memberId, int $conversationId): ?array {
    return boardOne('SELECT c.*, cm.last_read_id FROM conversations c JOIN conversation_members cm ON cm.conversation_id = c.id WHERE c.id = ? AND cm.member_id = ? AND cm.left_at IS NULL', [$conversationId, $memberId]);
}

function boardPmParticipants(int $conversationId): array {
    return boardAll('SELECT p.member_id, p.username, p.role, p.avatar, cm.left_at FROM conversation_members cm JOIN profiles p ON p.member_id = cm.member_id WHERE cm.conversation_id = ? ORDER BY p.username', [$conversationId]);
}

// Reply in a conversation. Returns [message id, null] or [null, error].
function boardPmReply(array $member, int $conversationId, string $body, array $opts, int $now): array {
    $profile = boardProfile((int)$member['id']);
    if ($problem = boardPmBlock($member, $profile, $now)) return [null, $problem];
    if (!boardPmConversation((int)$member['id'], $conversationId)) return [null, 'That conversation does not exist.'];
    $others = array_filter(boardPmParticipants($conversationId), fn($p) => !$p['left_at'] && (int)$p['member_id'] !== (int)$member['id']);
    if (!$others) return [null, 'Everyone else has left this conversation.'];
    foreach ($others as $o) if (boardBlocked((int)$o['member_id'], (int)$member['id'])) return [null, $o['username'] . ' is not accepting messages from you.'];
    $body = boardClean($body);
    if ($problem = boardBodyProblem($member, $body, $now)) return [null, $problem];
    $db = boardDb();
    $db->exec('BEGIN IMMEDIATE');
    if ($problem = boardPmLimit($member, 'messages', $now)) { $db->exec('ROLLBACK'); return [null, $problem]; }
    $id = boardPmInsert($conversationId, $member, $body, $opts, $now);
    $db->exec('COMMIT');
    boardPmNotify($conversationId, (int)$member['id'], $id, $now);
    return [$id, null];
}

// Messages, oldest first; marks the conversation read for this member.
function boardPmMessages(int $memberId, int $conversationId, int $now): array {
    $rows = boardAll('SELECT m.*, p.username, p.role, p.avatar FROM messages m JOIN profiles p ON p.member_id = m.member_id WHERE m.conversation_id = ? ORDER BY m.id', [$conversationId]);
    if ($rows) boardDb()->prepare('UPDATE conversation_members SET last_read_id = MAX(last_read_id, ?) WHERE conversation_id = ? AND member_id = ?')->execute([(int)end($rows)['id'], $conversationId, $memberId]);
    return $rows;
}

function boardPmInbox(int $memberId): array {
    return boardAll('SELECT c.*, cm.last_read_id, (SELECT COUNT(*) FROM messages m WHERE m.conversation_id = c.id) AS messages,'
        . ' (SELECT p.username FROM messages m JOIN profiles p ON p.member_id = m.member_id WHERE m.conversation_id = c.id ORDER BY m.id DESC LIMIT 1) AS last_by,'
        . ' (SELECT GROUP_CONCAT(p.username, \', \') FROM conversation_members o JOIN profiles p ON p.member_id = o.member_id WHERE o.conversation_id = c.id AND o.member_id != cm.member_id) AS others,'
        . ' (SELECT COUNT(*) FROM messages m WHERE m.conversation_id = c.id AND m.id > cm.last_read_id AND m.member_id != cm.member_id) AS unread'
        . ' FROM conversations c JOIN conversation_members cm ON cm.conversation_id = c.id WHERE cm.member_id = ? AND cm.left_at IS NULL ORDER BY c.last_message_at DESC, c.id DESC LIMIT 200', [$memberId]);
}

function boardPmUnread(int $memberId): int {
    return (int)boardOne('SELECT COUNT(DISTINCT c.id) AS n FROM conversations c JOIN conversation_members cm ON cm.conversation_id = c.id'
        . ' WHERE cm.member_id = ? AND cm.left_at IS NULL AND EXISTS (SELECT 1 FROM messages m WHERE m.conversation_id = c.id AND m.id > cm.last_read_id AND m.member_id != cm.member_id)', [$memberId])['n'];
}

// --- Email notices for new messages ---

// Token for the unsubscribe link: member id plus an HMAC, so the link works without signing in.
// $kind 'pm' (message emails) or 'mentions' (mention and quote emails); a token only works for its kind.
function boardUnsubscribeToken(int $memberId, string $kind = 'pm'): string {
    return $memberId . '.' . substr(hash_hmac('sha256', ($kind === 'pm' ? 'pm' : 'mentions') . '-unsubscribe:' . $memberId, faucetSecret()), 0, 32);
}

function boardUnsubscribeMember(mixed $token, string $kind = 'pm'): ?int {
    if (!is_string($token) || !preg_match('/\A([1-9][0-9]{0,18})\.[0-9a-f]{32}\z/', $token, $m)) return null;
    return hash_equals(boardUnsubscribeToken((int)$m[1], $kind), $token) && boardProfile((int)$m[1]) ? (int)$m[1] : null;
}

// Emails each other participant who wants notices, unless one is already waiting for them in this
// conversation (nothing new is mailed until they read it). The email names the sender only: no subject
// or message text leaves the server.
function boardPmNotify(int $conversationId, int $senderId, int $messageId, int $now): void {
    $sender = boardProfile($senderId);
    $base = faucetConfig()['base_url'];
    foreach (boardAll('SELECT cm.member_id, cm.last_read_id, cm.notified_id, p.username FROM conversation_members cm JOIN profiles p ON p.member_id = cm.member_id'
        . ' WHERE cm.conversation_id = ? AND cm.member_id != ? AND cm.left_at IS NULL AND p.notify_pm = 1', [$conversationId, $senderId]) as $r) {
        if ((int)$r['notified_id'] > (int)$r['last_read_id']) continue;   // an unread notice is already waiting
        $key = 'board-pm-mail:' . $r['member_id'];
        if (faucetLimitCount($key, 86400) >= BOARD_PM_MAIL_DAILY) continue;
        $email = faucetDb()->prepare("SELECT email FROM members WHERE id = ? AND status = 'active' AND email_verified_at IS NOT NULL");
        $email->execute([$r['member_id']]);
        $to = $email->fetchColumn();
        if (!is_string($to)) continue;
        $unsubscribe = $base . '/community/board/?unsubscribe=' . boardUnsubscribeToken((int)$r['member_id']);
        $sent = authMail($to, 'New private message on the Monzero forum',
            "Hello {$r['username']},\n\n{$sender['username']} sent you a private message on the Monzero community forum.\n\nRead it here (you will need to sign in):\n$base/community/board/?view=inbox\n\n"
            . "For your privacy this email does not include the message. Monzero will never ask for your password, seed phrase or keys, by email or in a message.\n\n"
            . "You get one email per conversation until you read it. To stop these emails, open this link or untick the option in your forum profile settings:\n$unsubscribe\n",
            'MONZERO_FAUCET_MAIL_LOG', ['List-Unsubscribe' => "<$unsubscribe>", 'List-Unsubscribe-Post' => 'List-Unsubscribe=One-Click']);
        if (!$sent) continue;
        faucetLimitHit($key);
        boardDb()->prepare('UPDATE conversation_members SET notified_id = ? WHERE conversation_id = ? AND member_id = ?')->execute([$messageId, $conversationId, $r['member_id']]);
    }
}

// Leave a conversation; when nobody is left, it is deleted from the server.
function boardPmLeave(int $memberId, int $conversationId, int $now): bool {
    if (!boardPmConversation($memberId, $conversationId)) return false;
    $db = boardDb();
    $db->exec('BEGIN IMMEDIATE');
    $db->prepare('UPDATE conversation_members SET left_at = ? WHERE conversation_id = ? AND member_id = ?')->execute([$now, $conversationId, $memberId]);
    if (!(int)boardOne('SELECT COUNT(*) AS n FROM conversation_members WHERE conversation_id = ? AND left_at IS NULL', [$conversationId])['n']) {
        $db->prepare('DELETE FROM message_reports WHERE message_id IN (SELECT id FROM messages WHERE conversation_id = ?)')->execute([$conversationId]);
        $db->prepare('DELETE FROM messages WHERE conversation_id = ?')->execute([$conversationId]);
        $db->prepare('DELETE FROM conversation_members WHERE conversation_id = ?')->execute([$conversationId]);
        $db->prepare('DELETE FROM conversations WHERE id = ?')->execute([$conversationId]);
    }
    $db->exec('COMMIT');
    return true;
}

// Report one message to staff (only that message becomes visible to them). Returns an error or null.
function boardPmReport(array $member, int $messageId, string $reason, int $now): ?string {
    $message = boardOne('SELECT * FROM messages WHERE id = ?', [$messageId]);
    if (!$message || !boardPmConversation((int)$member['id'], (int)$message['conversation_id'])) return 'That message does not exist.';
    if ((int)$message['member_id'] === (int)$member['id']) return 'You cannot report your own message.';
    $reason = trim(preg_replace('/\s+/u', ' ', $reason) ?? '');
    if ($problem = boardTextProblem($reason, 3, 500, 'reason')) return $problem;
    $stmt = boardDb()->prepare('INSERT OR IGNORE INTO message_reports (message_id, member_id, reason, created_at) VALUES (?, ?, ?, ?)');
    $stmt->execute([$messageId, $member['id'], $reason, $now]);
    return $stmt->rowCount() ? null : 'You have already reported this message.';
}

function boardPmOpenReports(): array {
    return boardAll('SELECT m.id AS message_id, m.body, m.status, m.created_at, m.bbcode, m.smilies, m.autolink, p.username, COUNT(r.id) AS reports, GROUP_CONCAT(r.reason, \' | \') AS reasons'
        . ' FROM message_reports r JOIN messages m ON m.id = r.message_id JOIN profiles p ON p.member_id = m.member_id WHERE r.resolved_at IS NULL GROUP BY m.id ORDER BY MIN(r.created_at)');
}

// --- Staff roles and moderation ---

function boardRoleRank(?array $profile): int { return $profile ? (BOARD_ROLES[$profile['role'] ?? 'member'][0] ?? 0) : 0; }
function boardRoleName(string $role): string { return BOARD_ROLES[$role][1] ?? 'Member'; }

// Staff who may moderate: fully signed in, not suspended, role rank 1-3.
function boardStaff(?array $member, ?array $profile, int $now): bool {
    return $member && $profile && !boardMemberBlock($member) && !boardSuspended($profile, $now) && boardRoleRank($profile) >= 1;
}

function boardCanModerate(?array $profile, string $action): bool { return isset(BOARD_MOD_ACTIONS[$action]) && boardRoleRank($profile) >= BOARD_MOD_ACTIONS[$action]; }

function boardModLog(string $actor, string $action, string $target, string $detail, int $now): void {
    boardDb()->prepare('INSERT INTO mod_log (at, actor, action, target, detail) VALUES (?, ?, ?, ?, ?)')->execute([$now, $actor, $action, $target, mb_substr($detail, 0, 500)]);
}

function boardModLogRecent(int $limit = 50): array { return boardAll('SELECT * FROM mod_log ORDER BY id DESC LIMIT ?', [$limit]); }

function boardStaffList(): array {
    return boardAll("SELECT * FROM profiles WHERE role IN ('administrator', 'global_moderator', 'moderator') ORDER BY CASE role WHEN 'administrator' THEN 0 WHEN 'global_moderator' THEN 1 ELSE 2 END, username");
}

// One moderation action by a member with a staff role. $args: post, thread, category, username, days, reason, role.
// Staff can never act on content or members of an equal or higher role. Returns an error or null.
function boardModerate(array $actor, string $action, array $args, int $now): ?string {
    if (!boardCanModerate($actor, $action)) return 'Your role cannot do that.';
    $rank = boardRoleRank($actor);
    $target = ''; $detail = '';
    $outranks = fn(?array $p) => $p && boardRoleRank($p) < $rank;
    if (in_array($action, ['hide_post', 'restore_post', 'dismiss_reports'], true)) {
        $post = boardPost((int)($args['post'] ?? 0));
        if (!$post) return 'That post does not exist.';
        if (!$outranks(boardProfile((int)$post['member_id']))) return 'You cannot moderate posts by staff of your rank or higher.';
        $ok = match ($action) {
            'hide_post' => boardSetPostStatus((int)$post['id'], 'hidden', $now),
            'restore_post' => boardSetPostStatus((int)$post['id'], 'visible', $now),
            'dismiss_reports' => boardDismissReports((int)$post['id'], $now),
        };
        if (!$ok) return 'Nothing to change on that post.';
        $target = 'post ' . $post['id'];
    } elseif (in_array($action, ['lock', 'unlock', 'pin', 'unpin', 'move', 'hide_topic', 'show_topic'], true)) {
        $thread = boardThread((int)($args['thread'] ?? 0));
        if (!$thread) return 'That topic does not exist.';
        if (!$outranks(boardProfile((int)$thread['member_id']))) return 'You cannot moderate topics started by staff of your rank or higher.';
        $ok = match ($action) {
            'lock', 'unlock' => boardSetThreadFlag((int)$thread['id'], 'locked', $action === 'lock' ? 1 : 0),
            'pin', 'unpin' => boardSetThreadFlag((int)$thread['id'], 'pinned', $action === 'pin' ? 1 : 0),
            'hide_topic', 'show_topic' => boardSetThreadFlag((int)$thread['id'], 'hidden', $action === 'hide_topic' ? 1 : 0),
            'move' => ($c = boardCategory((string)($args['category'] ?? ''))) && $c['status'] !== 'archived' && boardMoveThread((int)$thread['id'], $c['slug']),
        };
        if (!$ok) return 'Nothing to change on that topic.';
        $target = 'topic ' . $thread['id'];
        if ($action === 'move') $detail = 'to ' . $args['category'];
    } elseif (in_array($action, ['hide_message', 'dismiss_message_report'], true)) {
        // Staff only ever reach a message through a report on it.
        $message = boardOne('SELECT m.*, p.username FROM messages m JOIN profiles p ON p.member_id = m.member_id WHERE m.id = ? AND EXISTS (SELECT 1 FROM message_reports r WHERE r.message_id = m.id)', [(int)($args['message'] ?? 0)]);
        if (!$message) return 'That message has not been reported.';
        if (!$outranks(boardProfile((int)$message['member_id']))) return 'You cannot moderate messages by staff of your rank or higher.';
        $open = (int)boardOne('SELECT COUNT(*) AS n FROM message_reports WHERE message_id = ? AND resolved_at IS NULL', [$message['id']])['n'];
        if (!$open) return 'That report was already handled.';
        if ($action === 'hide_message') boardDb()->prepare("UPDATE messages SET status = 'hidden' WHERE id = ?")->execute([$message['id']]);
        boardDb()->prepare('UPDATE message_reports SET resolved_at = ?, outcome = ? WHERE message_id = ? AND resolved_at IS NULL')->execute([$now, $action === 'hide_message' ? 'hidden' : 'dismissed', $message['id']]);
        $target = 'message ' . $message['id'] . ' by ' . $message['username'];
    } elseif (in_array($action, ['clear_signature', 'remove_avatar'], true)) {
        $member = boardProfileByName((string)($args['username'] ?? ''));
        if (!$member) return 'No member has that username.';
        if (!$outranks($member)) return 'You cannot act on staff of your rank or higher.';
        if ($action === 'clear_signature') {
            if ($member['signature'] === '') return 'That member has no signature.';
            boardDb()->prepare("UPDATE profiles SET signature = '' WHERE member_id = ?")->execute([$member['member_id']]);
        } elseif (!boardRemoveAvatar((int)$member['member_id'])) {
            return 'That member has no avatar.';
        }
        $target = 'member ' . $member['username'];
    } elseif (in_array($action, ['suspend', 'unsuspend', 'set_role'], true)) {
        $member = boardProfileByName((string)($args['username'] ?? ''));
        if (!$member) return 'No member has that username.';
        if (!$outranks($member)) return 'You cannot act on staff of your rank or higher.';
        $target = 'member ' . $member['username'];
        if ($action === 'suspend') {
            $days = (int)($args['days'] ?? -1);
            $max = BOARD_SUSPEND_MAX_DAYS[$rank] ?? 0;
            if ($days < ($rank >= 3 ? 0 : 1) || $days > $max) return $rank >= 3 ? 'Suspend for 0 (until lifted) to 3650 days.' : 'Suspend for 1 to ' . $max . ' days. Longer suspensions need an administrator.';
            if ($error = boardSuspend($member['username'], $days, (string)($args['reason'] ?? ''), $now)) return $error;
            $detail = ($days ? "$days days" : 'until lifted') . ': ' . trim((string)$args['reason']);
        } elseif ($action === 'unsuspend') {
            if (!boardUnsuspend($member['username'])) return 'That member is not suspended.';
        } else {
            $role = (string)($args['role'] ?? '');
            if (!in_array($role, ['member', 'moderator', 'global_moderator'], true)) return 'Administrators can appoint moderators and global moderators. Only the site owner appoints administrators.';
            boardDb()->prepare('UPDATE profiles SET role = ? WHERE member_id = ?')->execute([$role, $member['member_id']]);
            $detail = 'role ' . $role;
        }
    } else {
        return 'Unknown action.';
    }
    boardModLog($actor['username'] . ' (' . boardRoleName($actor['role']) . ')', $action, $target, $detail, $now);
    return null;
}

// /admin console: any role, including administrator. Returns an error or null.
function boardSetRole(string $username, string $role, string $by, int $now): ?string {
    $member = boardProfileByName($username);
    if (!$member) return 'No member has that username.';
    if (!isset(BOARD_ROLES[$role])) return 'Choose a valid role.';
    boardDb()->prepare('UPDATE profiles SET role = ? WHERE member_id = ?')->execute([$role, $member['member_id']]);
    boardModLog($by, 'set_role', 'member ' . $member['username'], 'role ' . $role, $now);
    return null;
}

// --- Rendering: member text in, escaped HTML out ---
// Posts may use BBCode (optional per post), smilies and the older plain-text forms (``` code
// fences and "> " quotes). A small stack parser emits only tags from fixed lists, closes every
// tag it opens in order, and escapes all member text, so no member HTML reaches a page.

const BOARD_SMILIES = [':-)' => '🙂', ':)' => '🙂', ':-(' => '🙁', ':(' => '🙁', ':-D' => '😀', ':D' => '😀', ';-)' => '😉', ';)' => '😉', ':-P' => '😛', ':P' => '😛', ':p' => '😛',
    ':-O' => '😮', ':O' => '😮', ':o' => '😮', ':|' => '😐', ":'(" => '😢', 'xD' => '😆', 'XD' => '😆', '<3' => '❤️'];
const BOARD_EMOJI = ['smile' => '😄', 'grin' => '😁', 'joy' => '😂', 'wink' => '😉', 'blush' => '😊', 'heart_eyes' => '😍', 'thinking' => '🤔', 'cool' => '😎', 'sweat_smile' => '😅',
    'cry' => '😢', 'angry' => '😠', 'scream' => '😱', 'thumbsup' => '👍', 'thumbsdown' => '👎', 'clap' => '👏', 'pray' => '🙏', 'wave' => '👋', 'muscle' => '💪', 'eyes' => '👀',
    'fire' => '🔥', 'rocket' => '🚀', 'tada' => '🎉', '100' => '💯', 'heart' => '❤️', 'star' => '⭐', 'sparkles' => '✨', 'check' => '✅', 'x' => '❌', 'warning' => '⚠️', 'bulb' => '💡',
    'lock' => '🔒', 'key' => '🔑', 'pick' => '⛏️', 'gem' => '💎', 'moneybag' => '💰', 'chart' => '📈', 'computer' => '💻', 'gear' => '⚙️', 'link' => '🔗', 'coffee' => '☕'];
const BOARD_BB_INLINE = ['b' => 'strong', 'i' => 'em', 'u' => 'u', 's' => 'del'];
const BOARD_BB_COLORS = ['red', 'orange', 'yellow', 'green', 'teal', 'blue', 'purple', 'gray'];
const BOARD_BB_SIZES = ['small', 'large'];
const BOARD_BB_BLOCKS = ['quote', 'code', 'list', '*', 'spoiler', 'center'];
const BOARD_BB_DEPTH = 20;
const BOARD_LINK_ATTRS = ' rel="nofollow ugc noopener noreferrer"';

// The plain-text forms become BBCode: ``` fences → [code], runs of "> " lines → [quote].
function boardLegacyMarkup(string $body): string {
    $parts = preg_split('/^```[^\n]*\n(.*?)^```[^\n]*$/ms', $body, -1, PREG_SPLIT_DELIM_CAPTURE);
    $out = '';
    foreach ($parts as $i => $part) $out .= $i % 2 === 1 ? '[code]' . $part . '[/code]' : boardLegacyQuotes($part);
    return $out;
}

function boardLegacyQuotes(string $text): string {
    $lines = explode("\n", $text);
    $out = []; $run = [];
    $flush = function () use (&$out, &$run) {
        if (!$run) return;
        $inner = array_map(fn($l) => preg_replace('/^> ?/', '', $l), $run);
        $name = preg_match('/\A([A-Za-z0-9][A-Za-z0-9_-]{2,23}) wrote:\z/', $inner[0], $m) ? $m[1] : null;
        if ($name) array_shift($inner);
        $out[] = ($name ? "[quote=$name]" : '[quote]') . boardLegacyQuotes(implode("\n", $inner)) . '[/quote]';
        $run = [];
    };
    foreach ($lines as $line) {
        if (str_starts_with($line, '>')) { $run[] = $line; continue; }
        $flush(); $out[] = $line;
    }
    $flush();
    return implode("\n", $out);
}

function boardSmilies(string $raw): string {
    static $pattern = null;
    $pattern ??= '/(?<=^|\s)(' . implode('|', array_map(fn($s) => preg_quote($s, '/'), array_keys(BOARD_SMILIES))) . ')(?=$|\s|[.,!?])/u';
    $raw = (string)preg_replace_callback($pattern, fn($m) => BOARD_SMILIES[$m[1]], $raw);
    return (string)preg_replace_callback('/:([a-z0-9_]{1,20}):/', fn($m) => BOARD_EMOJI[$m[1]] ?? $m[0], $raw);
}

function boardSafeUrl(?string $url): ?string {
    $url = trim((string)$url);
    return preg_match('#\Ahttps?://[^\s"\'<>\[\]\x00-\x1f]{1,2000}\z#i', $url) ? $url : null;
}

// One run of member text between tags: links (optional), smilies (optional), escaping, line breaks.
// Escaped text in, with @name turned into a profile link when that member exists.
function boardLinkMentions(string $escaped): string {
    static $known = [];
    return (string)preg_replace_callback(BOARD_MENTION_PATTERN, function (array $m) use (&$known): string {
        $key = strtolower($m[1]);
        if (!array_key_exists($key, $known)) $known[$key] = boardProfileByName($m[1])['username'] ?? null;
        return $known[$key] === null ? $m[0] : '<a class="bb-mention" href="/community/board/?u=' . rawurlencode($known[$key]) . '">@' . authH($known[$key]) . '</a>';
    }, $escaped);
}

function boardBbText(string $raw, array $opts): string {
    $parts = $opts['autolink'] ? preg_split('#(\bhttps?://[^\s<>"\'\[\]\x00-\x1f]+)#i', $raw, -1, PREG_SPLIT_DELIM_CAPTURE) : [$raw];
    $plain = fn(string $t) => boardLinkMentions(nl2br(authH($opts['smilies'] ? boardSmilies($t) : $t), false));
    $out = '';
    foreach ($parts as $k => $part) {
        if ($k % 2 === 0) { $out .= $plain($part); continue; }
        $url = rtrim($part, '.,;:!?)');
        $out .= '<a href="' . authH($url) . '"' . BOARD_LINK_ATTRS . '>' . authH($url) . '</a>' . $plain(substr($part, strlen($url)));
    }
    return $out;
}

// $opts['allow']: only these tags (others stay as text) and no ``` or "> " forms — used for signatures.
function boardRender(string $body, array $opts = []): string {
    $opts += ['bbcode' => true, 'smilies' => true, 'autolink' => true, 'allow' => null];
    if (!$opts['bbcode']) $body = str_replace('[', "\x02", $body);   // shown as "[" but never parsed
    $tokens = preg_split('/(\[\/?(?:[a-z]+|\*)(?:=[^\]\n]{1,300})?\])/i', $opts['allow'] === null ? boardLegacyMarkup($body) : $body, -1, PREG_SPLIT_DELIM_CAPTURE);
    $count = count($tokens);
    // A line break right next to a block tag is layout, not content.
    for ($i = 1; $i < $count; $i += 2) {
        if (!preg_match('/\A\[\/?([a-z]+|\*)/i', $tokens[$i], $m) || !in_array(strtolower($m[1]), BOARD_BB_BLOCKS, true)) continue;
        $tokens[$i - 1] = preg_replace('/\n\z/', '', $tokens[$i - 1]);
        if ($i + 1 < $count) $tokens[$i + 1] = preg_replace('/\A\n/', '', $tokens[$i + 1]);
    }
    $html = ''; $stack = [];
    $closeTo = function (int $depth) use (&$stack, &$html) { while (count($stack) > $depth) $html .= array_pop($stack)[1]; };
    $find = function (string $name) use (&$stack): ?int { for ($d = count($stack) - 1; $d >= 0; $d--) if ($stack[$d][0] === $name) return $d; return null; };
    // Raw text up to the matching close tag (for [code], [url] and [img]); null if it never closes.
    $rawUntil = function (int &$i, string $name) use ($tokens, $count): ?string {
        $raw = '';
        for ($j = $i + 1; $j < $count; $j++) {
            if ($j % 2 === 1 && strtolower($tokens[$j]) === "[/$name]") { $i = $j; return $raw; }
            $raw .= $tokens[$j];
        }
        return null;
    };
    for ($i = 0; $i < $count; $i++) {
        $tok = $tokens[$i];
        if ($i % 2 === 0) { $html .= boardBbText($tok, $opts); continue; }
        preg_match('/\A\[(\/?)([a-z]+|\*)(?:=([^\]]*))?\]\z/i', $tok, $m);
        $closing = $m[1] === '/'; $name = strtolower($m[2]); $arg = $m[3] ?? null;
        if ($opts['allow'] !== null && !in_array($name, $opts['allow'], true)) { $html .= authH($tok); continue; }
        if ($closing) {
            $d = $find($name);
            if ($d === null) { $html .= authH($tok); continue; }
            $closeTo($d);
            continue;
        }
        if (in_array($name, ['code', 'url', 'img'], true)) {
            $start = $i;
            $raw = $rawUntil($i, $name);
            if ($raw === null) { $i = $start; $html .= authH($tok); continue; }
            if ($name === 'code') { $html .= '<pre><code>' . authH(trim($raw, "\n")) . '</code></pre>'; continue; }
            $url = boardSafeUrl($name === 'url' && $arg !== null ? $arg : $raw);
            if (!$url || ($name === 'img' && stripos($url, 'https://') !== 0)) { $i = $start; $html .= authH($tok); continue; }
            if ($name === 'img') {
                // Click to show: the reader's browser contacts the image host only when they ask
                // (board-editor.mjs); without JavaScript the card is a plain link to the image.
                $html .= '<span class="bb-img" data-src="' . authH($url) . '"><a class="bb-img-card" href="' . authH($url) . '"' . BOARD_LINK_ATTRS . ' target="_blank">'
                    . '<span aria-hidden="true">🖼</span> Image from <strong>' . authH((string)parse_url($url, PHP_URL_HOST)) . '</strong></a></span>';
                continue;
            }
            $label = $arg !== null && trim($raw) !== '' ? $raw : $url;
            $html .= '<a href="' . authH($url) . '"' . BOARD_LINK_ATTRS . '>' . authH($label) . '</a>';
            continue;
        }
        if (count($stack) >= BOARD_BB_DEPTH) { $html .= authH($tok); continue; }
        $open = null;
        if (isset(BOARD_BB_INLINE[$name]) && $arg === null) $open = ['<' . BOARD_BB_INLINE[$name] . '>', '</' . BOARD_BB_INLINE[$name] . '>'];
        elseif ($name === 'color' && in_array(strtolower((string)$arg), BOARD_BB_COLORS, true)) $open = ['<span class="bb-color-' . strtolower($arg) . '">', '</span>'];
        elseif ($name === 'size' && in_array(strtolower((string)$arg), BOARD_BB_SIZES, true)) $open = ['<span class="bb-size-' . strtolower($arg) . '">', '</span>'];
        elseif ($name === 'center' && $arg === null) $open = ['<div class="bb-center">', '</div>'];
        elseif ($name === 'quote') $open = ['<blockquote>' . ($arg !== null && trim($arg) !== '' ? '<cite>' . authH(mb_substr(trim($arg, " \"'"), 0, 40)) . ' wrote:</cite>' : ''), '</blockquote>'];
        elseif ($name === 'spoiler') $open = ['<details class="bb-spoiler"><summary>' . authH($arg !== null && trim($arg) !== '' ? mb_substr(trim($arg), 0, 60) : 'Spoiler') . '</summary><div>', '</div></details>'];
        elseif ($name === 'list' && in_array($arg, [null, '1', 'a'], true)) $open = $arg === null ? ['<ul>', '</ul>'] : ['<ol type="' . $arg . '">', '</ol>'];
        elseif ($name === '*' && $arg === null) {
            $top = $stack ? end($stack)[0] : null;
            if ($top === '*') { $closeTo(count($stack) - 1); $top = $stack ? end($stack)[0] : null; }
            if ($top === 'list') $open = ['<li>', '</li>'];
        }
        if (!$open) { $html .= authH($tok); continue; }
        $html .= $open[0];
        $stack[] = [$name, $open[1]];
    }
    $closeTo(0);
    return str_replace("\x02", '&#91;', $html);
}

// Post text without markup, for excerpts and descriptions.
function boardPlain(string $body): string {
    $text = preg_replace(['/^```[^\n]*$/m', '/^> ?/m', '/\[\/?(?:[a-z]+|\*)(?:=[^\]\n]{0,300})?\]/i'], '', $body);
    return boardSmilies((string)$text);
}

function boardExcerpt(string $body, int $length = 160): string {
    $flat = trim(preg_replace('/\s+/u', ' ', boardPlain($body)) ?? '');
    return mb_strlen($flat) > $length ? mb_substr($flat, 0, $length - 1) . '…' : $flat;
}

function boardWhen(int $t): string { return gmdate('Y-m-d H:i', $t) . ' UTC'; }
