<?php
declare(strict_types=1);
// Server-side checks for the reviewed site catalogs, mirroring the browser validators in
// exchanges/catalog.mjs, ecosystem/catalog.mjs, community/bounties/catalog.mjs and
// mining/benchmarks/catalog.mjs. They may be stricter than the browser, never looser: a catalog
// saved from /admin must always render (tests/website/test_admin_content.py cross-checks both).
// Input is decoded with objects as stdClass, so {} and [] stay distinct, as in JavaScript.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }

const CATALOG_JS_SPACE = '\t\n\x{0B}\f\r \x{A0}\x{1680}\x{2000}-\x{200A}\x{2028}\x{2029}\x{202F}\x{205F}\x{3000}\x{FEFF}';

final class CatalogError extends Exception {}

function catalogFail(string $message): never { throw new CatalogError($message); }

// JavaScript string length counts UTF-16 code units.
function catalogJsLength(string $v): int { return intdiv(strlen((string)mb_convert_encoding($v, 'UTF-16LE', 'UTF-8')), 2); }

function catalogTrimmed(string $v): bool { return !preg_match('/\A[' . CATALOG_JS_SPACE . ']|[' . CATALOG_JS_SPACE . ']\z/u', $v); }

// text(): non-empty, no surrounding whitespace, bounded, no control/format characters.
function catalogText(mixed $v, int $max): bool {
    return is_string($v) && mb_check_encoding($v, 'UTF-8') && $v !== '' && catalogTrimmed($v) && catalogJsLength($v) <= $max
        && !preg_match('/[\p{Cc}\p{Cf}\p{Cs}\x{2028}\x{2029}]/u', $v);
}

// prose(): like text() but newlines are allowed.
function catalogProse(mixed $v, int $max): bool { return is_string($v) && catalogText(str_replace("\n", ' ', $v), $max); }

function catalogKeys(mixed $o, array $names): bool {
    if (!$o instanceof stdClass) return false;
    $keys = array_keys(get_object_vars($o));
    sort($keys); sort($names);
    return $keys === $names;
}

function catalogDate(mixed $v): bool {
    return is_string($v) && preg_match('/\A(\d{4})-(\d{2})-(\d{2})\z/', $v, $m) === 1 && checkdate((int)$m[2], (int)$m[3], (int)$m[1]);
}

// Absolute https URL with a plain host (stricter than the browser's URL parser).
function catalogHttps(mixed $v, int $max = 2048): bool {
    return catalogText($v, $max) && !preg_match('/[\s' . CATALOG_JS_SPACE . '\\\\]/u', $v)
        && preg_match('~\Ahttps://[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?)*(?::[0-9]{1,5})?(?:[/?#][\x21-\x7E]*)?\z~', $v) === 1;
}

// link() in exchanges/catalog.mjs: https URL or a same-site path such as /fund/.
function catalogLink(mixed $v): bool {
    if (!catalogText($v, 2048) || preg_match('/[\s' . CATALOG_JS_SPACE . ']/u', $v)) return false;
    if (preg_match('#\A/(?!/)#', $v)) return !str_contains($v, '\\') && preg_match('#\A/[\x21-\x7E]*\z#', $v) === 1;
    return catalogHttps($v);
}

function catalogId(mixed $v, string $pattern = '/\A[a-z0-9]+(?:-[a-z0-9]+)*\z/'): bool { return catalogText($v, 64) && preg_match($pattern, $v) === 1; }

function catalogTop(mixed $data, string $schema, int $max, string $today): void {
    if (!catalogKeys($data, ['schema', 'updated_at', 'records'])) catalogFail('The catalog must have exactly schema, updated_at and records.');
    if ($data->schema !== $schema) catalogFail("schema must be $schema.");
    if (!catalogDate($data->updated_at) || $data->updated_at > $today) catalogFail('updated_at must be a date no later than today.');
    if (!is_array($data->records) || count($data->records) > $max) catalogFail("records must be a list of at most $max entries.");
}

function catalogRecordName(mixed $r, int $i): string { return $r instanceof stdClass && is_string($r->id ?? null) ? "Record \"" . mb_substr($r->id, 0, 64) . "\"" : 'Record ' . ($i + 1); }

function catalogAmount(mixed $v): bool { return is_string($v) && preg_match('/\A(0|[1-9]\d{0,11})(\.\d{1,2})?\z/', $v) === 1 && !preg_match('/\A0(\.0+)?\z/', $v); }

