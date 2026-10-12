# Interfaces

This document separates existing upstream contracts from a proposed downstream
result contract. It does not claim any of these future consumer interfaces are
implemented.

## Normalized observation boundary

The core consumes normalized, typed observation records; it is independent of
any collector, project checkout, transport, or hardware API. Each adapter maps
one declared source schema to this boundary and preserves opaque source ID,
sequence and epoch facts, capture and ingest times, integrity/parser status,
missingness, and time-mapping uncertainty. It must not infer absent facts.
The proposed `cws-source-record/2` adapter is an optional first integration,
not the canonical or required input. That adapter validates the source schema
and raw hash; compatibility with older source versions is adapter-specific and
must leave missing fields unknown.

Unknown schema versions, malformed records, failed integrity checks, invalid
fields, and unusable sequence facts are not valid observations. An adapter may
support legacy input, but absent hashes, epochs, parser results, or device
sequences stay unknown. A record is usable only if the frozen pipeline does
not require an absent fact.

Capture time differs from ingest time. Preserve the source timestamp and any
clock mapping's source and uncertainty. Ingest clocks can indicate arrival
order and transport delay, not capture instant. Timing assessments may be
consumed only as explicit, separately versioned facts. The monitor does not
invent synchronization or infer simultaneity from arrivals.

## Optional operational context

Operational context is not sensing evidence by default. A future adapter may
consume a documented context interface, normalize known fields, and report
context age and availability. It must retain the context source and original
sample time. A network-service failure does not instantly invalidate a
still-fresh presence window. Conversely, fresh context does not repair stale,
missing, or corrupt observations. A reviewed profile may explicitly connect a
particular context field to an evidence gate;
that mapping must be fixed in the profile and reported in result provenance.

The adapter must not parse undocumented vendor text by heuristic field names,
write provider state, invoke provider control commands, or infer that “high
load” means “person present.” Context parse failure is an independent status
and reason.

## Frozen model bundle

Before classifying, verify a model-bundle manifest hash. It declares:

- stable model ID, manifest schema version, model artifact path and SHA-256;
- feature-pipeline ID/version/hash, exact feature names and order, units,
  extraction and normalization steps, window duration, and required sample
  count;
- class mapping and the model's declared decision rule/thresholds;
- supported radio/source profiles and required metadata;
- supported single-source or cooperative subsets, minimum source count, and
  temporal compatibility requirements;
- declared domain envelope and the metadata needed to check it;
- build and artifact provenance sufficient to identify the reviewed bundle.

The manifest describes the tested envelope; it cannot detect unmeasured room,
radio, participant, antenna, placement, or environmental changes. Runtime checks
compare available metadata and features with that declaration. Missing required
metadata yields `unknown` eligibility; mismatch yields `ineligible`. Model and
feature code stay frozen for a window. Online learning, threshold tuning, and
stream-based model selection are out of scope.

Feature extraction must match the frozen manifest exactly. Missing fields,
unexpected order, incompatible units, unrecognized preprocessing version, or
model-hash mismatch blocks inference. Source-specific confidence, reliability,
trigger, or motion fields are not model probabilities or presence labels. They
may be retained as named context only if the model manifest explicitly declares
them as inputs and records their schema and version.

## Time and expiry contract

Every assembled window records start/end in its source time basis, the mapping
to the monitor's clock if available, and a conservative uncertainty bound.
Cross-node fusion is allowed only if every included node's capture-time mapping
is valid and their combined alignment uncertainty is within a frozen model or
profile tolerance. If a source clock resets, its boot/config epoch changes, or
the mapping is missing or outside tolerance, cross-node fusion is not attempted;
the service may evaluate a separately supported single-node model, otherwise
the result is `unknown`.

The evidence age is computed from capture-window ends, not arrival. For every
included source `i`, let `e_i` be its mapped capture end, `epsilon_i` its
nonnegative time-mapping uncertainty, and `A_max` the frozen maximum age. All
`e_i`, the publisher's `now`, and the uncertainty bounds must share one declared
time basis. The conservative age is
`A = max_i(now - e_i + epsilon_i)`. Evidence is fresh only while `A <= A_max`.
The common conservative expiry is
`valid_until = min_i(e_i - epsilon_i + A_max)`. For one source these reduce to
the same single-source formula. If any mapped source end is in the future by
more than its uncertainty (`e_i - now > epsilon_i`), reject that clock mapping;
do not clip a negative age to zero. Unknown clock mapping, unbounded
uncertainty, or incompatible time bases prevents cooperative inference. A
single-source model may still run if its own clock requirements pass.

