<?php
declare(strict_types=1);
// Owner console for the community airdrop (lib/airdrop.php): add, edit, suspend, reset and
// remove airdrop moderators, and audit every review they make (with warning flags), overriding
// any decision. Permission: 'airdrop'. Every change goes to the admin audit log; passwords and
// PINs are never logged and are shown only once.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once dirname(__DIR__) . '/lib/airdrop.php';

const AIRDROP_ADMIN_MESSAGES = [
    'ad-saved' => 'Saved.', 'ad-suspended' => 'Moderator suspended. They are signed out and cannot review.',
    'ad-activated' => 'Moderator reactivated.', 'ad-removed' => 'Moderator removed.', 'ad-overridden' => 'Decision changed.',
    'ad-p-saved' => 'Saved.', 'ad-q-done' => 'Decision saved.', 'ad-q-gone' => 'Someone already reviewed that one.', 'ad-p-disqualified' => 'Entry disqualified: it earns nothing and cannot submit work.', 'ad-p-reinstated' => 'Entry reinstated.',
];
const AIRDROP_TASK_LABELS = ['share_x' => 'Share on X', 'share_instagram' => 'Share on Instagram', 'forum_post' => 'Forum post',
    'forum_active' => 'Forum regular', 'run_node' => 'Run a node'];

function airdropAdminCsrf(array $user): string { return '<input type="hidden" name="csrf" value="' . adminH($user['csrf']) . '">'; }
function airdropAdminBtn(array $user, string $do, array $fields, string $label, string $class = ''): string {
    $hidden = '';
    foreach ($fields as $k => $v) $hidden .= '<input type="hidden" name="' . adminH($k) . '" value="' . adminH($v) . '">';
    return '<form method="post" action="/admin/?do=' . adminH($do) . '" class="inline">' . airdropAdminCsrf($user) . $hidden
        . '<button type="submit"' . ($class ? ' class="' . adminH($class) . '"' : '') . '>' . adminH($label) . '</button></form>';
}
function airdropAdminBack(string $msg, string $tab = 'moderators', array $extra = []): never {
    header('Location: /admin/?' . http_build_query(['view' => 'airdrop', 'tab' => $tab, 'msg' => $msg] + $extra), true, 303); exit;
}
function airdropWhen(?int $t): string {
    if (!$t) return 'never';
    $d = time() - $t;
    if ($d < 60) return 'just now';
    if ($d < 3600) return intdiv($d, 60) . ' min ago';
    if ($d < 86400) return intdiv($d, 3600) . ' h ago';
    if ($d < 30 * 86400) return intdiv($d, 86400) . ' d ago';
    return gmdate('j M Y', $t);
}

function airdropAdminAction(string $do, array $user): bool {
    if (!str_starts_with($do, 'airdrop-')) return false;
    if (!adminCan($user, 'airdrop')) adminDeny('You cannot manage the airdrop.');
    $actor = (int)$user['id'];
    $id = (int)($_POST['id'] ?? 0);
    switch ($do) {
        case 'airdrop-mod-add':
            $r = adModCreate((string)($_POST['name'] ?? ''), (string)($_POST['address'] ?? ''), (string)($_POST['telegram'] ?? ''),
                (string)($_POST['note'] ?? ''), (string)$user['email'], (string)($_POST['password'] ?? ''), (string)($_POST['pin'] ?? ''));
            if (isset($r['error'])) { http_response_code(400); airdropAdminView($user, 'moderators', $r['error'], null, $_POST); exit; }
            adminAudit($actor, 'airdrop.moderator_added', ['name' => $r['name'], 'telegram' => $r['telegram'], 'custom_secret' => ($_POST['password'] ?? '') !== '']);
            airdropAdminView($user, 'moderators', '', $r + ['fresh' => true]);
            exit;
        case 'airdrop-mod-reset':
            $m = adModById($id);
            if (!$m || $m['source'] !== 'db') adminDeny('That moderator cannot be reset here.', 409);
            $sec = adModResetSecrets($id);
            adminAudit($actor, 'airdrop.moderator_reset', ['name' => $m['name']]);
            airdropAdminView($user, 'moderators', '', ['name' => $m['name'], 'address' => $m['address'], 'telegram' => $m['telegram']] + $sec + ['fresh' => false]);
            exit;
        case 'airdrop-mod-edit':
            $m = adModById($id);
            if (!$m || $m['source'] !== 'db') adminDeny('That moderator cannot be edited here.', 409);
            if ($err = adModUpdate($id, (string)($_POST['address'] ?? ''), (string)($_POST['telegram'] ?? ''), (string)($_POST['note'] ?? ''))) {
                http_response_code(400); airdropAdminView($user, 'moderators', $err); exit;
            }
            adminAudit($actor, 'airdrop.moderator_edited', ['name' => $m['name']]);
            airdropAdminBack('ad-saved');
        case 'airdrop-mod-suspend':
        case 'airdrop-mod-activate':
            $m = adModById($id);
            if (!$m || $m['source'] !== 'db' || !adModSetStatus($id, $do === 'airdrop-mod-suspend' ? 'suspended' : 'active')) adminDeny('That moderator cannot be changed.', 409);
            adminAudit($actor, $do === 'airdrop-mod-suspend' ? 'airdrop.moderator_suspended' : 'airdrop.moderator_activated', ['name' => $m['name']]);
            airdropAdminBack($do === 'airdrop-mod-suspend' ? 'ad-suspended' : 'ad-activated');
        case 'airdrop-mod-remove':
            $m = adModById($id);
            if (!$m || $m['source'] !== 'db') adminDeny('That moderator cannot be removed here.', 409);
            if (($_POST['confirm'] ?? '') !== $m['name']) { http_response_code(400); airdropAdminView($user, 'moderators', 'To remove ' . $m['name'] . ', type their name exactly in the confirmation box.'); exit; }
            adModDelete($id);
            adminAudit($actor, 'airdrop.moderator_removed', ['name' => $m['name']]);
            airdropAdminBack('ad-removed');
        case 'airdrop-override':
            $to = (string)($_POST['to'] ?? '');
            if ($err = adOverride((int)($_POST['submission'] ?? 0), $to, 'console:' . $user['email'], (string)($_POST['note'] ?? ''))) adminDeny($err, 409);
            adminAudit($actor, 'airdrop.review_overridden', ['submission' => (int)$_POST['submission'], 'to' => $to]);
            if ((int)($_POST['back'] ?? 0) > 0) airdropAdminBack('ad-overridden', 'participant', ['id' => (int)$_POST['back']]);
            airdropAdminBack('ad-overridden', 'activity', array_filter(['mod' => (string)($_POST['mod'] ?? ''), 'status' => (string)($_POST['fstatus'] ?? '')]));
        case 'airdrop-q-review':
            $sid = (int)($_POST['submission'] ?? 0); $decision = (string)($_POST['decision'] ?? '');
            $err = adReview($sid, $decision === 'approve' ? 'approve' : 'reject', 'console');
            if ($err === null) adminAudit($actor, 'airdrop.queue_' . ($decision === 'approve' ? 'approved' : 'rejected'), ['submission' => $sid]);
            airdropAdminBack($err === null ? 'ad-q-done' : 'ad-q-gone', 'queue', array_filter(['show' => (string)($_POST['show'] ?? '')]));
        case 'airdrop-p-disqualify':
        case 'airdrop-p-reinstate':
        case 'airdrop-p-adjust':
        case 'airdrop-p-revoke':
            $pid = (int)($_POST['pid'] ?? 0); $who = 'console:' . $user['email'];
            $err = match ($do) {
                'airdrop-p-disqualify' => adDisqualify($pid, (string)($_POST['reason'] ?? ''), $who),
                'airdrop-p-reinstate' => adReinstate($pid, $who),
                'airdrop-p-adjust' => adAdjust($pid, (string)($_POST['xmz'] ?? ''), (string)($_POST['reason'] ?? ''), $who),
                'airdrop-p-revoke' => adRevokeTask($pid, (string)($_POST['task'] ?? ''), (string)($_POST['reason'] ?? ''), $who),
            };
            if ($err !== null) { http_response_code(400); airdropParticipantView($user, $pid, $err); exit; }
            adminAudit($actor, 'airdrop.participant_' . substr($do, 10), ['participant' => $pid] + array_filter(['xmz' => $_POST['xmz'] ?? null, 'task' => $_POST['task'] ?? null]));
            airdropAdminBack($do === 'airdrop-p-disqualify' ? 'ad-p-disqualified' : ($do === 'airdrop-p-reinstate' ? 'ad-p-reinstated' : 'ad-p-saved'), 'participant', ['id' => $pid]);
    }
    adminDeny('Unknown action.', 400);
}

