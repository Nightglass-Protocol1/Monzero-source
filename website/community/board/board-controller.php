<?php
declare(strict_types=1);
if (!defined("MONZERO_BOARD_CONTROLLER")) { http_response_code(404); exit; }
// Community discussion board (lib/board.php), laid out like a classic bulletin board.
// Reading is public; posting uses faucet accounts.
require dirname(__DIR__, 2) . '/lib/board.php';
require dirname(__DIR__, 2) . '/lib/site-header.php';

if (isset($_GET['avatar'])) {
    $file = boardAvatarFile(is_string($_GET['avatar']) ? $_GET['avatar'] : null);
    $path = $file ? boardAvatarDir() . '/' . $file : '';
    if (!$file || !is_file($path) || is_link($path)) { http_response_code(404); exit; }
    header('Content-Type: ' . (str_ends_with($file, '.webp') ? 'image/webp' : 'image/png'));
    header('Content-Length: ' . filesize($path));
    header('Cache-Control: public, max-age=31536000, immutable');
    header('X-Content-Type-Options: nosniff');
    header("Content-Security-Policy: default-src 'none'; sandbox");
    readfile($path);
    exit;
}

const BOARD_MESSAGES = [
    'profile' => 'Username saved. One last step: introduce yourself below, then you can post in every forum.',
    'moderated' => 'Done. The action is in the moderation log.',
    'avatar' => 'Your new avatar is saved.',
    'avatar-removed' => 'Your avatar was removed.',
    'signature' => 'Your signature is saved.',
    'pm-sent' => 'Message sent.',
    'pm-left' => 'You left the conversation. If everyone leaves, it is deleted from the server.',
    'pm-reported' => 'Thank you. Forum staff will see that one message and nothing else from the conversation.',
    'pm-settings' => 'Message settings saved.',
    'notifications-read' => 'All notifications marked as read.',
    'blocked' => 'Blocked. That member can no longer message you.',
    'unblocked' => 'Unblocked.',
    'edited' => 'Your post was updated.',
    'deleted' => 'Your post was deleted.',
    'reported' => 'Thank you. A moderator will review the post.',
];
const BOARD_ICONS = [
    'forum' => '<path d="M4 4h16a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H9l-5 4v-4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z"/>',
    'topic' => '<path d="M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zm2 4v2h10V8zm0 4v2h7v-2z"/>',
    'sticky' => '<path d="M14 2l8 8-3 1-4 4 1 5-2 2-4-5-5 5-1-1 5-5-5-4 2-2 5 1 4-4z"/>',
    'locked' => '<path d="M7 10V7a5 5 0 0 1 10 0v3h1a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2zm2 0h6V7a3 3 0 0 0-6 0z"/>',
    'code' => '<path d="M8.6 16.6 4 12l4.6-4.6L7.2 6 1.2 12l6 6zm6.8 0L20 12l-4.6-4.6L16.8 6l6 6-6 6z"/>',
    'award' => '<path d="M12 2a7 7 0 0 1 4.2 12.6L17.5 22 12 19l-5.5 3 1.3-7.4A7 7 0 0 1 12 2zm0 3.5-1.2 2.5-2.7.4 2 1.9-.5 2.7 2.4-1.3 2.4 1.3-.5-2.7 2-1.9-2.7-.4z"/>',
    'shield' => '<path d="M12 2 4 5v6c0 5 3.4 9.4 8 11 4.6-1.6 8-6 8-11V5zm-1 13-3.5-3.5 1.4-1.4 2.1 2.1 4.6-4.6 1.4 1.4z"/>',
    'bell' => '<path d="M12 22a2.5 2.5 0 0 0 2.5-2.5h-5A2.5 2.5 0 0 0 12 22zm7-6V11a7 7 0 0 0-5.5-6.8V3.5a1.5 1.5 0 0 0-3 0v.7A7 7 0 0 0 5 11v5l-2 2v1h18v-1z"/>',
    'mail' => '<path d="M3 5h18a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1zm9 7.2L4.4 7H4v.6l8 5.4 8-5.4V7h-.4z"/>',
    'gear' => '<path d="M19.4 13a7.6 7.6 0 0 0 0-2l2.1-1.6-2-3.5-2.5 1a7.4 7.4 0 0 0-1.7-1L15 3h-4l-.4 2.6a7.4 7.4 0 0 0-1.7 1l-2.5-1-2 3.5L6.6 11a7.6 7.6 0 0 0 0 2l-2.1 1.6 2 3.5 2.5-1a7.4 7.4 0 0 0 1.7 1L11 21h4l.4-2.6a7.4 7.4 0 0 0 1.7-1l2.5 1 2-3.5zM13 15.5a3.5 3.5 0 1 1 0-7 3.5 3.5 0 0 1 0 7z"/>',
    'home' => '<path d="M12 3l9 8h-3v9h-5v-6h-2v6H6v-9H3z"/>',
    'clock' => '<path d="M12 2a10 10 0 1 1 0 20 10 10 0 0 1 0-20zm1 5h-2v6l5 3 1-1.7-4-2.3z"/>',
    'rules' => '<path d="M6 2h9l5 5v13a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2zm8 1.5V8h4.5zM8 12v2h8v-2zm0 4v2h8v-2z"/>',
    'user' => '<path d="M12 12a5 5 0 1 0 0-10 5 5 0 0 0 0 10zm0 2c-5 0-9 2.5-9 6v2h18v-2c0-3.5-4-6-9-6z"/>',
    'users' => '<path d="M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8zm8 0a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM9 13c-4 0-7 2-7 5v2h14v-2c0-3-3-5-7-5zm8 0c-.6 0-1.2 0-1.7.2A6 6 0 0 1 18 18v2h4v-2c0-2.8-2.2-5-5-5z"/>',
    'chart' => '<path d="M4 20h16v2H2V3h2zm3-3V9h3v8zm5 0V5h3v12zm5 0v-6h3v6z"/>',
];

// A small inline icon for links and headings (decorative; the text next to it is the label).
function glyph(string $name): string { return '<svg class="bb-glyph" viewBox="0 0 24 24" aria-hidden="true" focusable="false">' . BOARD_ICONS[$name] . '</svg>'; }

function icon(string $name, string $label): string {
    return '<span class="bb-icon bb-icon-' . $name . '" title="' . authH($label) . '"><svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">' . BOARD_ICONS[$name] . '</svg><span class="visually-hidden">' . authH($label) . '</span></span>';
}

function boardPage(string $title, string $body, ?array $account, array $crumbs = [], int $status = 200, string $canonical = '', string $description = ''): never {
    http_response_code($status);
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('Referrer-Policy: same-origin');
    header('X-Frame-Options: DENY');
    header('X-Content-Type-Options: nosniff');
    $description = $description !== '' ? $description : 'The Monzero community forum: questions, ideas and conversation about the project.';
    $trail = '<a href="/community/board/">Board index</a>';
    foreach ($crumbs as [$href, $label]) $trail .= ' <span aria-hidden="true">›</span> ' . ($href ? '<a href="' . authH($href) . '">' . authH($label) . '</a>' : '<span>' . authH($label) . '</span>');
    echo '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>' . authH($title) . ' · Monzero forum</title>'
        . '<meta name="description" content="' . authH($description) . '">' . ($canonical !== '' ? '<link rel="canonical" href="https://monzero.org' . authH($canonical) . '">' : '<meta name="robots" content="noindex">')
        . '<link rel="icon" href="/assets/monzero-emblem-20261026-96.webp"><link rel="stylesheet" href="/styles.css?v=20261026-brand"><link rel="stylesheet" href="/theme.css?v=20261026-brand"><link rel="stylesheet" href="/community/community.css?v=20261026-brand">'
        . '<link rel="stylesheet" href="/faucet/faucet.css?v=20261020-sitewide"><link rel="stylesheet" href="/site-nav.css?v=20261020-sitewide"><link rel="stylesheet" href="/site-shell.css?v=20261026-brand"><link rel="stylesheet" href="/community/board/board.css?v=20261026-brand">'
        . '<noscript><link rel="stylesheet" href="/no-script.css?v=20261020-sitewide"></noscript></head>'
        . '<body class="site-shell bb"><a class="skip-link" href="#main">Skip to content</a>' . siteHeader('/community/board/')
        . '<main id="main" tabindex="-1">' . navbar($account) . '<div class="bb-crumbs" role="navigation" aria-label="Breadcrumb">' . $trail . '</div><h1>' . authH($title) . '</h1>' . $body . '</main>'
        . '<footer><p><a href="/community/">Monzero community</a> · <a href="/community/board/?view=rules">Forum rules</a> · <a href="/community/board/?view=team">The team</a> · <a href="/community/board/?view=bbcode">BBCode guide</a> · <a href="/privacy/">Privacy Policy</a> · <a href="/terms/">Website Terms</a> · All times are UTC</p></footer>'
        . '<script src="/site-nav.js?v=20261020-sitewide" defer></script><script type="module" src="/community/board/board-editor.mjs?v=20261022-likes"></script></body></html>';
    exit;
}

function navbar(?array $account): string {
    $left = '<a href="/community/board/">' . glyph('home') . 'Board index</a><a href="/community/board/?view=active">' . glyph('clock') . 'Active topics</a><a href="/community/board/?view=badges">' . glyph('award') . 'Achievements</a><a href="/community/board/?view=rules">' . glyph('rules') . 'Rules</a>'
        . (($staff = staffProfile($account)) ? '<a class="bb-mcp-link" href="/community/board/?view=mcp">' . glyph('shield') . 'Moderator CP' . (($open = count(boardOpenReports()) + count(boardPmOpenReports())) ? ' <span class="bb-count-pill">' . $open . '</span>' : '') . '</a>' : '')
        . '<form class="bb-search" action="/community/board/" method="get" role="search"><input type="hidden" name="view" value="search"><label class="visually-hidden" for="bb-q">Search the forum</label>'
        . '<input id="bb-q" type="search" name="q" placeholder="Search…" maxlength="100" value="' . authH(($_GET['view'] ?? '') === 'search' && is_string($_GET['q'] ?? null) ? $_GET['q'] : '') . '"><button type="submit">Search</button></form>';
    if (!$account) $right = '<a href="/faucet/?return=board">' . glyph('user') . 'Login</a><a class="bb-register" href="/community/board/?view=signup">Register</a>';
    else {
        $logout = '<form method="post" action="/faucet/?do=logout"><input type="hidden" name="csrf" value="' . authH($account['member']['csrf']) . '"><button type="submit">Logout</button></form>';
        if (boardMemberBlock($account['member'])) $right = '<a href="/faucet/?return=board">Finish signing in</a>' . $logout;
        elseif (!$account['profile']) $right = '<a href="/community/board/?view=profile">Choose a username</a>' . $logout;
        else $right = '<span class="bb-me"><span class="visually-hidden">Signed in as </span>' . avatar($account['profile']['username'], 'small', $account['profile']['avatar']) . userLink($account['profile']['username'], $account['profile']['role']) . '</span>'
            . '<a class="bb-pm-nav" href="/community/board/?view=notifications" title="Notifications">' . glyph('bell') . '<span class="visually-hidden">Notifications</span>' . (($alerts = boardNotificationsUnread((int)$account['member']['id'])) ? '<span class="bb-count-pill bb-pm-pill">' . $alerts . '<span class="visually-hidden"> unread</span></span>' : '') . '</a>'
            . '<a class="bb-pm-nav" href="/community/board/?view=inbox" title="Private messages">' . glyph('mail') . '<span class="visually-hidden">Private messages</span>' . (($unread = boardPmUnread((int)$account['member']['id'])) ? '<span class="bb-count-pill bb-pm-pill">' . $unread . '<span class="visually-hidden"> unread</span></span>' : '') . '</a>'
            . '<a href="/community/board/?view=settings" title="Profile settings">' . glyph('gear') . '<span class="visually-hidden">Profile settings</span></a>' . $logout;
    }
    return '<div class="bb-navbar"><div class="bb-navbar-left">' . $left . '</div><div class="bb-navbar-right">' . $right . '</div></div>';
}

