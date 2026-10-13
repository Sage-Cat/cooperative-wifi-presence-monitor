# Software acceptance requirements

These are software acceptance requirements. [Testing
evidence](testing/README.md) records the concrete executed checks and their
exact scope; not every possible platform or physical condition is covered.
Manuscripts and physical experiments remain separate from software conformance.

## Contract and provenance checks

- Validate normalized records at the core boundary and test the one initial source decoder
  against its declared schema, integrity rules, time semantics, and supported
  version. Validate required integrity fields, parser status, epoch types and
  source identity without guessing missing facts. Collector-specific adapters
  are later, separately tested integrations.
- Reject malformed JSON/CSI, bad hashes, unknown majors, impossible sequence
  transitions, duplicate or late inputs where the configured window cannot
  resolve them, and invalid configuration/model manifests. Preserve typed
  failure reasons in the bounded diagnostics section of the same result record.
- Verify model, feature-pipeline, profile, and configuration hashes before
  startup inference activation, including comparison to a trusted expected
  manifest hash. Test that feature order, units, preprocessing, window length, and
  class mapping exactly match the bundle. A mismatch blocks inference and
  prevents first activation. A later candidate mismatch does not invalidate an
  independently eligible earlier result unless it proves a relevant change.
- Verify canonical serialization and result schema. Unknown output majors and
  missing required fields are errors; unavailable fields remain `null` or
  typed unknown states.

## Validity and inference behavior

- Exercise all three estimate values and all three evidence-validity and
  domain-eligibility states. A failed prerequisite must never produce a
  present/absent estimate for that candidate. Prior-result retention is checked
  separately against its own evidence. Invalid or missing evidence must not be silently
  dropped to make a source subset appear compatible.
- Exercise capture-time freshness at just-inside, equal-to, and just-outside
  configured limits, including clock mapping uncertainty. Equality remains
  eligible under `A <= A_max`; greater age is expired. Assert age is based
  on capture end, not arrival. Missing capture-time mapping yields no positive
  validity horizon.
- Exercise device reconnect, boot/config epoch change, source gaps, late
  arrival, clock reset, pipeline mismatch and controlled service stop/start.
  Separate later-candidate failures from known invalidation of the current
  window. Gaps/dropout may retain an independently eligible current estimate
  only to its original expiry. Restart, invalidating changes and expiry
  withdraw it; last-known remains separate with its original window and expiry.
- Test single-source and cooperative subsets against manifest declarations.
  Unsupported subsets, incompatible feature profiles, insufficient source
  count, or cross-source timing uncertainty above the declared tolerance must
  abstain. The service must not infer compatibility from node count alone.
- Test deterministic replay for identical windows, artifact hashes, and config;
  only explicitly variable issuance/clock fields may differ.

## Adapter and service checks

- Linux tests use temporary paths and mocked inputs to verify read-only owner
  boundaries, atomic publication, bounded history, permissions, restart
  behavior, and absence of firmware/network-control calls.
- Feed normalized fixtures through replay and each supported source adapter.
  Deliberately drop, duplicate, reorder, and reconnect records; the result must
  expose those facts and follow the frozen unknown/expiry behavior.
- Build and run unit/contract tests for every named Linux architecture and
  compiler/toolchain in the eventual support matrix. OpenWrt SDK/service
  package tests are separate future acceptance checks; prplOS operation is not
  implied by a Linux build.

## What these checks do not establish

Contract tests establish interface conformance; fixtures establish behavior on
their declared inputs. Neither proves people detection, transfer to a new room,
protective-site suitability, or dependable alarms. Those claims require
separately authorized studies, reference labels, held-out conditions, privacy
and human/operational review. No such evidence is part of this design.

## Installation boundary checks

The clean-install check compares package members against the declared
allowlist, run without a development checkout, and verify that defaults create
no persistent cache, log or identity mapping. Test optional logging limits and
that configuration, inputs and outputs stay outside the installation tree. No
telemetry or source-discovery connection may occur on startup.

## Decision, concurrency and consumer scenarios

The requirements in [the design
overview](design.md#requirements-and-acceptance-routes) map to these acceptance
cases. Use deterministic clocks and immutable fixtures for software checks,
without treating those fixtures as physical evidence.

| Case | Trigger | Required observable behavior |
|---|---|---|
| V1 / R1,R6 | Valid window, then gaps or dropout in later input | Current estimate survives only while independently eligible; rejected candidate has separate diagnostics; no deadline extension |
| V2 / R2,R3 | Unsupported reduced subset or mismatched feature order | No inference on that candidate; no zero filling or hidden subset substitution |
| V3 / R3 | Qualified window but model abstains | Current unknown with `model_abstained`; evidence validity may remain valid; no old-class fallback |
| V4 / R4 | No new input or stream EOF through the age limit | Timer progresses; equality follows the declared boundary; expiry publishes unknown without waiting for another record |
| V5 / R4 | Publisher stalls or exits leaving a result file | Consumer independently withdraws an expired or unmappable result |
| V6 / R5 | Inference finishes after its window expired | Publication-time check rejects categorical output |
| V7 / R5 | Relevant domain/epoch invalidation while a job is running | Immediate affected-state withdrawal; late old-generation completion cannot publish |
| V8 / R5 | One running job and new eligible windows arrive | At most one pending latest window; skips are diagnosed; acquisition records are not silently removed from retained windows |
| V9 / R1,R7 | Frame/window memory limit reached | Bounded allocation and explicit capacity/data-loss facts; no apparent complete capture |
| V10 / R2,R3 | Motion-only model declares its negative class as absence | Bundle review/contract rejects the unsupported presence semantics |
| V11 / R6 | Known relevant epoch/config/domain change | Current invalidation is immediate; restored packet flow alone does not restore eligibility |
| V12 / R7,R8 | Clean installation and output consumption | No workspace dependency, telemetry, persistent cache/history, default persistent log, control call or automatic assistance action |
| V13 / R9 | Build the portable core and exercise coordinator with plain values/callable outcomes | No Linux/JSON/backend dependency in the core; no abstract clock/publisher/source interfaces needed |
| V14 / R4,R5,R7 | Continuous input flood, delayed inference and duplicate completion | Bounded loop/queues; timers remain serviced; old/duplicate jobs cannot publish. Suspend is included by the Linux clock contract; physical suspend/resume is not tested |
| V15 / R2,R7 | Controlled configuration stop/start or startup failure | Old process exits before new writer starts; new session starts unknown; no restored job/history/class or hot-reload endpoint |

These cases do not prove stationary-person detection, unseen-room transfer,
resilience to interference or operational safety. Such claims need a qualified
model and separate physical studies. The implementation and synthetic fixtures
do not supply a physical presence model or scientific accuracy estimate.

## Architecture acceptance review

Inspect the actual include/build dependency graph and the composition root. The
core must build without Linux, JSON or model-runtime libraries. Its tests use
explicit time and deterministic inference outcomes rather than platform mocks.
Review that domain rules remain pure, the coordinator owns all state, backend
handles stay in the adapter, payload lifetime reaches completion, and only one
process owns the output path. Integration tests cover real adapters and service
scheduling separately. Core-only builds, independent tests, installed-package
checks and QEMU system guests exercise these boundaries. The actual run
receipts state which code and artifacts were checked.
