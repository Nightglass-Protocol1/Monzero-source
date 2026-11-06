<?php
declare(strict_types=1);
// Non-binding community polls (lib/voting.php). Voting uses faucet accounts.
require dirname(__DIR__, 2) . '/lib/voting.php';
require dirname(__DIR__, 2) . '/lib/site-header.php';

function votePage(string $title, string $body, int $status = 200): never {
    http_response_code($status);
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('Referrer-Policy: same-origin');
    header('X-Frame-Options: DENY');
    echo '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>' . authH($title) . ' · Monzero community voting</title>'
        . '<meta name="description" content="Non-binding Monzero community polls. Faucet members vote once per proposal.">'
        . '<link rel="stylesheet" href="/styles.css?v=20261026-brand"><link rel="stylesheet" href="/theme.css?v=20261026-brand"><link rel="stylesheet" href="/community/community.css?v=20261026-brand"><link rel="stylesheet" href="/community/voting/voting.css?v=20261020-sitewide"><link rel="stylesheet" href="/site-nav.css?v=20261020-sitewide"><link rel="stylesheet" href="/site-shell.css?v=20261026-brand"><noscript><link rel="stylesheet" href="/no-script.css?v=20261020-sitewide"></noscript></head>'
        . '<body class="site-shell"><a class="skip-link" href="#main">Skip to content</a>' . siteHeader('/community/voting/')
        . '<main id="main" tabindex="-1"><p class="eyebrow">COMMUNITY VOTING</p><h1>' . authH($title) . '</h1>' . $body . '</main>'
        . '<footer><p><a href="/community/">Monzero community</a> · <a href="/privacy/">Privacy Policy</a> · <a href="/terms/">Website Terms</a></p></footer>'
        . '<script src="/site-nav.js?v=20261020-sitewide" defer></script></body></html>';
    exit;
}

function voteWhen(int $t): string { return gmdate('Y-m-d H:i', $t) . ' UTC'; }

$notice = '<p class="notice">These polls are <strong>non-binding feedback</strong>. They do not change the Monzero protocol, its software or any funds. Votes come from faucet accounts with a confirmed email and an authenticator code; each account votes once per proposal.</p>';
try { voteDb(); $member = faucetCurrent(); } catch (Throwable $e) { votePage('Voting unavailable', '<p class="notice error">Voting is temporarily unavailable.</p>', 503); }
$now = time();
$slug = is_string($_GET['p'] ?? null) ? $_GET['p'] : '';

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    $proposal = voteProposal($slug);
    if (!$proposal || $proposal['status'] === 'withdrawn') votePage('Not found', '<p>That proposal does not exist.</p>', 404);
    if (!authSameOrigin()) votePage('Not allowed', '<p class="notice error">Request origin not allowed.</p>', 403);
    if (!$member || !hash_equals($member['csrf'], (string)($_POST['csrf'] ?? ''))) votePage('Form expired', '<p class="notice error">Your form expired. Reload the page and try again.</p>', 403);
    $option = filter_var($_POST['option'] ?? null, FILTER_VALIDATE_INT);
    $error = voteCast($proposal, $member, $option === false ? -1 : $option, $now);
    if ($error) votePage($proposal['title'], '<p class="notice error" role="alert">' . authH($error) . '</p><p><a href="?p=' . authH($slug) . '">Back to the proposal</a></p>', 400);
    header('Location: /community/voting/?' . http_build_query(['p' => $slug, 'voted' => 1]), true, 303);
    exit;
}

if ($slug !== '') {
    $p = voteProposal($slug);
    if (!$p || $p['status'] === 'withdrawn') votePage('Not found', '<p>That proposal does not exist or was withdrawn.</p><p><a href="/community/voting/">All proposals</a></p>', 404);
    $results = voteResults($p);
    $mine = $member ? voteOf($p, (int)$member['id']) : null;
    $open = voteOpen($p, $now);
    $body = $notice . '<p class="meta">' . authH(voteState($p, $now)) . ' · opened ' . voteWhen((int)$p['opens_at']) . ' · ' . ($open ? 'closes ' : 'closed ') . voteWhen((int)$p['closes_at']) . '</p>'
        . '<div class="proposal-text">' . implode('', array_map(fn($para) => '<p>' . authH($para) . '</p>', preg_split('/\n+/', $p['description']))) . '</div>';
    if (isset($_GET['voted']) && $mine !== null) $body .= '<p class="notice" role="status">Your vote was recorded.</p>';
    if ($open && $mine === null) {
        $block = voteBlock($p, $member, $now);
        if ($block) {
            $body .= '<p class="notice">' . authH($block) . ($member ? '' : ' <a href="/faucet/">Sign in or create an account</a>.') . '</p>';
        } else {
            $body .= '<form method="post" action="?p=' . authH($slug) . '" class="vote-form"><fieldset><legend>Your vote (final once cast)</legend><input type="hidden" name="csrf" value="' . authH($member['csrf']) . '">';
            foreach ($results['options'] as $i => $o) $body .= '<label class="choice"><input type="radio" name="option" value="' . $i . '" required> ' . authH($o) . '</label>';
            $body .= '</fieldset><button type="submit">Cast vote</button></form>';
        }
    }
    if ($mine !== null || !$open) {
        $rows = '';
        foreach ($results['options'] as $i => $o) {
            $n = $results['counts'][$i];
            $pct = $results['total'] ? round(100 * $n / $results['total'], 1) : 0;
            $rows .= '<li' . ($mine === $i ? ' class="mine"' : '') . '><div class="bar-label"><span>' . authH($o) . ($mine === $i ? ' (your vote)' : '') . '</span><span>' . $n . ' · ' . $pct . '%</span></div>'
                . '<meter min="0" max="100" value="' . $pct . '">' . $pct . '%</meter></li>';
        }
        $body .= '<section class="results"><h2>' . ($open ? 'Results so far' : 'Final results') . '</h2><p>' . $results['total'] . ' ' . ($results['total'] === 1 ? 'vote' : 'votes') . '.</p><ol>' . $rows . '</ol></section>';
    } else {
        $body .= '<p>Results are shown after you vote, or when voting closes.</p>';
    }
    votePage($p['title'], $body . '<p><a href="/community/voting/">All proposals</a></p>');
}

$list = '';
foreach (voteProposals() as $p) {
    $results = voteResults($p);
    $list .= '<li><a href="?p=' . authH($p['slug']) . '">' . authH($p['title']) . '</a><span class="meta">' . authH(voteState($p, $now)) . ' · ' . $results['total'] . ' ' . ($results['total'] === 1 ? 'vote' : 'votes')
        . ' · ' . (voteOpen($p, $now) ? 'closes ' : 'closed ') . voteWhen((int)$p['closes_at']) . '</span></li>';
}
votePage('Community voting', $notice . ($list ? '<ol class="proposals">' . $list . '</ol>' : '<p>No proposals yet. Maintainers post proposals here when they want community feedback.</p>')
    . '<p>To vote, <a href="/faucet/">sign in to your faucet account</a> (accounts need a confirmed email and an authenticator app).</p>');