`valid_until` is meaningful only in its explicitly named time basis. A
publisher's host-monotonic deadline is never portable to another host, and a
wall-clock deadline is usable only when the consumer has a verified mapping to
that same basis and accounts for its uncertainty. Otherwise the publisher may
include `remaining_ttl_ms` only when maximum transport delay `D_max` is
declared. It computes
`max(0, valid_until - send_time - D_max - clock_uncertainty)` in its own
monotonic basis; the consumer starts that conservative remainder from its
receive-monotonic clock. If transport delay is unbounded or exceeds the
declared bound, or the time basis cannot be matched,
the consumer must mark the current estimate `unknown`/expired rather than
assuming that the publisher's timestamp is comparable. Within a process boot,
monotonic time governs local expiry. Wall-clock steps and restarts require
re-establishing the mapping.

## Proposed result: `wifi-presence-result/1`

One immutable result record contains the current estimate, evidence and domain
states, source window, provenance, and optional history pointer. Field meanings
are fixed below; implementation must reject unknown major versions and missing
required fields.

| Field | Contract |
|---|---|
| `schema_version` | Exactly `wifi-presence-result/1` for this design. |
| `result_id` | Unique result identifier generated by the monitor; no user or site identity. |
| `zone_id` | Opaque operator-configured sensing-zone label. |
| `estimate` | `present`, `absent`, or `unknown`; `unknown` is the only allowed result when a required gate fails. |
| `evidence_validity` | `valid`, `invalid`, or `unknown`, with an array of reason codes. |
| `domain_eligibility` | `eligible`, `ineligible`, or `unknown`, with reasons and the checked profile ID/hash. |
| `evidence_window` | Source IDs, session/connection epochs, first/last source sequence, capture start/end, time basis, mapping status, and uncertainty. |
| `age` | Conservative upper bound `A`, configured maximum age, and calculation time basis. |
| `valid_until` | Conservative expiry instant in an explicit named time basis, or `null` when it cannot be established; never an unqualified cross-host monotonic value. |
| `provenance` | Source-record schema version; model, pipeline and config IDs/hashes; software build ID; optional context IDs/hash. |
| `last_known` | Separate optional historical estimate with its original result ID, class, window, validity, and expiry. Never substituted for `estimate`. |
| `operational_context` | Separate status/time/source for optional network-service context; never folded into the class without a manifest/profile rule. |

Illustrative shape (placeholders only):

```json
{
  "schema_version": "wifi-presence-result/1",
  "result_id": "<opaque-result-id>",
  "zone_id": "<configured-zone-label>",
  "estimate": "unknown",
  "evidence_validity": {
    "state": "unknown",
    "reasons": ["<reason-code>"]
  },
  "domain_eligibility": {
    "state": "unknown",
    "profile_id": "<profile-id>",
    "profile_sha256": "<sha256>"
  },
  "evidence_window": {
    "source_ids": ["<opaque-source-id>"],
    "record_schema": "<source-schema/version>",
    "capture_start": null,
    "capture_end": null,
    "time_basis": "unknown",
    "uncertainty_ms": null
  },
  "age": {"upper_bound_ms": null, "max_age_ms": "<configured-limit>"},
  "valid_until": null,
  "provenance": {
    "model_id": "<model-id>",
    "model_sha256": "<sha256>",
    "pipeline_id": "<pipeline-id>",
    "pipeline_sha256": "<sha256>",
    "config_sha256": "<sha256>",
    "service_build_id": "<build-id>"
  },
  "last_known": null,
  "operational_context": {"state": "unknown", "source": null, "sample_time": null}
}
```

Freeze the JSON schema and canonical hash rules before code or external
consumers depend on this contract. `null` means unavailable or inapplicable;
never invent sentinel timestamps or zero values for missing evidence.
