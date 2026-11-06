<?php
declare(strict_types=1);
// Select one controller before PHP registers its page helper functions.
if (($_GET['view'] ?? '') === 'signup' || ($_GET['do'] ?? '') === 'signup') {
    define('MONZERO_FORUM_SIGNUP', true);
    require dirname(__DIR__, 2) . '/faucet/index.php';
} else {
    define('MONZERO_BOARD_CONTROLLER', true);
    require __DIR__ . '/board-controller.php';
}
