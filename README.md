# Cooperative Wi-Fi Presence Monitor

**Software MEANS design** · `cooperative_wifi_presence_monitor`

This directory specifies a separately installable, read-only Linux service for
publishing a current zone-level Wi-Fi presence estimate together with its
validity, age, and evidence provenance. It consumes records owned by the
Autonomous CSI Collector and may consume read-only operational context from the
CWS Control Daemon. It does not acquire serial data, control sensing rates,
change a radio or network, or replace either existing service.

**Status: design only; review pending. There is no executable implementation,
model artifact, experiment, deployment, or active scientific claim here.** The
proposed implementation language is C++17. The portable decision core and
external interfaces are described in `docs/`; authoring code is a later step.

## Bounded practical task

The monitor turns a bounded window of compatible, integrity-checked CSI source
records into one of three current states: `present`, `absent`, or `unknown`.
Every result carries separate evidence-validity and domain-eligibility states,
an age bound and expiration, and identifiers and hashes for the source,
feature pipeline, model, and configuration. Invalid, stale, unsupported,
incompatible, or insufficient evidence yields `unknown`; it must never be
converted to `absent`. The last known estimate is retained as historical
context and is not presented as the current state after expiry.

The intended operator is an authorized operator of an indoor cooperative Wi-Fi
sensing setup. A future deployment could expose the result to a local
human-facing or downstream application, but this design makes no claim about
shelter, defense, medical, safety, or production-network performance. “Zone” is
a configured sensing area, not a person identity or precise location.

## Relationship to current means

The Autonomous CSI Collector remains the authority for serial acquisition,
source-record integrity, and firmware-rate transaction acknowledgements. The
CWS Control Daemon remains the authority for measurement-policy decisions,
timing evaluation, and its verified rate-control path. The monitor may read
their declared outputs but does not write to their state, submit control
commands, or reinterpret their decisions as presence labels.

CWS currently exposes evidence availability, confidence/reliability, a motion
trigger, and a fused motion score. Those values support measurement control;
they are not a presence classifier. This design therefore requires an explicit
frozen feature pipeline and model artifact before it can publish `present` or
`absent`. It does not claim a new scientific method. Any new classification
rule, quantitative result, or human-performance claim needs a separately
defined and reviewed method and its own evidence.

`mean.toml` records the identity and practical contract. The four documents in
`docs/` specify architecture, interfaces, platforms, and future verification.
Runtime outputs, local configuration, model bundles, private experiment
material, generated build trees, and worktrees are excluded. Privacy, licensing,
validation, and publication review precede any distribution.