function notice(string $text, bool $error = false): string { return '<p class="notice' . ($error ? ' error" role="alert"' : '" role="status"') . '>' . authH($text) . '</p>'; }
function field(string $name): string { $v = $_POST[$name] ?? ''; return is_string($v) ? $v : ''; }
function intParam(string $name, int $default = 0): int { $v = filter_var($_GET[$name] ?? null, FILTER_VALIDATE_INT, ['options' => ['min_range' => 1]]); return $v === false || $v === null ? $default : $v; }
function csrfField(array $account): string { return '<input type="hidden" name="csrf" value="' . authH($account['member']['csrf']) . '">'; }
function threadUrl(int $id, int $page = 1, ?int $post = null): string { return '/community/board/?t=' . $id . ($page > 1 ? '&page=' . $page : '') . ($post ? '#post-' . $post : ''); }
function postUrl(int $id): string { return '/community/board/?post=' . $id; }
function userLink(string $username, string $role = 'member'): string { return '<a class="bb-user bb-role-' . authH($role) . '" href="/community/board/?u=' . rawurlencode($username) . '">' . authH($username) . '</a>'; }
function roleBadge(string $role): string { return $role === 'member' ? '' : '<span class="bb-staff bb-staff-' . authH($role) . '">' . authH(boardRoleName($role)) . '</span>'; }
function levelBadge(int $posts): string { [$level, $name] = boardLevel($posts); return '<span class="bb-level bb-level-' . $level . '" title="Level ' . $level . ' of 10">Lv ' . $level . ' · ' . authH($name) . '</span>'; }
function badgeIcons(array $badges, int $max = 5): string {
    if (!$badges) return '';
    $icons = array_map(fn($b) => '<span class="bb-badge-icon" title="' . authH(BOARD_BADGES[$b][1] . ': ' . BOARD_BADGES[$b][2]) . '">' . BOARD_BADGES[$b][0] . '</span>', array_slice($badges, 0, $max));
    $more = count($badges) > $max ? '<span class="bb-badge-more">+' . (count($badges) - $max) . '</span>' : '';
    return '<span class="bb-badge-row" aria-label="' . count($badges) . ' badges">' . implode('', $icons) . $more . '</span>';
}
// "Liked by alice, bob and 3 others" (plain text, also sent to the like button script).
function likeSummary(array $like): string {
    $names = $like['names']; $others = $like['count'] - count($names);
    if (!$like['count']) return '';
    if ($others > 0) return 'Liked by ' . implode(', ', $names) . ' and ' . plural($others, 'other', 'others');
    return 'Liked by ' . (count($names) > 1 ? implode(', ', array_slice($names, 0, -1)) . ' and ' . end($names) : $names[0]);
}

function likeButton(?array $account, array $post, array $like, bool $can): string {
    $count = '<span class="bb-like-count">' . ($like['count'] ? number_format($like['count']) : '') . '</span>';
    if (!$can) return $like['count'] ? '<span class="bb-like bb-like-static" title="Likes">👍 ' . $count . '</span>' : '';
    return '<form method="post" action="/community/board/?do=like&amp;post=' . (int)$post['id'] . '" class="bb-inline bb-like-form">' . csrfField($account)
        . '<button type="submit" class="bb-like" aria-pressed="' . ($like['mine'] ? 'true' : 'false') . '" title="' . ($like['mine'] ? 'Remove your like' : 'Like this post') . '">👍 <span class="bb-like-label">' . ($like['mine'] ? 'Liked' : 'Like') . '</span> ' . $count . '</button></form>';
}

function staffProfile(?array $account): ?array { return $account && boardStaff($account['member'], $account['profile'], time()) ? $account['profile'] : null; }
function modButton(array $account, string $action, array $fields, string $label, string $class = 'bb-btn-small'): string {
    $hidden = '';
    foreach ($fields as $name => $value) $hidden .= '<input type="hidden" name="' . authH($name) . '" value="' . authH((string)$value) . '">';
    return '<form method="post" action="/community/board/?do=mod&amp;action=' . authH($action) . '" class="bb-inline">' . csrfField($account) . $hidden . '<button type="submit" class="' . $class . '">' . authH($label) . '</button></form>';
}
// An uploaded avatar when the member has one, otherwise their coloured initial.
function avatar(string $username, string $size = '', ?string $file = null): string {
    if ($file = boardAvatarFile($file)) {
        $px = $size === 'small' ? 28 : 72;
        return '<img class="bb-avatar bb-avatar-img' . ($size ? ' bb-avatar-' . $size : '') . '" src="/community/board/?avatar=' . $file . '" alt="" width="' . $px . '" height="' . $px . '" loading="lazy" decoding="async">';
    }
    return '<span class="bb-avatar bb-avatar-' . (crc32(strtolower($username)) % 8) . ($size ? ' bb-avatar-' . $size : '') . '" aria-hidden="true">' . authH(mb_strtoupper(mb_substr($username, 0, 1))) . '</span>';
}
// "Today, 11:49" / "Yesterday, 18:02" for recent posts, the full date otherwise (all UTC).
function when(int $t): string {
    $day = intdiv($t, 86400); $today = intdiv(time(), 86400);
    $label = match ($today - $day) { 0 => 'Today, ' . gmdate('H:i', $t), 1 => 'Yesterday, ' . gmdate('H:i', $t), default => gmdate('D M j, Y H:i', $t) };
    return '<time datetime="' . gmdate('c', $t) . '" title="' . gmdate('D M j, Y H:i', $t) . ' UTC">' . $label . '</time>';
}
function plural(int $n, string $one, string $many): string { return number_format($n) . ' ' . ($n === 1 ? $one : $many); }
function canPost(?array $account): bool { return $account && !boardMemberBlock($account['member']); }

function lastPost(?int $postId, bool $withTitle = false): string {
    $p = boardPostBrief($postId);
    if (!$p) return '<span class="bb-muted">No posts</span>';
    return '<div class="bb-lastpost">' . avatar($p['username'], 'small', $p['avatar']) . '<div>' . ($withTitle ? '<a class="bb-last-title" href="' . authH(postUrl((int)$p['id'])) . '">' . authH(boardExcerpt($p['title'], 36)) . '</a>' : '')
        . '<span>by ' . userLink($p['username']) . ' <a class="bb-jump" href="' . authH(postUrl((int)$p['id'])) . '" title="Go to last post" aria-label="Go to last post">»</a></span><span class="bb-muted">' . when((int)$p['created_at']) . '</span></div></div>';
}

// phpBB-style page links: Page 2 of 7 · 1 … 3 4 5 … 7 · Next
function pager(string $base, int $page, int $total, int $per): string {
    $pages = max(1, (int)ceil($total / $per));
    if ($pages === 1) return '';
    $href = fn(int $n) => authH($base . ($n > 1 ? '&page=' . $n : ''));
    $links = $page > 1 ? ['<a href="' . $href($page - 1) . '" rel="prev">Previous</a>'] : [];
    $last = 0;
    for ($n = 1; $n <= $pages; $n++) {
        if ($n !== 1 && $n !== $pages && abs($n - $page) > 2) continue;
        if ($last && $n > $last + 1) $links[] = '<span class="bb-muted">…</span>';
        $links[] = $n === $page ? '<strong aria-current="page">' . $n . '</strong>' : '<a href="' . $href($n) . '">' . $n . '</a>';
        $last = $n;
    }
    if ($page < $pages) $links[] = '<a href="' . $href($page + 1) . '" rel="next">Next</a>';
    return '<div class="bb-pager" role="navigation" aria-label="Pages"><span class="bb-muted">Page ' . $page . ' of ' . $pages . '</span>' . implode('', $links) . '</div>';
}

// Page links next to a topic title when the topic spans several pages.
function miniPager(int $threadId, int $posts): string {
    $pages = (int)ceil($posts / BOARD_POSTS_PER_PAGE);
    if ($pages < 2) return '';
    $links = [];
    foreach (array_unique([1, 2, $pages - 1, $pages]) as $n) if ($n >= 1) $links[] = '<a href="' . authH(threadUrl($threadId, $n)) . '">' . $n . '</a>';
    return ' <span class="bb-minipager">[ Page ' . implode(' ', $links) . ' ]</span>';
}

const BOARD_ALL_ON = ['bbcode' => true, 'smilies' => true, 'autolink' => true];

// The message box. board-editor.mjs adds the formatting toolbar and emoji picker; without
// JavaScript the box still works and the guide explains the codes.
function editor(string $name, string $label, string $value = '', int $rows = 10, array $opts = BOARD_ALL_ON): string {
    $check = fn(string $field, string $text, bool $on) => '<label class="bb-check"><input type="checkbox" name="' . $field . '" value="1"' . ($on ? '' : ' checked') . '> ' . $text . '</label>';
    return '<div class="bb-editor" data-emoji="' . authH(json_encode(BOARD_EMOJI, JSON_UNESCAPED_UNICODE)) . '"><label for="bb-' . $name . '">' . $label . '</label><div class="bb-toolbar" hidden></div>'
        . '<textarea id="bb-' . $name . '" name="' . $name . '" rows="' . $rows . '" maxlength="' . BOARD_BODY_MAX . '" required>' . authH($value) . '</textarea>'
        . '<details class="bb-options"' . (in_array(false, $opts, true) ? ' open' : '') . '><summary>Posting options</summary>' . $check('disable_bbcode', 'Disable BBCode', $opts['bbcode'])
        . $check('disable_smilies', 'Disable smilies', $opts['smilies']) . $check('disable_autolink', 'Do not automatically parse URLs', $opts['autolink']) . '</details>'
        . '<p class="note">Use BBCode such as <code>[b]bold[/b]</code>, <code>[url=https://…]link[/url]</code> or <code>[quote]…[/quote]</code>; see the <a href="/community/board/?view=bbcode" target="_blank" rel="noopener">BBCode guide</a>. '
        . 'Smilies like <code>:)</code> and <code>:fire:</code> become emoji; <code>@username</code> notifies a member. Never post a seed phrase, private key or password.</p></div>';
}

function formButtons(): string { return '<div class="bb-formbuttons"><button type="submit" name="preview" value="1" class="bb-secondary">Preview</button><button type="submit">Submit</button></div>'; }

function previewPanel(string $body, array $opts): string {
    $body = boardClean($body);
    if ($body === '' || mb_strlen($body) > BOARD_BODY_MAX) return '';
    return '<section class="bb-panel bb-preview" aria-labelledby="bb-preview-title"><h2 id="bb-preview-title">Preview</h2><div class="board-body">' . boardRender($body, $opts) . '</div></section>';
}

function rules(): string {
    return '<ol class="rules"><li>Be respectful. No harassment, hate speech, threats or personal information about others.</li>'
        . '<li>No spam, scams, impersonation, referral links or price promotion. Do not ask anyone to send coins.</li>'
        . '<li>Never share a seed phrase, private key, password or wallet file. Monzero staff will never ask for them.</li>'
        . '<li>Report security vulnerabilities privately through the <a href="/security/">security page</a>, not on the forum.</li>'
        . '<li>Post in the forum that fits your topic. Moderators may hide posts, lock or move topics, or suspend accounts that break these rules.</li>'
        . '<li>New accounts can post links after 3 days. Posts are public and stay attached to your username.</li></ol>';
}

function topicTable(array $threads, string $heading, bool $showForum = false): string {
    if (!$threads) return '';
    $rows = '';
    foreach ($threads as $t) {
        $kind = $t['locked'] ? 'locked' : ($t['pinned'] ? 'sticky' : 'topic');
        $label = $t['locked'] ? 'Locked topic' : ($t['pinned'] ? 'Sticky topic' : 'Topic');
        $rows .= '<tr><td class="bb-ico">' . icon($kind, $label) . '</td><td class="bb-main">' . ($t['pinned'] ? '<span class="bb-tag">Sticky</span> ' : '')
            . '<a class="bb-title" href="' . authH(threadUrl((int)$t['id'])) . '">' . authH($t['title']) . '</a>' . miniPager((int)$t['id'], (int)$t['total_posts'])
            . '<div class="bb-sub">by ' . userLink($t['username']) . ' » ' . when((int)$t['created_at']) . ($showForum ? ' in <a href="/community/board/?c=' . authH($t['category_slug']) . '">' . authH($t['category_name']) . '</a>' : '') . '</div>'
            . '<div class="bb-sub bb-narrow">' . plural((int)$t['replies'], 'reply', 'replies') . ' · ' . plural((int)$t['views'], 'view', 'views') . '</div></td>'
            . '<td class="bb-num">' . number_format((int)$t['replies']) . '</td><td class="bb-num">' . number_format((int)$t['views']) . '</td><td class="bb-last">' . lastPost($t['last_post_id'] ? (int)$t['last_post_id'] : null) . '</td></tr>';
    }
    return '<div class="bb-table-wrap"><table class="bb-table bb-topics"><thead><tr><th class="bb-ico"><span class="visually-hidden">Status</span></th><th>' . authH($heading) . '</th><th class="bb-num">Replies</th><th class="bb-num">Views</th><th class="bb-last">Last post</th></tr></thead><tbody>' . $rows . '</tbody></table></div>';
}