function airdropCredentialPanel(array $c): string {
    $text = "Monzero airdrop moderator access\n\nSign in: https://monzero.org/airdrop/admin.php\nXMZ address: {$c['address']}\nTelegram: {$c['telegram']}\nPassword: {$c['password']}\nPIN: {$c['pin']}\n\n"
        . "Keep these private. The Monzero team will never ask you for them. Moderator rules: approve only genuine, public work; never review your own entry.";
    return '<section class="adr-cred" role="status"><h2>' . ($c['fresh'] ? 'Moderator added: ' : 'New sign-in details for ') . adminH($c['name']) . '</h2>'
        . '<p><strong>Copy this now — the password and PIN are shown only once</strong> and are stored only as hashes. Send it to them privately (a direct Telegram message, not a group).</p>'
        . '<pre id="adr-cred-text">' . adminH($text) . '</pre><div class="actions"><button type="button" data-copy-target="adr-cred-text">Copy sign-in details</button>'
        . '<a class="button" href="/admin/?view=airdrop&amp;tab=moderators">Done</a></div></section>';
}

function airdropAdminView(array $user, string $tab = 'overview', string $error = '', ?array $cred = null, array $form = []): void {
    $tab = in_array($tab, ['overview', 'moderators', 'activity', 'participants', 'queue'], true) ? $tab : 'overview';
    $cfg = adConfig();
    $budget = adBudget();
    $closed = adClosedReason();
    $mods = adModerators();
    $stats = adModStats();
    $db = adDb();
    $pending = (int)$db->query("SELECT COUNT(*) FROM submissions WHERE status = 'pending'")->fetchColumn();
    $flagged = 0;
    foreach ($stats as $st) $flagged += $st['self'] + $st['referred'];
    $active = count(array_filter($mods, fn($m) => $m['status'] === 'active'));
    $flash = isset($_GET['msg'], AIRDROP_ADMIN_MESSAGES[$_GET['msg']]) ? '<p class="notice" role="status">' . adminH(AIRDROP_ADMIN_MESSAGES[$_GET['msg']]) . '</p>' : '';
    $err = $error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '';
    $csrf = airdropAdminCsrf($user);

    $tabs = airdropTabs($tab, count($mods));

    $pct = $budget['pool'] > 0 ? min(100, $budget['committed'] / $budget['pool'] * 100) : 0;
    $kpis = '<div class="adr-kpis">'
        . '<div><span>Campaign</span><strong class="' . ($closed === null ? 'ok' : 'warn') . '">' . ($closed === null ? 'Open' : 'Closed') . '</strong><em>' . ($closed === null ? 'ends ' . adminH(gmdate('j M Y', (int)$cfg['ends_at'])) : adminH($closed)) . '</em></div>'
        . '<div><span>Budget used</span><strong>' . adminH(number_format($pct, 1)) . '%</strong><em>' . adminH(number_format($budget['committed'], 2)) . ' of ' . adminH(number_format($budget['pool'])) . ' XMZ</em></div>'
        . '<div><span>Participants</span><strong>' . adParticipantCount() . '</strong><em>' . count(adPayouts()) . ' earning</em></div>'
        . '<a href="/admin/?view=airdrop&amp;tab=queue"><span>Waiting for review</span><strong>' . $pending . '</strong><em>' . adPendingCounts()['organic'] . ' from people with no referral</em></a>'
        . '<a href="/admin/?view=airdrop&amp;tab=moderators"><span>Active moderators</span><strong>' . $active . '</strong><em>' . (count($mods) - $active) . ' suspended</em></a>'
        . '<a href="/admin/?view=airdrop&amp;tab=activity"><span>Warning flags</span><strong class="' . ($flagged ? 'warn' : 'ok') . '">' . $flagged . '</strong><em>own entry or own referrals approved</em></a>'
        . '</div>';

    $body = $flash . $err . ($cred ? airdropCredentialPanel($cred) : '') . $tabs;

    if ($tab === 'overview') {
        $board = '';
        foreach ($mods as $m) {
            $st = $stats[$m['name']];
            $board .= '<tr><td><strong>' . adminH($m['name']) . '</strong>' . ($m['status'] !== 'active' ? ' <span class="adr-badge warn">suspended</span>' : '') . '</td><td class="num">' . $st['approved'] . '</td><td class="num">' . $st['rejected'] . '</td><td class="num">'
                . ($st['rate'] === null ? '—' : adminH(number_format($st['rate'] * 100)) . '%') . '</td><td class="num">' . $st['day'] . '</td><td>' . adminH(airdropWhen($st['last'])) . '</td><td>'
                . ($st['self'] + $st['referred'] ? '<span class="adr-badge warn">' . ($st['self'] + $st['referred']) . ' flagged</span>' : '<span class="adr-badge ok">clean</span>') . '</td>'
                . '<td><a href="/admin/?view=airdrop&amp;tab=activity&amp;mod=' . rawurlencode($m['name']) . '">Reviews</a></td></tr>';
        }
        $log = '';
        foreach (array_slice(adReviewLog(15), 0, 15) as $l) {
            $log .= '<li><span class="adr-when">' . adminH(airdropWhen((int)$l['at'])) . '</span> <strong>' . adminH(str_starts_with($l['actor'], 'console:') ? 'You (console)' : $l['actor']) . '</strong> '
                . ($l['action'] === 'override' ? 'changed' : 'reviewed') . ' #' . (int)$l['submission_id'] . ': ' . adminH((string)$l['from_status']) . ' → <span class="adr-st ' . adminH((string)$l['to_status']) . '">' . adminH((string)$l['to_status']) . '</span>'
                . ($l['note'] !== '' ? ' — ' . adminH($l['note']) : '') . '</li>';
        }
        $body .= $kpis . '<div class="adr-meter"><span class="w' . ($pct > 0 ? max(5, (int)round($pct / 5) * 5) : 0) . '"></span></div>'
            . '<section><h2>Moderator scoreboard</h2>' . ($board ? '<div class="table-scroll"><table class="adr-table"><thead><tr><th>Moderator</th><th class="num">Approved</th><th class="num">Rejected</th><th class="num">Approval rate</th><th class="num">Last 24 h</th><th>Last review</th><th>Checks</th><th></th></tr></thead><tbody>' . $board . '</tbody></table></div>' : '<p>No moderators yet. <a href="/admin/?view=airdrop&amp;tab=moderators">Add the first one</a>.</p>') . '</section>'
            . '<section><h2>Latest decisions</h2>' . ($log ? '<ul class="adr-feed">' . $log . '</ul>' : '<p>No reviews yet.</p>') . '</section>';
    }

    if ($tab === 'moderators') {
        $cards = '';
        foreach ($mods as $m) {
            $st = $stats[$m['name']];
            $isDb = $m['source'] === 'db';
            $flags = ($st['self'] ? '<span class="adr-badge warn">approved own entry ×' . $st['self'] . '</span>' : '') . ($st['referred'] ? '<span class="adr-badge warn">approved own referrals ×' . $st['referred'] . '</span>' : '');
            $actions = '';
            if ($isDb) {
                $actions .= $m['status'] === 'active' ? airdropAdminBtn($user, 'airdrop-mod-suspend', ['id' => $m['id']], 'Suspend', 'secondary') : airdropAdminBtn($user, 'airdrop-mod-activate', ['id' => $m['id']], 'Reactivate');
                $actions .= airdropAdminBtn($user, 'airdrop-mod-reset', ['id' => $m['id']], 'New password & PIN', 'secondary');
                $actions .= '<a class="button" href="/admin/?view=airdrop&amp;tab=activity&amp;mod=' . rawurlencode($m['name']) . '">Their reviews</a>';
            }
            $edit = $isDb ? '<details><summary>Edit details</summary><form method="post" action="/admin/?do=airdrop-mod-edit" class="stack">' . $csrf . '<input type="hidden" name="id" value="' . (int)$m['id'] . '">'
                . '<label>XMZ payout address<input name="address" value="' . adminH($m['address']) . '" spellcheck="false" required></label>'
                . '<label>Telegram handle<input name="telegram" value="' . adminH($m['telegram']) . '" required></label>'
                . '<label>Private note<input name="note" value="' . adminH($m['note'] ?? '') . '" maxlength="300"></label><button type="submit">Save</button></form></details>'
                . '<details><summary>Remove moderator</summary><form method="post" action="/admin/?do=airdrop-mod-remove" class="stack">' . $csrf . '<input type="hidden" name="id" value="' . (int)$m['id'] . '">'
                . '<p>Removing forfeits their unpaid moderator rewards (' . adminH(number_format($st['reward'], 2)) . ' XMZ). Suspend instead to stop them but still pay what they earned. Their past reviews stay in the history.</p>'
                . '<label>Type <strong>' . adminH($m['name']) . '</strong> to confirm<input name="confirm" autocomplete="off" required></label><button type="submit" class="danger">Remove permanently</button></form></details>'
                : '<p class="adr-note">Managed in <code>airdrop-config.php</code> on the server. Move them here by adding them below and deleting their config entry.</p>';
            $cards .= '<article class="adr-mod' . ($m['status'] !== 'active' ? ' off' : '') . '">'
                . '<div class="adr-mod-head"><h3>' . adminH($m['name']) . '</h3><span class="adr-badge ' . ($m['status'] === 'active' ? 'ok' : 'warn') . '">' . adminH($m['status']) . '</span>' . (!$isDb ? '<span class="adr-badge">config file</span>' : '') . $flags . '</div>'
                . '<dl><dt>Telegram</dt><dd><a href="https://t.me/' . adminH(ltrim($m['telegram'], '@')) . '" target="_blank" rel="noopener">' . adminH($m['telegram']) . '</a></dd>'
                . '<dt>Payout address</dt><dd><code title="' . adminH($m['address']) . '">' . adminH($m['address'] !== '' ? adShort($m['address']) : '—') . '</code></dd>'
                . ($isDb ? '<dt>Added</dt><dd>' . adminH(gmdate('j M Y', (int)$m['created_at'])) . ' by ' . adminH($m['created_by']) . '</dd><dt>Last sign-in</dt><dd>' . adminH(airdropWhen($m['last_login_at'])) . '</dd>' : '')
                . (($m['note'] ?? '') !== '' ? '<dt>Note</dt><dd>' . adminH($m['note']) . '</dd>' : '')
                . '<dt>Own referral link</dt><dd>' . ($st['joined'] ? 'yes · ' . $st['referrals'] . ' referred' : 'not joined the airdrop yet') . '</dd></dl>'
                . '<div class="adr-stats"><div><strong>' . $st['approved'] . '</strong><span>approved</span></div><div><strong>' . $st['rejected'] . '</strong><span>rejected</span></div>'
                . '<div><strong>' . ($st['rate'] === null ? '—' : adminH(number_format($st['rate'] * 100)) . '%') . '</strong><span>approval rate</span></div><div><strong>' . $st['day'] . '</strong><span>last 24 h</span></div>'
                . '<div><strong>' . $st['overridden'] . '</strong><span>overridden</span></div><div><strong>' . adminH(number_format($st['reward'], 2)) . '</strong><span>XMZ earned</span></div></div>'
                . ($actions ? '<div class="actions">' . $actions . '</div>' : '') . $edit . '</article>';
        }
        $v = fn($k) => adminH((string)($form[$k] ?? ''));
        $body .= '<section><h2>Moderators</h2><p>Moderators sign in at <a href="/airdrop/admin.php" target="_blank" rel="noopener">/airdrop/admin.php</a> with their XMZ address, Telegram handle, password and PIN. Suspending signs them out at once.</p>'
            . ($cards ? '<div class="adr-mods">' . $cards . '</div>' : '<p>No moderators yet.</p>') . '</section>'
            . '<section class="adr-add"><h2>Add a moderator</h2><form method="post" action="/admin/?do=airdrop-mod-add" class="stack">' . $csrf
            . '<label>Name (shown on reviews)<input name="name" value="' . $v('name') . '" maxlength="32" required></label>'
            . '<label>XMZ payout address (also their sign-in)<input name="address" value="' . $v('address') . '" spellcheck="false" required></label>'
            . '<label>Telegram handle<input name="telegram" value="' . $v('telegram') . '" placeholder="@handle" required></label>'
            . '<label>Private note (optional)<input name="note" value="' . $v('note') . '" maxlength="300" placeholder="e.g. applied via Telegram on 14 Nov"></label>'
            . '<details><summary>Set the password and PIN yourself (optional)</summary><p>Leave empty to generate strong ones (recommended). If the moderator already sent you a password in a chat, it is no longer private: generate new ones instead.</p>'
            . '<label>Password (10+ characters)<input name="password" type="text" autocomplete="off" spellcheck="false" minlength="10"></label><label>PIN (6–8 digits)<input name="pin" type="text" inputmode="numeric" autocomplete="off" pattern="[0-9]{6,8}"></label></details>'
            . '<button type="submit">Add moderator</button></form></section>';
    }

    if ($tab === 'participants') $body .= airdropParticipantsSection($user);
    if ($tab === 'queue') $body .= airdropQueueSection($user);

    if ($tab === 'activity') {
        $fmod = is_string($_GET['mod'] ?? null) ? $_GET['mod'] : '';
        $fst = in_array($_GET['status'] ?? '', ['approved', 'rejected'], true) ? $_GET['status'] : '';
        $opts = '<option value="">All moderators</option>';
        foreach (array_unique(array_merge(array_column($mods, 'name'), ['console'])) as $n) $opts .= '<option' . ($n === $fmod ? ' selected' : '') . '>' . adminH($n) . '</option>';
        $rows = '';
        foreach (adReviewHistory($fmod, $fst, 300) as $r) {
            $u = (string)$r['url'];
            $detail = preg_match('#^https?://#', $u) ? '<a href="' . adminH($u) . '" target="_blank" rel="noopener noreferrer nofollow">' . adminH(mb_strimwidth($u, 0, 48, '…')) . ' ↗</a>' : '<code>' . adminH($u) . '</code>';
            $flags = implode('', array_map(fn($f) => '<span class="adr-badge warn">' . adminH($f) . '</span>', $r['flags']));
            $hidden = ['submission' => $r['id'], 'mod' => $fmod, 'fstatus' => $fst];
            $over = ($r['status'] !== 'approved' ? airdropAdminBtn($user, 'airdrop-override', $hidden + ['to' => 'approved'], 'Approve', 'small') : '')
                . ($r['status'] !== 'rejected' ? airdropAdminBtn($user, 'airdrop-override', $hidden + ['to' => 'rejected'], 'Reject', 'small danger') : '')
                . airdropAdminBtn($user, 'airdrop-override', $hidden + ['to' => 'pending'], 'Back to queue', 'small secondary');
            $rows .= '<tr' . ($r['flags'] ? ' class="flag"' : '') . '><td>' . adminH(airdropWhen($r['reviewed_at'] !== null ? (int)$r['reviewed_at'] : null)) . '</td><td>' . adminH((string)$r['reviewer']) . '</td><td>' . adminH(AIRDROP_TASK_LABELS[$r['platform']] ?? $r['platform']) . '</td>'
                . '<td>' . $detail . '</td><td><code>' . adminH(adShort($r['address'])) . '</code></td><td><span class="adr-st ' . adminH($r['status']) . '">' . adminH($r['status']) . '</span> ' . $flags . '</td><td class="adr-over">' . $over . '</td></tr>';
        }
        $body .= '<section><h2>Review activity</h2><p>Every decision moderators have made, newest first. Rows with warning flags are highlighted. Changing a decision updates the participant\'s reward; the moderator keeps no reward for a decision you overturn.</p>'
            . '<form method="get" action="/admin/" class="adr-filter"><input type="hidden" name="view" value="airdrop"><input type="hidden" name="tab" value="activity">'
            . '<label>Moderator<select name="mod">' . $opts . '</select></label><label>Decision<select name="status"><option value="">All</option><option value="approved"' . ($fst === 'approved' ? ' selected' : '') . '>Approved</option><option value="rejected"' . ($fst === 'rejected' ? ' selected' : '') . '>Rejected</option></select></label><button type="submit">Filter</button></form>'
            . ($rows ? '<div class="table-scroll"><table class="adr-table"><thead><tr><th>When</th><th>Moderator</th><th>Task</th><th>Submitted</th><th>Participant</th><th>Decision</th><th>Change</th></tr></thead><tbody>' . $rows . '</tbody></table></div>' : '<p>No reviews match.</p>') . '</section>';
    }

    adminPage('Airdrop', $body, $user);
}

