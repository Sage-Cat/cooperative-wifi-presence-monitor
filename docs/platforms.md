# Platforms and deployment boundary

## Portable core

The proposed C++17 core owns deterministic validation of normalized records,
window assembly, eligibility and freshness checks, frozen feature execution,
three-valued classification, and result serialization. It receives typed
values from adapters. It does not own radio acquisition, device discovery,
wireless drivers, platform control APIs, or network policy.

A future build must name supported architectures, compilers, libraries and
runtime dependencies. C++17 alone does not define ABI or guarantee that a
target supplies those dependencies. No source code, binary, package, or
supported toolchain is provided by this design.

## Linux service adapter

Linux is the proposed first service target, subject to a named and tested
support matrix. The adapter may manage service lifecycle, local input access,
result publication, and bounded diagnostics. Paths, permissions, retention,
service user, and transport are deployment configuration, not compiled
defaults. The process should run with least privilege and read-only access to
observation sources. A build for one Linux target does not establish support
for other architectures or distributions.

## Optional embedded adapters

OpenWrt is a possible later target requiring a selected SDK, target ABI,
dependency verification, package metadata, service definition, and device
testing. `procd` and `ubus` provide service and IPC mechanisms; they do not
provide CSI or normalized observations. Integration must remain read-only and
must not write configuration or alter radio, association, routing, QoS, or
measurement policy.

prplOS or another managed access-point stack is a possible platform adapter
only when a specific image, SDK, permission model, and stable observation
interface have been verified. A stack-specific sensing interface is not a
universal CSI interface. No AP, firmware, vendor, or sensing API compatibility
is claimed.

## Deployment claims

“Portable” describes a core boundary and design intent. Installability and
support apply only after clean builds, packaging, and operation have been
verified for named targets. A platform adapter cannot compensate for missing
or unsuitable sensing data. Software conformance does not establish detection
accuracy, transfer to another environment, privacy acceptability, or safety.
