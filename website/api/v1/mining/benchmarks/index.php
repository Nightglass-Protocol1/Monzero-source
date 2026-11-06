<?php
declare(strict_types=1);
require dirname(__DIR__, 2) . '/lib.php';
v1Begin(300);
$data = v1Catalog('mining/benchmarks/catalog.json', 'monzero-randomx-benchmarks-v1');
v1Reply(['records' => $data['records'], 'catalog_updated_at' => $data['updated_at'],
    'note' => 'Each record carries its own verification status; community estimates are not verified measurements.']);
