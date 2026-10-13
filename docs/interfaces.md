# Interfaces

These are proposed data contracts, not implemented APIs. The first version has
one declared normalized local NDJSON input and one atomic local JSON output.
There is no separate health, management, context or remote-consumer interface.
See the [data model](data-model.md) and [lifecycle](lifecycle.md) for ownership
and state-transition semantics.

## Normalized observation boundary

The core consumes normalized, typed observation records; it is independent of
any collector, project checkout, transport, or hardware API. The initial
concrete decoder maps one frozen normalized schema to this boundary and
preserves opaque source ID, sequence and epoch facts, capture and ingest times,
integrity/parser status, missingness, and time-mapping uncertainty. It must not
infer absent facts. Collector-specific contracts such as `cws-source-record/2`
are possible later adapters, not first-version dependencies or
runtime-discovered alternatives. The initial encoding and required integrity
fields must be frozen alongside the chosen producer and model before
implementation.

Unknown schema versions, malformed records, failed integrity checks, invalid
fields, and unusable sequence facts are not valid observations. Absent hashes,
epochs, parser results or device sequences stay unknown; legacy compatibility
is not part of the first decoder. A record is usable only if the frozen
pipeline does not require an absent fact.

Required declared operating metadata is carried in the same observation record
or startup profile, not a separate context channel. Known invalidations come
from those source facts or the host clock; the runner passes them as typed
data. An operator who knows the operating envelope no longer applies must stop
use and supply a reviewed profile through controlled restart. Unobserved
changes are not detected by this design.

Capture time differs from ingest time. Preserve the source timestamp and any
clock mapping's source and uncertainty. Ingest clocks can indicate arrival
order and transport delay, not capture instant. Timing assessments may be
consumed only as explicit, separately versioned facts. The monitor does not
invent synchronization or infer simultaneity from arrivals.

## Frozen model bundle

At startup, verify the supplied manifest against its operator-approved expected
hash, then verify referenced artifact bytes. A self-declared hash is not a
trust anchor. One bundle and profile remain fixed for the process lifetime;
changing them requires controlled stop/start. The manifest declares:

- stable model ID, manifest schema version, model artifact path and SHA-256;
- feature-pipeline ID/version/hash, exact feature names and order, units,
  extraction and normalization steps, window duration, and required sample
  count;
- presence-task identity, class semantics, stationary-presence scope, and the
  model's declared decision rule/thresholds;
- supported radio/source profiles and required metadata;
- supported single-source or cooperative subsets, minimum source count, and
  temporal compatibility requirements;
- declared domain envelope and the metadata needed to check it;
- build and artifact provenance sufficient to identify the reviewed bundle.

A reviewed motion-only negative output cannot be mapped to `absent`. The
stationary-presence scope and class semantics require scientific review; hashes
only verify artifact identity. No trained model or universal backend is
provided.

The manifest describes the tested envelope; it cannot detect unmeasured room,
radio, participant, antenna, placement, or environmental changes. Runtime
checks compare available metadata and features with that declaration. Missing
required metadata yields `unknown` eligibility; mismatch yields `ineligible`.
Model and feature code stay frozen for a window. Online learning, threshold
tuning, and stream-based model selection are out of scope.

Feature extraction must match the frozen manifest exactly. Missing fields,
unexpected order, incompatible units, unrecognized preprocessing version, or
model-hash mismatch blocks inference. Source-specific confidence, reliability,
trigger, or motion fields are not model probabilities or presence labels. They
may be used as input features only if the frozen manifest explicitly declares
their exact schema and meaning; there is no operational-context input channel.

## Time and expiry contract

Every assembled window records start/end in its source time basis, the mapping
to the monitor's clock if available, and a conservative uncertainty bound.
Cross-node fusion is allowed only if every included node's capture-time mapping
is valid and their combined alignment uncertainty is within a frozen model or
profile tolerance. If a source clock resets, its boot/config epoch changes, or
the mapping is missing or outside tolerance, cross-node fusion is not
attempted; the service may evaluate a separately supported single-node model,
otherwise the result is `unknown`.

