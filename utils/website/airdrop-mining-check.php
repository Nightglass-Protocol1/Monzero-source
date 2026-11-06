<?php
// Record a mining day for every airdrop participant currently mining on the pool, and credit the
// mining task once they reach the required number of distinct active days. Run on a schedule on
// the web server (e.g. every 6 hours) so sustained mining accrues even when people aren't looking:
//   MONZERO_PRIVATE_DIR=~/monzero-private php utils/website/airdrop-mining-check.php
declare(strict_types=1);
require dirname(__DIR__) . '/website/lib/airdrop.php';
$db = adDb();
$n = 0; $credited = 0;
foreach ($db->query('SELECT * FROM participants')->fetchAll() as $p) {
    $before = (bool)($db->query('SELECT 1 FROM progress WHERE participant_id = ' . (int)$p['id'] . " AND task = 'mining'")->fetchColumn());
    adCheckMining($p);
    $after = (bool)($db->query('SELECT 1 FROM progress WHERE participant_id = ' . (int)$p['id'] . " AND task = 'mining'")->fetchColumn());
    $n++; if (!$before && $after) $credited++;
    usleep(100000); // be gentle on the pool API
}
fwrite(STDERR, date('c') . " checked $n participants, newly credited $credited mining tasks\n");
