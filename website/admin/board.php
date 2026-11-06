<?php
declare(strict_types=1);
// Moderation for the community board (lib/board.php): the report queue, recent posts, thread
// controls, member suspensions and categories. Every change is recorded in the audit log.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once dirname(__DIR__) . '/lib/board.php';

const BOARD_ADMIN_MESSAGES = [
    'board-saved' => 'Saved.',
];

function boardAdminCsrf(array $user): string { return '<input type="hidden" name="csrf" value="' . adminH($user['csrf']) . '">'; }

function boardAdminButton(array $user, string $do, array $fields, string $label): string {
    $hidden = '';
    foreach ($fields as $name => $value) $hidden .= '<input type="hidden" name="' . adminH($name) . '" value="' . adminH($value) . '">';
    return '<form method="post" action="/admin/?do=' . adminH($do) . '" class="inline">' . boardAdminCsrf($user) . $hidden . '<button type="submit">' . adminH($label) . '</button></form>';
}

// Console actions go to the admin audit log and to the forum's moderation log (visible to staff).
function boardAdminRecord(array $user, string $action, array $detail): void {
    adminAudit((int)$user['id'], $action, $detail);
    $target = isset($detail['username']) ? 'member ' . $detail['username'] : (isset($detail['post']) ? 'post ' . $detail['post'] : (isset($detail['thread']) ? 'topic ' . $detail['thread'] : (string)($detail['slug'] ?? '')));
    $rest = array_diff_key($detail, array_flip(['username', 'post', 'thread', 'slug']));
    boardModLog('Site console', str_replace('board.', '', $action), $target, $rest ? json_encode($rest, JSON_UNESCAPED_UNICODE) : '', time());
}

function boardAdminBack(): never { header('Location: /admin/?view=board&msg=board-saved', true, 303); exit; }

function boardAdminAction(string $do, array $user): bool {
    if (!str_starts_with($do, 'board-')) return false;
    if (!adminCan($user, 'board')) adminDeny('You cannot moderate the community board.');
    $actor = (int)$user['id'];
    $now = time();
    $post = (int)($_POST['post'] ?? 0);
    $thread = (int)($_POST['thread'] ?? 0);
    switch ($do) {
        case 'board-post-hide':
        case 'board-post-restore':
            if (!boardSetPostStatus($post, $do === 'board-post-hide' ? 'hidden' : 'visible', $now)) adminDeny('That post cannot be changed.', 409);
            boardAdminRecord($user, $do === 'board-post-hide' ? 'board.post_hidden' : 'board.post_restored', ['post' => $post]);
            boardAdminBack();
        case 'board-reports-dismiss':
            if (!boardDismissReports($post, $now)) adminDeny('There are no open reports for that post.', 409);
            boardAdminRecord($user, 'board.reports_dismissed', ['post' => $post]);
            boardAdminBack();
        case 'board-thread':
            $flag = (string)($_POST['flag'] ?? ''); $value = (int)($_POST['value'] ?? 0);
            if (!boardSetThreadFlag($thread, $flag, $value)) adminDeny('That thread cannot be changed.', 409);
            boardAdminRecord($user, 'board.thread_' . $flag, ['thread' => $thread, 'value' => $value]);
            boardAdminBack();
        case 'board-thread-move':
            $category = (string)($_POST['category'] ?? '');
            if (!boardMoveThread($thread, $category)) adminDeny('That thread or category does not exist.', 409);
            boardAdminRecord($user, 'board.thread_moved', ['thread' => $thread, 'category' => $category]);
            boardAdminBack();
        case 'board-suspend':
            $username = (string)($_POST['username'] ?? '');
            if ($error = boardSuspend($username, (int)($_POST['days'] ?? -1), (string)($_POST['reason'] ?? ''), $now)) { http_response_code(400); boardAdminView($user, $error); exit; }
            boardAdminRecord($user, 'board.member_suspended', ['username' => $username, 'days' => (int)$_POST['days']]);
            boardAdminBack();
        case 'board-unsuspend':
            $username = (string)($_POST['username'] ?? '');
            if (!boardUnsuspend($username)) adminDeny('That member is not suspended.', 409);
            boardAdminRecord($user, 'board.member_unsuspended', ['username' => $username]);
            boardAdminBack();
        case 'board-role':
            $username = (string)($_POST['username'] ?? ''); $role = (string)($_POST['role'] ?? '');
            if ($error = boardSetRole($username, $role, 'Site console (' . $user['email'] . ')', $now)) { http_response_code(400); boardAdminView($user, $error); exit; }
            adminAudit($actor, 'board.role_set', ['username' => $username, 'role' => $role]);
            boardAdminBack();
        case 'board-badge-award':
        case 'board-badge-revoke':
            $username = (string)($_POST['username'] ?? ''); $badge = (string)($_POST['badge'] ?? '');
            $member = boardProfileByName($username);
            if (!$member) { http_response_code(400); boardAdminView($user, 'No member has that username.'); exit; }
            if (!in_array($badge, BOARD_MANUAL_BADGES, true)) adminDeny('Only team-awarded badges can be given or removed here.', 400);
            $ok = $do === 'board-badge-award' ? boardGrantBadge((int)$member['member_id'], $badge, $now, $user['email']) : boardRevokeBadge((int)$member['member_id'], $badge);
            if (!$ok) { http_response_code(409); boardAdminView($user, $do === 'board-badge-award' ? 'That member already has this badge.' : 'That member does not have this badge.'); exit; }
            boardAdminRecord($user, $do === 'board-badge-award' ? 'board.badge_awarded' : 'board.badge_revoked', ['username' => $member['username'], 'badge' => $badge]);
            boardAdminBack();
        case 'board-category-save':
            $slug = (string)($_POST['slug'] ?? '');
            $error = boardSaveCategory($slug, (string)($_POST['name'] ?? ''), (string)($_POST['description'] ?? ''), (int)($_POST['position'] ?? 0), (string)($_POST['status'] ?? ''), $now, (string)($_POST['section'] ?? ''));
            if ($error) { http_response_code(400); boardAdminView($user, $error); exit; }
            boardAdminRecord($user, $slug === '' ? 'board.category_created' : 'board.category_updated', ['slug' => $slug ?: (string)($_POST['name'] ?? '')]);
            boardAdminBack();
    }
    adminDeny('Unknown action.', 400);
}