The evidence age is computed from capture-window ends, not arrival. For every
included source `i`, let `e_i` be its mapped capture end, `epsilon_i` its
nonnegative time-mapping uncertainty, and `A_max` the frozen maximum age. All
`e_i`, the publisher's `now`, and the uncertainty bounds must share one
declared time basis. The conservative age is `A = max_i(now - e_i +
epsilon_i)`. Evidence is fresh only while `A <= A_max`. The common conservative
expiry is `valid_until = min_i(e_i - epsilon_i + A_max)`. For one source these
reduce to the same single-source formula. If any mapped source end is in the
future by more than its uncertainty (`e_i - now > epsilon_i`), reject that
clock mapping; do not clip a negative age to zero. Unknown clock mapping,
unbounded uncertainty, or incompatible time bases prevents cooperative
inference. A single-source model may still run if its own clock requirements
pass.

`valid_until` is meaningful only in its named clock domain. The first consumer
is on the same host and verifies a common OS clock instance through trusted
local runtime context. A matching publisher-supplied label alone is
insufficient. Clock-domain identifiers are opaque runtime tokens, not exported
raw host/boot identities. An unverified mapping means current `unknown`.

The first Linux runner uses `CLOCK_BOOTTIME` so suspend time counts toward
expiry; the consumer must use the same verified basis. Capture-to-host mapping
and its uncertainty are still required. Restart/reboot establishes a new
runtime session and fresh unknown state; retained files never establish
continuity. Remote delivery, wall-clock interchange and cross-host TTL are
outside the first version and have no placeholder fields or adapter here.

## Proposed result: `wifi-presence-result/1`

One immutable result record contains the current estimate, evidence and domain
states, source window, provenance, a bounded diagnostics section, and optional
historical value. Field meanings are fixed below; implementation must reject
unknown major versions and missing required fields.

| Field | Contract |
|---|---|
| `schema_version` | Exactly `wifi-presence-result/1` for this design. |
| `result_id` | Unique result identifier generated by the monitor; no user or site identity. |
| `zone_id` | Opaque operator-configured sensing-zone label. |
| `estimate` | `present`, `absent`, or `unknown`; current categorical estimates require all applicable gates to hold. A failed new candidate alone does not invalidate a prior eligible window. |
| `decision_reasons` | Reasons for the current decision, including model abstention, expiry or startup failure. Separate from evidence/domain assessments and rejected-candidate diagnostics. |
| `evidence_validity` | `valid`, `invalid`, or `unknown`, with an array of reason codes. |
| `domain_eligibility` | `eligible`, `ineligible`, or `unknown`, with reasons and the checked profile ID/hash. |
| `evidence_window` | Source IDs, session/connection epochs, first/last source sequence, capture start/end, time basis, mapping status, and uncertainty. |
| `age` | Conservative upper bound `A`, configured maximum age, `as_of` calculation instant and named time basis. A consumer updates age from the capture facts or expiry, not from file modification time. |
| `valid_until` | Conservative expiry instant in an explicit named time basis, or `null` when it cannot be established; never an unqualified cross-host monotonic value. |
| `provenance` | Source-record schema version; model, pipeline and config IDs/hashes; software build ID; opaque runtime session and in-process evidence generation. |
| `last_known` | Separate optional historical estimate with its original result ID, class, window, validity, and expiry. Never substituted for `estimate`. |
| `diagnostics` | Bounded latest candidate/input reason set and fixed-size counters, including capacity loss and whole-window skips. Separate from current evidence; no raw payload or unbounded history. |

Illustrative shape (placeholders only):