try { boardDb(); $member = faucetCurrent(); } catch (Throwable $e) {
    error_log('Monzero board: storage unavailable');
    boardPage('Forum unavailable', notice('The community forum is temporarily unavailable.', true), null, [], 503);
}
if ($member && !boardMemberBlock($member)) {
    try { registrationApply((int)$member['id']); } catch (Throwable) { error_log('Registration preferences pending'); }
}
$account = $member ? ['member' => $member, 'profile' => boardProfile((int)$member['id'])] : null;
$now = time();
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if (!in_array($method, ['GET', 'POST'], true)) { header('Allow: GET, POST'); http_response_code(405); exit; }

// --- Forms that post ---
function forumCrumb(array $category): array { return ['/community/board/?c=' . $category['slug'], $category['name']]; }

function newThreadForm(array $account, array $category, string $error = '', string $title = '', string $body = '', array $opts = BOARD_ALL_ON, bool $preview = false): never {
    $intro = $category['slug'] === BOARD_WELCOME_SLUG && $account['profile'] && !$account['profile']['introduced_at'];
    if ($intro && $title === '' && $body === '') {
        $title = 'Hello, I am ' . $account['profile']['username'];
        $body = "How I found Monzero: \n\nWhat I am interested in (mining, privacy, development, trading…): \n\nSomething about me: \n";
    }
    $block = boardPostBlock($account['member'], $account['profile'], 'thread', time(), $category['slug']);
    $form = $block ? notice($block) . (!$account['profile'] ? '<p><a href="/community/board/?view=profile">Choose a username</a></p>' : '')
        : '<form method="post" action="/community/board/?do=thread&amp;c=' . authH($category['slug']) . '" class="stack bb-form bb-panel">' . csrfField($account)
            . '<label>Subject<input name="title" maxlength="' . BOARD_TITLE_MAX . '" value="' . authH($title) . '" required></label>' . editor('body', 'Message', $body, 14, $opts)
            . formButtons() . '</form>';
    $flash = isset($_GET['msg'], BOARD_MESSAGES[$_GET['msg']]) && !$error ? notice(BOARD_MESSAGES[$_GET['msg']]) : '';
    $guide = $intro ? '<div class="bb-panel bb-intro-help"><h2>' . glyph('award') . 'Introduce yourself</h2><p>Every new member starts here. Tell the community a little about yourself (at least ' . BOARD_INTRO_MIN
        . ' characters). Once your introduction is posted you can post in every forum, and you earn the 👋 <strong>Said hello</strong> badge.</p><p class="bb-muted">Do not share your real name, location or anything that identifies you unless you want to.</p></div>' : '';
    boardPage($intro ? 'Introduce yourself' : 'Post a new topic', $flash . ($error ? notice($error, true) : '') . $guide . ($preview ? previewPanel($body, $opts) : '') . '<p class="bb-muted">Forum: ' . authH($category['name']) . '</p>' . $form, $account, [forumCrumb($category), [null, 'Post a new topic']], $error ? 400 : 200);
}

function replyForm(array $account, array $thread, string $error = '', string $body = '', array $opts = BOARD_ALL_ON, bool $preview = false): never {
    $block = boardPostBlock($account['member'], $account['profile'], 'reply', time(), $thread['category_slug']);
    $form = $block ? notice($block) : '<form method="post" action="/community/board/?do=reply&amp;t=' . (int)$thread['id'] . '" class="stack bb-form bb-panel">' . csrfField($account)
        . '<label>Subject<input value="Re: ' . authH($thread['title']) . '" disabled></label>' . editor('body', 'Message', $body, 14, $opts) . formButtons() . '</form>';
    boardPage('Post a reply', ($error ? notice($error, true) : '') . ($preview ? previewPanel($body, $opts) : '') . $form . '<p><a href="' . authH(threadUrl((int)$thread['id'])) . '">Return to the topic</a></p>', $account,
        [['/community/board/?c=' . $thread['category_slug'], $thread['category_name']], [threadUrl((int)$thread['id']), $thread['title']], [null, 'Post a reply']], $error ? 400 : 200);
}

function profileForm(array $account, string $error = '', string $value = ''): never {
    boardPage('Choose a username', ($error ? notice($error, true) : '')
        . '<div class="bb-panel"><p>Your username is shown on everything you post. It cannot be changed later, and your email address is never shown.</p>'
        . '<form method="post" action="/community/board/?do=profile" class="stack">' . csrfField($account)
        . '<label>Username<input name="username" minlength="3" maxlength="24" pattern="[A-Za-z0-9][A-Za-z0-9_\-]{2,23}" value="' . authH($value) . '" autocomplete="nickname" required></label>'
        . '<button type="submit">Save username</button></form></div><h2>Forum rules</h2>' . rules(), $account, [[null, 'Choose a username']], $error ? 400 : 200);
}

function editForm(array $account, array $post, string $error = '', ?string $body = null, ?array $opts = null, bool $preview = false): never {
    $opts ??= boardPostOptions($post);
    boardPage('Edit post', ($error ? notice($error, true) : '') . ($preview ? previewPanel($body ?? $post['body'], $opts) : '') . '<form method="post" action="/community/board/?do=edit&amp;post=' . (int)$post['id'] . '" class="stack bb-form bb-panel">' . csrfField($account)
        . editor('body', 'Message', $body ?? $post['body'], 14, $opts) . formButtons() . '</form>'
        . '<form method="post" action="/community/board/?do=delete&amp;post=' . (int)$post['id'] . '" class="board-delete">' . csrfField($account)
        . '<button type="submit" class="secondary">Delete this post</button></form><p><a href="' . authH(postUrl((int)$post['id'])) . '">Return to the topic</a></p>', $account,
        [[threadUrl((int)$post['thread_id']), $post['title']], [null, 'Edit post']], $error ? 400 : 200);
}

function reportForm(array $account, array $post, string $error = ''): never {
    boardPage('Report this post', ($error ? notice($error, true) : '') . '<blockquote class="board-quote">' . authH(boardExcerpt($post['body'], 300)) . '</blockquote>'
        . '<form method="post" action="/community/board/?do=report&amp;post=' . (int)$post['id'] . '" class="stack bb-panel">' . csrfField($account)
        . '<label>What is wrong with this post?<textarea name="reason" rows="3" maxlength="500" required></textarea></label><button type="submit">Submit report</button></form>', $account,
        [[threadUrl((int)$post['thread_id']), $post['title']], [null, 'Report']], $error ? 400 : 200);
}

function settingsPage(array $account, string $error = '', ?string $signature = null, bool $preview = false): never {
    $profile = boardProfile((int)$account['member']['id']);
    if (!$profile) boardPage('Profile settings', notice('Choose a username first.') . '<p><a href="/community/board/?view=profile">Choose a username</a></p>', $account);
    $signature ??= $profile['signature'] !== '' ? $profile['signature'] : registrationSignatureDraft((int)$profile['member_id']);
    [$level] = boardLevel(boardMemberPostCount((int)$profile['member_id']));
    $flash = isset($_GET['msg'], BOARD_MESSAGES[$_GET['msg']]) && !$error ? notice(BOARD_MESSAGES[$_GET['msg']]) : '';
    $hasAvatar = boardAvatarFile($profile['avatar']) !== null;
    $avatarForm = '<section class="bb-panel"><h2>Avatar</h2><div class="bb-avatar-edit">' . avatar($profile['username'], '', $profile['avatar']) . '<div>'
        . '<form method="post" action="/community/board/?do=avatar" enctype="multipart/form-data" class="stack">' . csrfField($account)
        . '<label>Upload a picture<input type="file" name="avatar" accept="image/jpeg,image/png,image/webp,image/gif" required></label>'
        . '<p class="note">JPEG, PNG, WebP or GIF, up to 2 MB. It is cropped to a square and redrawn at ' . BOARD_AVATAR_SIZE . ' × ' . BOARD_AVATAR_SIZE . ' pixels; hidden details such as camera or location data are removed. Do not use a photo of yourself unless you want to.</p>'
        . '<button type="submit">Upload avatar</button></form>'
        . ($hasAvatar ? '<form method="post" action="/community/board/?do=avatar_remove">' . csrfField($account) . '<button type="submit" class="bb-secondary">Remove avatar</button></form>' : '') . '</div></div></section>';
    $locked = $level < BOARD_SIGNATURE_LEVEL;
    $sigForm = '<section class="bb-panel"><h2>Signature</h2>' . ($locked ? notice('Signatures unlock at level ' . BOARD_SIGNATURE_LEVEL . ' (' . BOARD_LEVELS[BOARD_SIGNATURE_LEVEL - 1][0] . ' posts). Keep taking part!') : '')
        . ($preview && trim($signature) !== '' ? '<div class="bb-preview bb-panel"><h2>Preview</h2><div class="bb-signature">' . boardRenderSignature(boardClean($signature)) . '</div></div>' : '')
        . '<form method="post" action="/community/board/?do=signature" class="stack bb-form">' . csrfField($account)
        . '<label>Shown under your posts<textarea name="signature" rows="4" maxlength="' . BOARD_SIGNATURE_MAX . '"' . ($locked ? ' disabled' : '') . '>' . authH($signature) . '</textarea></label>'
        . '<p class="note">Up to ' . BOARD_SIGNATURE_MAX . ' characters and ' . BOARD_SIGNATURE_LINES . ' lines. Allowed: <code>[b] [i] [u] [s] [color] [url]</code>, at most ' . BOARD_SIGNATURE_LINKS . ' links, and smilies. Leave it empty to remove your signature.</p>'
        . ($locked ? '' : '<div class="bb-formbuttons"><button type="submit" name="preview" value="1" class="bb-secondary">Preview</button><button type="submit">Save signature</button></div>') . '</form></section>';
    $blocked = boardBlockList((int)$profile['member_id']);
    $pmForm = '<section class="bb-panel" id="messages"><h2>Private messages</h2><form method="post" action="/community/board/?do=pm_prefs" class="stack">' . csrfField($account)
        . '<fieldset class="bb-radios"><legend>Who can start a conversation with you</legend><label class="bb-check"><input type="radio" name="allow_pms" value="1"' . ($profile['allow_pms'] ? ' checked' : '') . '> Any member</label>'
        . '<label class="bb-check"><input type="radio" name="allow_pms" value="0"' . ($profile['allow_pms'] ? '' : ' checked') . '> Only forum staff</label></fieldset>'
        . '<label class="bb-check"><input type="checkbox" name="notify_pm" value="1"' . ($profile['notify_pm'] ? ' checked' : '') . '> Email me when I get a new private message</label>'
        . '<p class="note">One email per conversation until you read it, naming only the sender. The message itself is never emailed.</p>'
        . '<label class="bb-check"><input type="checkbox" name="notify_mentions_email" value="1"' . ($profile['notify_mentions_email'] ? ' checked' : '') . '> Email me when someone mentions (@' . authH($profile['username']) . ') or quotes me</label>'
        . '<p class="note">You always see mentions and quotes under the 🔔 bell. Emails are off unless you tick this; you get one until you check your notifications.</p><button type="submit" class="bb-secondary">Save</button></form>'
        . '<h3>Blocked members</h3>' . ($blocked ? '<ul class="bb-blocklist">' . implode('', array_map(fn($b) => '<li>' . authH($b['username']) . ' <form method="post" action="/community/board/?do=unblock" class="bb-inline">' . csrfField($account)
            . '<input type="hidden" name="username" value="' . authH($b['username']) . '"><button type="submit" class="bb-btn-small">Unblock</button></form></li>', $blocked)) . '</ul>' : '<p class="bb-muted">Nobody.</p>')
        . '<form method="post" action="/community/board/?do=block" class="bb-modform">' . csrfField($account) . '<label>Block a member<input name="username" maxlength="24" required></label><button type="submit" class="bb-btn-small">Block</button></form>'
        . '<p class="note">Blocked members cannot message you or reply in your conversations. Forum staff cannot be blocked.</p></section>';
    boardPage('Profile settings', $flash . ($error ? notice($error, true) : '') . $avatarForm . $sigForm . $pmForm . '<p><a href="/community/board/?u=' . rawurlencode($profile['username']) . '">View your profile</a></p>',
        $account, [['/community/board/?u=' . rawurlencode($profile['username']), $profile['username']], [null, 'Profile settings']], $error ? 400 : 200);
}

