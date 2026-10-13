# Version 1 runtime contracts

The implemented service reads one local NDJSON stream and writes one atomic
JSON result. The strict decoder in `src/adapters/contracts.cpp` is the
executable input contract. Unknown fields, duplicate keys, unsupported schemas,
missing required fields, excessive nesting, invalid types and non-finite
numbers are rejected. There is no field-name guessing or legacy auto-detection.

All configuration/model limits are explicit integers. Millisecond time values
are nonnegative, bounded signed-64-bit values internally; booleans and floating
point encodings are not accepted as integer substitutes. Strings,
cardinalities, file sizes and input frames are bounded. No missing scientific
fact receives a presence-class default.

## Startup configuration: `wifi-presence-config/1`

| Field | Meaning |
|---|---|
| `schema_version` | Exactly `wifi-presence-config/1` |
| `zone_id` | Opaque configured zone label |
| `input_path` | Absolute local FIFO path, or `-` for standard input |
| `output_path` | Absolute current-result JSON path in an operator-owned volatile directory |
| `manifest_path` | Absolute frozen model-manifest path |
| `manifest_sha256` | Trusted expected SHA-256 of the exact manifest bytes |
| `max_frame_bytes` | Optional resource bound, 256–65536 bytes; default 16384 |

Paths must be normalized, non-overlapping and free of symlink components. The
writer requires an owned output directory not writable by other users,
restrictive single-link regular output/lock files and one exclusive advisory
writer lock. Inputs and installed files are read-only to the means; runtime
output and its lock are separate from the installation tree. Configuration is
fixed for the process lifetime.

## Frozen model: `wifi-presence-model/1`

Required fields are `model_id`, `pipeline_id`, `profile_id`, `provenance_kind`,
`task`, `stationary_presence`, `negative_class`, `source_ids`, `feature_names`,
`feature_units`, `required_metadata`, `samples_per_source`,
`max_window_span_ms`, `max_alignment_ms`, `max_age_ms`, `max_uncertainty_ms`,
`artifact_path`, `artifact_sha256`, `pipeline_path` and `pipeline_sha256`, plus
the exact schema identifier.

The task is `zone-presence`, stationary-presence scope must be declared true,
and the negative class must be `absence`. `provenance_kind` is either
`synthetic`, requiring `--allow-synthetic`, or `reviewed-presence`, requiring a
nonempty `review_reference`. The latter is operator-supplied review provenance,
not an automatically verified scientific finding.

Source IDs are ordered and unique, with at most 16 sources. Feature names are
ordered and unique, with at most 64 features; units have matching cardinality.
Each source contributes 1–256 samples per disjoint window. Required operating
metadata is an exact string-valued map with at most 32 entries. Time/resource
limits are validated; maximum age is positive and bounded to one day. These
bounds are implementation limits, not scientifically recommended thresholds.
The operator must select limits justified for the actual reviewed model.

Model and pipeline paths are sibling basenames of the manifest. SHA-256 is
computed over exact file bytes, including whitespace; there is no implicit JSON
canonicalization. First verify the operator-pinned manifest, then its
referenced artifact/pipeline hashes and their contracts. A self-declared hash
is not a trust anchor. No model search, online training, live reload or
automatic subset fallback is implemented.

