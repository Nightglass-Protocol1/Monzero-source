export const ranges = Object.freeze({'24h':86400,'7d':604800,'30d':2592000,'90d':7776000,'1y':31536000,all:null});
const integer = (v, minimum = 0) => Number.isSafeInteger(v) && v >= minimum;
const positive = v => typeof v === 'number' && Number.isFinite(v) && v > 0;
function requireValue(ok) { if (!ok) throw new Error('Network history is invalid or inconsistent.'); }
function difficulty(v) {
  requireValue(typeof v === 'string' && /^[1-9][0-9]{0,15}$/.test(v));
  const n = Number(v); requireValue(integer(n, 1)); return n;
}
// Validate before charting. Missing buckets remain missing; heights may decrease after a reorganization.
export function validateHistory(data, range) {
  requireValue(Object.prototype.hasOwnProperty.call(ranges, range) && data && data.schema === 'monzero-network-history-v1' && data.network === 'mainnet' && data.range === range);
  requireValue(integer(data.generated_at, 1) && data.generated_at <= 253402300799 && integer(data.archive_samples) && integer(data.bucket_seconds, 60));
  requireValue(Array.isArray(data.sources) && data.sources.length <= 1 && data.sources.every(s => s === 'preview' || s === 'live'));
  requireValue(Array.isArray(data.points) && data.points.length <= 721);
  const first = data.archive_first_observed_at, last = data.archive_last_observed_at;
  if (data.archive_samples === 0) {
    requireValue(first === null && last === null && data.sources.length === 0 && data.points.length === 0);
  } else {
    requireValue(integer(first, 1) && integer(last, first) && last <= data.generated_at && data.sources.length === 1);
  }
  const start = ranges[range] === null ? (first ?? data.generated_at) : Math.max(0, data.generated_at - ranges[range]);
  const bucket = Math.max(60, Math.ceil(Math.max(1, data.generated_at - start) / 720));
  requireValue(data.bucket_seconds === bucket);
  if (data.archive_samples > 0) {
    requireValue(data.archive_samples === 1 ? first === last : (last - first) / 60 >= data.archive_samples - 1);
    if (last >= start) requireValue(data.points.length > 0 && data.points[data.points.length - 1].last_observed_at === last);
    if (first >= start) requireValue(data.points.length > 0 && data.points[0].first_observed_at === first);
  }
  const anchor = Math.floor(start / bucket) * bucket;
  let previousBucket = -1, count = 0;
  for (const p of data.points) {
    requireValue(p && integer(p.first_observed_at, Math.max(1, start, first ?? 0)) && integer(p.last_observed_at, p.first_observed_at) && p.last_observed_at <= last);
    const index = Math.floor((p.first_observed_at - anchor) / bucket);
    requireValue(index > previousBucket && Math.floor((p.last_observed_at - anchor) / bucket) === index);
    previousBucket = index;
    requireValue(integer(p.samples, 1) && p.samples <= Math.floor((p.last_observed_at - p.first_observed_at) / 60) + 1);
    count += p.samples; requireValue(integer(count) && count <= data.archive_samples);
    const minimum = difficulty(p.minimum_difficulty), maximum = difficulty(p.maximum_difficulty);
    requireValue(minimum <= maximum && positive(p.mean_difficulty) && p.mean_difficulty >= minimum - maximum * Number.EPSILON * p.samples * 2 && p.mean_difficulty <= maximum + maximum * Number.EPSILON * p.samples * 2);
    requireValue(positive(p.mean_estimated_hashrate_hps) && Math.abs(p.mean_estimated_hashrate_hps - p.mean_difficulty / 120) <= Math.max(1, p.mean_difficulty / 120) * 1e-12);
    requireValue(integer(p.minimum_height, 2) && integer(p.maximum_height, p.minimum_height));
    requireValue(integer(p.synchronized_samples) && p.synchronized_samples <= p.samples && typeof p.all_samples_synchronized === 'boolean' && p.all_samples_synchronized === (p.synchronized_samples === p.samples));
    requireValue(Number.isSafeInteger(p.minimum_tip_age_seconds) && Number.isSafeInteger(p.maximum_tip_age_seconds) && p.minimum_tip_age_seconds <= p.maximum_tip_age_seconds);
  }
  if (range === 'all') requireValue(count === data.archive_samples);
  return data;
}
export function historyFreshness(data, now = Date.now() / 1000) {
  if (!Number.isFinite(now) || now < data.generated_at) return 'clock-mismatch';
  if (data.archive_samples === 0) return 'empty';
  return now - data.generated_at > 900 || now - data.archive_last_observed_at > 900 ? 'stale' : 'recent';
}
