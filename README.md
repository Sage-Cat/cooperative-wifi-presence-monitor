# Cooperative Wi-Fi Presence Monitor

**Software means — design specification** · `cooperative_wifi_presence_monitor`

This project specifies a portable, read-only service that publishes a current
zone-level Wi-Fi presence estimate with evidence validity, age and provenance.
The estimate is `present`, `absent` or `unknown`. Missing, incompatible or expired
evidence cannot be interpreted as absence.

**Status: design only. No executable, trained model, installable package or
experimental performance result is provided.** C++17 and Linux are the first
implementation targets. OpenWrt and prplOS remain later qualification tasks.
Implementation and evaluation follow design review.

Start with the [design overview](docs/design.md) and
[Clean Architecture boundaries](docs/architecture.md). The
[PlantUML catalogue](docs/diagrams/README.md) contains editable sources and SVG
views of dependencies, processing, data, states, restart, deployment and user goals.

## First-version scope

One process serves one explicitly configured sensing zone:

1. Read one declared normalized NDJSON input stream from a local pipe.
2. Assemble bounded windows with exact sources, gaps, timestamps and epochs.
3. Check integrity, model/feature compatibility, declared domain and freshness.
4. Execute one frozen, reviewed model/pipeline through a bounded worker.
5. Recheck the result and atomically replace one local current-result JSON file.

The portable domain and application logic do not depend on JSON, Linux, threads
or an inference library. Concrete adapters are wired at startup; only model
execution needs a replaceable behavior contract. There is no interface per
processing step, plugin loader, network API, operational-context input or hot
reload. Configuration and model changes require a controlled restart.

A later gap or node dropout does not erase an independently eligible earlier
window or extend its expiry. Known relevant invalidation and expiry withdraw the
current class. A consumer also checks freshness at every use, so a stalled
publisher cannot make old evidence current indefinitely. Rejected-candidate
diagnostics and optional last-known history remain distinct from current evidence
within the same output record.

## Boundaries

Acquisition, model training, measurement-rate control and radio/network policy
remain outside this means. No research workspace, private configuration or
collector-specific project is a runtime dependency. Later source/platform
adapters may be added when there is a concrete supported integration.

The estimate concerns a zone, not identity or exact position. `absent` is the
reviewed model's negative class, not proof that nobody is there. Counting, fall
detection, medical interpretation and SOS handling are separate prospective
capabilities. [Use cases](docs/use-cases.md) show that boundary.

## Design documents

- [Architecture, patterns and proposed code boundaries](docs/architecture.md)
- [Data objects and ownership](docs/data-model.md)
- [Result lifecycle and five failure causes](docs/lifecycle.md)
- [Input, model, time and output contracts](docs/interfaces.md)
- [Platforms and deployment](docs/platforms.md)
- [Installation and privacy](docs/installation.md)
- [Future verification and acceptance scenarios](docs/verification.md)

`mean.toml` records the practical contract. Runtime state is volatile. There is
no telemetry, persistent cache, automatic discovery, author configuration or site
mapping in package defaults. Optional bounded process diagnostics contain no raw
observations and introduce no additional consumer interface.