const BOARD_PM_NOTICE = 'Private messages are only visible to the members in the conversation. They are not end-to-end encrypted: they are stored on the monzero.org server. Never share a seed phrase, private key or password, and never send coins to someone because of a message.';

function composePage(array $account, string $error = '', string $to = '', string $subject = '', string $body = '', array $opts = BOARD_ALL_ON, bool $preview = false): never {
    $block = boardPmBlock($account['member'], $account['profile'], time());
    $form = $block ? notice($block) : '<form method="post" action="/community/board/?do=pm_start" class="stack bb-form bb-panel">' . csrfField($account)
        . '<label>To<input name="to" value="' . authH($to) . '" placeholder="username, another_username" maxlength="200" required></label><p class="note">Up to ' . BOARD_PM_MAX_RECIPIENTS . ' usernames, separated by commas.</p>'
        . '<label>Subject<input name="subject" value="' . authH($subject) . '" maxlength="' . BOARD_PM_SUBJECT_MAX . '" required></label>' . editor('body', 'Message', $body, 10, $opts) . formButtons() . '</form>';
    boardPage('New message', ($error ? notice($error, true) : '') . '<p class="notice bb-pm-notice">' . authH(BOARD_PM_NOTICE) . '</p>' . ($preview ? previewPanel($body, $opts) : '') . $form,
        $account, [['/community/board/?view=inbox', 'Messages'], [null, 'New message']], $error ? 400 : 200);
}

function conversationPage(array $account, int $id, string $error = '', string $body = '', array $opts = BOARD_ALL_ON, bool $preview = false): never {
    $me = (int)$account['member']['id'];
    $now = time();
    $conversation = boardPmConversation($me, $id);
    if (!$conversation) boardPage('Not found', notice('That conversation does not exist, or you have left it.', true) . '<p><a href="/community/board/?view=inbox">Your messages</a></p>', $account, [['/community/board/?view=inbox', 'Messages']], 404);
    $people = boardPmParticipants($id);
    $messages = boardPmMessages($me, $id, $now);
    $items = '';
    foreach ($messages as $i => $m) {
        $mine = (int)$m['member_id'] === $me;
        $body_ = $m['status'] === 'visible' ? '<div class="board-body">' . boardRender($m['body'], boardPostOptions($m)) . '</div>' : '<p class="board-removed">This message was removed by a moderator.</p>';
        $tools = !$mine && $m['status'] === 'visible' ? '<a class="bb-btn-small" href="/community/board/?view=pm_report&amp;message=' . (int)$m['id'] . '">Report</a>' : '';
        $unread = !$mine && (int)$m['id'] > (int)$conversation['last_read_id'];
        $items .= '<article class="bb-message' . ($mine ? ' bb-mine' : '') . '" id="message-' . (int)$m['id'] . '">' . avatar($m['username'], 'small', $m['avatar']) . '<div class="bb-message-body"><div class="bb-posthead"><div>'
            . userLink($m['username'], $m['role']) . ' <span class="bb-sub">' . when((int)$m['created_at']) . '</span>' . ($unread ? ' <span class="bb-tag bb-tag-ok">New</span>' : '') . '</div><div class="bb-posttools">' . $tools . '</div></div>' . $body_ . '</div></article>';
    }
    $names = implode(', ', array_map(fn($p) => userLink($p['username'], $p['role']) . ($p['left_at'] ? ' <span class="bb-muted">(left)</span>' : ''), array_filter($people, fn($p) => (int)$p['member_id'] !== $me)));
    $others = array_values(array_filter($people, fn($p) => (int)$p['member_id'] !== $me && !$p['left_at']));
    $reply = !$others ? notice('Everyone else has left this conversation.')
        : '<form method="post" action="/community/board/?do=pm_reply&amp;pm=' . $id . '" class="stack bb-form">' . csrfField($account) . editor('body', 'Message', $body, 6, $opts) . formButtons() . '</form>';
    $leave = '<form method="post" action="/community/board/?do=pm_leave&amp;pm=' . $id . '" class="bb-inline">' . csrfField($account) . '<button type="submit" class="bb-btn-small bb-btn-mod">Leave conversation</button></form>';
    $blockOne = count($people) === 2 && $others && boardRoleRank($others[0]) < 1 ? '<form method="post" action="/community/board/?do=block" class="bb-inline">' . csrfField($account)
        . '<input type="hidden" name="username" value="' . authH($others[0]['username']) . '"><input type="hidden" name="back" value="pm"><button type="submit" class="bb-btn-small bb-btn-mod">Block ' . authH($others[0]['username']) . '</button></form>' : '';
    $flash = isset($_GET['msg'], BOARD_MESSAGES[$_GET['msg']]) && !$error ? notice(BOARD_MESSAGES[$_GET['msg']]) : '';
    boardPage($conversation['subject'], $flash . ($error ? notice($error, true) : '') . '<div class="bb-actionbar"><span class="bb-muted">With ' . $names . '</span><span class="bb-pm-actions">' . $blockOne . $leave . '</span></div>'
        . '<div class="bb-messages">' . $items . '</div>' . ($preview ? previewPanel($body, $opts) : '') . '<section class="bb-panel bb-quickreply" id="reply"><h2>Reply</h2>' . $reply . '</section>'
        . '<p class="note">' . authH(BOARD_PM_NOTICE) . '</p>', $account, [['/community/board/?view=inbox', 'Messages'], [null, $conversation['subject']]], $error ? 400 : 200);
}

if (isset($_GET['unsubscribe_mentions'])) {
    $who = boardUnsubscribeMember($_GET['unsubscribe_mentions'], 'mentions');
    if (!$who) boardPage('Unsubscribe', notice('This unsubscribe link is not valid. You can turn these emails off in your forum profile settings.', true), $account, [[null, 'Unsubscribe']], 400);
    if ($method === 'POST') {
        boardDb()->prepare('UPDATE profiles SET notify_mentions_email = 0 WHERE member_id = ?')->execute([$who]);
        boardPage('Unsubscribed', notice('Done. You will no longer get emails when someone mentions or quotes you.'), $account, [[null, 'Unsubscribe']]);
    }
    boardPage('Unsubscribe', '<div class="bb-panel"><p>Stop emails about mentions and quotes for <strong>' . authH(boardProfile($who)['username']) . '</strong>?</p><form method="post" action="/community/board/?unsubscribe_mentions=' . authH($_GET['unsubscribe_mentions']) . '">'
        . '<button type="submit">Stop these emails</button></form></div>', $account, [[null, 'Unsubscribe']]);
}

if (isset($_GET['unsubscribe'])) {
    $who = boardUnsubscribeMember($_GET['unsubscribe']);
    if (!$who) boardPage('Unsubscribe', notice('This unsubscribe link is not valid. You can turn message emails off in your forum profile settings.', true), $account, [[null, 'Unsubscribe']], 400);
    if ($method === 'POST') {
        boardDb()->prepare('UPDATE profiles SET notify_pm = 0 WHERE member_id = ?')->execute([$who]);
        boardPage('Unsubscribed', notice('Done. You will no longer get emails about new private messages. You can turn them back on in your forum profile settings.'), $account, [[null, 'Unsubscribe']]);
    }
    boardPage('Unsubscribe', '<div class="bb-panel"><p>Stop emails about new private messages for <strong>' . authH(boardProfile($who)['username']) . '</strong>?</p><form method="post" action="/community/board/?unsubscribe=' . authH($_GET['unsubscribe']) . '">'
        . '<button type="submit">Stop message emails</button></form></div>', $account, [[null, 'Unsubscribe']]);
}

if ($method === 'POST') {
    if (!authSameOrigin()) boardPage('Not allowed', notice('Request origin not allowed.', true), $account, [], 403);
    if (!$_POST && (int)($_SERVER['CONTENT_LENGTH'] ?? 0) > 0) boardPage('Too large', notice('That upload is too large. Avatars can be 2 MB at most.', true) . '<p><a href="/community/board/?view=settings">Back to profile settings</a></p>', $account, [], 413);
    if (!$account || !hash_equals($account['member']['csrf'], field('csrf'))) boardPage('Form expired', notice('Your form expired or you were signed out. Sign in, reload the page and try again.', true), $account, [], 403);
    $do = is_string($_GET['do'] ?? null) ? $_GET['do'] : '';
    $postId = intParam('post');
    switch ($do) {
        case 'mod':
            $staff = staffProfile($account);
            if (!$staff) boardPage('Not allowed', notice('Only forum staff can moderate.', true), $account, [], 403);
            $action = is_string($_GET['action'] ?? null) ? $_GET['action'] : '';
            $error = boardModerate($staff, $action, ['post' => field('post'), 'thread' => field('thread'), 'category' => field('category'), 'username' => field('username'),
                'days' => field('days'), 'reason' => field('reason'), 'role' => field('role'), 'message' => field('message')], $now);
            if ($error) boardPage('Moderation', notice($error, true) . '<p><a href="/community/board/?view=mcp">Moderator Control Panel</a></p>', $account, [[null, 'Moderation']], 400);
            // Return to the page the action came from (only forum pages), with a confirmation.
            $back = field('back');
            if (!preg_match('#\A/community/board/\?[A-Za-z0-9=&_%.\-]*\z#', $back)) $back = '/community/board/?view=mcp';
            header('Location: ' . $back . '&msg=moderated', true, 303); exit;
        case 'avatar':
            if ($error = boardSetAvatar($account['member'], is_array($_FILES['avatar'] ?? null) ? $_FILES['avatar'] : [], $now)) settingsPage($account, $error);
            header('Location: /community/board/?view=settings&msg=avatar', true, 303); exit;
        case 'avatar_remove':
            boardRemoveAvatar((int)$account['member']['id']);
            header('Location: /community/board/?view=settings&msg=avatar-removed', true, 303); exit;
        case 'signature':
            if (field('preview') !== '') settingsPage($account, '', field('signature'), true);
            if ($error = boardSetSignature($account['member'], field('signature'), $now)) settingsPage($account, $error, field('signature'));
            registrationDb()->prepare("UPDATE registration_drafts SET signature = '' WHERE member_id = ?")->execute([$account['member']['id']]);
            header('Location: /community/board/?view=settings&msg=signature', true, 303); exit;
        case 'pm_start':
            $opts = boardOptions($_POST);
            if (field('preview') !== '') composePage($account, '', field('to'), field('subject'), field('body'), $opts, true);
            [$id, $error] = boardPmStart($account['member'], explode(',', field('to')), field('subject'), field('body'), $opts, $now);
            if ($error) composePage($account, $error, field('to'), field('subject'), field('body'), $opts);
            header('Location: /community/board/?pm=' . $id . '&msg=pm-sent', true, 303); exit;
        case 'pm_reply':
            $id = intParam('pm');
            $opts = boardOptions($_POST);
            if (field('preview') !== '') conversationPage($account, $id, '', field('body'), $opts, true);
            [$message, $error] = boardPmReply($account['member'], $id, field('body'), $opts, $now);
            if ($error) conversationPage($account, $id, $error, field('body'), $opts);
            header('Location: /community/board/?pm=' . $id . '#message-' . $message, true, 303); exit;
        case 'pm_leave':
            boardPmLeave((int)$account['member']['id'], intParam('pm'), $now);
            header('Location: /community/board/?view=inbox&msg=pm-left', true, 303); exit;
        case 'pm_report':
            $messageId = intParam('message');
            if ($error = boardPmReport($account['member'], $messageId, field('reason'), $now)) boardPage('Report a message', notice($error, true) . '<p><a href="/community/board/?view=inbox">Your messages</a></p>', $account, [], 400);
            $row = boardOne('SELECT conversation_id FROM messages WHERE id = ?', [$messageId]);
            header('Location: /community/board/?pm=' . (int)$row['conversation_id'] . '&msg=pm-reported', true, 303); exit;
        case 'notifications_read':
            boardMarkNotificationsRead((int)$account['member']['id'], $now);
            header('Location: /community/board/?view=notifications&msg=notifications-read', true, 303); exit;
        case 'pm_prefs':
            boardDb()->prepare('UPDATE profiles SET allow_pms = ?, notify_pm = ?, notify_mentions_email = ? WHERE member_id = ?')
                ->execute([field('allow_pms') === '0' ? 0 : 1, field('notify_pm') === '1' ? 1 : 0, field('notify_mentions_email') === '1' ? 1 : 0, $account['member']['id']]);
            header('Location: /community/board/?view=settings&msg=pm-settings#messages', true, 303); exit;
        case 'block':
        case 'unblock':
            if ($error = boardSetBlock((int)$account['member']['id'], field('username'), $do === 'block', $now)) settingsPage($account, $error);
            header('Location: ' . (field('back') === 'pm' ? '/community/board/?view=inbox&msg=blocked' : '/community/board/?view=settings&msg=' . ($do === 'block' ? 'blocked' : 'unblocked') . '#messages'), true, 303); exit;
        case 'like':
            $post = boardPost($postId);
            [$liked, $count, $error] = $post ? boardToggleLike($account['member'], $post, $now) : [false, 0, 'That post does not exist.'];
            if (str_contains((string)($_SERVER['HTTP_ACCEPT'] ?? ''), 'application/json')) {
                header('Content-Type: application/json'); header('Cache-Control: no-store'); header('X-Content-Type-Options: nosniff');
                if ($error) { http_response_code(400); echo json_encode(['error' => $error]); exit; }
                $like = boardLikesFor([$post['id']], (int)$account['member']['id'])[(int)$post['id']] ?? ['count' => 0, 'mine' => false, 'names' => []];
                echo json_encode(['liked' => $liked, 'count' => $count, 'summary' => likeSummary($like)], JSON_UNESCAPED_UNICODE);
                exit;
            }
            if ($error) boardPage('Like', notice($error, true) . ($post ? '<p><a href="' . authH(postUrl((int)$post['id'])) . '">Back to the post</a></p>' : ''), $account, [], 400);
            header('Location: ' . threadUrl((int)$post['thread_id'], boardPostPage($post), (int)$post['id']), true, 303); exit;
        case 'preview':
            $body = boardClean(field('body'));
            header('Content-Type: text/html; charset=utf-8');
            header('Cache-Control: no-store');
            header('X-Content-Type-Options: nosniff');
            if (mb_strlen($body) > BOARD_BODY_MAX) { http_response_code(400); echo '<p class="bb-muted">The message is too long to preview.</p>'; exit; }
            echo $body === '' ? '<p class="bb-muted">Nothing to preview yet.</p>' : boardRender($body, boardOptions($_POST));
            exit;
        case 'profile':
            if ($error = boardCreateProfile($account['member'], field('username'), $now)) profileForm($account, $error, field('username'));
            header('Location: /community/board/?view=new&c=' . BOARD_WELCOME_SLUG . '&msg=profile', true, 303); exit;
        case 'thread':
            $category = boardCategory(is_string($_GET['c'] ?? null) ? $_GET['c'] : '');
            if (!$category || $category['status'] === 'archived') boardPage('Not found', notice('That forum does not exist.', true), $account, [], 404);
            $opts = boardOptions($_POST);
            if (field('preview') !== '') newThreadForm($account, $category, '', field('title'), field('body'), $opts, true);
            [$thread, $error] = boardCreateThread($account['member'], $category, field('title'), field('body'), $now, $opts);
            if ($error) newThreadForm($account, $category, $error, field('title'), field('body'), $opts);
            header('Location: ' . threadUrl($thread), true, 303); exit;
        case 'reply':
            $thread = boardThread(intParam('t'));
            if (!$thread || $thread['status'] !== 'visible') boardPage('Not found', notice('That topic does not exist.', true), $account, [], 404);
            $opts = boardOptions($_POST);
            if (field('preview') !== '') replyForm($account, $thread, '', field('body'), $opts, true);
            [$post, $error] = boardReply($account['member'], $thread, field('body'), $now, $opts);
            if ($error) replyForm($account, $thread, $error, field('body'), $opts);
            $row = boardPost($post);
            header('Location: ' . threadUrl((int)$thread['id'], boardPostPage($row), $post), true, 303); exit;
        case 'edit':
        case 'delete':
        case 'report':
            $post = boardPost($postId);
            if (!$post || $post['thread_status'] !== 'visible') boardPage('Not found', notice('That post does not exist.', true), $account, [], 404);
            if ($do === 'edit' && field('preview') !== '') {
                if ($error = boardOwnPostBlock($account['member'], $post, $now)) boardPage('Edit post', notice($error, true), $account, [], 403);
                editForm($account, $post, '', field('body'), boardOptions($_POST), true);
            }
            if ($do === 'edit' && ($error = boardEdit($account['member'], $post, field('body'), $now, boardOptions($_POST)))) editForm($account, $post, $error, field('body'), boardOptions($_POST));
            if ($do === 'delete' && ($error = boardDelete($account['member'], $post, $now))) editForm($account, $post, $error);
            if ($do === 'report' && ($error = boardReport($account['member'], $post, field('reason'), $now))) reportForm($account, $post, $error);
            $message = ['edit' => 'edited', 'delete' => 'deleted', 'report' => 'reported'][$do];
            header('Location: /community/board/?' . http_build_query(['t' => $post['thread_id'], 'page' => boardPostPage($post), 'msg' => $message]) . '#post-' . $post['id'], true, 303); exit;
    }
    boardPage('Not found', notice('Unknown action.', true), $account, [], 400);
}