```json
{
  "schema_version": "wifi-presence-result/1",
  "result_id": "<opaque-result-id>",
  "zone_id": "<configured-zone-label>",
  "estimate": "unknown",
  "decision_reasons": ["<current-decision-reason>"],
  "evidence_validity": {
    "state": "unknown",
    "reasons": ["<reason-code>"]
  },
  "domain_eligibility": {
    "state": "unknown",
    "profile_id": "<profile-id>",
    "profile_sha256": "<sha256>",
    "reasons": ["<reason-code>"]
  },
  "evidence_window": {
    "source_ids": ["<opaque-source-id>"],
    "record_schema": "<source-schema/version>",
    "capture_start": null,
    "capture_end": null,
    "time_basis": "unknown",
    "uncertainty_ms": null
  },
  "age": {
    "upper_bound_ms": null,
    "max_age_ms": "<configured-limit>",
    "as_of": null,
    "time_basis": "unknown"
  },
  "valid_until": null,
  "provenance": {
    "model_id": "<model-id>",
    "model_sha256": "<sha256>",
    "pipeline_id": "<pipeline-id>",
    "pipeline_sha256": "<sha256>",
    "config_sha256": "<sha256>",
    "service_build_id": "<build-id>",
    "runtime_session": "<opaque-runtime-session>",
    "evidence_generation": "<in-process-generation>"
  },
  "last_known": null,
  "diagnostics": {"last_input_reasons": [], "counters": {}}
}
```

Freeze the JSON schema and canonical hash rules before code or external
consumers depend on this contract. `null` means unavailable or inapplicable;
never invent sentinel timestamps or zero values for missing evidence.

## First local transport

The first Linux host adapter is planned to read a newline-delimited JSON stream
from an explicitly supplied local pipe. The one declared normalized schema is
configuration, not inferred from field names. The stream is nonblocking, with
bounded input processing per loop iteration so timers cannot starve. It maps
those envelopes to typed observations. Record-size and buffer limits are
explicit. Malformed frames are rejected without echoing raw content; source
continuity is never silently reset. EOF or a disconnected pipe is no-new-data,
while the monotonic expiry timer continues. A new connection establishes its
declared source/epoch facts.

The publisher writes one complete JSON temporary file and atomically replaces
the configured current-result file in the same volatile runtime directory. It
never writes inside the installed package. Same-filesystem replacement prevents
partially readable records; it does not guarantee delivery or make the content
fresh. Candidate diagnostics are a separately labelled bounded section of this
same record, not a second file, endpoint or presence label. Updating
diagnostics must not change the current evidence window or its original
deadline. Fixed-size counters must not wrap; the schema will specify saturation
semantics. Optional bounded process diagnostics may be enabled, but contain no
raw observations.

Consumers evaluate freshness at every use, even when file contents have not
changed. On unreadable, malformed, unsupported or unmappable output, they use
`unknown`; an atomic rename proves completeness, not truth or liveness. There
is no included consumer UI, transport client or monitoring daemon.

## Decision and diagnostic reasons

All applicable reasons are retained. The proposed vocabulary distinguishes:

| Reason | Typical owner and interpretation |
|---|---|
| `service_starting`, `model_unavailable` | Coordinator cannot yet issue a categorical estimate |
| `missing_observations`, `required_source_unavailable` | Candidate window/input coverage is insufficient |
| `input_contract_mismatch`, `model_integrity_failed` | Feature/source/bundle identity is incompatible or invalid |
| `domain_unknown`, `domain_ineligible` | Required domain facts are absent or contradict the reviewed envelope |
| `time_mapping_unknown`, `clock_discontinuity` | Capture age/alignment cannot be justified |
| `evidence_expired` | Current window exceeds its original age limit |
| `model_abstained` | Evidence may be valid and domain eligible, but the model provides no accepted class |
| `configuration_invalid` | Startup configuration/bundle cannot establish a usable contract |
| `capacity_exceeded`, `window_skipped` | Bounded processing lost records or skipped a pending whole window; status must state which |

A candidate-only reason belongs to `diagnostics` until it also applies to the
current window. It must not be copied into current evidence reasons while
retaining a categorical class. An old-generation completion is discarded and
diagnosed, without reclassifying current evidence. The schema and vocabulary
must be frozen with producer/consumer contract fixtures before implementation.
