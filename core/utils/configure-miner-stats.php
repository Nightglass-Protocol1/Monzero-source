<?php
declare(strict_types=1);

if (PHP_SAPI !== 'cli' || $argc !== 3) {
    fwrite(STDERR, "Usage: php configure-miner-stats.php CONFIG_PATH DATA_PATH\n");
    exit(1);
}

$configPath = $argv[1];
$dataPath = $argv[2];
if (file_exists($configPath)) {
    fwrite(STDERR, "Refusing to overwrite existing configuration: {$configPath}\n");
    exit(1);
}

$serverSecret = bin2hex(random_bytes(32));
$ingestToken = bin2hex(random_bytes(32));
$config = "<?php\nreturn " . var_export([
    'MONZERO_STATS_SECRET' => $serverSecret,
    'MONZERO_STATS_INGEST_TOKEN' => $ingestToken,
    'MONZERO_STATS_FILE' => $dataPath,
], true) . ";\n";

if (file_put_contents($configPath, $config, LOCK_EX) === false) {
    fwrite(STDERR, "Unable to create configuration.\n");
    exit(1);
}
chmod($configPath, 0600);

fwrite(STDOUT, "Configuration created. Team ingest token (save privately):\n{$ingestToken}\n");