// --- Pages ---
$flash = isset($_GET['msg'], BOARD_MESSAGES[$_GET['msg']]) ? notice(BOARD_MESSAGES[$_GET['msg']]) : '';
if ($account && $account['profile'] && !boardMemberBlock($account['member'])) {
    boardAwardBadges((int)$account['member']['id'], $now);   // time-based badges (anniversary) are earned without posting
    foreach (boardTakeNewBadges((int)$account['member']['id']) as $b) $flash .= '<p class="notice bb-badge-notice" role="status"><span class="bb-badge-big">' . BOARD_BADGES[$b][0] . '</span> You earned the <strong>' . authH(BOARD_BADGES[$b][1])
        . '</strong> badge: ' . authH(BOARD_BADGES[$b][2]) . ' <a href="/community/board/?u=' . rawurlencode($account['profile']['username']) . '#badges">See your badges</a></p>';
    if (!$account['profile']['introduced_at'] && !(($_GET['view'] ?? '') === 'new' && ($_GET['c'] ?? '') === BOARD_WELCOME_SLUG))
        $flash .= '<p class="notice bb-welcome-notice">👋 Welcome! Please <a href="/community/board/?view=new&amp;c=' . BOARD_WELCOME_SLUG . '">introduce yourself in New user welcome</a>. You can reply to other introductions now, and post everywhere once yours is up.</p>';
}
$view = is_string($_GET['view'] ?? null) ? $_GET['view'] : '';
$signIn = '<p class="bb-cta"><a href="/faucet/?return=board">Login</a> or <a href="/community/board/?view=signup">register</a> to post. Accounts are shared with the XMZ faucet.</p>';

if ($view === 'bbcode') {
    $examples = [
        ['[b]bold[/b], [i]italic[/i], [u]underline[/u], [s]strike[/s]', 'Text styles'],
        ['[color=teal]teal[/color] [color=red]red[/color] [color=orange]orange[/color] [color=blue]blue[/color]', 'Colours: ' . implode(', ', BOARD_BB_COLORS)],
        ['[size=small]small[/size] and [size=large]large[/size]', 'Sizes: small or large'],
        ['[url=https://monzero.org/]The Monzero website[/url]', 'Link with your own text'],
        ['[url]https://monzero.org/download/[/url]', 'Link showing the address (plain addresses are linked automatically)'],
        ["[quote=satoshi_fan]Is one core enough?[/quote]\nYes, for a start.", 'Quote someone (the Quote button fills this in)'],
        ["[code]\n\"threads\": 11,\n\"huge-pages\": true\n[/code]", 'Code or configuration, shown exactly as typed'],
        ["[list]\n[*]First\n[*]Second\n[/list]\n[list=1]\n[*]Step one\n[*]Step two\n[/list]", 'Bulleted or numbered lists'],
        ['[spoiler=Answer]Hidden until clicked[/spoiler]', 'Spoiler'],
        ['Thanks @xmz_dev for the help!', 'Mention a member with @username: it links to their profile and notifies them (not inside code or quotes)'],
        ['[center]Centred text[/center]', 'Centre text'],
        ['[img]https://monzero.org/assets/monzero-share-20261026-1200x630.jpg[/img]', 'Images (https only). Readers click “Show image” or turn on “Always show images”, so other sites only see readers who choose to load them'],
    ];
    $rows = '';
    foreach ($examples as [$code, $what]) $rows .= '<tr><td><pre class="bb-guide-code">' . authH($code) . '</pre></td><td><div class="board-body">' . boardRender($code) . '</div><div class="bb-sub">' . authH($what) . '</div></td></tr>';
    $smilies = '';
    foreach (array_unique(BOARD_SMILIES) as $emoji) $smilies .= '<li><code>' . authH(implode(' ', array_keys(BOARD_SMILIES, $emoji, true))) . '</code> ' . $emoji . '</li>';
    foreach (BOARD_EMOJI as $code => $emoji) $smilies .= '<li><code>:' . authH($code) . ':</code> ' . $emoji . '</li>';
    boardPage('BBCode guide', '<p class="bb-desc">BBCode lets you format posts. It is optional: tick “Disable BBCode” under Posting options to post exactly what you type. The toolbar above the message box inserts these codes for you.</p>'
        . '<div class="bb-table-wrap"><table class="bb-table bb-guide"><thead><tr><th>You type</th><th>You get</th></tr></thead><tbody>' . $rows . '</tbody></table></div>'
        . '<h2>Smilies and emoji</h2><p class="bb-desc">Typed smilies become emoji unless you tick “Disable smilies”. You can also type or paste any emoji directly.</p><ul class="bb-smilies">' . $smilies . '</ul>',
        $account, [[null, 'BBCode guide']], 200, '/community/board/?view=bbcode');
}

if ($view === 'notifications' || isset($_GET['notification'])) {
    if (!$account || !$account['profile'] || boardMemberBlock($account['member'])) boardPage('Notifications', notice(boardMemberBlock($account['member'] ?? null) ?? 'Choose a username first.') . $signIn, $account, [[null, 'Notifications']]);
    $me = (int)$account['member']['id'];
    if (isset($_GET['notification'])) {
        $postId = boardOpenNotification($me, intParam('notification'), $now);
        $post = $postId ? boardPost($postId) : null;
        if (!$post || $post['thread_status'] !== 'visible') boardPage('Not found', notice('That post is no longer available.', true) . '<p><a href="/community/board/?view=notifications">Your notifications</a></p>', $account, [[null, 'Notifications']], 404);
        header('Location: ' . threadUrl((int)$post['thread_id'], boardPostPage($post), (int)$post['id']), true, 303); exit;
    }
    $items = '';
    foreach (boardNotifications($me) as $n) {
        $gone = $n['post_status'] !== 'visible' || $n['thread_status'] !== 'visible';
        $what = $n['type'] === 'quote' ? 'quoted you' : 'mentioned you';
        $items .= '<li class="bb-notification' . ($n['read_at'] ? '' : ' bb-unread') . '">' . avatar($n['actor'], 'small', $n['actor_avatar']) . '<div><span>' . userLink($n['actor'], $n['actor_role']) . ' ' . $what . ' in '
            . ($gone ? '<em>a post that was removed</em>' : '<a href="/community/board/?notification=' . (int)$n['id'] . '">' . authH($n['title']) . '</a>') . '</span><span class="bb-sub">' . when((int)$n['created_at'])
            . ($n['read_at'] ? '' : ' · <strong class="bb-new">new</strong>') . '</span></div><span class="bb-notif-icon" aria-hidden="true">' . ($n['type'] === 'quote' ? '🗨️' : '@') . '</span></li>';
    }
    $markAll = boardNotificationsUnread($me) ? '<form method="post" action="/community/board/?do=notifications_read" class="bb-inline">' . csrfField($account) . '<button type="submit" class="bb-btn-small">Mark all as read</button></form>' : '';
    boardPage('Notifications', $flash . '<div class="bb-actionbar"><span class="bb-muted">Mentions (@' . authH($account['profile']['username']) . ') and quotes of your posts. Kept for ' . BOARD_NOTIFY_KEEP_DAYS . ' days.</span><span class="bb-pm-actions">' . $markAll
        . '<a class="bb-btn-small" href="/community/board/?view=settings#messages">Email settings</a></span></div>' . ($items ? '<ol class="bb-notifications">' . $items . '</ol>' : '<p class="bb-panel">No notifications yet. When someone mentions you with @' . authH($account['profile']['username']) . ' or quotes your post, it shows up here.</p>'),
        $account, [[null, 'Notifications']]);
}

