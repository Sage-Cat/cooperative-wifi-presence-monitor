# Synthetic software acceptance — 2026-10-13

Checked implementation and tests: `2416e4e9a1cbbd80373c9ee213b86bcdd3b75d30`.
Version: `0.1.0`. The checked source, test and tooling paths were unchanged during
execution. Documentation was being prepared separately.

| Check | Observed result |
|---|---|
| Static native CTest | 4/4 suites passed |
| Clang 18.1.3 ASan/UBSan | 4/4 suites passed; no sanitizer findings |
| Python process acceptance | 13 cases passed, including 12 invalid-bundle subcases |
| QEMU deterministic core | 5/5 groups passed |
| QEMU installed service | 8/8 groups passed |
| Actual guest time namespace | Passed; no guest namespace skip |
| Real guest and emulator exit | 0 and 0 |
| Fixed capture deadline through rejected candidate | Preserved |
| Guest EOF expiry observation | 7 ms after the original deadline |
| Installation | Exact five-file allowlist; package unchanged |
| Installed network syscall trace | Zero network syscalls |
| Default XDG cache/state directories | None created |

The synthetic fixture uses two sources, two samples per source and one
`synthetic_level` feature in `unitless` units. The backend takes each source's
window mean, applies weights `[1, 1]` and bias `0`, then returns absent at a
score at or below `-0.5`, present at or above `0.5`, and abstains between them.
The tested source means `-1`, `0` and `1` produce scores `-2`, `0` and `2`.
This fixture is numerical software input, not a trained presence model.

The EOF case uses maximum age `650 ms` and zero mapping uncertainty. A separate
native transition observation retained its original relative deadline at
`650 ms` after capture end through a rejected candidate, published unknown at
`654 ms`, and was read as unknown at `667 ms`.
An independent consumer accepted the retained record exactly at the deadline
and returned unknown one millisecond later. These are observed timings, not
performance guarantees. The [sanitized numerical receipt](software-acceptance-2026-10-13.json)
contains all fixture parameters, per-scenario maximum ages, the transition data,
artifact/image digests and execution times.

The guest ran Linux `6.12.107+deb13-amd64` under x86-64 QEMU TCG with one CPU,
192 MiB memory and no network device. It contained static installed executables,
static BusyBox and volatile runtime files; no development checkout. The kernel
was verified against the official Debian installer SHA-256 list. The Linux
process harness used a real FIFO, CLOCK_BOOTTIME and atomic local file
replacement. Vendor unit paths were independently checked under `/usr` and
`/usr/local`; the namespace-aware runtime checks used a fresh staged install.
The tests start the process directly; they do not claim a systemd boot test.

Clock-domain tests bind the opaque token to both the local boot and exact time
namespace identity. They reject a different namespace token on the same boot
and the superseded raw `clock_boot_id` input. Inside QEMU a disposable child
successfully created a real time namespace and forked a descendant to obtain
its new identity; that token was rejected, then normal-namespace observations
recovered. Native tests deliberately disable this namespace operation.

The two images below are genuine QMP VGA screen captures at separate execution
checkpoints. PPM-to-PNG conversion preserved every RGB pixel; no content was
edited. Raw runtime logs and provenance, including the earlier checked
`564b95b` evidence, are retained privately.

![Real QEMU deterministic core checkpoint](qemu-core.png)

![Real QEMU installed-service and time-namespace checkpoint](qemu-service.png)

The independent tests exposed simultaneous epoch/feature invalidation and
source-gap assembly recovery defects; both have retained regressions. Capture
age boundaries, late/obsolete/duplicate inference, bounded pending replacement,
exact source/feature contracts, domain changes, restart/writer exclusion and
consumer schema/clock/expiry checks remain covered by the
[reproducible acceptance routes](README.md).

These are synthetic software checks. They do not establish physical presence
accuracy, stationary-person detection, unseen-room transfer, Wi-Fi hardware
integration, OpenWrt/prplOS support, physical suspend/resume behavior or a hard
real-time bound.