function catalogExchanges(mixed $data, string $today): void {
    catalogTop($data, 'monzero-exchanges-v1', 100, $today);
    $statuses = ['planned', 'application-submitted', 'under-review', 'approved', 'live', 'suspended'];
    $keys = explode(' ', 'id exchange status note updated_at application_date listing_fee funding announcements pair trading_url deposits withdrawals trading last_verified verification_url');
    $venue = ['pair', 'trading_url', 'deposits', 'withdrawals', 'trading', 'last_verified', 'verification_url'];
    $ids = [];
    foreach ($data->records as $i => $r) {
        $n = catalogRecordName($r, $i);
        if (!catalogKeys($r, $keys)) catalogFail("$n must have exactly these fields: " . implode(', ', $keys) . '.');
        if (!catalogId($r->id) || isset($ids[$r->id])) catalogFail("$n: id must be unique lowercase words joined by hyphens."); $ids[$r->id] = true;
        if (!catalogText($r->exchange, 80)) catalogFail("$n: exchange must be 1-80 characters.");
        if (!is_string($r->status) || !in_array($r->status, $statuses, true)) catalogFail("$n: status must be one of " . implode(', ', $statuses) . '.');
        if (!catalogText($r->note, 600)) catalogFail("$n: note must be 1-600 characters on one line.");
        if (!catalogDate($r->updated_at) || $r->updated_at > $data->updated_at) catalogFail("$n: updated_at must be a date no later than today.");
        if ($r->status === 'planned' ? $r->application_date !== null : (!catalogDate($r->application_date) || $r->application_date > $r->updated_at))
            catalogFail("$n: application_date must be null while planned, otherwise a date no later than updated_at.");
        if ($r->listing_fee !== null && (!catalogKeys($r->listing_fee, ['amount', 'currency', 'evidence_url']) || !catalogAmount($r->listing_fee->amount)
            || !is_string($r->listing_fee->currency) || !preg_match('/\A[A-Z]{3,5}\z/', $r->listing_fee->currency) || !catalogLink($r->listing_fee->evidence_url)))
            catalogFail("$n: listing_fee must be null or {amount, currency, evidence_url}.");
        if ($r->funding !== null && (!catalogKeys($r->funding, ['target', 'currency', 'progress_url']) || !catalogAmount($r->funding->target)
            || !is_string($r->funding->currency) || !preg_match('/\A[A-Z]{3,5}\z/', $r->funding->currency) || !catalogLink($r->funding->progress_url)))
            catalogFail("$n: funding must be null or {target, currency, progress_url}.");
        if (!is_array($r->announcements) || count($r->announcements) > 10) catalogFail("$n: announcements must be a list of at most 10.");
        foreach ($r->announcements as $a) if (!catalogKeys($a, ['label', 'url']) || !catalogText($a->label, 160) || !catalogLink($a->url)) catalogFail("$n: each announcement needs a label and a link.");
        if ($r->status === 'live' || $r->status === 'suspended') {
            if (!catalogText($r->pair, 24) || !preg_match('#\AXMZ/[A-Z0-9]{2,10}\z#', $r->pair)) catalogFail("$n: pair must look like XMZ/USDT.");
            if (!catalogHttps($r->trading_url)) catalogFail("$n: trading_url must be an https link.");
            if (!in_array($r->deposits, ['enabled', 'disabled'], true) || !in_array($r->withdrawals, ['enabled', 'disabled'], true)) catalogFail("$n: deposits and withdrawals must be enabled or disabled.");
            if ($r->trading !== ($r->status === 'live' ? 'active' : 'halted')) catalogFail("$n: trading must be active when live and halted when suspended.");
            if (!catalogDate($r->last_verified) || $r->last_verified > $r->updated_at || !catalogLink($r->verification_url)) catalogFail("$n: a live or suspended listing needs last_verified and verification_url.");
        } else {
            foreach ($venue as $k) if ($r->$k !== null) catalogFail("$n: $k must be null until the listing is live.");
        }
    }
}