if (in_array($view, ['inbox', 'compose', 'pm_report'], true) || isset($_GET['pm'])) {
    if (!$account || !$account['profile'] || boardMemberBlock($account['member'])) boardPage('Private messages', notice(boardMemberBlock($account['member'] ?? null) ?? 'Choose a username first.') . $signIn, $account, [[null, 'Messages']]);
    if (isset($_GET['pm'])) conversationPage($account, intParam('pm'));
    if ($view === 'compose') composePage($account, '', is_string($_GET['to'] ?? null) ? mb_substr($_GET['to'], 0, 200) : '');
    if ($view === 'pm_report') {
        $message = boardOne('SELECT * FROM messages WHERE id = ?', [intParam('message')]);
        if (!$message || !boardPmConversation((int)$account['member']['id'], (int)$message['conversation_id'])) boardPage('Not found', notice('That message does not exist.', true), $account, [], 404);
        boardPage('Report a message', '<p class="bb-desc">Forum staff will see this one message and your reason, and nothing else from the conversation. To stop someone messaging you, you can also block them in your profile settings.</p>'
            . '<blockquote class="board-quote">' . authH(boardExcerpt($message['body'], 400)) . '</blockquote><form method="post" action="/community/board/?do=pm_report&amp;message=' . (int)$message['id'] . '" class="stack bb-panel">' . csrfField($account)
            . '<label>What is wrong with this message?<textarea name="reason" rows="3" maxlength="500" required></textarea></label><button type="submit">Send report</button></form>', $account, [['/community/board/?view=inbox', 'Messages'], [null, 'Report']]);
    }
    $rows = '';
    foreach (boardPmInbox((int)$account['member']['id']) as $c) {
        $unread = (int)$c['unread'] > 0;
        $rows .= '<tr class="' . ($unread ? 'bb-unread' : '') . '"><td class="bb-ico">' . icon('topic', $unread ? 'Unread' : 'Read') . '</td><td class="bb-main"><a class="bb-title" href="/community/board/?pm=' . (int)$c['id'] . '">' . authH($c['subject']) . '</a>'
            . ($unread ? ' <span class="bb-tag bb-tag-ok">' . (int)$c['unread'] . ' new</span>' : '') . '<div class="bb-sub">with ' . authH((string)$c['others']) . '</div></td><td class="bb-num">' . (int)$c['messages'] . '</td>'
            . '<td class="bb-last">by ' . authH((string)$c['last_by']) . '<br><span class="bb-muted">' . when((int)$c['last_message_at']) . '</span></td></tr>';
    }
    boardPage('Private messages', $flash . '<div class="bb-actionbar"><a class="bb-button" href="/community/board/?view=compose">' . glyph('mail') . ' New message</a><a class="bb-muted" href="/community/board/?view=settings#messages">Message settings and blocked members</a></div>'
        . ($rows ? '<div class="bb-table-wrap"><table class="bb-table bb-inbox"><thead><tr><th class="bb-ico"><span class="visually-hidden">Status</span></th><th>Conversations</th><th class="bb-num">Messages</th><th class="bb-last">Last message</th></tr></thead><tbody>' . $rows . '</tbody></table></div>'
            : '<p class="bb-panel">No messages yet. Start one from a member’s profile or with “New message”.</p>') . '<p class="note">' . authH(BOARD_PM_NOTICE) . '</p>', $account, [[null, 'Messages']]);
}

if ($view === 'settings') {
    if (!canPost($account)) boardPage('Profile settings', notice(boardMemberBlock($account['member'] ?? null)) . $signIn, $account, [[null, 'Profile settings']]);
    settingsPage($account);
}

if ($view === 'badges') {
    $holders = boardBadgeHolders();
    $mine = $account && $account['profile'] ? array_column(boardBadgesOf((int)$account['member']['id']), 'badge') : [];
    $cards = '';
    foreach (BOARD_BADGES as $key => [$emoji, $name, $how]) {
        $have = in_array($key, $mine, true);
        $cards .= '<li class="bb-badge-card' . ($have ? ' bb-earned' : '') . '"><span class="bb-badge-big">' . $emoji . '</span><div><strong>' . authH($name) . '</strong>' . ($have ? ' <span class="bb-tag bb-tag-ok">Earned</span>' : '')
            . (in_array($key, BOARD_MANUAL_BADGES, true) ? ' <span class="bb-tag">Awarded by the team</span>' : '') . '<p>' . authH($how) . '</p><span class="bb-muted">' . plural($holders[$key] ?? 0, 'member has', 'members have') . ' this badge</span></div></li>';
    }
    $levels = '';
    foreach (BOARD_LEVELS as $i => [$min, $name]) $levels .= '<tr><td class="bb-num">' . ($i + 1) . '</td><td>' . levelBadge($min) . '</td><td class="bb-num">' . number_format($min) . '</td></tr>';
    boardPage('Levels and achievements', '<p class="bb-desc">Every visible post counts towards your level. Badges are earned automatically as you take part; two are awarded by the team. Hidden or deleted posts do not count.</p>'
        . '<h2>Levels</h2><div class="bb-table-wrap bb-levels"><table class="bb-table"><thead><tr><th class="bb-num">Level</th><th>Title</th><th class="bb-num">Posts</th></tr></thead><tbody>' . $levels . '</tbody></table></div>'
        . '<h2>Badges</h2><ul class="bb-badge-grid">' . $cards . '</ul>', $account, [[null, 'Achievements']], 200, '/community/board/?view=badges');
}

if ($view === 'team') {
    $groups = [];
    foreach (boardStaffList() as $m) $groups[$m['role']][] = $m;
    $what = ['administrator' => 'Run the forum: everything moderators can do, longer suspensions, and appointing moderators.',
        'global_moderator' => 'Moderate every forum: pin, move and hide topics, and suspend members for up to 30 days.',
        'moderator' => 'Review reports, hide or restore posts, and lock topics.'];
    $html = '';
    foreach ($what as $role => $text) {
        $names = array_map(fn($m) => '<li>' . avatar($m['username'], 'small', $m['avatar']) . userLink($m['username'], $role) . '</li>', $groups[$role] ?? []);
        $html .= '<section class="bb-panel"><h2>' . roleBadge($role) . '</h2><p class="bb-desc">' . authH($text) . '</p>' . ($names ? '<ul class="bb-team">' . implode('', $names) . '</ul>' : '<p class="bb-muted">Nobody yet.</p>') . '</section>';
    }
    boardPage('The team', '<p class="bb-desc">Staff are members trusted by the project to keep the forum friendly and on topic. Use “Report” on a post to bring it to their attention.</p>' . $html, $account, [[null, 'The team']], 200, '/community/board/?view=team');
}

if ($view === 'mcp') {
    $staff = staffProfile($account);
    if (!$staff) boardPage('Moderator Control Panel', notice('Only forum staff can open the Moderator Control Panel.', true), $account, [[null, 'Moderator CP']], 403);
    $reports = '';
    foreach (boardOpenReports() as $r) {
        $back = '/community/board/?view=mcp';
        $reports .= '<li class="bb-panel"><div class="bb-sub">' . plural((int)$r['reports'], 'open report', 'open reports') . ' · post #' . (int)$r['post_id'] . ' by ' . userLink($r['username']) . ' in <a href="' . authH(postUrl((int)$r['post_id'])) . '">' . authH($r['title']) . '</a>'
            . ($r['status'] === 'hidden' ? ' · <strong>hidden until reviewed</strong>' : '') . '</div><blockquote class="board-quote">' . authH(boardExcerpt($r['body'], 400)) . '</blockquote><p class="bb-sub">Reasons: ' . authH($r['reasons']) . '</p><div class="bb-modtools">'
            . modButton($account, 'hide_post', ['post' => $r['post_id'], 'back' => $back], 'Remove post (uphold reports)') . modButton($account, 'dismiss_reports', ['post' => $r['post_id'], 'back' => $back], 'Keep post (dismiss reports)') . '</div></li>';
    }
    $pmReports = '';
    foreach (boardPmOpenReports() as $r) {
        $pmReports .= '<li class="bb-panel"><div class="bb-sub">' . plural((int)$r['reports'], 'open report', 'open reports') . ' · message by ' . userLink($r['username']) . ' · ' . when((int)$r['created_at']) . '</div>'
            . '<div class="board-body board-quote">' . boardRender($r['body'], boardPostOptions($r)) . '</div><p class="bb-sub">Reasons: ' . authH($r['reasons']) . '</p><div class="bb-modtools">'
            . modButton($account, 'hide_message', ['message' => $r['message_id'], 'back' => '/community/board/?view=mcp'], 'Remove message') . modButton($account, 'dismiss_message_report', ['message' => $r['message_id'], 'back' => '/community/board/?view=mcp'], 'Keep message (dismiss)') . '</div></li>';
    }
    $log = '';
    foreach (boardModLogRecent() as $l) $log .= '<tr><td class="bb-last">' . when((int)$l['at']) . '</td><td>' . authH($l['actor']) . '</td><td>' . authH(str_replace('_', ' ', $l['action'])) . '</td><td>' . authH($l['target']) . ($l['detail'] !== '' ? ' — ' . authH($l['detail']) : '') . '</td></tr>';
    $suspended = '';
    foreach (boardSuspendedMembers($now) as $m) $suspended .= '<li>' . userLink($m['username']) . ' · ' . ($m['suspended_until'] ? 'until ' . when((int)$m['suspended_until']) : 'until lifted') . ' · ' . authH((string)$m['suspend_reason'])
        . (boardCanModerate($staff, 'unsuspend') ? ' ' . modButton($account, 'unsuspend', ['username' => $m['username'], 'back' => '/community/board/?view=mcp'], 'Lift') : '') . '</li>';
    boardPage('Moderator Control Panel', $flash . '<p class="bb-desc">You are a <strong>' . authH(boardRoleName($staff['role'])) . '</strong>. Moderation tools also appear on posts, topics and member profiles. Every action is recorded in the moderation log below.</p>'
        . '<h2>Open reports</h2>' . ($reports ? '<ol class="bb-results">' . $reports . '</ol>' : '<p class="bb-panel">No open reports. 🎉</p>')
        . '<h2>Reported private messages</h2><p class="bb-desc">Only the reported message is shown, never the rest of the conversation.</p>' . ($pmReports ? '<ol class="bb-results">' . $pmReports . '</ol>' : '<p class="bb-panel">None.</p>')
        . '<h2>Suspended members</h2>' . ($suspended ? '<ul class="bb-panel bb-suspended">' . $suspended . '</ul>' : '<p class="bb-panel">Nobody is suspended.</p>')
        . '<h2>Moderation log</h2>' . ($log ? '<div class="bb-table-wrap"><table class="bb-table bb-modlog"><thead><tr><th class="bb-last">When</th><th>Who</th><th>Action</th><th>On</th></tr></thead><tbody>' . $log . '</tbody></table></div>' : '<p class="bb-panel">Nothing yet.</p>'),
        $account, [[null, 'Moderator CP']]);
}

if ($view === 'rules') boardPage('Forum rules', rules(), $account, [[null, 'Rules']], 200, '/community/board/?view=rules');

if ($view === 'active') boardPage('Active topics', topicTable(boardRecentThreads(50), 'Topics', true) ?: '<p>No topics yet.</p>', $account, [[null, 'Active topics']]);

if ($view === 'search') {
    $q = is_string($_GET['q'] ?? null) ? mb_substr(trim($_GET['q']), 0, 100) : '';
    $results = $q !== '' ? boardSearch($q) : [];
    $items = '';
    foreach ($results as $r) $items .= '<li class="bb-panel"><a class="bb-title" href="' . authH(postUrl((int)$r['id'])) . '">' . authH($r['title']) . '</a><div class="bb-sub">by ' . userLink($r['username']) . ' » ' . when((int)$r['created_at']) . ' in ' . authH($r['category_name']) . '</div><p>' . authH(boardExcerpt($r['body'], 240)) . '</p></li>';
    boardPage('Search', ($q === '' ? '<p>Type at least one word of two or more letters.</p>' : '<p>' . plural(count($results), 'match', 'matches') . ' for “' . authH($q) . '”' . (count($results) === 50 ? ' (showing the newest 50)' : '') . '.</p>')
        . ($items ? '<ol class="bb-results">' . $items . '</ol>' : ''), $account, [[null, 'Search']]);
}

if ($view === 'profile') {
    if (!$account || boardMemberBlock($account['member'])) boardPage('Choose a username', notice(boardMemberBlock($account['member'] ?? null)) . $signIn, $account, [[null, 'Choose a username']]);
    if ($account['profile']) { header('Location: /community/board/?u=' . rawurlencode($account['profile']['username']), true, 303); exit; }
    profileForm($account);
}

