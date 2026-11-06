<?php
declare(strict_types=1);
// Read-only presentation contract. No RPC, wallet access or user-input logging.
function poolAtomic(mixed $value): ?string {
    if (is_int($value) && $value >= 0) return (string)$value;
    if (is_string($value) && preg_match('/\A(?:0|[1-9][0-9]{0,19})\z/D', $value)
        && (strlen($value)<20 || strcmp($value,'18446744073709551615')<=0)) return $value;
    return null;
}
function poolNonnegative(mixed $value): int|float|null {
    return (is_int($value) || is_float($value)) && is_finite((float)$value) && $value>=0 ? $value : null;
}
function poolLookupShape(mixed $value): bool {
    // Input boundary only: checksum validity is not implied. Never accept keys/seeds.
    return is_string($value) && in_array(strlen($value),[95,106],true)
        && preg_match('/\A[1-9A-HJ-NP-Za-km-z]+\z/D',$value)===1;
}
function poolMinerView(array $miner, array $stats, string $address, int $observedAt, ?string $workerKey=null): array {
    if (isset($miner['error']) || ($miner['address'] ?? null)!==$address) throw new UnexpectedValueException('Unmatched miner response');
    if (($stats['config']['coin'] ?? null)!=='monzero' || ($stats['config']['coinUnits'] ?? null)!==100000000000)
        throw new UnexpectedValueException('Wrong pool denomination');
    $hashrate=poolNonnegative($miner['hashrate'] ?? null);
    $poolHashrate=poolNonnegative($stats['pool']['hashrate'] ?? null);
    $workers=null;
    if (isset($miner['workers']) && is_array($miner['workers']) && array_is_list($miner['workers'])) {
        if(count($miner['workers'])>1000) throw new UnexpectedValueException('Too many workers');
        $workers=[];
        foreach($miner['workers'] as $worker) {
            if (!is_array($worker) || !is_string($worker['name'] ?? null) || strlen($worker['name'])>256)
                throw new UnexpectedValueException('Invalid worker');
            $workers[]=['label_id'=>$workerKey===null?null:hash_hmac('sha256',$address."\0".$worker['name'],$workerKey),'name'=>'Worker connection '.(count($workers)+1),'share_difficulty'=>poolNonnegative($worker['difficulty'] ?? null)];
        }
    }
    $payments=null;
    if(isset($miner['payments']) && is_array($miner['payments']) && array_is_list($miner['payments'])) {
        if(count($miner['payments'])>50) throw new UnexpectedValueException('Too many payments');
        $payments=[];
        foreach($miner['payments'] as $payment) {
            if(!is_array($payment) || !is_string($payment['txid'] ?? null)
                || preg_match('/\A[0-9a-f]{64}\z/D',$payment['txid'])!==1
                || !is_int($payment['timestamp'] ?? null) || $payment['timestamp']<=0
                || poolAtomic($payment['amount'] ?? null)===null) throw new UnexpectedValueException('Invalid payment');
            $payments[]=['txid'=>$payment['txid'],'timestamp'=>$payment['timestamp'],
                'amount_atomic'=>poolAtomic($payment['amount']),'status'=>'pool-reported-sent','confirmations'=>null];
        }
    }
    return ['schema'=>'monzero-pool-miner-view-v1','source'=>'https://pool.monzero.org/',
        'observed_at'=>$observedAt,'upstream_generated_at'=>null,'hashrate_window_seconds'=>600,
        'hashrate_hps'=>$hashrate,'pool_hashrate_hps'=>$poolHashrate,
        'pool_share_percent'=>$hashrate!==null && $poolHashrate!==null && $poolHashrate>0 && $hashrate<=$poolHashrate
            ?$hashrate/$poolHashrate*100:null,
        'pending_atomic'=>poolAtomic($miner['pending'] ?? null),'paid_atomic'=>poolAtomic($miner['paid'] ?? null),
        'worker_labels_available'=>$workerKey!==null,'workers'=>$workers,'active_workers'=>$workers===null?null:count($workers),'payments'=>$payments,
        'accepted_shares'=>null,'rejected_shares'=>null,'worker_uptime'=>null,'hashrate_history'=>null,
        'limitations'=>['Hashrate is a ten-minute share-based estimate reported by the pool.',
            'Payments are reported sent by the pool; blockchain confirmations have not been verified.',
            'A zero response does not prove this address has previously mined.',
            'Share counts, worker uptime and historical hashrate are not supplied by this API.']];
}
