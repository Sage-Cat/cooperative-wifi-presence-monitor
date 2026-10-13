# Software acceptance evidence

These tests use synthetic numerical observations and a synthetic linear fixture.
They establish software behavior, not stationary-person detection, physical
accuracy, room transfer, radio performance or operational safety.

| Route | Concrete checks |
|---|---|
| `presence_core_acceptance` | Inclusive expiry, uncertainty and future mapping; candidate/current separation; exact source subset; bounded latest pending; unexpected, duplicate and obsolete completion; slow inference; source/domain invalidation; simultaneous epoch/feature change; signed-clock extremes |
| `presence_service` | Real process and FIFO; three estimates; synthetic provenance and opt-in; malformed/nonfinite/schema/integer inputs; exact subset and features; capture deadlines; domain/epoch invalidation and source-gap recovery; EOF; atomic readers; writer exclusion and restart; independent consumer clock/schema/expiry |
| `presence_service_guest` | Portable Linux process harness for real guest FIFO, CLOCK_BOOTTIME and atomic filesystem; eight service groups, including continuous input flood, EOF timers and time-namespace identity |
| `tools/check_install.py` | Installed-member allowlist; installed service launched from `/`; unchanged package; network-syscall trace; no default XDG cache/state directories |
| `tools/run_qemu_acceptance.py` | Actual x86-64 Linux system guest with TCG, custom volatile initramfs, no network device or development checkout; independent core and installed-service executables; real VGA checkpoints through QMP; guest test status and QEMU exit code |

The deterministic core tests pass explicit timestamps and completion values. A
slow or obsolete completion is tested through that typed API; there is no
production delay switch. Source loss and invalid input never imply absence.
The service harness reports its observed EOF expiry lag relative to the fixed
capture deadline. This observation is not a hard real-time guarantee.

Clock-domain checks independently derive the opaque token from the local boot
identity and exact `/proc/self/ns/time` link. Only a missing namespace link
uses `global`; other errors fail closed. They reject a same-boot token for a
different time namespace and the superseded raw `clock_boot_id` input field.
The reference consumer receives its trusted expected token independently.
The real namespace operation is enabled only by the guest harness flag: a
disposable guest child calls `unshare(CLONE_NEWTIME)`, then its descendant
obtains the new identity. Native tests leave that operation disabled. Guest
unavailability or permission failure is reported explicitly as a skip.

## Reproduction

Build, test and stage installation using the normal CMake workflow. A separate
static build is convenient for the minimal guest:

```sh
cmake -S . -B build/static -DPRESENCE_STATIC=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/static -j
ctest --test-dir build/static --output-on-failure
cmake --install build/static --prefix /tmp/presence-installed
python3 tools/check_install.py --prefix /tmp/presence-installed --scratch /tmp/presence-install-check
python3 tools/run_qemu_acceptance.py \
  --qemu /path/to/qemu-system-x86_64 --kernel /path/to/verified-linux \
  --bios-directory /path/to/seabios \
  --monitor /tmp/presence-installed/bin/wifi-presence-monitor \
  --guest-tests build/static/presence_service_tests \
  --core-tests build/static/presence_core_acceptance \
  --scratch /tmp/presence-qemu-check
```

Supply an independently verified compatible kernel, x86 QEMU and BIOS files.
The harness never downloads them. Its host dependencies are Python 3, CPIO,
static BusyBox and QEMU; the installation check additionally uses `strace`.
Scratch directories must be new, outside the installation and source tree;
keep their paths short enough for a Unix-domain QMP socket. The guest has one
virtual CPU, 192 MiB memory and no network device. Its runtime is tmpfs.

The core checkpoint and service checkpoint are separate unmodified QMP PPM
screen captures. They may be converted losslessly to PNG for publication.
Receipts record executable, kernel, BIOS, initramfs, serial-log and image hashes.
Raw logs and runtime provenance belong in private execution evidence; only
reviewed synthetic summaries and console images belong here. A failed run is
retained as failed evidence, never relabelled by a later successful run.

Suspend time contributes to CLOCK_BOOTTIME by the Linux clock contract. These
tests do not claim a physical suspend/resume experiment or qualification on
OpenWrt, prplOS, non-x86 architectures or real Wi-Fi receivers.