function boardAdminPostRow(array $user, array $p, array $categories, bool $report = false): string {
    $link = '<a href="/community/board/?t=' . (int)$p['thread_id'] . '" rel="noopener">' . adminH($p['title']) . '</a>';
    $body = $p['status'] === 'deleted' ? '<em>Deleted by its author.</em>' : adminH(boardExcerpt($p['body'], 400));
    $actions = $p['status'] === 'visible' ? boardAdminButton($user, 'board-post-hide', ['post' => $p['post_id']], 'Hide post')
        : ($p['status'] === 'hidden' ? boardAdminButton($user, 'board-post-restore', ['post' => $p['post_id']], 'Restore post') : '');
    if ($report) $actions .= boardAdminButton($user, 'board-reports-dismiss', ['post' => $p['post_id']], 'Dismiss reports (keep post)');
    if (!$report) {
        $actions .= boardAdminButton($user, 'board-thread', ['thread' => $p['thread_id'], 'flag' => 'locked', 'value' => $p['locked'] ? 0 : 1], $p['locked'] ? 'Unlock thread' : 'Lock thread')
            . boardAdminButton($user, 'board-thread', ['thread' => $p['thread_id'], 'flag' => 'pinned', 'value' => $p['pinned'] ? 0 : 1], $p['pinned'] ? 'Unpin thread' : 'Pin thread')
            . boardAdminButton($user, 'board-thread', ['thread' => $p['thread_id'], 'flag' => 'hidden', 'value' => $p['thread_status'] === 'hidden' ? 0 : 1], $p['thread_status'] === 'hidden' ? 'Show thread' : 'Hide thread');
        $options = implode('', array_map(fn($c) => '<option value="' . adminH($c['slug']) . '">' . adminH($c['name']) . '</option>', $categories));
        $actions .= '<form method="post" action="/admin/?do=board-thread-move" class="inline">' . boardAdminCsrf($user) . '<input type="hidden" name="thread" value="' . (int)$p['thread_id'] . '">'
            . '<label class="inline">Move to <select name="category">' . $options . '</select></label><button type="submit">Move</button></form>';
    }
    return '<li><p><strong>' . adminH($p['username']) . '</strong> in ' . $link . ' · ' . adminH(boardWhen((int)$p['created_at'])) . ' · post ' . (int)$p['post_id'] . ' · <strong>' . adminH($p['status']) . '</strong>'
        . ($report ? ' · ' . (int)$p['reports'] . ' open ' . ((int)$p['reports'] === 1 ? 'report' : 'reports') : '') . '</p>'
        . '<blockquote>' . $body . '</blockquote>' . ($report ? '<p>Reasons: ' . adminH($p['reasons']) . '</p>' : '') . '<div class="actions">' . $actions . '</div></li>';
}

