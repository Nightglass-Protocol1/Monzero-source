<?php
declare(strict_types=1);
// Server-side Monzero public address validation, mirroring get_account_address_from_str
// (src/cryptonote_basic/cryptonote_basic_impl.cpp) and payments/request/address.mjs.
// Needs the gmp extension for the ed25519 point check.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }

const XMZ_PREFIXES = [
    'mainnet' => ['standard' => 86, 'integrated' => 87, 'subaddress' => 88],
    'testnet' => ['standard' => 111, 'integrated' => 112, 'subaddress' => 113],
    'stagenet' => ['standard' => 131, 'integrated' => 132, 'subaddress' => 133],
];
const XMZ_BASE58 = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
const XMZ_BLOCK_SIZES = [0, 2, 3, 5, 6, 7, 9, 10, 11];

function xmzRotl(int $x, int $n): int {
    if ($n === 0) return $x;
    // PHP's >> is arithmetic, so mask the bits shifted in from the sign.
    return ($x << $n) | (($x >> (64 - $n)) & ((1 << $n) - 1));
}

// Original Keccak-256 (cn_fast_hash): padding byte 0x01, not NIST SHA3's 0x06.
function xmzKeccak256(string $input): string {
    static $rc = null;
    if ($rc === null) {
        $rc = [];
        foreach (['0000000000000001', '0000000000008082', '800000000000808a', '8000000080008000', '000000000000808b', '0000000080000001',
            '8000000080008081', '8000000000008009', '000000000000008a', '0000000000000088', '0000000080008009', '000000008000000a',
            '000000008000808b', '800000000000008b', '8000000000008089', '8000000000008003', '8000000000008002', '8000000000000080',
            '000000000000800a', '800000008000000a', '8000000080008081', '8000000000008080', '0000000080000001', '8000000080008008'] as $hex)
            $rc[] = unpack('J', hex2bin($hex))[1];
    }
    $rot = [0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14];
    $rate = 136;
    $padded = $input . "\x01";
    $padded .= str_repeat("\x00", (($rate - strlen($padded) % $rate) % $rate));
    $padded[strlen($padded) - 1] = chr(ord($padded[strlen($padded) - 1]) | 0x80);
    $s = array_fill(0, 25, 0);
    foreach (str_split($padded, $rate) as $block) {
        $lanes = array_values(unpack('P17', $block));
        for ($i = 0; $i < 17; $i++) $s[$i] ^= $lanes[$i];
        for ($round = 0; $round < 24; $round++) {
            $c = [];
            for ($x = 0; $x < 5; $x++) $c[$x] = $s[$x] ^ $s[$x + 5] ^ $s[$x + 10] ^ $s[$x + 15] ^ $s[$x + 20];
            for ($x = 0; $x < 5; $x++) {
                $d = $c[($x + 4) % 5] ^ xmzRotl($c[($x + 1) % 5], 1);
                for ($y = 0; $y < 25; $y += 5) $s[$x + $y] ^= $d;
            }
            $b = [];
            for ($x = 0; $x < 5; $x++) for ($y = 0; $y < 5; $y++) $b[$y + 5 * ((2 * $x + 3 * $y) % 5)] = xmzRotl($s[$x + 5 * $y], $rot[$x + 5 * $y]);
            for ($x = 0; $x < 5; $x++) for ($y = 0; $y < 5; $y++) $s[$x + 5 * $y] = $b[$x + 5 * $y] ^ ((~$b[($x + 1) % 5 + 5 * $y]) & $b[($x + 2) % 5 + 5 * $y]);
            $s[0] ^= $rc[$round];
        }
    }
    return substr(pack('P4', $s[0], $s[1], $s[2], $s[3]), 0, 32);
}

function xmzBase58Decode(string $text): ?string {
    $full = intdiv(strlen($text), 11);
    $lastSize = array_search(strlen($text) % 11, XMZ_BLOCK_SIZES, true);
    if ($text === '' || $lastSize === false) return null;
    $out = '';
    foreach (str_split($text, 11) as $index => $chunk) {
        $size = $index < $full ? 8 : $lastSize;
        $value = gmp_init(0);
        foreach (str_split($chunk) as $ch) {
            $digit = strpos(XMZ_BASE58, $ch);
            if ($digit === false) return null;
            $value = gmp_add(gmp_mul($value, 58), $digit);
        }
        if (gmp_cmp($value, gmp_pow(2, 8 * $size)) >= 0) return null;
        $out .= str_pad(gmp_export($value, 1, GMP_MSW_FIRST | GMP_BIG_ENDIAN), $size, "\x00", STR_PAD_LEFT);
    }
    return $out;
}

