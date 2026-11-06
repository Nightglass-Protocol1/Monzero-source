<?php
// Copy to  <monzero-private>/airdrop-config.php  on the web server and edit.
// Model: each task pays a fixed amount of XMZ (harder tasks pay more); a participant can earn
// at most 'per_person_cap' XMZ. The 'pool_xmz' budget is funded by the mining pool's 1% fee and
// topped up automatically (see utils/website/airdrop-topup.sh). Payout is one batch at the end.
return [
    'enabled'        => false,            // set true to open the airdrop
    'pool_xmz'       => 8000,             // budget for this round (from the pool fee); auto-renewed
    'per_person_cap' => 50,               // most XMZ one participant can earn from TASKS (they sum to this)
    'referral_cap'   => 25,               // max XMZ one participant can earn from referrals (separate track)
    'admin_reward_xmz' => 0.5,            // XMZ paid to a moderator for each confirmed (approved) submission
    'auto_renew'     => true,             // let airdrop-topup.sh keep pool_xmz funded from the fee
    'mining_days_required' => 7,          // distinct days a miner must be active to earn the mining reward
    'max_per_ip'     => 3,                // max airdrop entries per IP (0 = no limit). Lenient for shared NAT.
    'trusted_proxies' => [],              // e.g. ['173.245.48.0/20'] if a CDN sets CF-Connecting-IP / X-Real-IP
    'starts_at'      => 0,                // unix time, 0 = now
    'ends_at'        => 0,                // unix time ~30 days out, e.g. time()+2592000 — set a real end
    'admin_token'    => '',               // optional master key; then open /airdrop/admin.php?key=THAT
    // Moderators sign in at /airdrop/admin.php with their XMZ address + Telegram handle +
    // password + PIN (all four must match). Approvals are credited to them and paid to 'address'.
    // password/pin may be plaintext here (this file is private, mode 600) or bcrypt hashes
    // (php -r "echo password_hash('secret', PASSWORD_DEFAULT);").
    'admins' => [
        // ['name' => 'alice', 'address' => 'FW6V...alice', 'telegram' => '@alice', 'password' => 'change-me', 'pin' => '1234'],
        // ['name' => 'bob',   'address' => 'FW6V...bob',   'telegram' => '@bob',   'password' => 'change-me', 'pin' => '5678'],
    ],
    // XMZ each task pays (harder = more). Core tasks sum to 45 (cap 50). Referrals are a separate
    // track: referral XMZ per verified referral, up to referral_cap.
    'rewards'        => [
        'wallet' => 2, 'telegram' => 2, 'mining' => 12,
        'forum_post' => 5, 'forum_active' => 10, 'run_node' => 8,
        'share_x' => 3, 'share_instagram' => 3, 'referral' => 1,
    ],
    'x'         => 'MonzeroXMZ',
    'instagram' => 'monzeroxmz',
    'telegram'  => '@MonzeroOfficial',
];
