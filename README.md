# Cooperative Wi-Fi Presence Monitor

**Software means — design specification** · `cooperative_wifi_presence_monitor`

This repository specifies a portable, read-only software service for publishing
a zone-level Wi-Fi presence estimate together with its validity, age, and
evidence provenance. Its core consumes a normalized observation interface;
platform and source adapters are optional boundaries. A proposed adapter for
`cws-source-record/2` and optional read-only CWS context are examples, not
dependencies. The service does not acquire radio data or control a radio,
network, or measurement rate.

**Status: design only. There is no executable implementation, model artifact,
experiment, deployment, or measured performance claim here.** The proposed
implementation language is C++17. The portable decision core and external
interfaces are described in `docs/`; implementation and installability remain
future work.

## Bounded practical task

The monitor turns a bounded window of compatible, integrity-checked CSI source
records into one of three current states: `present`, `absent`, or `unknown`.
Every result carries separate evidence-validity and domain-eligibility states,
an age bound and expiration, and identifiers and hashes for the source,
feature pipeline, model, and configuration. Invalid, stale, unsupported,
incompatible, or insufficient evidence yields `unknown`; it must never be
converted to `absent`. The last known estimate is retained as historical
context and is not presented as the current state after expiry.

The intended use is an indoor cooperative Wi-Fi sensing setup. A future
deployment could expose the result to a local application, but this design
makes no claim about shelter, defense, medical, safety, or production-network
performance. “Zone” is a configured sensing area, not a person identity or
precise location.

## Adapter boundaries

Source adapters translate declared, versioned observation formats into the
normalized core interface while preserving source facts and uncertainty. The
proposed `cws-source-record/2` adapter is one optional integration. A CWS
context adapter is also optional and cannot supply a presence class. Neither
adapter or project checkout is a runtime requirement; an equivalent source can
implement the normalized interface independently.

Optional operational context remains distinct from sensing evidence. A context
field is usable as a model input only when the frozen model contract explicitly
declares its source and version. The service requires a frozen feature pipeline
and model artifact before it can publish `present` or `absent`. This design
does not claim a new scientific method or measured performance.

`mean.toml` records the identity and practical contract. The documents in
`docs/` specify architecture, interfaces, platforms, future verification, and
a planned installation and privacy contract. No install command is available
until an implementation exists.

See [use cases and actors](docs/use-cases.md) for the operator, integrator,
observation-source, and consuming-application interactions.
