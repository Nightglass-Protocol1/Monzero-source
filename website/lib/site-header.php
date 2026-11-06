<?php
declare(strict_types=1);
// The same navigation definition and markup as utils/website/nav_submenus.py.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once __DIR__ . '/auth.php';

// $current: the path of this page, e.g. /faucet/ (marked aria-current in its submenu).
function siteHeader(string $current): string {
    $config = json_decode((string)file_get_contents(__DIR__ . '/site-nav-submenus.json'), true, 512, JSON_THROW_ON_ERROR);
    $nav = '';
    foreach ($config['links'] as [$href, $label]) {
        $currentTop = $href === $current;
        $top = '<a href="' . authH($href) . '"' . ($currentTop ? ' aria-current="page"' : '') . '>' . authH($label) . '</a>';
        $subs = $config['submenus'][$label] ?? [];
        if (!$subs) { $nav .= $top; continue; }
        $id = 'nav-sub-' . trim((string)preg_replace('/[^a-z]+/', '-', strtolower($label)), '-');
        $here = false;
        foreach ($subs as [$path]) {
            if (str_starts_with($path, '/') && !str_contains($path, '#') && !str_contains($path, '?') &&
                ($path === $current || ($path !== '/' && str_ends_with($path, '/') && str_starts_with($current, $path)))) $here = true;
        }
        $items = implode('', array_map(fn($s) => '<a href="' . authH($s[0]) . '"' . ($s[0] === $current && !$currentTop ? ' aria-current="page"' : '') . '>' . authH($s[1]) . '</a>', $subs));
        $nav .= '<div class="nav-group' . ($here ? ' current' : '') . '">' . $top . '<button class="nav-sub-toggle" type="button" aria-expanded="false" aria-controls="' . $id . '"><span class="visually-hidden">'
            . authH($label) . ' pages</span></button><div class="nav-sub" id="' . $id . '">' . $items . '</div></div>';
    }
    return '<header class="site-header"><a class="brand" href="/" aria-label="Monzero home"><img class="brand-mark brand-coin" src="/assets/monzero-emblem-20261026-96.webp" alt="" width="32" height="32"><span>MONZERO</span></a>'
        . '<button class="menu-toggle" type="button" aria-expanded="false" aria-controls="site-nav">Menu</button><nav id="site-nav" aria-label="Primary navigation">' . $nav . '</nav>'
        . '<a class="header-cta" href="/#get-started">Get started</a></header>';
}
