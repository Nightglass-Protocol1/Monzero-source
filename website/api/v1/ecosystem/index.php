<?php
declare(strict_types=1);
require dirname(__DIR__) . '/lib.php';
v1Begin(300);
$data = v1Catalog('ecosystem/catalog.json', 'monzero-ecosystem-v1');
v1Reply(['records' => $data['records'], 'catalog_updated_at' => $data['updated_at']]);