function catalogEcosystem(mixed $data, string $today): void {
    catalogTop($data, 'monzero-ecosystem-v1', 500, $today);
    $categories = ['Mining pools', 'Wallet applications', 'Block explorers', 'Community projects', 'Developer libraries', 'Exchanges', 'Merchants', 'Educational resources'];
    $ids = [];
    foreach ($data->records as $i => $r) {
        $n = catalogRecordName($r, $i);
        if (!catalogKeys($r, ['id', 'name', 'category', 'operator', 'description', 'url', 'verified_at'])) catalogFail("$n must have exactly id, name, category, operator, description, url and verified_at.");
        if (!catalogId($r->id) || isset($ids[$r->id])) catalogFail("$n: id must be unique lowercase words joined by hyphens."); $ids[$r->id] = true;
        if (!catalogText($r->name, 100) || !catalogText($r->description, 400)) catalogFail("$n: name (1-100) and description (1-400 characters) are required.");
        if (!is_string($r->category) || !in_array($r->category, $categories, true) || $r->category === 'Exchanges')
            catalogFail("$n: category must be one of " . implode(', ', array_diff($categories, ['Exchanges'])) . ' (exchanges go in the exchange tracker).');
        if (!in_array($r->operator, ['project', 'third-party'], true)) catalogFail("$n: operator must be project or third-party.");
        if (!catalogLink($r->url) || ($r->operator === 'third-party' && !str_starts_with($r->url, 'https://'))) catalogFail("$n: url must be a link (third-party services need an https link).");
        if (!catalogDate($r->verified_at) || $r->verified_at > $data->updated_at) catalogFail("$n: verified_at must be a date no later than today.");
    }
}

function catalogBounties(mixed $data, string $today): void {
    catalogTop($data, 'monzero-bounties-v1', 200, $today);
    $categories = ['Bug fixes', 'Security improvements', 'Wallet improvements', 'Documentation', 'Translations', 'Website features', 'Blockchain integrations'];
    $keys = explode(' ', 'id title description category difficulty acceptance_criteria submission_instructions discussion_url status review_status updated_at maintainer reviewed_by reward_currency reward_atomic funding_status funding_evidence_url funding_checked_at completion_evidence_url completion_checked_at payment_evidence_url payment_checked_at');
    $review = ['open' => 'awaiting-submission', 'in-review' => 'under-review', 'completed' => 'accepted', 'cancelled' => 'closed'];
    $ids = [];
    foreach ($data->records as $i => $b) {
        $n = catalogRecordName($b, $i);
        if (!catalogKeys($b, $keys)) catalogFail("$n must have exactly these fields: " . implode(', ', $keys) . '.');
        if (!catalogId($b->id) || isset($ids[$b->id])) catalogFail("$n: id must be unique lowercase words joined by hyphens."); $ids[$b->id] = true;
        if (!catalogText($b->title, 160) || !catalogProse($b->description, 4000)) catalogFail("$n: title (1-160) and description (1-4000 characters) are required.");
        if (!is_string($b->category) || !in_array($b->category, $categories, true)) catalogFail("$n: category must be one of " . implode(', ', $categories) . '.');
        if (!in_array($b->difficulty, ['beginner', 'intermediate', 'advanced'], true)) catalogFail("$n: difficulty must be beginner, intermediate or advanced.");
        if (!is_array($b->acceptance_criteria) || count($b->acceptance_criteria) < 1 || count($b->acceptance_criteria) > 20) catalogFail("$n: acceptance_criteria must list 1-20 items.");
        foreach ($b->acceptance_criteria as $c) if (!catalogText($c, 1000)) catalogFail("$n: each acceptance criterion must be 1-1000 characters on one line.");
        if (!catalogProse($b->submission_instructions, 3000) || !catalogHttps($b->discussion_url)) catalogFail("$n: submission_instructions and an https discussion_url are required.");
        if (!is_string($b->status) || !isset($review[$b->status]) || $b->review_status !== $review[$b->status])
            catalogFail("$n: status/review_status must be open/awaiting-submission, in-review/under-review, completed/accepted or cancelled/closed.");
        if (!catalogDate($b->updated_at) || $b->updated_at > $data->updated_at || !catalogText($b->maintainer, 120) || !catalogText($b->reviewed_by, 120))
            catalogFail("$n: updated_at, maintainer and reviewed_by are required.");
        if ($b->reward_currency !== 'XMZ' || !is_string($b->reward_atomic) || !preg_match('/\A(0|[1-9][0-9]{0,19})\z/', $b->reward_atomic)
            || (strlen($b->reward_atomic) === 20 && strcmp($b->reward_atomic, '18446744073709551615') > 0)) catalogFail("$n: reward must be XMZ with reward_atomic as a whole number string.");
        if (!in_array($b->funding_status, ['none', 'proposed', 'funded', 'paid'], true)) catalogFail("$n: funding_status must be none, proposed, funded or paid.");
        if ($b->funding_status === 'none' ? $b->reward_atomic !== '0' : $b->reward_atomic === '0') catalogFail("$n: reward_atomic must be 0 exactly when funding_status is none.");
        if (in_array($b->funding_status, ['funded', 'paid'], true)) {
            if (!catalogHttps($b->funding_evidence_url) || !catalogDate($b->funding_checked_at) || $b->funding_checked_at > $b->updated_at) catalogFail("$n: funded bounties need funding_evidence_url and funding_checked_at.");
        } elseif ($b->funding_evidence_url !== null || $b->funding_checked_at !== null) catalogFail("$n: funding evidence must be null until funded.");
        if ($b->status === 'cancelled' && $b->funding_status === 'funded') catalogFail("$n: a cancelled bounty cannot stay funded.");
        if ($b->status === 'completed') {
            if (!catalogHttps($b->completion_evidence_url) || !catalogDate($b->completion_checked_at) || $b->completion_checked_at > $b->updated_at) catalogFail("$n: completed bounties need completion evidence and a check date.");
        } elseif ($b->completion_evidence_url !== null || $b->completion_checked_at !== null) catalogFail("$n: completion evidence must be null until completed.");
        if ($b->funding_status === 'paid') {
            if ($b->status !== 'completed' || !catalogHttps($b->payment_evidence_url) || !catalogDate($b->payment_checked_at) || $b->payment_checked_at > $b->updated_at) catalogFail("$n: paid bounties must be completed and need payment evidence.");
        } elseif ($b->payment_evidence_url !== null || $b->payment_checked_at !== null) catalogFail("$n: payment evidence must be null until paid.");
    }
}

