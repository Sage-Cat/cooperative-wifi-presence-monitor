# Design and implementation overview

This document records the implemented minimal C++17 Linux pilot. Native,
sanitizer, installed-process and QEMU checks establish software conformance on
synthetic inputs. No reviewed physical presence model or sensing-performance
result is supplied. Exact execution evidence is in
[testing](testing/README.md).

## Practical goal

Publish one current zone-level estimate only while a reviewed model and
supplied observations support that use. Keep model class, evidence validity and
declared domain eligibility separate. `unknown` is a useful result when a
condition is unresolved or the model abstains.

Version 0.1.0 is one C++17 Linux process, one local normalized NDJSON input,
one startup-selected frozen model/profile and one atomic local JSON output. An
operator or local application inspects class, age and reasons through that
record; there is no built-in UI or management API. Acquisition, training and
measurement policy remain outside the means. Configuration changes use restart.

## Views

| View | Main question | Document |
|---|---|---|
| Source dependencies | Which code may depend on which boundary? | [Architecture](architecture.md#dependency-rule), [diagram](diagrams/dependencies.svg) |
| Runtime flow | Who owns each processing step? | [Architecture](architecture.md#processing-responsibilities), [diagram](diagrams/components.svg) |
| Data | Which facts belong to candidate, current and history? | [Data model](data-model.md), [diagram](diagrams/core_contract.svg) |
| States | When may an estimate persist or be withdrawn? | [Lifecycle](lifecycle.md), [diagram](diagrams/result_state.svg) |
| Processing | What is checked before and after inference? | [Sequence](diagrams/observation_flow.svg) |
| Interrupted input | How do gaps, expiry and recovery differ? | [Sequence](diagrams/failure_recovery.svg) |
| Startup/restart | How is a new configuration activated? | [Sequence](diagrams/configuration_startup.svg) |
| Deployment | What is portable or platform-specific? | [Platforms](platforms.md), [diagram](diagrams/deployment.svg) |
| Users | What does each actor do? | [Use cases](use-cases.md) |

All editable sources are in the [PlantUML catalogue](diagrams/README.md).

## Design decisions

| Decision | Choice and reason |
|---|---|
| Clean Architecture | Plain domain values/rules; one concrete application coordinator; adapters and Linux composition root outside. Source dependencies point inward. |
| Minimal abstractions | One inference job/outcome boundary. Time, observations, jobs and results are plain values; other components are concrete. No interface per box. |
| Execution | Serialized coordinator plus one worker keeps inference off the expiry loop. One running job and at most one pending latest window bound memory. |
| Retention | Rejecting a candidate does not erase independently eligible earlier evidence. Its original horizon never changes. |
| Publication | Exactly one atomic current JSON file with separate bounded diagnostics and optional history, in a configured volatile directory. |
| Cooperative processing | Only exact model-supported source subsets and defensible capture-time alignment; node count alone is insufficient. |
| Consumer | Same-host consumer verifies schema, its trusted common clock domain and expiry on every use. An unreadable/unmappable result is unknown. |
| Scope | No hot reload, context input, remote delivery, plugin registry, database or separate health service. |
| Privacy/platform | Opaque IDs, volatile defaults, no telemetry; Linux first. Embedded support requires separate qualification. |

Patterns and their reasons are documented in
[architecture](architecture.md#patterns-kept-and-complexity-avoided). They are
implementation tools, not additional product features.

A bundle must declare a reviewed presence task, including stationary-presence
scope before its negative class is interpreted as `absent`. The service checks
artifacts/declarations; it cannot certify their scientific validity.

## Requirements and acceptance routes

These requirements are exercised by the software checks linked below. Physical
model/platform qualification remains distinct.

| ID | Required behavior | Verification route |
|---|---|---|
| R1 | Preserve input integrity, gaps and capture provenance | Adapter/window contract cases |
| R2 | Infer only with a compatible reviewed bundle and exact supported subset | Bundle, feature and subset cases |
| R3 | Keep evidence validity, domain eligibility and model output separate | Decision and result-schema cases |
| R4 | Never refresh age from arrival, completion or repeated publication | Clock, expiry and consumer cases |
| R5 | Keep expiry responsive and reject stale/duplicate job completions | Delayed-worker and invalidation cases |
| R6 | Retain an eligible earlier result only until its original horizon | Gap/dropout/recovery cases |
| R7 | Remain read-only, bounded and independent of a development checkout | Service and clean-install cases |
| R8 | Make no person-count, distress or assistance decision | Boundary review and consumer contract cases |
| R9 | Keep inner code independent of platform/backend/serialization types | Core build and dependency review |

See [verification](verification.md) for scenarios. The [five-cause decision
table](lifecycle.md#five-causes-of-lost-observational-support) connects the
practical behavior to the observation-support problem. Physical accuracy,
environmental transfer and operational suitability need separate studies.

## Inputs required for physical deployment

- Select and review one actual presence model/pipeline/backend, including
  stationary-presence evidence and exact supported subsets.
- Use the frozen v1 normalized input, bundle and result contracts. Qualify the
  chosen producer against their time, feature, continuity and integrity facts.
- Derive window, missingness, alignment, age and resource limits from that model
  and intended evaluation; no numerical defaults are justified by this design.
- The tested host/guest architecture is Linux x86-64 with GCC/Clang builds.
  Qualify the actual producer and any embedded SDK/image/package separately.
- Establish source-to-host capture mappings and same-host consumer clock trust.
  Linux expiry uses a clock including suspend time; a process restart never
  resets evidence age or restores a prior categorical result.
