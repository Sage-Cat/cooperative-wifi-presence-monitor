# Design overview

This is a proposed software design for the Cooperative Wi-Fi Presence Monitor.
It specifies behavior to implement and verify; diagrams are not execution or
scientific-performance evidence. No executable or validated presence model is
supplied.

## Practical goal

Publish a current zone-level estimate only while a reviewed model and its
observations support that use. Separate three questions: what class the model
returns, whether the evidence is usable, and whether it is eligible for the
declared operating conditions. `unknown` is a usable output when any required
condition is unresolved or the model abstains.

The means consumes observations; acquisition and measurement policy remain with
their existing owners. The operator can inspect the estimate, its age and reasons.
The integrator supplies a reviewed model/profile and explicitly configured local
interfaces. A consumer must respect the validity horizon rather than treating the
last readable record as indefinitely current.

## Views

| View | Main question | Document |
|---|---|---|
| Components | Who owns each processing step? | [Architecture](architecture.md), [diagram](diagrams/components.svg) |
| Data | Which facts belong to observations, candidates, current and historical results? | [Data model](data-model.md), [diagram](diagrams/core_contract.svg) |
| States | When may a categorical estimate persist or be withdrawn? | [Lifecycle](lifecycle.md), [diagram](diagrams/result_state.svg) |
| Normal processing | What is checked before and after inference? | [Sequence](diagrams/observation_flow.svg) |
| Interrupted input | How do gaps, expiry and recovery differ? | [Sequence](diagrams/failure_recovery.svg) |
| Reconfiguration | How are old-generation results excluded? | [Sequence](diagrams/model_reload.svg) |
| Deployment | What is portable and what is platform-specific? | [Platforms](platforms.md), [diagram](diagrams/deployment.svg) |
| Users | What does each actor do? | [Use cases](use-cases.md) |

All editable sources and rendering instructions are in the
[PlantUML catalogue](diagrams/README.md).

## Design decisions

| Decision | Choice and reason |
|---|---|
| Core boundary | A C++17 library receives typed events and immutable model/window data. OS, source transport and radio drivers stay outside it. |
| Execution | One serialized state coordinator owns current state, generation and expiry. One inference worker processes immutable jobs so model computation does not own the expiry timer. |
| Admission versus retention | Rejecting a new candidate does not automatically invalidate a previous eligible window. Retention is limited by that window's original deadline and applicability. |
| Publication | The first Linux adapter reads an explicitly configured local newline-delimited stream and writes one atomic current-result JSON record into a configured volatile runtime directory. |
| Cooperative processing | Only exact source subsets declared by the reviewed model may be combined, with a defensible common time basis. Node count alone is insufficient. |
| Consumer responsibility | Every consumer verifies result schema and expiry independently. Missing or unbounded clock/transport mapping yields no current categorical estimate. |
| Privacy and resources | Opaque IDs, no default persistent cache/log or telemetry, bounded windows, one running job and at most one pending latest eligible window. Capacity loss remains visible. |
| Platform support | Linux first; OpenWrt and prplOS are separate build/package/permission/interface qualification tasks. A Linux build does not establish AP support. |

A model bundle must declare a presence task and the meaning of its classes.
A motion-only negative label cannot be renamed `absent`. Stationary-presence
coverage must be part of the reviewed task evidence before that interpretation
is used. The service can validate the declaration and artifacts; it cannot
certify the underlying scientific study automatically.

## Requirements and acceptance routes

All routes below are future checks, not completed software tests.

| ID | Required behavior | Verification route |
|---|---|---|
| R1 | Preserve input integrity, gaps and capture provenance | Adapter/window contract cases |
| R2 | Infer only with a compatible reviewed bundle and exact supported subset | Bundle, feature and subset cases |
| R3 | Keep evidence validity, domain eligibility and model output separate | Decision and result-schema cases |
| R4 | Never refresh age from receipt, inference completion or repeated publication | Clock, expiry and consumer cases |
| R5 | Keep expiry responsive and reject stale-generation inference completions | Delayed-worker and reload cases |
| R6 | Preserve an eligible old result only until its original horizon | Gap/dropout/recovery cases |
| R7 | Remain read-only, bounded and independent of a development checkout | Host-adapter and clean-install cases |
| R8 | Make no automatic person-count, distress or assistance decision | Boundary review and consumer contract cases |

See [verification](verification.md) for concrete scenarios and the limits of
software conformance. Physical presence accuracy and environmental transfer
require separate studies.

## Inputs to resolve before implementation

The behavior is specified, but deployment-specific facts must not be invented:

- Select and review an actual presence model, feature pipeline and inference
  backend, including stationary-presence evidence and exact supported subsets.
- Freeze the normalized input encoding, result schema and canonical hash rules
  before any producer/consumer depends on them.
- Set window, missingness, alignment, expiry and resource limits from the
  reviewed model and intended evaluation; no numeric defaults are justified here.
- Name the first Linux architecture/toolchain and observation interface, then
  qualify any embedded SDK/image and packaging separately.
- Establish clock/transport bounds for the chosen consumer. If they cannot be
  bounded, that route cannot carry a current categorical estimate.
