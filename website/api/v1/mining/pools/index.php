<?php
declare(strict_types=1);
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(300);
$data = v1Catalog('ecosystem/catalog.json', 'monzero-ecosystem-v1');
$pools = [];
foreach ($data['records'] as $r) {
    if (is_array($r) && ($r['category'] ?? null) === 'Mining pools')
        $pools[] = ['id' => $r['id'] ?? null, 'name' => $r['name'] ?? null, 'operator' => $r['operator'] ?? null,
            'url' => $r['url'] ?? null, 'verified_at' => $r['verified_at'] ?? null];
}
v1Reply(['pools' => $pools, 'catalog_updated_at' => $data['updated_at'],
    'note' => 'Listing is not an endorsement. Check each pool for its current fee and payout terms.']);
