# Cooperative Wi-Fi Presence Monitor

**Linux software means** · C++17 · version 0.1.0

Publish a current zone-level `present`, `absent` or `unknown` estimate from
supplied normalized Wi-Fi features, with separate evidence validity, declared
domain eligibility, capture age and provenance. Missing or expired evidence
never means absence.

The implementation includes a portable core, strict local NDJSON decoder,
frozen linear inference adapter, bounded Linux worker/timer loop and atomic
local JSON publication. It provides no radio acquisition, trained physical
presence model or claim of real-world detection accuracy. Included fixture
recipes are explicitly synthetic and require `--allow-synthetic`.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /usr/local
wifi-presence-monitor --config /etc/wifi-presence-monitor/config.json
```

See [build/install instructions](docs/building.md) for dependencies, unprivileged
staging, core-only and sanitizer/static builds, service-account setup and the
optional systemd unit. The operator supplies the input FIFO, frozen reviewed
bundle/profile and trusted manifest hash. No model or site configuration is
installed by default.

## Current scope

One process serves one configured zone. It reads one local stream, assembles
bounded exact-source windows, qualifies them, executes one frozen model and
writes one atomic current-result file. Configuration changes require restart.
One worker and at most one pending candidate keep inference backlog bounded;
input processing cannot own the expiry loop. Consumers verify the local clock
domain and original deadline independently at every use.

A later bad candidate or node dropout cannot renew or by itself erase an
independently eligible earlier estimate. Known relevant invalidation, accepted
abstention and expiry withdraw the class. Candidate diagnostics and optional
last-known history remain distinct from current evidence in the same record.

## Verification and evidence

[Software acceptance evidence](docs/testing/README.md) documents native tests,
sanitizers, clean installation and actual QEMU Linux system guests. The QEMU
harness boots the installed service with a volatile root filesystem, synthetic
inputs and no network device; genuine VGA console captures and derived results
are retained with exact artifact hashes. These are software checks, not physical
sensing studies. Linux x86-64 is tested; OpenWrt and prplOS qualification is future
work.

## Documentation

- [Architecture, patterns and dependency direction](docs/architecture.md)
- [Implemented pipeline and its scope](docs/implementation.md)
- [Versioned configuration/model/input/result contracts](docs/interfaces.md)
- [Data ownership](docs/data-model.md) and [result lifecycle](docs/lifecycle.md)
- [Five observation-support failure causes](docs/lifecycle.md#five-causes-of-lost-observational-support)
- [Editable PlantUML and SVG diagrams](docs/diagrams/README.md)
- [Installation/privacy](docs/installation.md) and [platform boundary](docs/platforms.md)
- [Acceptance requirements](docs/verification.md) and [use cases](docs/use-cases.md)

The core depends on standard C++ values and pure rules. JSON, OpenSSL, Linux I/O
and threading stay outside it. Runtime state is volatile; there is no telemetry,
persistent cache, source discovery, remote/context/health API or radio control.
Outputs concern a zone, not identity or exact position. Person counting, fall
detection, medical interpretation and SOS are separate prospective capabilities.
Dependency license notices are retained in the installed package.