function airdropAdminViewRoute(string $view, array $user): bool {
    if ($view !== 'airdrop') return false;
    if (!adminCan($user, 'airdrop')) adminDeny('You cannot manage the airdrop.');
    try { adDb(); } catch (Throwable $e) { adminDeny('The airdrop storage is unavailable.', 503); }
    if (($_GET['tab'] ?? '') === 'participant') { airdropParticipantView($user, (int)($_GET['id'] ?? 0)); return true; }
    airdropAdminView($user, is_string($_GET['tab'] ?? null) ? $_GET['tab'] : 'overview');
    return true;
}

function airdropAdminLinks(array $user): string {
    if (!adminCan($user, 'airdrop')) return '';
    try { $pending = (int)adDb()->query("SELECT COUNT(*) FROM submissions WHERE status = 'pending'")->fetchColumn(); $n = count(adModerators()); }
    catch (Throwable $e) { $pending = 0; $n = 0; }
    return '<section><h2>Community airdrop</h2><ul><li><a href="/admin/?view=airdrop">Airdrop overview</a> (' . $pending . ' waiting for review)</li>'
        . '<li><a href="/admin/?view=airdrop&amp;tab=moderators">Manage moderators</a> (' . $n . ')</li><li><a href="/admin/?view=airdrop&amp;tab=activity">Audit moderator reviews</a></li></ul></section>';
}