// Benchmark text: non-blank (surrounding spaces allowed), at most 200 characters, no controls or line breaks.
function catalogBenchText(mixed $v): bool {
    return is_string($v) && mb_check_encoding($v, 'UTF-8') && trim($v, " \t\n\r\0\x0B\u{A0}\u{FEFF}") !== '' && preg_match('/[^' . CATALOG_JS_SPACE . ']/u', $v) === 1
        && catalogJsLength($v) <= 200 && !preg_match('/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}]/u', $v);
}

function catalogBenchUrl(mixed $v): bool { return catalogHttps($v) && !str_contains(explode('/', substr((string)$v, 8), 2)[0], '@'); }

// Comparable form of an evidence link: lowercase host, no default port, fragment or empty query.
function catalogEvidence(string $url): string {
    $url = (string)preg_replace('/#.*\z/s', '', $url);
    $url = (string)preg_replace('/\?\z/', '', $url);
    if (preg_match('#\Ahttps://([^/?]+)(.*)\z#s', $url, $m)) $url = 'https://' . strtolower((string)preg_replace('/:443\z/', '', $m[1])) . ($m[2] === '' ? '/' : $m[2]);
    return rawurldecode($url);
}

function catalogPositive(mixed $v): bool { return (is_int($v) || is_float($v)) && is_finite((float)$v) && $v > 0; }

function catalogBenchmarks(mixed $data, string $today): void {
    if (!$data instanceof stdClass || count(get_object_vars($data)) !== 3 || ($data->schema ?? null) !== 'monzero-randomx-benchmarks-v1' || !catalogDate($data->updated_at ?? null)
        || $data->updated_at > $today || !is_array($data->records ?? null) || count($data->records) > 5000) catalogFail('The catalog must have schema monzero-randomx-benchmarks-v1, a dated updated_at and at most 5000 records.');
    $fields = ['id', 'manufacturer', 'model', 'os', 'miner', 'miner_version', 'methodology', 'cores', 'threads', 'hashrate_hps', 'power_watts', 'power_basis', 'date', 'status', 'source_url', 'verification_url', 'verified_by'];
    $ids = [];
    foreach ($data->records as $i => $r) {
        $n = catalogRecordName($r, $i);
        if (!catalogKeys($r, $fields)) catalogFail("$n must have exactly these fields: " . implode(', ', $fields) . '.');
        if (!is_string($r->id) || !preg_match('/\A[a-z0-9][a-z0-9-]{0,79}\z/', $r->id) || isset($ids[$r->id])) catalogFail("$n: id must be unique, lowercase letters, digits and hyphens."); $ids[$r->id] = true;
        foreach (['manufacturer', 'model', 'os', 'miner', 'miner_version', 'methodology'] as $k) if (!catalogBenchText($r->$k)) catalogFail("$n: $k must be 1-200 characters.");
        if (!is_int($r->cores) || $r->cores < 1 || $r->cores > 4096 || !is_int($r->threads) || $r->threads < $r->cores || $r->threads > 16384) catalogFail("$n: cores (1-4096) and threads (at least cores) must be whole numbers.");
        if (!catalogPositive($r->hashrate_hps) || ($r->power_watts !== null && !catalogPositive($r->power_watts))) catalogFail("$n: hashrate_hps must be positive; power_watts positive or null.");
        if (!in_array($r->power_basis, ['wall', 'cpu-package', 'unknown'], true) || (($r->power_watts === null) !== ($r->power_basis === 'unknown'))) catalogFail("$n: power_basis must be unknown exactly when power_watts is null (otherwise wall or cpu-package).");
        if (!catalogDate($r->date) || $r->date > $data->updated_at) catalogFail("$n: date must be a date no later than today.");
        if (!in_array($r->status, ['community', 'verified'], true) || !catalogBenchUrl($r->source_url)) catalogFail("$n: status must be community or verified, with an https source_url.");
        if ($r->status === 'verified') {
            if (!catalogBenchUrl($r->verification_url) || !catalogBenchText($r->verified_by) || catalogEvidence($r->verification_url) === catalogEvidence($r->source_url))
                catalogFail("$n: verified results need a different verification_url and verified_by.");
        } elseif ($r->verification_url !== null || $r->verified_by !== null) catalogFail("$n: community results must have verification_url and verified_by null.");
    }
}

