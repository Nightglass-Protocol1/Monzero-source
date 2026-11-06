<?php
// Copy to  <monzero-private>/dex-status.php  on the server to control the DEX bootstrap
// endpoint (/dex/api.php) that the Monzero app fetches. Discovery/display only — never
// fund-authoritative (see the trust model in dex/api.php). Contract addresses are NOT
// configured here; they are pinned in the app build.
return [
    'live'            => false,       // presentation gate; set true only when trading is actually enabled in the app
    'launch_at'       => 1796342400,  // countdown target, unix time (2026-12-04T00:00:00Z)
    'min_app_version' => null,        // e.g. '1.0.0' to ask older apps to update; null = no floor
    // libp2p multiaddrs where makers register, so the app can discover peers. Must start
    // with /ip4 /ip6 /dns4 /dns6 /dnsaddr (others are dropped). A hostile value can only
    // point the app at bad makers; atomicity still prevents theft.
    'rendezvous'      => [
        // '/dns4/rendezvous.monzero.org/tcp/8888/p2p/12D3KooW...'
    ],
];
