# Cooperative Wi-Fi Presence Monitor

**Software means — design specification** · `cooperative_wifi_presence_monitor`

This project specifies a portable, read-only service that publishes a current
zone-level Wi-Fi presence estimate with its evidence validity, age and provenance.
The estimate is `present`, `absent` or `unknown`. Missing, incompatible or expired
evidence cannot be interpreted as absence.

**Status: design only. No executable, trained model, installable package or
experimental performance result is provided.** The proposed language is C++17;
Linux is the first service target. OpenWrt and prplOS require separately qualified
platform adapters and equipment. Implementation and evaluation are future work.

Start with the [design overview](docs/design.md), including the decisions and
items to resolve before implementation. The [PlantUML catalogue](docs/diagrams/README.md)
contains editable sources and SVG views for architecture, data, result states,
normal processing, interrupted input, model reload, deployment and user goals.

## Planned behavior

1. Receive versioned observations through an explicitly configured source adapter.
2. Assemble bounded windows while preserving gaps, timestamps and source facts.
3. Check evidence, exact source/model compatibility, declared domain eligibility
   and capture-based freshness.
4. Apply a frozen, reviewed feature pipeline and model to eligible windows.
5. Recheck eligibility and age before atomically publishing a result.

A gap or receiver dropout after a complete window does not by itself erase that
window's still-eligible estimate. It never extends its original expiry. Known
invalidation, reload or expiry withdraws the current categorical estimate;
last-known information remains separately identified history. The consumer also
checks expiry so a stalled publisher cannot make old evidence current forever.

## Boundaries

The means does not collect CSI, train models, command measurement rates, or alter
radio/network policy. Source and context adapters are optional; the proposed
`cws-source-record/2` and CWS integrations are examples, not dependencies. A
research workspace or private configuration is not a runtime requirement.

Outputs concern a configured zone, not a person's identity or exact position.
`absent` is the reviewed model's negative class, not proof that nobody is there.
Counting, fall detection, medical interpretation and SOS handling belong to
separate prospective components. [Use cases](docs/use-cases.md) show that boundary.

## Design documents

- [Architecture and component responsibilities](docs/architecture.md)
- [Data objects and ownership](docs/data-model.md)
- [Result lifecycle and failure decisions](docs/lifecycle.md)
- [Input, model, time and output contracts](docs/interfaces.md)
- [Platforms and deployment](docs/platforms.md)
- [Installation and privacy](docs/installation.md)
- [Future verification and acceptance scenarios](docs/verification.md)

`mean.toml` records the practical contract. Runtime state is volatile by default;
logging and persistence are separate bounded opt-ins. No telemetry, automatic
source discovery, author configuration or site mapping is part of the package.