// Announcements and roadmap updates (edited from /admin): dated short posts.
function catalogPosts(mixed $data, string $schema, string $today): void {
    catalogTop($data, $schema, 200, $today);
    $ids = [];
    foreach ($data->records as $i => $p) {
        $n = catalogRecordName($p, $i);
        if (!catalogKeys($p, ['id', 'date', 'title', 'body', 'url'])) catalogFail("$n must have exactly id, date, title, body and url.");
        if (!catalogId($p->id) || isset($ids[$p->id])) catalogFail("$n: id must be unique."); $ids[$p->id] = true;
        if (!catalogDate($p->date) || $p->date > $data->updated_at) catalogFail("$n: date must be no later than today.");
        if (!catalogText($p->title, 120) || !catalogProse($p->body, 1500)) catalogFail("$n: title (1-120) and text (1-1500 characters) are required.");
        if ($p->url !== null && !catalogLink($p->url)) catalogFail("$n: the link must be an https link or a path such as /roadmap/.");
    }
}

// Fundraising goals (fund/goals.json), in the order donations fill them.
function catalogFundGoals(mixed $data, string $today): void {
    catalogTop($data, 'monzero-fund-goals-v1', 10, $today);
    if (count($data->records) < 1) catalogFail('Keep at least one goal; close a goal instead of removing the last one.');
    $ids = [];
    foreach ($data->records as $i => $g) {
        $n = catalogRecordName($g, $i);
        if (!catalogKeys($g, ['id', 'title', 'description', 'target_usd', 'status'])) catalogFail("$n must have exactly id, title, description, target_usd and status.");
        if (!catalogId($g->id) || isset($ids[$g->id])) catalogFail("$n: id must be unique lowercase words joined by hyphens."); $ids[$g->id] = true;
        if (!catalogText($g->title, 120) || !catalogProse($g->description, 2000)) catalogFail("$n: title (1-120) and description (1-2000 characters) are required.");
        if (!is_string($g->target_usd) || !preg_match('/\A[1-9]\d{0,8}\z/', $g->target_usd)) catalogFail("$n: target_usd must be a whole number of US dollars as a string, for example \"15000\".");
        if (!in_array($g->status, ['open', 'reached', 'closed'], true)) catalogFail("$n: status must be open, reached or closed.");
    }
}