function airdropTabs(string $tab, ?int $mods = null): string {
    $mods ??= count(adModerators());
    $tabs = '<nav class="adr-tabs" aria-label="Airdrop sections">';
    $pc = adPendingCounts();
    foreach (['overview' => 'Overview', 'queue' => 'Review queue (' . $pc['organic'] . ')', 'participants' => 'Participants (' . adParticipantCount() . ')', 'moderators' => 'Moderators (' . $mods . ')', 'activity' => 'Review activity'] as $k => $label)
        $tabs .= '<a href="/admin/?view=airdrop&amp;tab=' . $k . '"' . ($k === $tab ? ' aria-current="page"' : '') . '>' . adminH($label) . '</a>';
    return $tabs . '<a href="/airdrop/admin.php" target="_blank" rel="noopener">Moderator panel ↗</a></nav>';
}

function airdropParticipantsSection(array $user): string {
    $filters = ['organic' => 'Joined without a referral link', 'referred' => 'Joined through a referral', 'flagged' => 'Same network as another entry', 'disqualified' => 'Disqualified', 'all' => 'Everyone'];
    $f = is_string($_GET['f'] ?? null) && isset($filters[$_GET['f']]) ? $_GET['f'] : 'organic';
    $q = is_string($_GET['q'] ?? null) ? mb_substr(trim($_GET['q']), 0, 110) : '';
    $counts = adParticipantCounts();
    $chips = '';
    foreach ($filters as $k => $label) $chips .= '<a class="adr-chip' . ($k === $f ? ' on' : '') . '" href="/admin/?view=airdrop&amp;tab=participants&amp;f=' . $k . '">' . adminH($label) . ' <span>' . $counts[$k] . '</span></a>';
    $rows = '';
    $list = adParticipantsList($f, $q, 500);
    $sum = 0;
    foreach ($list as $r) {
        $sum += $r['centi'];
        $badges = ($r['same_network'] > 1 ? '<span class="adr-badge warn">' . ($r['same_network'] - 1) . ' on same network</span>' : '')
            . ($r['status'] !== 'active' ? '<span class="adr-badge warn">disqualified</span>' : '') . ($r['pending'] ? '<span class="adr-badge">' . (int)$r['pending'] . ' waiting</span>' : '')
            . ((int)$r['adj'] !== 0 ? '<span class="adr-badge">' . adminH(sprintf('%+.2f', $r['adj'] / 100)) . ' adj.</span>' : '');
        $rows .= '<tr' . ($r['same_network'] > 1 || $r['status'] !== 'active' ? ' class="flag"' : '') . '><td><a href="/admin/?view=airdrop&amp;tab=participant&amp;id=' . (int)$r['id'] . '"><code>' . adminH(adShort($r['address'])) . '</code></a></td>'
            . '<td>' . adminH(airdropWhen((int)$r['created_at'])) . '</td><td>' . ($r['referred_by'] ? 'via <code>' . adminH($r['referred_by']) . '</code>' : 'direct') . '</td>'
            . '<td>' . ($r['telegram'] ? '✓' : '—') . '</td><td class="num">' . (int)$r['mining_days'] . '</td><td class="num">' . (int)$r['approved'] . '</td><td class="num">' . (int)$r['referrals'] . '</td>'
            . '<td class="num"><strong>' . adminH(number_format($r['centi'] / 100, 2)) . '</strong></td><td>' . $badges . '</td></tr>';
    }
    return '<section><h2>Participants</h2><p>Everyone who registered an address. The default list shows people who joined <strong>without a referral link</strong>. Open an entry to check their work, adjust their XMZ, take back a task or disqualify them.</p>'
        . '<div class="adr-chips">' . $chips . '</div>'
        . '<form method="get" action="/admin/" class="adr-filter"><input type="hidden" name="view" value="airdrop"><input type="hidden" name="tab" value="participants"><input type="hidden" name="f" value="' . adminH($f) . '">'
        . '<label>Find address or referral code<input name="q" value="' . adminH($q) . '" spellcheck="false" placeholder="part of an address"></label><button type="submit">Search</button></form>'
        . ($rows ? '<p class="adr-note">' . count($list) . ' shown · ' . adminH(number_format($sum / 100, 2)) . ' XMZ earned between them.</p><div class="table-scroll"><table class="adr-table"><thead><tr><th>Address</th><th>Joined</th><th>Came from</th><th>Telegram</th><th class="num">Mining days</th><th class="num">Approved work</th><th class="num">Referred</th><th class="num">XMZ</th><th>Notes</th></tr></thead><tbody>' . $rows . '</tbody></table></div>'
            : '<p>Nobody matches.</p>') . '</section>';
}

