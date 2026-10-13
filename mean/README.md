# Cooperative Wi-Fi Presence Monitor mean record

The [mean contract](../mean.toml) identifies this software mean and its practical
task. Its implementation is the repository root; this directory provides
navigation to that contract, the maintained software and its verification.

## Implemented practical result

Version 0.1.0 is an installable C++17 Linux program. It consumes explicitly
configured normalized cooperative Wi-Fi observations, qualifies exact-source
windows and publishes a local `present`, `absent` or `unknown` zone estimate.
Evidence validity, domain eligibility, capture-based expiry and provenance are
separate from the estimated class. Consumers independently check the original
validity deadline; missing observations never imply absence.

| Responsibility | Implementation | Documentation |
|---|---|---|
| Pure qualification and current-state rules | [Domain](../src/domain/policy.cpp) | [Result lifecycle](../docs/lifecycle.md) |
| Window assembly, job identity and bounded pending state | [Coordinator](../src/application/monitor.cpp) | [Clean Architecture](../docs/architecture.md) |
| Strict contracts, pinned model and atomic publication | [Adapters](../src/adapters/) | [Versioned interfaces](../docs/interfaces.md) |
| Clock, local stream, timer and one worker | [Linux executable](../app/main.cpp) | [Build and installation](../docs/building.md) |

The installed package contains the executable, optional systemd unit and license
notices. Operator configuration, model data and volatile runtime output have
separate ownership. No author-specific input, recording or build cache is
installed. See the [installation boundary](../docs/installation.md).

## Verification and maturity

The [checked software report](../docs/testing/software-acceptance-2026-10-13.md)
and [numerical receipt](../docs/testing/software-acceptance-2026-10-13.json)
identify the tested source and artifact hashes. Native, static and Clang
ASan/UBSan checks passed. A real QEMU Linux guest passed five core and eight
installed-service test groups, including a real child time-namespace check.
The [reproduction routes](../docs/testing/README.md) and genuine captures in the
[README gallery](../README.md#verification-and-evidence) make the checks inspectable.

These checks establish software behavior with synthetic inputs and a synthetic
model. Raw CSI acquisition, a physically validated presence model, real occupancy
accuracy and OpenWrt/prplOS qualification remain outside this verified scope.
The mean is a software implementation, not a separately claimed scientific
method or evidence of field deployment.