The pipeline file uses `wifi-presence-pipeline/1`, `aggregation:
"per-source-mean"` and exact `feature_names` / `feature_units`. The model
artifact uses `wifi-presence-linear/1` with finite `weights`, `bias`,
`absent_threshold` and `present_threshold`. Weights have one entry per source
and feature in source-major order. The lower threshold is strictly less than
the upper. See [the inference
definition](implementation.md#fixed-processing-pipeline).

## Observation: `wifi-presence-observation/1`

Each newline terminates exactly one complete JSON object:

| Required field | Meaning |
|---|---|
| `schema_version` | Exactly `wifi-presence-observation/1` |
| `source_id` | One configured opaque source ID |
| `boot_epoch`, `config_epoch` | Declared source continuity tokens |
| `sequence` | Nonnegative integer through uint64 maximum; continuity is checked per source |
| `capture_ms` | Capture instant already mapped to the local `CLOCK_BOOTTIME` basis |
| `uncertainty_ms` | Conservative nonnegative mapping uncertainty |
| `clock_domain` | Opaque local boot/time-namespace token, independently checked against the local OS |
| `pipeline_sha256` | Exact declared feature-pipeline identity |
| `feature_names`, `feature_units` | Exact ordered feature contract |
| `features` | Finite numerical vector with matching cardinality |
| `metadata` | String-valued operating facts required by the manifest |
| `integrity`, `parser_status` | Explicit upstream integrity boolean and parser status; usable input requires true / `ok` |

Integrity/parser declarations are source facts. The service checks their types
and values, encoding and continuity; it does not authenticate a producer or
verify raw CSI that was not supplied. A trusted normalization producer must
preserve the real acquisition facts and clock mapping. Arrival is never used as
capture time.

Missing required domain metadata makes that candidate's applicability unknown;
a contradiction makes it ineligible. A known relevant epoch/domain change can
invalidate current evidence and running jobs. A rejected later candidate alone
does not erase independently eligible earlier evidence. No dedicated
operational context or environment-change detector is supplied.

## Time and expiry contract

For each included source `i`, let `e_i` be the mapped capture-window end and
`epsilon_i` its conservative uncertainty. All values share a verified local
`CLOCK_BOOTTIME` domain. The core computes:

```text
A = max_i(now - e_i + epsilon_i)
valid_until = min_i(e_i - epsilon_i + max_age_ms)
fresh iff A <= max_age_ms
```

Expiry is inclusive at equality and withdrawn at the first representable tick
beyond the deadline. Time uncertainty, future mappings, window spans and
cross-source capture-end alignment are checked without signed overflow. A
future end is allowed only within its declared uncertainty. Pending and
completed jobs are requalified; issuance uses a current coordinator snapshot.
No receipt, inference completion or repeated publication extends the capture
horizon.

Linux `CLOCK_BOOTTIME` includes suspend time. The service publishes an opaque
clock token `SHA256("wifi-presence-clock/1:" + local_boot_id + ":" +
time_namespace)` and a separate random runtime session. `time_namespace` is the
exact local `/proc/self/ns/time` link identity; on kernels without time
namespaces (that link is absent), it is the literal `global`. Other lookup
failures prevent clock verification. A local consumer computes the token
independently from its own trusted OS context and evaluates expiry at every
use. A producer in a different time namespace cannot reuse its timestamps
without a separately established mapping into this domain. A matching publisher
label alone is insufficient. Raw boot and namespace identities are not included
in observations or results. Linux virtualizes `CLOCK_BOOTTIME` per time
namespace; see [the kernel userspace clock
contract](https://man7.org/linux/man-pages/man7/time_namespaces.7.html).
Unknown/mismatched domains yield unknown; no remote TTL or cross-host route is
implemented. Restart establishes fresh unknown state and does not restore jobs
or history.

## Result: `wifi-presence-result/1`

| Field | Meaning |
|---|---|
| `schema_version`, `result_id`, `zone_id` | Version, opaque publication identity and configured zone |
| `estimate` | Current `present`, `absent` or `unknown` |
| `decision_reasons` | Reasons for the current decision, distinct from later candidate problems |
| `evidence_validity` | `valid`, `invalid` or `unknown`, with reasons |
| `domain_eligibility` | `eligible`, `ineligible` or `unknown`, reasons and profile identity/hash |
| `evidence_window` | Null before usable evidence; otherwise ordered source records with epochs, first/last sequence, capture start/end, uncertainty, mapping status and time basis |
| `age` | Null when no horizon is known; otherwise `upper_bound_ms`, `maximum_ms`, `as_of_ms`, `time_basis` |
| `valid_until` | Original conservative deadline in the named verified domain, or null |
| `provenance` | Source schema; model/pipeline/manifest/config identities and hashes; software version; runtime session/generation; model scope; opaque clock-domain token and time basis |
| `diagnostics` | Separate latest input/candidate assessment and saturating uint64 rejection/skip/stale-completion counters; no raw input or unbounded history |
| `last_known` | Optional earlier class with its original evidence/domain assessments, capture window and deadline; never the current answer |

The complete JSON encoder is `src/adapters/output.cpp`. Null means unavailable,
not a zero timestamp or absent class. The consumer must verify required nested
fields, schema, evidence/domain states, independent local clock token and
deadline before using a categorical class. The independent reference consumer
in `tests/integration/consumer_contract.py` demonstrates these checks for
synthetic software tests; it is not a shipped UI or a sensing safety system.

## Local transport and failure reasons

The runner reads nonblocking input with bounded work per iteration. Oversized
frames are discarded through the next newline and diagnosed; malformed content
is never echoed. EOF preserves timer processing. A disconnected/reconnected
FIFO must still supply coherent source epochs and sequences.

One complete temporary JSON record replaces the current file on the same
filesystem. Its lock and temporary file are transient writer mechanics, not a
second result API or persistent cache. Partial files are never the current
record. An old readable file does not establish liveness: consumer expiry still
applies after a stalled process or failed write.

Reasons distinguish startup/configuration failure, input/feature
incompatibility, integrity, required source/sequence loss, domain
unknown/ineligible, clock/alignment, capacity loss, expired evidence, model
abstention, inference failure and stale completion. Candidate-only reasons
remain in diagnostics until they also apply to current evidence; historical
assessments retain their original meaning.