function airdropParticipantView(array $user, int $id, string $error = ''): void {
    $d = adParticipantDetail($id);
    if (!$d) adminDeny('No such airdrop entry.', 404);
    $p = $d['p']; $t = $d['totals']; $cfg = adConfig();
    $csrf = airdropAdminCsrf($user);
    $hid = fn(array $f) => implode('', array_map(fn($k, $v) => '<input type="hidden" name="' . adminH($k) . '" value="' . adminH($v) . '">', array_keys($f), $f));
    $form = fn(string $do, array $fields, string $inner, string $btn, string $cls = '') => '<form method="post" action="/admin/?do=' . $do . '" class="stack adr-act">' . $csrf . $hid($fields) . $inner . '<button type="submit"' . ($cls ? ' class="' . $cls . '"' : '') . '>' . adminH($btn) . '</button></form>';
    $flash = isset($_GET['msg'], AIRDROP_ADMIN_MESSAGES[$_GET['msg']]) ? '<p class="notice" role="status">' . adminH(AIRDROP_ADMIN_MESSAGES[$_GET['msg']]) . '</p>' : '';
    $active = $p['status'] === 'active';
    $done = []; foreach ($d['progress'] as $r) $done[$r['task']] = $r;
    $auto = '';
    foreach (['wallet' => 'Wallet (unlocks with Telegram or mining)', 'telegram' => 'Telegram verified', 'mining' => 'Mining (' . count($d['mining_days']) . ' of ' . $cfg['mining_days_required'] . ' days)'] as $k => $label) {
        $ok = isset($done[$k]);
        $auto .= '<tr><td>' . adminH($label) . '</td><td>' . ($ok ? '<span class="adr-st approved">credited</span> ' . adminH(airdropWhen((int)$done[$k]['updated_at'])) : '<span class="adr-st pending">not yet</span>') . '</td><td class="num">' . ($ok ? adminH(number_format((int)$done[$k]['points'] / 100, 2)) : '—') . '</td><td>'
            . ($ok ? '<details><summary>Take back</summary>' . $form('airdrop-p-revoke', ['pid' => $id, 'task' => $k], '<label>Reason<input name="reason" maxlength="300" required placeholder="e.g. left the Telegram straight away"></label>', 'Take back this task', 'danger') . '</details>' : '') . '</td></tr>';
    }
    $subs = '';
    foreach ($d['submissions'] as $s) {
        $u = (string)$s['url'];
        $detail = preg_match('#^https?://#', $u) ? '<a href="' . adminH($u) . '" target="_blank" rel="noopener noreferrer nofollow">' . adminH(mb_strimwidth($u, 0, 60, '…')) . ' ↗</a>' : '<code>' . adminH($u) . '</code>';
        $hidden = ['submission' => $s['id'], 'back' => $id];
        $over = ($s['status'] !== 'approved' ? airdropAdminBtn($user, 'airdrop-override', $hidden + ['to' => 'approved'], 'Approve', 'small') : '')
            . ($s['status'] !== 'rejected' ? airdropAdminBtn($user, 'airdrop-override', $hidden + ['to' => 'rejected'], 'Reject', 'small danger') : '')
            . ($s['status'] !== 'pending' ? airdropAdminBtn($user, 'airdrop-override', $hidden + ['to' => 'pending'], 'Back to queue', 'small secondary') : '');
        $subs .= '<tr><td>' . adminH(AIRDROP_TASK_LABELS[$s['platform']] ?? $s['platform']) . '</td><td>' . $detail . '</td><td>' . adminH(airdropWhen((int)$s['created_at'])) . '</td><td><span class="adr-st ' . adminH($s['status']) . '">' . adminH($s['status']) . '</span>'
            . ($s['reviewer'] ? ' by ' . adminH($s['reviewer']) : '') . '</td><td class="num">' . adminH(number_format((int)$s['points'] / 100, 2)) . '</td><td class="adr-over">' . $over . '</td></tr>';
    }
    $refs = '';
    foreach ($d['referrals'] as $r) $refs .= '<li><a href="/admin/?view=airdrop&amp;tab=participant&amp;id=' . (int)$r['id'] . '"><code>' . adminH(adShort($r['address'])) . '</code></a> · joined ' . adminH(airdropWhen((int)$r['created_at'])) . ' · ' . ($r['verified'] ? '<span class="adr-st approved">verified</span>' : '<span class="adr-st pending">not verified</span>') . ($r['status'] !== 'active' ? ' <span class="adr-badge warn">disqualified</span>' : '') . '</li>';
    $same = '';
    foreach ($d['same_network'] as $r) $same .= '<li><a href="/admin/?view=airdrop&amp;tab=participant&amp;id=' . (int)$r['id'] . '"><code>' . adminH(adShort($r['address'])) . '</code></a> · joined ' . adminH(airdropWhen((int)$r['created_at'])) . ($r['status'] !== 'active' ? ' <span class="adr-badge warn">disqualified</span>' : '') . '</li>';
    $adj = '';
    foreach ($d['adjustments'] as $a) $adj .= '<li><strong>' . adminH(sprintf('%+.2f XMZ', $a['centi'] / 100)) . '</strong> · ' . adminH($a['reason']) . ' <span class="adr-when">' . adminH(airdropWhen((int)$a['at'])) . ' by ' . adminH(str_replace('console:', '', $a['actor'])) . '</span></li>';
    $log = '';
    foreach ($d['log'] as $l) $log .= '<li><span class="adr-when">' . adminH(airdropWhen((int)$l['at'])) . '</span> <strong>' . adminH($l['action']) . '</strong>' . ($l['detail'] !== '' ? ' — ' . adminH($l['detail']) : '') . ' <span class="adr-when">' . adminH(str_replace('console:', '', $l['actor'])) . '</span></li>';
    $days = $d['mining_days'] ? implode(', ', array_map(fn($day) => gmdate('j M', $day * 86400), $d['mining_days'])) : 'none yet';

    $head = '<div class="adr-phead"><div><p class="adr-note"><a href="/admin/?view=airdrop&amp;tab=participants">← All participants</a></p><h2><code>' . adminH(adShort($p['address'])) . '</code></h2>'
        . '<p><code class="adr-full">' . adminH($p['address']) . '</code></p><p>' . ($active ? '<span class="adr-badge ok">active</span>' : '<span class="adr-badge warn">disqualified</span> ' . adminH($p['status_reason'])) . ((int)($p['exempt'] ?? 0) ? ' <span class="adr-badge">exempt from the one-entry check (you reinstated it)</span>' : '')
        . ' · joined ' . adminH(gmdate('j M Y H:i', (int)$p['created_at'])) . ' UTC · ' . ($d['referrer'] ? 'referred by <a href="/admin/?view=airdrop&amp;tab=participant&amp;id=' . (int)$d['referrer']['id'] . '"><code>' . adminH(adShort($d['referrer']['address'])) . '</code></a>' : 'joined <strong>without a referral link</strong>')
        . ' · Telegram ' . ($p['telegram_id'] ? 'linked' : 'not linked') . ' · own referral code <code>' . adminH($p['ref_code']) . '</code></p></div>'
        . '<div class="adr-earn"><span>Earns</span><strong>' . adminH(number_format($t['centi'] / 100, 2)) . ' XMZ</strong><em>tasks ' . adminH(number_format($t['tasks'] / 100, 2)) . ' · referrals ' . adminH(number_format($t['refs'] / 100, 2)) . ' · adjustments ' . adminH(sprintf('%+.2f', $t['adj'] / 100)) . '</em></div></div>';
    $manage = '<section class="adr-manage"><h2>Manage</h2><div class="adr-mgrid">'
        . '<div><h3>Adjust XMZ</h3><p>Add a bonus or take some away. Shown to the participant as an adjustment.</p>' . $form('airdrop-p-adjust', ['pid' => $id], '<label>XMZ (e.g. 2 or -1.5)<input name="xmz" inputmode="decimal" required pattern="[+-]?[0-9]{1,4}([.][0-9]{1,2})?"></label><label>Reason<input name="reason" maxlength="300" required></label>', 'Save adjustment') . '</div>'
        . '<div><h3>' . ($active ? 'Disqualify' : 'Reinstate') . '</h3>' . ($active
            ? '<p>Earns nothing, cannot submit work, drops off the leaderboard, and stops counting as anyone\'s referral. Reversible.</p>' . $form('airdrop-p-disqualify', ['pid' => $id], '<label>Reason<input name="reason" maxlength="300" required placeholder="e.g. one of several entries from the same person"></label>', 'Disqualify', 'danger')
            : '<p>Restores their earnings from completed work and exempts the entry from the automatic one-entry-per-person check (use for genuine shared connections).</p>' . $form('airdrop-p-reinstate', ['pid' => $id], '', 'Reinstate')) . '</div></div></section>';
    $body = $flash . ($error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '') . airdropTabs('participants') . $head . $manage
        . '<section><h2>Automatic tasks</h2><div class="table-scroll"><table class="adr-table"><thead><tr><th>Task</th><th>Status</th><th class="num">XMZ</th><th></th></tr></thead><tbody>' . $auto . '</tbody></table></div>'
        . '<p class="adr-note">Mining days counted: ' . adminH($days) . ($d['pool_earned'] !== null ? ' · pool earnings seen: ' . adminH(number_format($d['pool_earned'] / 1e11, 4)) . ' XMZ' : '') . '</p></section>'
        . '<section><h2>Submitted work</h2>' . ($subs ? '<div class="table-scroll"><table class="adr-table"><thead><tr><th>Task</th><th>Submitted</th><th>When</th><th>Decision</th><th class="num">XMZ</th><th>Change</th></tr></thead><tbody>' . $subs . '</tbody></table></div>' : '<p>Nothing submitted yet.</p>') . '</section>'
        . '<div class="adr-mgrid"><section><h2>People they referred (' . count($d['referrals']) . ')</h2>' . ($refs ? '<ul class="adr-list">' . $refs . '</ul>' : '<p>None.</p>') . '</section>'
        . '<section><h2>Other entries from the same network</h2><p class="adr-note">Same internet connection when they registered (stored only as a keyed hash). Can be innocent: families, offices, mobile carriers.</p>' . ($same ? '<ul class="adr-list">' . $same . '</ul>' : '<p>None.</p>') . '</section></div>'
        . '<div class="adr-mgrid"><section><h2>Adjustments</h2>' . ($adj ? '<ul class="adr-list">' . $adj . '</ul>' : '<p>None.</p>') . '</section>'
        . '<section><h2>History</h2>' . ($log ? '<ul class="adr-list">' . $log . '</ul>' : '<p>No owner actions yet.</p>') . '</section></div>';
    adminPage('Airdrop participant', $body, $user);
}