// Spending records (fund/spending.json): every spend over $100 needs an evidence link.
function catalogFundSpending(mixed $data, string $today): void {
    catalogTop($data, 'monzero-fund-spending-v1', 500, $today);
    $ids = [];
    foreach ($data->records as $i => $s) {
        $n = catalogRecordName($s, $i);
        if (!catalogKeys($s, ['id', 'date', 'goal', 'description', 'paid_to', 'amount', 'currency', 'amount_usd', 'evidence_url'])) catalogFail("$n must have exactly id, date, goal, description, paid_to, amount, currency, amount_usd and evidence_url.");
        if (!catalogId($s->id) || isset($ids[$s->id])) catalogFail("$n: id must be unique."); $ids[$s->id] = true;
        if (!catalogDate($s->date) || $s->date > $data->updated_at) catalogFail("$n: date must be no later than today.");
        if (!catalogId($s->goal)) catalogFail("$n: goal must be a goal id from the goals list, or general.");
        if (!catalogText($s->description, 400) || !catalogText($s->paid_to, 120)) catalogFail("$n: description (1-400) and paid_to (1-120 characters) are required.");
        if (!is_string($s->amount) || !preg_match('/\A(0|[1-9]\d{0,17})(\.\d{1,11})?\z/', $s->amount) || preg_match('/\A0(\.0+)?\z/', $s->amount)) catalogFail("$n: amount must be a positive number as a string.");
        if (!is_string($s->currency) || !preg_match('/\A[A-Z]{3,5}\z/', $s->currency)) catalogFail("$n: currency must be a code such as USD, BTC or XMZ.");
        if (!is_string($s->amount_usd) || !preg_match('/\A(0|[1-9]\d{0,8})(\.\d{2})?\z/', $s->amount_usd)) catalogFail("$n: amount_usd must be US dollars as a string, for example \"1500.00\".");
        if ($s->evidence_url === null ? (float)$s->amount_usd > 100 : !catalogLink($s->evidence_url)) catalogFail("$n: spends over $100 need an evidence_url (https link or a path on this site).");
    }
}

// Returns null when valid, otherwise a message for the editor.
function catalogProblem(string $kind, string $json, string $today): ?string {
    try { $data = json_decode($json, false, 32, JSON_THROW_ON_ERROR); } catch (JsonException $e) { return 'Not valid JSON: ' . $e->getMessage() . '.'; }
    try {
        match ($kind) {
            'exchanges' => catalogExchanges($data, $today),
            'ecosystem' => catalogEcosystem($data, $today),
            'bounties' => catalogBounties($data, $today),
            'benchmarks' => catalogBenchmarks($data, $today),
            'announcements' => catalogPosts($data, 'monzero-announcements-v1', $today),
            'roadmap-updates' => catalogPosts($data, 'monzero-roadmap-updates-v1', $today),
            'fund-goals' => catalogFundGoals($data, $today),
            'fund-spending' => catalogFundSpending($data, $today),
            'founder-disclosures' => catalogFounderDisclosures($data, $today),
        };
    } catch (CatalogError $e) { return $e->getMessage(); }
    catch (Throwable $e) { return 'The catalog has the wrong shape.'; }
    return null;
}

function catalogFounderDisclosures(mixed $data, string $today): void {
    catalogTop($data, 'monzero-founder-disclosures-v1', 120, $today);
    $fields = ['id','date','height','address','view_key','reserve_proof_url','reserve_proof_sha256','proof_message','proven_xmz','note'];
    $ids = [];
    foreach ($data->records as $i => $r) {
        $n = catalogRecordName($r, $i);
        if (!catalogKeys($r, $fields) || !catalogId($r->id) || isset($ids[$r->id])) catalogFail("$n has invalid fields or duplicate id."); $ids[$r->id] = true;
        if (!catalogDate($r->date) || $r->date > $data->updated_at || !is_int($r->height) || $r->height < 1 || $r->height > 1000000000) catalogFail("$n has an invalid date or height.");
        if (!catalogText($r->address, 95) || !preg_match('/\AF[1-9A-HJ-NP-Za-km-z]{94}\z/', $r->address)) catalogFail("$n address is invalid.");
        if ($r->view_key !== null && (!is_string($r->view_key) || !preg_match('/\A[0-9a-f]{64}\z/', $r->view_key))) catalogFail("$n view_key is invalid.");
        $proof = $r->reserve_proof_url !== null;
        if ($proof !== ($r->reserve_proof_sha256 !== null) || $proof !== ($r->proof_message !== null) || $proof !== ($r->proven_xmz !== null)) catalogFail("$n reserve proof fields must be all present or all null.");
        if ($proof && (!is_string($r->reserve_proof_url) || !preg_match('#\A/founder/proofs/[a-z0-9-]{1,64}\.txt\z#', $r->reserve_proof_url) || !preg_match('/\A[0-9a-f]{64}\z/', (string)$r->reserve_proof_sha256) || !catalogText($r->proof_message, 200) || !is_string($r->proven_xmz) || !preg_match('/\A(0|[1-9]\d{0,11})(\.\d{1,11})?\z/', $r->proven_xmz))) catalogFail("$n reserve proof is invalid.");
        if ($r->note !== null && !catalogProse($r->note, 1500)) catalogFail("$n note is invalid.");
    }
}
