# Future implementation verification

These are future software acceptance criteria, not completed checks, physical
experiment requirements, or scientific evidence. Manuscripts and experiment
plans belong to their publication/experiment owners.

## Contract and provenance checks

- Accept collector `cws-source-record/2` only after validating required fields,
  raw-record SHA-256, parser status, epoch types, source identity, and supported
  major version. Test historical v1 handling without inventing v2 facts.
- Reject malformed JSON/CSI, bad hashes, unknown majors, impossible sequence
  transitions, duplicate or late inputs where the configured window cannot
  resolve them, and invalid configuration/model manifests. Preserve typed
  failure reasons in service health output.
- Verify model, feature-pipeline, profile, and configuration hashes before
  inference. Test that feature order, units, preprocessing, window length, and
  class mapping exactly match the bundle. A mismatch blocks inference and
  publishes current `unknown`.
- Verify canonical serialization and result schema. Unknown output majors and
  missing required fields are errors; unavailable fields remain `null` or
  typed unknown states.

## Validity and inference behavior

- Exercise all three estimate values and all three evidence-validity and
  domain-eligibility states. A failed prerequisite must never produce a
  present/absent estimate. Invalid or missing evidence must not be silently
  dropped to make a source subset appear compatible.
- Exercise capture-time freshness at just-inside, equal-to, and just-outside
  configured limits, including clock mapping uncertainty. Assert age is based
  on capture end, not arrival. Missing capture-time mapping yields no positive
  validity horizon.
- Exercise device reconnect, boot/config epoch change, source gaps, late
  arrival, clock reset, model reload, pipeline mismatch, and service restart.
  Verify the current result becomes unknown until a new complete compatible
  window is available, while any last-known result remains separate and retains
  its original expiry.
- Test single-source and cooperative subsets against manifest declarations.
  Unsupported subsets, incompatible feature profiles, insufficient source
  count, or cross-source timing uncertainty above the declared tolerance must
  abstain. The service must not infer compatibility from node count alone.
- Test network/QoS context stale or failed independently from sensing-window
  validity. A service-context fault must not instantaneously erase a fresh
  result unless an explicit frozen profile rule requires that consequence.
- Test deterministic replay for identical windows, artifact hashes, and config;
  only explicitly variable issuance/clock fields may differ.

## Adapter and service checks

- Linux tests use temporary paths and mocked inputs to verify read-only owner
  boundaries, atomic publication, bounded history, permissions, restart
  behavior, and absence of firmware/network-control calls.
- Feed collector source-record fixtures through both replay and the chosen live
  adapter. Deliberately drop, duplicate, reorder, and reconnect records; the
  result must expose those facts and follow the frozen unknown/expiry behavior.
- Verify the CWS context adapter against a versioned fixture. Until its schema
  is selected, context remains optional and may report unknown.
- Build and run unit/contract tests for every named Linux architecture and
  compiler/toolchain in the eventual support matrix. OpenWrt SDK/procd/ubus
  package tests are separate future acceptance checks; prplOS operation is not
  implied by a Linux build.

## What these checks do not establish

Contract tests establish interface conformance; fixtures establish behavior on
their declared inputs. Neither proves people detection, transfer to a new room,
protective-site suitability, or dependable alarms. Those claims require
separately authorized studies, reference labels, held-out conditions, privacy
and human/operational review. No such evidence is part of this design.
