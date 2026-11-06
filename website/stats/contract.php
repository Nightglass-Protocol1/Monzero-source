<?php
declare(strict_types=1);
// Validate source consistency, not independent consensus or node honesty.
function statsInteger(mixed $value, int $minimum=0): bool {
    return is_int($value) && $value >= $minimum && $value <= 9007199254740991;
}
function statsHash(mixed $value): bool {
    return is_string($value) && preg_match('/\A[0-9a-f]{64}\z/', $value) === 1;
}
function statsHeight(array $info): int {
    if (($info['untrusted'] ?? false)!==false || ($info['status'] ?? null) !== 'OK' || ($info['nettype'] ?? null) !== 'mainnet'
        || !statsInteger($info['height'] ?? null, 2) || ($info['target'] ?? null) !== 120
        || !is_bool($info['synchronized'] ?? null) || !statsHash($info['top_block_hash'] ?? null))
        throw new UnexpectedValueException('Invalid mainnet observation');
    return $info['height'];
}
function statsObservation(array $info, array $range, int $observedAt): array {
    $height=statsHeight($info);$end=$height-1;$start=max(1,$end-719);
    $headers=$range['headers'] ?? null;
    if (($range['untrusted'] ?? false)!==false || ($range['status'] ?? null)!=='OK' || !is_array($headers) || !array_is_list($headers)
        || count($headers)!==$end-$start+1 || !statsInteger($observedAt,1))
        throw new UnexpectedValueException('Incomplete block history');
    $blocks=[];$previous=null;
    foreach ($headers as $index=>$header) {
        if (!is_array($header) || ($header['height'] ?? null)!==$start+$index
            || !statsHash($header['hash'] ?? null) || !statsHash($header['prev_hash'] ?? null)
            || ($header['orphan_status'] ?? null)!==false
            || !statsInteger($header['timestamp'] ?? null,1)
            || !statsInteger($header['difficulty'] ?? null,1)
            || ($header['difficulty_top64'] ?? null)!==0
            || !statsInteger($header['num_txes'] ?? null)
            || ($previous!==null && $header['prev_hash']!==$previous))
            throw new UnexpectedValueException('Invalid or inconsistent block history');
        $blocks[]=['height'=>$header['height'],'timestamp'=>$header['timestamp'],
            'difficulty'=>$header['difficulty'],'transactions'=>$header['num_txes']];
        $previous=$header['hash'];
    }
    if ($previous!==$info['top_block_hash']) throw new UnexpectedValueException('Chain tip changed during observation');
    return ['schema'=>'monzero-chain-charts-v2','generated_at'=>$observedAt,'network'=>'mainnet',
        'target_seconds'=>120,'height'=>$height,'synchronized'=>$info['synchronized'],'blocks'=>$blocks];
}

function statsCachedObservation(array $value,int $now): bool {
    if(($value['schema'] ?? null)!=='monzero-chain-charts-v2'||($value['network'] ?? null)!=='mainnet'
        ||($value['target_seconds'] ?? null)!==120||!is_bool($value['synchronized'] ?? null)
        ||!statsInteger($value['height'] ?? null,2)||!statsInteger($value['generated_at'] ?? null,1)
        ||$value['generated_at']>$now||$now-$value['generated_at']>=60
        ||!is_array($value['blocks'] ?? null)||!array_is_list($value['blocks']))return false;
    $end=$value['height']-1;$start=max(1,$end-719);
    if(count($value['blocks'])!==$end-$start+1)return false;
    foreach($value['blocks'] as $i=>$block){
        if(!is_array($block)||($block['height'] ?? null)!==$start+$i
            ||!statsInteger($block['timestamp'] ?? null,1)||!statsInteger($block['difficulty'] ?? null,1)
            ||!statsInteger($block['transactions'] ?? null))return false;
    }
    return true;
}
