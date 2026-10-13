# Architecture

The monitor is a read-only observation consumer and result publisher. It owns
neither acquisition nor training data, reference labels, radio control or
measurement policy. The design is proposed; no component is implemented.

![Planned components](diagrams/components.svg)

## Components and ownership

| Component | Responsibility | Must not do |
|---|---|---|
| Source adapter | Decode one declared source schema; preserve sequence/epoch, capture time, integrity and missingness facts | Invent missing metadata, command a radio or infer presence from transport status |
| Window assembler | Build bounded source windows; account for gaps, duplicates, late records and discontinuities | Silently fill gaps, mix epochs or create an apparently compatible subset by dropping invalid sources |
| Evidence qualifier | Assess window integrity, feature/subset compatibility, clock support, declared domain eligibility and freshness | Guarantee detection of unobserved environmental change |
| State coordinator | Own current result, generation, invalidation and expiry; serialize all state transitions | Extend a window's horizon because input resumes or inference completes |
| Inference worker | Apply the frozen feature pipeline and reviewed model to an immutable qualified window | Train/tune online, select undeclared subsets or publish directly |
| Result publisher | Atomically serialize a coordinator-approved result and separate optional historical result | Substitute historical class for an unknown current class |
| Host adapter | Own local I/O, a monotonic timer, service lifecycle and bounded diagnostics | Change core semantics, network policy or source authority |

An optional context adapter supplies versioned operational facts. Missing context
is independent of sensing validity unless the frozen profile explicitly requires
that field. Network load is not a presence label. Source-specific formats and
optional CWS integration remain behind adapters; the core is not tied to one
collector, vendor API or project checkout.

## Event and execution model

The host adapter feeds one serialized coordinator with observation, clock,
source-discontinuity, context, domain-change, bundle-reload and worker-completion
events. The portable core receives explicit time values rather than reading an
OS clock itself. A host monotonic timer runs even when no records arrive.

One inference worker receives an immutable window, bundle handle and generation.
It cannot mutate current state. After completion, the coordinator verifies the
window/bundle generation and all still-applicable conditions, then recomputes age
using the publication-time clock. A result that became stale during computation
is not published as current. These steps are shown in the
[normal-processing sequence](diagrams/observation_flow.svg).

A running inference job is not assumed cancellable. On invalidation or reload,
the coordinator advances the generation and discards any later old-generation
completion. One worker limits concurrency; one pending latest eligible window
may replace an older pending window, with a skipped-window diagnostic. This
scheduling choice does not remove samples from a retained window or relax its
missingness limits. A hung worker leaves current state subject to normal expiry;
software conformance provides no hard real-time or accuracy guarantee.

Window sizes, maximum source count, frame size, bundle size and memory limits
are explicit
configuration checked against the bundle. Overflow is recorded as capacity/data
loss, never disguised as an intact window. No unbounded replay queue, automatic
source discovery or recovery by increasing measurement traffic is planned.

## Candidate and current result are different objects

A candidate is a new window/model output awaiting acceptance. A current result
is the last accepted estimate still eligible under its original evidence and
expiry. A rejected candidate cannot refresh that result's timestamp or horizon.
The current result may remain categorical after a gap/dropout only when its own
conditions still hold. Candidate failures go to health/status diagnostics.

Known changes that invalidate the current window's interpretation, an explicit
reload, restart or expiry withdraw the current categorical result. Missingness
in a later window alone is not such proof. Current `unknown` and historical
`last_known` remain separate. See [lifecycle rules](lifecycle.md).

## Model and configuration lifecycle

The reviewed bundle binds the model artifact, exact feature pipeline, class
semantics, supported source subsets, time requirements and operating envelope.
Hashes verify artifact identity, not scientific correctness. The task must cover
stationary presence if its negative output is interpreted as `absent`.

Reload first publishes `unknown`, advances the generation, and clears assembly
state. The candidate bundle is validated before becoming active. Failure leaves
the service unknown with explicit reasons; no automatic rollback or model search
is planned. A successful reload needs new observations under the new generation.
The [reload sequence](diagrams/model_reload.svg) makes the boundary explicit.

## Output and consumer boundary

The proposed `wifi-presence-result/1` record carries class, decision reasons,
evidence/domain assessments, source window, capture-based age, expiry and
model/pipeline/configuration provenance. Diagnostic health is separate from
presence. The initial publisher writes a complete temporary record and atomically
replaces the configured current-result file on the same filesystem.

A consumer checks schema, clock mapping and expiry on every use, including when
the file has not changed. Publisher/IPC failure is not a new observation. The
consumer must withdraw an expired class independently of the process producing
it. The service does not count people, locate them, detect falls or send SOS.