// Owner review queue: pending work, by default only from people who joined without a referral link.
// Moderators can review the same items; whoever decides first wins. Owner decisions are credited to
// "console" (no moderator reward).
function airdropQueueSection(array $user): string {
    $show = ($_GET['show'] ?? '') === 'all' ? 'all' : 'organic';
    $counts = adPendingCounts();
    $chips = '<div class="adr-chips"><a class="adr-chip' . ($show === 'organic' ? ' on' : '') . '" href="/admin/?view=airdrop&amp;tab=queue">No referral code <span>' . $counts['organic'] . '</span></a>'
        . '<a class="adr-chip' . ($show === 'all' ? ' on' : '') . '" href="/admin/?view=airdrop&amp;tab=queue&amp;show=all">Everyone <span>' . $counts['all'] . '</span></a></div>';
    $hints = ['share_x' => 'Public post, about Monzero, not deleted, not a bot account', 'share_instagram' => 'Public post about Monzero',
        'forum_post' => 'A genuine first post on the Monzero board under that username', 'forum_active' => '5+ real posts over a week, no spam',
        'run_node' => 'The node answers on that host/port and is not an official seed'];
    $cards = '';
    foreach (adPendingSubmissions($show === 'organic') as $r) {
        $u = (string)$r['url'];
        $detail = preg_match('#^https?://#', $u) ? '<a class="adr-qlink" href="' . adminH($u) . '" target="_blank" rel="noopener noreferrer nofollow">' . adminH(mb_strimwidth($u, 0, 90, '…')) . ' ↗</a>' : '<code class="adr-qlink">' . adminH($u) . '</code>';
        $fields = ['submission' => $r['id'], 'show' => $show === 'all' ? 'all' : ''];
        $cards .= '<article class="adr-q"><div class="adr-qhead"><span class="adr-badge ok">' . adminH(AIRDROP_TASK_LABELS[$r['platform']] ?? $r['platform']) . '</span><span class="reward">+' . adminH((string)adRewardXmz((string)$r['platform'])) . ' XMZ</span>'
            . ($r['referred_by'] ? '<span class="adr-badge">via referral</span>' : '<span class="adr-badge warn">no referral</span>')
            . ($r['same_network'] > 1 ? '<span class="adr-badge warn">' . ($r['same_network'] - 1) . ' other entr' . ($r['same_network'] > 2 ? 'ies' : 'y') . ' on same network</span>' : '')
            . '<span class="adr-when">#' . (int)$r['id'] . ' · submitted ' . adminH(airdropWhen((int)$r['created_at'])) . '</span></div>'
            . '<div class="adr-qbody">' . $detail . '<p class="adr-note">From <a href="/admin/?view=airdrop&amp;tab=participant&amp;id=' . (int)$r['pid'] . '"><code>' . adminH(adShort($r['address'])) . '</code></a> · joined ' . adminH(airdropWhen((int)$r['joined']))
            . ' · ' . (int)$r['approved_before'] . ' approved, ' . (int)$r['rejected_before'] . ' rejected before · Check: ' . adminH($hints[$r['platform']] ?? '') . '</p></div>'
            . '<div class="actions">' . airdropAdminBtn($user, 'airdrop-q-review', $fields + ['decision' => 'approve'], 'Approve') . airdropAdminBtn($user, 'airdrop-q-review', $fields + ['decision' => 'reject'], 'Reject', 'danger') . '</div></article>';
    }
    return '<section><h2>Review queue</h2><p>Work waiting for a decision. It opens on people who joined <strong>without a referral code</strong>; moderators see the same items marked "No referral", and whoever decides first wins. Your decisions are logged as yours and pay no moderator reward. You can still change any decision later in Review activity or on the person\'s page.</p>'
        . $chips . ($cards ? '<div class="adr-queue">' . $cards . '</div>' : '<p class="adr-empty">Nothing waiting. 🎉</p>') . '</section>';
}