if ($view === 'new') {
    $category = boardCategory(is_string($_GET['c'] ?? null) ? $_GET['c'] : '');
    if (!$category || $category['status'] === 'archived') boardPage('Not found', notice('That forum does not exist.', true), $account, [], 404);
    if (!canPost($account)) boardPage('Post a new topic', notice(boardMemberBlock($account['member'] ?? null)) . $signIn, $account, [forumCrumb($category), [null, 'Post a new topic']]);
    newThreadForm($account, $category);
}

if ($view === 'reply') {
    $thread = boardThread(intParam('t'));
    if (!$thread || $thread['status'] !== 'visible' || $thread['category_status'] === 'archived') boardPage('Not found', notice('That topic does not exist.', true), $account, [], 404);
    if (!canPost($account)) boardPage('Post a reply', notice(boardMemberBlock($account['member'] ?? null)) . $signIn, $account, [[threadUrl((int)$thread['id']), $thread['title']]]);
    if ($thread['locked']) boardPage('Post a reply', notice('This topic is locked. No new replies can be posted.'), $account, [[threadUrl((int)$thread['id']), $thread['title']]], 403);
    $quoted = boardPost(intParam('quote'));
    replyForm($account, $thread, '', $quoted && $quoted['status'] === 'visible' && (int)$quoted['thread_id'] === (int)$thread['id'] ? boardQuote($quoted) : '');
}

if ($view === 'edit' || $view === 'report') {
    $post = boardPost(intParam('post'));
    if (!$post || $post['thread_status'] !== 'visible') boardPage('Not found', notice('That post does not exist.', true), $account, [], 404);
    if (!$account) boardPage('Login', notice('Login to continue.') . $signIn, null);
    if ($view === 'edit') {
        if ($error = boardOwnPostBlock($account['member'], $post, $now)) boardPage('Edit post', notice($error, true), $account, [], 403);
        editForm($account, $post);
    }
    if ($error = boardMemberBlock($account['member'])) boardPage('Report this post', notice($error, true), $account, [], 403);
    reportForm($account, $post);
}

if (isset($_GET['post'])) {
    $post = boardPost(intParam('post'));
    if (!$post || $post['thread_status'] !== 'visible') boardPage('Not found', notice('That post does not exist.', true), $account, [], 404);
    header('Location: ' . threadUrl((int)$post['thread_id'], boardPostPage($post), (int)$post['id']), true, 303); exit;
}

if (isset($_GET['u'])) {
    $profile = is_string($_GET['u']) ? boardProfileByName($_GET['u']) : null;
    if (!$profile) boardPage('Not found', notice('No member has that username.', true), $account, [], 404);
    $count = boardMemberPostCount((int)$profile['member_id']);
    [$level, $levelName, $levelMin, $nextMin] = boardLevel($count);
    $earned = array_column(boardBadgesOf((int)$profile['member_id']), 'awarded_at', 'badge');
    $badgeList = '';
    foreach (BOARD_BADGES as $key => [$emoji, $name, $how]) {
        $have = isset($earned[$key]);
        $badgeList .= '<li class="bb-badge-card' . ($have ? ' bb-earned' : '') . '"><span class="bb-badge-big">' . $emoji . '</span><div><strong>' . authH($name) . '</strong><p>' . authH($how) . '</p>'
            . '<span class="bb-muted">' . ($have ? 'Earned ' . when((int)$earned[$key]) : 'Not earned yet') . '</span></div></li>';
    }
    $progress = $nextMin === null ? '<p class="bb-muted">Highest level reached.</p>'
        : '<progress max="' . ($nextMin - $levelMin) . '" value="' . ($count - $levelMin) . '">' . ($count - $levelMin) . ' of ' . ($nextMin - $levelMin) . '</progress><p class="bb-muted">' . plural($nextMin - $count, 'more post', 'more posts') . ' to level ' . ($level + 1) . ' (' . authH(BOARD_LEVELS[$level][1]) . ')</p>';
    $tools = '';
    $staff = staffProfile($account);
    if ($staff && boardRoleRank($staff) > boardRoleRank($profile)) {
        $back = '/community/board/?u=' . rawurlencode($profile['username']);
        if (boardSuspended($profile, $now)) $tools .= modButton($account, 'unsuspend', ['username' => $profile['username'], 'back' => $back], 'Lift suspension', 'bb-btn-small');
        elseif (boardCanModerate($staff, 'suspend')) {
            $max = BOARD_SUSPEND_MAX_DAYS[boardRoleRank($staff)];
            $tools .= '<form method="post" action="/community/board/?do=mod&amp;action=suspend" class="bb-modform">' . csrfField($account) . '<input type="hidden" name="username" value="' . authH($profile['username']) . '"><input type="hidden" name="back" value="' . authH($back) . '">'
                . '<label>Suspend for (days' . (boardRoleRank($staff) >= 3 ? ', 0 = until lifted' : '') . ')<input type="number" name="days" value="3" min="' . (boardRoleRank($staff) >= 3 ? 0 : 1) . '" max="' . $max . '" required></label>'
                . '<label>Reason (moderators only)<input name="reason" maxlength="500" required></label><button type="submit" class="bb-btn-small">Suspend posting</button></form>';
        }
        if (boardCanModerate($staff, 'set_role')) {
            $options = implode('', array_map(fn($r) => '<option value="' . $r . '"' . ($profile['role'] === $r ? ' selected' : '') . '>' . authH(boardRoleName($r)) . '</option>', ['member', 'moderator', 'global_moderator']));
            $tools .= '<form method="post" action="/community/board/?do=mod&amp;action=set_role" class="bb-modform">' . csrfField($account) . '<input type="hidden" name="username" value="' . authH($profile['username']) . '"><input type="hidden" name="back" value="' . authH($back) . '">'
                . '<label>Role<select name="role">' . $options . '</select></label><button type="submit" class="bb-btn-small">Change role</button></form>';
        }
        $clear = [];
        if ($profile['signature'] !== '') $clear[] = modButton($account, 'clear_signature', ['username' => $profile['username'], 'back' => $back], 'Clear signature');
        if (boardAvatarFile($profile['avatar'])) $clear[] = modButton($account, 'remove_avatar', ['username' => $profile['username'], 'back' => $back], 'Remove avatar');
        if ($clear) $tools .= '<div class="bb-modtools">' . implode('', $clear) . '</div>';
        $tools = $tools ? '<section class="bb-panel bb-modpanel"><h2>' . glyph('shield') . 'Moderation</h2>' . $tools . '</section>' : '';
    }
    $items = '';
    foreach (boardMemberPosts((int)$profile['member_id']) as $p) $items .= '<tr><td class="bb-main"><a class="bb-title" href="' . authH(postUrl((int)$p['id'])) . '">' . authH($p['title']) . '</a></td><td class="bb-last">' . when((int)$p['created_at']) . '</td></tr>';
    boardPage('Viewing profile - ' . $profile['username'], $flash . '<div class="bb-panel bb-profile-card">' . avatar($profile['username'], '', $profile['avatar']) . '<dl><dt>Username</dt><dd>' . userLink($profile['username'], $profile['role']) . ' ' . roleBadge($profile['role']) . '</dd>'
        . '<dt>Level</dt><dd>' . levelBadge($count) . (boardSuspended($profile, $now) ? ' <span class="bb-tag">Suspended</span>' : '') . $progress . '</dd><dt>Joined</dt><dd>' . when((int)$profile['created_at']) . '</dd><dt>Total posts</dt><dd>' . number_format($count) . '</dd>'
        . '<dt>Reputation</dt><dd><strong>' . number_format(boardReputation((int)$profile['member_id'])) . '</strong> <span class="bb-muted">likes received · ' . plural(boardLikesGiven((int)$profile['member_id']), 'like', 'likes') . ' given</span></dd>'
        . '<dt>Badges</dt><dd>' . count($earned) . ' of ' . count(BOARD_BADGES) . '</dd>'
        . ($profile['signature'] !== '' ? '<dt>Signature</dt><dd class="bb-signature">' . boardRenderSignature($profile['signature']) . '</dd>' : '') . '</dl>'
        . ($account && $account['profile'] && (int)$account['profile']['member_id'] === (int)$profile['member_id'] ? '<a class="bb-btn-small bb-edit-profile" href="/community/board/?view=settings">' . glyph('gear') . ' Edit avatar and signature</a>'
            : ($account && $account['profile'] ? '<div class="bb-edit-profile bb-profile-actions"><a class="bb-btn-small" href="/community/board/?view=compose&amp;to=' . rawurlencode($profile['username']) . '">' . glyph('mail') . ' Send message</a>'
                . (boardRoleRank($profile) < 1 ? '<form method="post" action="/community/board/?do=' . (boardBlocked((int)$account['profile']['member_id'], (int)$profile['member_id']) ? 'unblock' : 'block') . '" class="bb-inline">' . csrfField($account)
                    . '<input type="hidden" name="username" value="' . authH($profile['username']) . '"><button type="submit" class="bb-btn-small">' . (boardBlocked((int)$account['profile']['member_id'], (int)$profile['member_id']) ? 'Unblock' : 'Block') . '</button></form>' : '') . '</div>' : '')) . '</div>' . $tools
        . (($liked = boardMostLiked((int)$profile['member_id'])) ? '<div class="bb-table-wrap"><table class="bb-table"><thead><tr><th>Most liked posts</th><th class="bb-num">Likes</th></tr></thead><tbody>'
            . implode('', array_map(fn($l) => '<tr><td class="bb-main"><a class="bb-title" href="' . authH(postUrl((int)$l['id'])) . '">' . authH($l['title']) . '</a></td><td class="bb-num">👍 ' . (int)$l['likes'] . '</td></tr>', $liked)) . '</tbody></table></div>' : '')
        . '<h2 id="badges">Badges</h2><ul class="bb-badge-grid">' . $badgeList . '</ul>'
        . ($items ? '<div class="bb-table-wrap"><table class="bb-table"><thead><tr><th>Recent posts</th><th class="bb-last">Posted</th></tr></thead><tbody>' . $items . '</tbody></table></div>' : '<p>No posts yet.</p>'),
        $account, [[null, $profile['username']]]);
}

