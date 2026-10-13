# Planned installation and privacy contract

This document specifies future packaging behavior. There is no executable
software or install command yet, and no installability claim is made.

## Package contents

A future release package may contain only an allowlisted set of built
executables/libraries, required runtime assets, versioned schemas, example
configuration, service metadata, and applicable license notices. The release
process must record the exact allowlist and verify the package contents against
it. It must exclude build trees and caches, datasets, recordings, logs,
experiment outputs, trained models unless separately reviewed as required
runtime assets, personal or site configuration, machine identifiers, and
absolute paths from a development host. No project-workspace checkout or
private experiment directory may be needed to build or run a packaged release.

## Runtime state and network behavior

The default runtime should keep transient state in memory and publish only the
current result required by the configured local interface. Persistence and
diagnostic logging are opt-in, bounded by size and retention, and directed to
an operator-selected location outside the installation tree. Raw CSI and
training data are not logged by default. The service has no telemetry, cloud
dependency, remote reporting, or automatic source discovery. Adapters connect
only to explicitly configured local inputs and outputs.

Use per-install opaque source and zone identifiers. Do not require MAC
addresses, user identities, precise location, or a site name in the core
contract. Any deployment-specific mapping remains in operator-controlled
configuration and is not copied into package defaults or diagnostic output.

## Evidence semantics remain mandatory

Installation must not weaken source-integrity, model-compatibility, domain
eligibility, or capture-time freshness checks. A missing or incompatible
runtime asset, invalid timestamp mapping, or expired observation produces
`unknown`; packaging defaults must not supply a class label or reset evidence
age. Operational logs and service status must preserve this distinction.

## Proposed process configuration

The integrator explicitly supplies an input adapter/schema, local input stream,
reviewed bundle/profile, and volatile runtime output directory. No source, model,
site, user home or development path is guessed on startup. The installation tree
is read-only; model/configuration files are separate operator-managed inputs.

The initial service definition must disable persistent stdout/stderr capture by
default. An operator may enable bounded diagnostics explicitly. Current-result
publication is transient application output, not a history cache. At most one
historical result is held in memory; it is not restored automatically after
restart. A consumer must still expire a result if a crashed process leaves a
readable file behind. No example contains a real source or zone mapping.