function boardAdminView(array $user, string $error = ''): void {
    $now = time();
    $staffList = implode('', array_map(fn($m) => '<li><strong>' . adminH($m['username']) . '</strong> · ' . adminH(boardRoleName($m['role'])) . '</li>', boardStaffList()));
    $roleOptions = implode('', array_map(fn($r) => '<option value="' . $r . '">' . adminH(boardRoleName($r)) . '</option>', array_keys(BOARD_ROLES)));
    $badgeOptions = implode('', array_map(fn($b) => '<option value="' . $b . '">' . BOARD_BADGES[$b][0] . ' ' . adminH(BOARD_BADGES[$b][1]) . '</option>', BOARD_MANUAL_BADGES));
    $modLog = implode('', array_map(fn($l) => '<tr><td>' . adminH(gmdate('Y-m-d H:i', (int)$l['at'])) . ' UTC</td><td>' . adminH($l['actor']) . '</td><td>' . adminH($l['action']) . '</td><td>' . adminH($l['target'] . ($l['detail'] !== '' ? ' — ' . $l['detail'] : '')) . '</td></tr>', boardModLogRecent()));
    $categories = boardCategories(true);
    $flash = isset($_GET['msg'], BOARD_ADMIN_MESSAGES[$_GET['msg']]) ? '<p class="notice" role="status">' . adminH(BOARD_ADMIN_MESSAGES[$_GET['msg']]) . '</p>' : '';
    $reports = implode('', array_map(fn($p) => boardAdminPostRow($user, $p, $categories, true), boardOpenReports()));
    $recent = implode('', array_map(fn($p) => boardAdminPostRow($user, $p, $categories), boardRecentPosts()));
    $suspended = '';
    foreach (boardSuspendedMembers($now) as $m) {
        $suspended .= '<li><strong>' . adminH($m['username']) . '</strong> · ' . ($m['suspended_until'] ? 'until ' . adminH(boardWhen((int)$m['suspended_until'])) : 'until lifted') . ' · ' . adminH((string)$m['suspend_reason'])
            . ' ' . boardAdminButton($user, 'board-unsuspend', ['username' => $m['username']], 'Lift suspension') . '</li>';
    }
    $categoryForms = '';
    $statusSelect = fn(string $current) => '<select name="status">' . implode('', array_map(fn($s) => '<option value="' . $s . '"' . ($s === $current ? ' selected' : '') . '>' . $s . '</option>', ['open', 'locked', 'archived'])) . '</select>';
    foreach ($categories as $c) {
        $categoryForms .= '<details><summary>' . adminH($c['name']) . ' (' . adminH($c['status']) . ', ' . (int)$c['threads'] . ' threads)</summary><form method="post" action="/admin/?do=board-category-save" class="stack">' . boardAdminCsrf($user)
            . '<input type="hidden" name="slug" value="' . adminH($c['slug']) . '"><label>Name<input name="name" value="' . adminH($c['name']) . '" maxlength="60" required></label>'
            . '<label>Description<input name="description" value="' . adminH($c['description']) . '" maxlength="300" required></label><label>Section (heading on the forum index)<input name="section" value="' . adminH($c['section']) . '" maxlength="40" required></label><label>Position (lower first)<input type="number" name="position" value="' . (int)$c['position'] . '" min="0" max="10000"></label>'
            . '<label>Status ' . $statusSelect($c['status']) . '</label><button type="submit">Save category</button></form></details>';
    }
    adminPage('Community board', $flash . ($error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '')
        . '<p>Members post with faucet accounts and a public username. Hiding a post replaces it with a notice; its text stays here for review. Three open reports hide a post automatically until you restore it or hide it for good.</p>'
        . '<section><h2>Open reports</h2>' . ($reports ? '<ol class="moderation">' . $reports . '</ol>' : '<p>No open reports.</p>') . '</section>'
        . '<section><h2>Recent posts</h2>' . ($recent ? '<ol class="moderation">' . $recent . '</ol>' : '<p>No posts yet.</p>') . '</section>'
        . '<section><h2>Suspended members</h2>' . ($suspended ? '<ul>' . $suspended . '</ul>' : '<p>Nobody is suspended.</p>')
        . '<form method="post" action="/admin/?do=board-suspend" class="stack">' . boardAdminCsrf($user) . '<label>Username<input name="username" maxlength="24" required></label>'
        . '<label>Days (0 = until lifted)<input type="number" name="days" value="7" min="0" max="3650" required></label><label>Reason (shown to moderators only)<input name="reason" maxlength="500" required></label><button type="submit">Suspend posting</button></form></section>'
        . '<section><h2>Forum staff</h2><p>Three layers: Moderators review reports, hide posts and lock topics; Global moderators can also pin, move and hide topics and suspend for up to 30 days; Administrators can also suspend for longer and appoint moderators. Only this console appoints administrators.</p>'
        . ($staffList ? '<ul>' . $staffList . '</ul>' : '<p>No staff yet.</p>')
        . '<form method="post" action="/admin/?do=board-role" class="stack">' . boardAdminCsrf($user) . '<label>Username<input name="username" maxlength="24" required></label><label>Role<select name="role">' . $roleOptions . '</select></label><button type="submit">Set role</button></form></section>'
        . '<section><h2>Team-awarded badges</h2><form method="post" action="/admin/?do=board-badge-award" class="stack">' . boardAdminCsrf($user) . '<label>Username<input name="username" maxlength="24" required></label><label>Badge<select name="badge">' . $badgeOptions . '</select></label>'
        . '<button type="submit">Award badge</button><button type="submit" formaction="/admin/?do=board-badge-revoke">Remove badge</button></form>'
        . '<p>Donor badges: forum accounts are not linked to wallets, so only award them when a member asks and you have checked the donation (for example a transaction ID and its proof against the fund address). Never ask members to reveal more than they choose. '
        . 'Top XMZ donator is held by one member at a time' . (($top = boardBadgeHolder('top_donator')) ? ' (now: <strong>' . adminH($top['username']) . '</strong>)' : ' (nobody yet)') . '; awarding it moves it.</p></section>'
        . '<section><h2>Moderation log</h2>' . ($modLog ? '<div class="table-scroll"><table><thead><tr><th scope="col">Time</th><th scope="col">Who</th><th scope="col">Action</th><th scope="col">On</th></tr></thead><tbody>' . $modLog . '</tbody></table></div>' : '<p>Nothing yet.</p>') . '</section>'
        . '<section><h2>Categories</h2>' . $categoryForms . '<h3>New category</h3><form method="post" action="/admin/?do=board-category-save" class="stack">' . boardAdminCsrf($user)
        . '<input type="hidden" name="slug" value=""><label>Name<input name="name" maxlength="60" required></label><label>Description<input name="description" maxlength="300" required></label><label>Section (heading on the forum index)<input name="section" value="Community" maxlength="40" required></label>'
        . '<label>Position (lower first)<input type="number" name="position" value="100" min="0" max="10000"></label><label>Status ' . $statusSelect('open') . '</label><button type="submit">Create category</button></form></section>', $user);
}

function boardAdminViewRoute(string $view, array $user): bool {
    if ($view !== 'board') return false;
    if (!adminCan($user, 'board')) adminDeny('You cannot moderate the community board.');
    try { boardDb(); } catch (Throwable $e) { adminDeny('The board storage is unavailable.', 503); }
    boardAdminView($user);
    return true;
}

function boardAdminLinks(array $user): string {
    if (!adminCan($user, 'board')) return '';
    try { $open = ' (' . count(boardOpenReports()) . ' open reports)'; } catch (Throwable $e) { $open = ''; }
    return '<section><h2>Community board</h2><ul><li><a href="/admin/?view=board">Moderate the community board</a>' . $open . '</li></ul></section>';
}
