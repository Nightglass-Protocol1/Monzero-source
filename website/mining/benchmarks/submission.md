# RandomX measurement submissions

Open a benchmark submission issue at
https://github.com/Nightglass-Protocol1/Monzero-source/issues/new?template=randomx-benchmark.yml with evidence. Maintainers review submissions before publication.
Do not send keys, seeds, wallet addresses, worker identifiers or personal logs.

Each `catalog.json` record needs:
- `id`: unique lowercase slug; manufacturer/model; integer cores/threads.
- `hashrate_hps`: positive finite average, not a momentary peak.
- `power_watts`: positive measured watts, or null when unknown.
- `power_basis`: wall, cpu-package, or unknown. Only wall readings are ranked.
- `os`, `miner`, `miner_version`, ISO `date` and `methodology` (up to 200 chars).
- `source_url`: public HTTPS evidence including measurement interval, memory,
  thread settings, tuning, power measurement method and relevant sanitized logs.
- `status`: community initially; `verified_by` and `verification_url`: null.

A reviewer checks field validity, evidence, duplicates, personal-data removal and
measurement basis. Approval of a community submission does not mean reproduction.
Only a separate documented reproduction permits status `verified`, with a public
reviewer identifier and HTTPS verification report. Keep original source evidence.
Record corrections through reviewed commits; do not silently relabel estimates.
No invented seed records. Rejected submissions remain unpublished. Runtime schema
validation is in `catalog.mjs`; synthetic tests never populate production data.

Update `website/mining/benchmarks/catalog.json` and its `updated_at` date, which
must be on or after each measurement date. IDs match `[a-z0-9][a-z0-9-]{0,79}`.
All descriptive string fields are limited to 200 characters and must contain no
control or bidirectional override characters. Hashrate is in H/s, not kH/s.
Unknown power requires null watts; known power requires wall or cpu-package.
No extra fields are permitted. Reproduction evidence must use a different URL
from the original source. Reviewer judgement is still required to establish that
the report is an independent measurement; schema validity does not prove that.

## Validate before submitting

Run `node utils/website/validate_benchmarks.mjs` from the repository root.
To check a separate candidate file, append its path. Then run
`node --test tests/website/benchmarks.test.mjs`. No build or mining is required.

`catalog.schema.json` provides a JSON Schema for editor assistance. The Node
validator is authoritative for cross-field rules, unique IDs and calendar dates;
JSON Schema alone cannot enforce all of these. The pull-request workflow runs
the same runtime validator and regression tests. Passing validation does not
verify the measurement or authorize publication; evidence and reproduction
claims still need maintainer review.

Evidence URLs must use explicit `https://`, contain no credentials, backslashes,
whitespace or invisible control/format characters, and be at most 2048 characters.
Normalized URLs differing only in hostname case, default port or fragment do
not count as separate evidence. Descriptive text must contain no Unicode
control, format, surrogate, line-separator or paragraph-separator characters.
The CLI rejects catalog dates later than the current UTC date.

The workflow is a validation signal, not an enforced publication gate: repository
maintainers must configure their branch protection and review policy. Do not
mark this path-filtered workflow as required for every unrelated pull request.