if (isset($_GET['t'])) {
    $thread = boardThread(intParam('t'));
    if (!$thread || $thread['status'] !== 'visible' || $thread['category_status'] === 'archived') boardPage('Not found', notice('That topic does not exist or was removed.', true), $account, [], 404);
    $total = boardPostCount((int)$thread['id']);
    $pages = max(1, (int)ceil($total / BOARD_POSTS_PER_PAGE));
    $page = min(intParam('page', 1), $pages);
    if (!isset($_GET['msg'])) boardAddView((int)$thread['id']);
    $myId = canPost($account) ? (int)$account['member']['id'] : 0;
    $open = !$thread['locked'] && $thread['category_status'] !== 'archived';
    $items = '';
    $number = ($page - 1) * BOARD_POSTS_PER_PAGE;
    $rows = boardPosts((int)$thread['id'], $page);
    $badgesBy = boardBadgesFor(array_column($rows, 'member_id'));
    $repBy = boardReputationFor(array_column($rows, 'member_id'));
    $likesBy = boardLikesFor(array_column($rows, 'id'), $myId);
    $staff = staffProfile($account);
    $reported = $staff ? array_column(boardOpenReports(), 'reports', 'post_id') : [];
    $back = '/community/board/?t=' . (int)$thread['id'] . ($page > 1 ? '&page=' . $page : '');
    foreach ($rows as $p) {
        $number++;
        $tools = [];
        if ($p['status'] === 'visible') $tools[] = '<a class="bb-btn-small" data-share href="' . authH(threadUrl((int)$thread['id'], $page, (int)$p['id'])) . '" title="Copy a link to this post">Share</a>';
        if ($p['status'] === 'visible' && $myId && $open) {
            $tools[] = '<a class="bb-btn-small" href="/community/board/?view=reply&amp;t=' . (int)$thread['id'] . '&amp;quote=' . (int)$p['id'] . '">Quote</a>';
            if ((int)$p['member_id'] === $myId) $tools[] = '<a class="bb-btn-small" href="/community/board/?view=edit&amp;post=' . (int)$p['id'] . '">Edit</a>';
            else $tools[] = '<a class="bb-btn-small" href="/community/board/?view=report&amp;post=' . (int)$p['id'] . '">Report</a>';
        }
        // Staff tools, only on posts by members of a lower role.
        if ($staff && boardRoleRank($staff) > (BOARD_ROLES[$p['role']][0] ?? 0) && $p['status'] !== 'deleted') {
            if (isset($reported[$p['id']])) $tools[] = '<span class="bb-flag" title="Open reports">⚑ ' . (int)$reported[$p['id']] . '</span>' . modButton($account, 'dismiss_reports', ['post' => $p['id'], 'back' => $back], 'Dismiss', 'bb-btn-small bb-btn-mod');
            $tools[] = $p['status'] === 'visible' ? modButton($account, 'hide_post', ['post' => $p['id'], 'back' => $back], 'Hide', 'bb-btn-small bb-btn-mod')
                : modButton($account, 'restore_post', ['post' => $p['id'], 'back' => $back], 'Restore', 'bb-btn-small bb-btn-mod');
        }
        if ($staff && $p['status'] === 'hidden' && boardRoleRank($staff) > (BOARD_ROLES[$p['role']][0] ?? 0)) $hiddenText = '<details class="bb-hidden-text"><summary>Show hidden text (staff only)</summary><div class="board-body">' . boardRender($p['body'], boardPostOptions($p)) . '</div></details>';
        else $hiddenText = '';
        $content = match ($p['status']) {
            'visible' => '<div class="board-body">' . boardRender($p['body'], boardPostOptions($p)) . '</div>',
            'hidden' => '<p class="board-removed">This post was hidden by a moderator.</p>' . $hiddenText,
            default => '<p class="board-removed">This post was deleted by its author.</p>',
        };
        $suspended = boardSuspended(['status' => $p['profile_status'], 'suspended_until' => $p['suspended_until']], $now);
        $starter = (int)$p['member_id'] === (int)$thread['member_id'];
        $items .= '<article class="bb-post' . ($number % 2 === 0 ? ' bb-post-alt' : '') . '" id="post-' . (int)$p['id'] . '"><aside class="bb-postprofile">' . avatar($p['username'], '', $p['avatar']) . '<div><strong>' . userLink($p['username'], $p['role']) . '</strong>'
            . roleBadge($p['role']) . ($starter && $number > 1 ? '<span class="bb-op">Topic author</span>' : '') . ($suspended ? '<span class="bb-tag">Suspended</span>' : levelBadge((int)$p['author_posts']))
            . '<span class="bb-stat">Posts: ' . number_format((int)$p['author_posts']) . '</span><span class="bb-stat bb-rep" title="Likes received">Reputation: ' . number_format($repBy[(int)$p['member_id']] ?? 0) . '</span><span class="bb-stat">Joined: ' . gmdate('M Y', (int)$p['joined_at']) . '</span>' . badgeIcons($badgesBy[(int)$p['member_id']] ?? []) . '</div></aside>'
            . '<div class="bb-postbody"><div class="bb-posthead"><div><a class="bb-subject" href="' . authH(threadUrl((int)$thread['id'], $page, (int)$p['id'])) . '">' . ($number === 1 ? '' : 'Re: ') . authH($thread['title']) . '</a>'
            . '<div class="bb-sub">by ' . userLink($p['username'], $p['role']) . ' » ' . when((int)$p['created_at']) . '</div></div>'
            . '<div class="bb-posttools">' . implode('', $tools) . '<a class="bb-postnum" href="' . authH(threadUrl((int)$thread['id'], $page, (int)$p['id'])) . '">#' . $number . '</a></div></div>' . $content
            . ($p['status'] === 'visible' ? (function () use ($account, $p, $likesBy, $myId, $now) {
                $like = $likesBy[(int)$p['id']] ?? ['count' => 0, 'mine' => false, 'names' => []];
                $can = $myId && boardLikeBlock($account['member'], $account['profile'], $p + ['thread_status' => 'visible'], $now) === null;
                $button = likeButton($account, $p, $like, $can);
                return $button || $like['count'] ? '<div class="bb-likes">' . $button . '<span class="bb-like-summary">' . authH(likeSummary($like)) . '</span></div>' : '';
            })() : '')
            . ($p['status'] === 'visible' && $p['signature'] !== '' ? '<div class="bb-signature">' . boardRenderSignature($p['signature']) . '</div>' : '')
            . ($p['edited_at'] && $p['status'] === 'visible' ? '<p class="bb-edited">Last edited by ' . authH($p['username']) . ' on ' . when((int)$p['edited_at']) . '</p>' : '') . '</div></article>';
    }
    $base = '/community/board/?t=' . (int)$thread['id'];
    if (!$open) { $replyButton = '<span class="bb-button bb-button-off">' . icon('locked', 'Locked') . ' Topic locked</span>'; $reply = notice('This topic is locked. No new replies can be posted.'); }
    elseif (!canPost($account)) { $replyButton = '<a class="bb-button" href="/faucet/?return=board">Post Reply</a>'; $reply = $signIn; }
    elseif ($block = boardPostBlock($account['member'], $account['profile'], 'reply', $now, $thread['category_slug'])) { $replyButton = ''; $reply = notice($block) . (!$account['profile'] ? '<p><a href="/community/board/?view=profile">Choose a username</a></p>' : ''); }
    else { $replyButton = '<a class="bb-button" href="/community/board/?view=reply&amp;t=' . (int)$thread['id'] . '">Post Reply</a>';
        $reply = '<form method="post" action="/community/board/?do=reply&amp;t=' . (int)$thread['id'] . '" class="stack bb-form">' . csrfField($account) . editor('body', 'Message', '', 6) . formButtons() . '</form>'; }
    $bar = '<div class="bb-actionbar">' . $replyButton . '<span class="bb-muted bb-count">' . plural($total, 'post', 'posts') . ($pages > 1 ? '' : ' • Page 1 of 1') . '</span>' . pager($base, $page, $total, BOARD_POSTS_PER_PAGE) . '</div>';
    $first = boardPosts((int)$thread['id'], 1)[0] ?? null;
    $modbar = '';
    if ($staff && boardRoleRank($staff) > boardRoleRank(boardProfile((int)$thread['member_id']))) {
        $f = ['thread' => $thread['id'], 'back' => $back];
        $modbar = modButton($account, $thread['locked'] ? 'unlock' : 'lock', $f, $thread['locked'] ? 'Unlock topic' : 'Lock topic', 'bb-btn-small bb-btn-mod');
        if (boardCanModerate($staff, 'pin')) {
            $modbar .= modButton($account, $thread['pinned'] ? 'unpin' : 'pin', $f, $thread['pinned'] ? 'Unpin' : 'Make sticky', 'bb-btn-small bb-btn-mod')
                . modButton($account, 'hide_topic', ['thread' => $thread['id'], 'back' => '/community/board/?c=' . $thread['category_slug']], 'Hide topic', 'bb-btn-small bb-btn-mod');
            $options = implode('', array_map(fn($c) => '<option value="' . authH($c['slug']) . '"' . ($c['slug'] === $thread['category_slug'] ? ' selected' : '') . '>' . authH($c['name']) . '</option>', boardCategories()));
            $modbar .= '<form method="post" action="/community/board/?do=mod&amp;action=move" class="bb-inline bb-moveform">' . csrfField($account) . '<input type="hidden" name="thread" value="' . (int)$thread['id'] . '"><input type="hidden" name="back" value="' . authH($back) . '">'
                . '<label class="visually-hidden" for="bb-move">Move to forum</label><select id="bb-move" name="category">' . $options . '</select><button type="submit" class="bb-btn-small bb-btn-mod">Move</button></form>';
        }
        $modbar = '<div class="bb-modbar">' . glyph('shield') . '<span>Moderate topic:</span>' . $modbar . '</div>';
    }
    boardPage($thread['title'], $flash . $bar . '<div class="bb-thread">' . $items . '</div>' . $bar . $modbar
        . ($page === $pages ? (canPost($account) && $open ? '<section class="bb-panel bb-quickreply" id="reply"><h2>Quick Reply</h2>' . $reply . '</section>' : '<div class="bb-panel bb-quickreply" id="reply">' . $reply . '</div>') : '')
        . '<p><a href="/community/board/?c=' . authH($thread['category_slug']) . '">‹ Return to “' . authH($thread['category_name']) . '”</a></p>',
        $account, [['/community/board/?c=' . $thread['category_slug'], $thread['category_name']]], 200, $base . ($page > 1 ? '&page=' . $page : ''), $first && $first['status'] === 'visible' ? boardExcerpt($first['body']) : '');
}

if (isset($_GET['c'])) {
    $category = boardCategory(is_string($_GET['c']) ? $_GET['c'] : '');
    if (!$category || $category['status'] === 'archived') boardPage('Not found', notice('That forum does not exist.', true), $account, [], 404);
    $total = boardThreadCount((int)$category['id']);
    $page = min(intParam('page', 1), max(1, (int)ceil($total / BOARD_THREADS_PER_PAGE)));
    $threads = boardThreads((int)$category['id'], $page);
    $sticky = array_values(array_filter($threads, fn($t) => $t['pinned']));
    $normal = array_values(array_filter($threads, fn($t) => !$t['pinned']));
    $base = '/community/board/?c=' . rawurlencode($category['slug']);
    $button = $category['status'] === 'open' ? '<a class="bb-button" href="/community/board/?view=new&amp;c=' . authH($category['slug']) . '">New Topic</a>' : '<span class="bb-button bb-button-off">' . icon('locked', 'Locked') . ' Forum locked</span>';
    $bar = '<div class="bb-actionbar">' . $button . '<span class="bb-muted">' . plural($total, 'topic', 'topics') . '</span>' . pager($base, $page, $total, BOARD_THREADS_PER_PAGE) . '</div>';
    boardPage($category['name'], $flash . '<p class="bb-desc">' . authH($category['description']) . '</p>' . $bar
        . ($threads ? topicTable($sticky, 'Sticky topics') . topicTable($normal, 'Topics') : '<p class="bb-panel">There are no topics in this forum yet. Start the first one.</p>') . ($threads ? $bar : ''),
        $account, [[null, $category['name']]], 200, $base . ($page > 1 ? '&page=' . $page : ''), $category['description']);
}

// Board index: forums grouped by section.
$sections = [];
foreach (boardCategories() as $c) $sections[$c['section']][] = $c;
$tables = '';
foreach ($sections as $section => $forums) {
    $rows = '';
    foreach ($forums as $c) {
        $rows .= '<tr><td class="bb-ico">' . icon($c['status'] === 'locked' ? 'locked' : 'forum', $c['status'] === 'locked' ? 'Forum locked' : 'Forum') . '</td><td class="bb-main"><a class="bb-title" href="/community/board/?c=' . authH($c['slug']) . '">' . authH($c['name']) . '</a>'
            . '<div class="bb-sub">' . authH($c['description']) . '</div><div class="bb-sub bb-narrow">' . plural((int)$c['threads'], 'topic', 'topics') . ' · ' . plural((int)$c['posts'], 'post', 'posts') . '</div></td>'
            . '<td class="bb-num">' . number_format((int)$c['threads']) . '</td><td class="bb-num">' . number_format((int)$c['posts']) . '</td><td class="bb-last">' . lastPost($c['last_post_id'] ? (int)$c['last_post_id'] : null, true) . '</td></tr>';
    }
    $tables .= '<div class="bb-table-wrap"><table class="bb-table bb-forums"><thead><tr><th class="bb-ico"><span class="visually-hidden">Status</span></th><th>' . authH($section) . '</th><th class="bb-num">Topics</th><th class="bb-num">Posts</th><th class="bb-last">Last post</th></tr></thead><tbody>' . $rows . '</tbody></table></div>';
}
$stats = boardStats();
try { $online = boardOnlineCount($now); } catch (Throwable $e) { $online = 0; }
boardPage('Monzero community forum', $flash . '<p class="bb-desc">Ask questions, share ideas and discuss Monzero with other members. Anyone can read; members with a confirmed account can post. Please read the <a href="/community/board/?view=rules">forum rules</a> before posting.</p>'
    . $tables . '<section class="bb-panel bb-stats"><h2>' . glyph('users') . 'Who is online</h2><p>' . plural($online, 'member is', 'members are') . ' signed in (active in the last 15 minutes).</p>'
    . '<h2>' . glyph('chart') . 'Statistics</h2><p>Total posts <strong>' . number_format((int)$stats['posts']) . '</strong> • Total topics <strong>' . number_format((int)$stats['topics']) . '</strong> • Total members <strong>' . number_format((int)$stats['members']) . '</strong>'
    . ($stats['newest'] ? ' • Our newest member ' . userLink($stats['newest']) : '') . '</p></section>', $account, [], 200, '/community/board/');
