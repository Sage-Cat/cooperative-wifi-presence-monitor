# Platforms and deployment boundary

## Portable C++17 core

The proposed core is C++17 and owns only deterministic parsing/validation of
normalized records, window assembly, eligibility and freshness checks, frozen
feature execution, three-valued classification, and result serialization. It
must not own serial ports, shell commands, wireless drivers, AP configuration,
or network-control policy. The core receives typed values from adapters; it
does not discover a backend from a platform name and infer its capabilities.

A future CMake build keeps the core independent of systemd, `ubus`, TR-181, and
vendor SDKs. Select and pin JSON/model-runtime dependencies and cover them in
the bundle contract; C++17 alone does not define ABI, target, or dependencies.
The actual build manifest must name supported architectures and libraries.
There is no C++ source, project, binary, or supported toolchain today.

## POSIX Linux first

The first proposed service adapter is for a conventional Linux host, including
the existing Raspberry Pi controller/collector host. It may use a systemd unit,
read-only access to collector record output and approved CWS context, a local
socket for result publication, and an atomically replaced current-result file.
Service paths, user/group, permissions, and retention are deployment config,
not compiled defaults. The adapter must not run as root without a separately
reviewed device-access requirement. The Collector remains responsible for
exclusive serial access; the monitor does not open the sensor serial device.

Every feed must preserve the source envelope and expose gaps/reconnects. The
collector UDP mirror is best-effort; finalized records remain replay authority.
Files, sockets, and UDP binds must be local or deployment-isolated and
authenticated. This design does not authorize a network listener.

## OpenWrt SDK / procd / ubus: optional future target

OpenWrt is not implied by Linux portability. A future package would require a
selected OpenWrt SDK and target ABI, cross-build verification, package metadata,
procd service definition, storage and watchdog decisions, and compatibility
checks for the exact release and hardware. `ubus`/UCI integration would be a
separate adapter whose operations, readback, and permissions are documented.
The presence monitor itself remains read-only. It would not write UCI, call a
radio/channel setter, change association, set QoS, or install a measurement
policy.

The CWS Control Daemon has a backend-neutral adapter contract but no real AP
adapter, client, or confirmed API. It is not a runtime dependency here. Any
later controller integration remains read-only to this monitor. OpenWrt is an
explicit optional later design target, subject to a selected SDK, target ABI,
package build, and target-device verification. OpenWrt documents `procd` service
management, `ubus` inter-process APIs, and package creation; these provide
platform integration mechanisms, not a guarantee that a device exports the
required CSI or normalized source records. See the [OpenWrt procd guide](https://openwrt.org/docs/guide-developer/procd),
[ubus reference](https://openwrt.org/docs/techref/ubus),
[package creation guide](https://openwrt.org/docs/guide-developer/packages), and
[build options](https://openwrt.org/docs/guide-developer/packages.flags). The
package must verify its actual C++17 compiler/library and dependency support in
the chosen SDK; the platform documentation does not establish that support for
all targets.

## prplOS as currently integrated

The existing prplOS path is a read-only shell telemetry agent for available
TR-181/pWHM counters. It posts to CWS; it does not run this service on an AP or
configure radio/QoS/security/association. The initial implementation target remains the Linux collector/controller host,
with available prplOS data as optional context. A later prplOS adapter and
AP-local inference are included in the design range, conditional on a named
image, SDK, permissions, and stable radio/CSI APIs. The prplFoundation
[Wi-Fi sensing component](https://gitlab.com/prpl-foundation/prplmesh/wifi-sensing)
and its [prplMesh Wi-Fi Sensing data-model documentation](https://gitlab.com/prpl-foundation/prplmesh/prplMesh/-/wikis/Wi-Fi-Sensing)
are concrete integration references for a particular stack profile. They do
not imply a universal CSI interface across prplOS images, vendors, radios, or
firmware. No installed support, API compatibility, CSI availability, or AP-local
runtime is claimed for this means today.

## Deployment claims

“Portable Linux service” means buildable and operable on each verified named
target. It does not imply OpenWrt, prplOS, every Pi, arbitrary CSI chipsets, or
production hardening. OpenWrt and prplOS remain explicit optional later targets,
not part of the initial Linux pilot. Record toolchain, dependencies, transport, context schema,
and model profile. A build does not establish scientific accuracy, occupancy
truth, privacy acceptability, or operational safety.