function xmzReadVarint(string $bytes): ?array {
    $value = 0;
    for ($i = 0; $i < min(strlen($bytes), 9); $i++) {
        $byte = ord($bytes[$i]);
        $value |= ($byte & 0x7f) << (7 * $i);
        if (!($byte & 0x80)) return ($i > 0 && $byte === 0) ? null : [$value, $i + 1];
    }
    return null;
}

// crypto::check_key: canonical y below p, decompressible, and not negative zero.
function xmzValidPoint(string $key): bool {
    static $p = null, $d, $sqrtm1;
    if ($p === null) {
        $p = gmp_sub(gmp_pow(2, 255), 19);
        $d = gmp_mod(gmp_mul(-121665, gmp_powm(121666, gmp_sub($p, 2), $p)), $p);
        $sqrtm1 = gmp_powm(2, gmp_div_q(gmp_sub($p, 1), 4), $p);
    }
    $y = gmp_import(strrev($key), 1, GMP_MSW_FIRST | GMP_BIG_ENDIAN);
    $sign = gmp_testbit($y, 255);
    if ($sign) gmp_clrbit($y, 255);
    if (gmp_cmp($y, $p) >= 0) return false;
    $yy = gmp_mod(gmp_mul($y, $y), $p);
    $u = gmp_mod(gmp_sub($yy, 1), $p);
    $v = gmp_mod(gmp_add(gmp_mul($d, $yy), 1), $p);
    $x = gmp_powm(gmp_mod(gmp_mul($u, gmp_powm($v, gmp_sub($p, 2), $p)), $p), gmp_div_q(gmp_add($p, 3), 8), $p);
    $vxx = gmp_mod(gmp_mul($v, gmp_mul($x, $x)), $p);
    if (gmp_cmp($vxx, $u) !== 0) {
        if (gmp_cmp(gmp_mod(gmp_add($vxx, $u), $p), 0) !== 0) return false;
        $x = gmp_mod(gmp_mul($x, $sqrtm1), $p);
    }
    return !(gmp_cmp($x, 0) === 0 && $sign);
}

// Returns ['network','kind','address','payment_id'] or ['error' => message].
function xmzParseAddress(mixed $input): array {
    $text = is_string($input) ? trim($input) : '';
    if ($text === '') return ['error' => 'Enter a Monzero address.'];
    if (strlen($text) > 200) return ['error' => 'This is too long to be a Monzero address.'];
    $bytes = xmzBase58Decode($text);
    if ($bytes === null || strlen($bytes) < 5) return ['error' => 'The address contains characters or a length that Monzero addresses do not use.'];
    $body = substr($bytes, 0, -4);
    if (!hash_equals(substr(xmzKeccak256($body), 0, 4), substr($bytes, -4))) return ['error' => 'The address checksum does not match. Check for a typing or copying mistake.'];
    $tag = xmzReadVarint($body);
    if (!$tag) return ['error' => 'The address prefix is malformed.'];
    $found = null;
    foreach (XMZ_PREFIXES as $network => $kinds) foreach ($kinds as $kind => $prefix) if ($prefix === $tag[0]) $found = [$network, $kind];
    if (!$found) return ['error' => 'This is not a Monzero address.'];
    $keys = substr($body, $tag[1]);
    if (strlen($keys) !== ($found[1] === 'integrated' ? 72 : 64)) return ['error' => 'The address has the wrong length for its type.'];
    if (!xmzValidPoint(substr($keys, 0, 32)) || !xmzValidPoint(substr($keys, 32, 32))) return ['error' => 'The address contains an invalid public key.'];
    return ['network' => $found[0], 'kind' => $found[1], 'address' => $text,
        'payment_id' => $found[1] === 'integrated' ? bin2hex(substr($keys, 64, 8)) : null];
}
