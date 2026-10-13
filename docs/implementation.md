# Implemented inference and runtime scope

The implementation provides a C++17 portable evidence/state core and a Linux
service with a concrete frozen linear inference adapter. It consumes normalized
features supplied by an explicitly configured producer. It does not extract
CSI from a radio, train a model or validate a sensing study.

## Fixed processing pipeline

The model manifest declares the exact ordered source IDs, feature names and
units, samples per source, capture/alignment/uncertainty/age limits and required
operating metadata. Each source contributes a bounded disjoint window. Gaps,
reordered records and epoch changes are handled explicitly; missing samples are
not zero-filled. A cooperative window requires the entire configured subset.
Other subsets require separately selected and reviewed startup contracts.

For each declared source, the adapter computes the arithmetic mean of each
feature over that window. The model vector concatenates those means in source
order and then feature order. The score is:

```text
score = bias + sum(weights[j] * feature_mean[j])
score <= absent_threshold    -> absent
score >= present_threshold   -> present
otherwise                   -> unknown (model abstention)
```

Thresholds and finite weights are fixed runtime assets. The lower threshold
must be strictly less than the upper. Non-finite inputs or computation failure
cannot produce a categorical estimate. Pipeline and model artifact bytes are
verified against the manifest; the manifest is verified against an expected
SHA-256 supplied by the operator. These checks establish artifact identity,
not scientific validity or source authenticity.

## Model maturity

The included fixture recipes create only synthetic test models and observations.
They exercise software behavior, not people detection. Synthetic models require
an explicit `--allow-synthetic` startup option, and results identify that scope
in provenance. The ordinary startup path rejects such a model.

A deployment model must declare a reviewed zone-presence task, stationary-person
scope, negative-class semantics and a review reference. The operator must review
and pin that bundle. A declaration in a file does not independently establish
accuracy, transfer to another room or operational safety. No reviewed physical
model is supplied by this repository.

## Input trust and time

The first input encoding is `wifi-presence-observation/1`. Its feature vectors
are already normalized according to the pinned pipeline. Integrity/parser fields
are explicit upstream facts; the service validates their declarations, encoding,
finite values, sequence continuity and model compatibility. It cannot verify the
integrity of raw CSI that it never receives. A compromised or incorrectly
implemented trusted producer is outside what these checks can establish.

Capture times must already be mapped to the same Linux `CLOCK_BOOTTIME` instance,
with declared conservative uncertainty. The decoder verifies an opaque token bound to the local boot and time namespace;
it never substitutes arrival time for capture time. Raw boot/namespace identity
is not included in observations or results. Consumers must independently verify the opaque local clock-domain
token and evaluate the original deadline at every use. Suspend contributes to
age. The clock protocol is local; no cross-host delivery is supported.

## State and resources

One serialized coordinator owns current state, bounded assembly, one pending
candidate and at most one historical categorical result. The Linux runner owns
one worker and one completion handoff. Only the coordinator may admit a result;
job, window, runtime and generation identities prevent obsolete or duplicate
completions from publishing. Pending windows are requalified before dispatch.

A new bad candidate cannot extend or by itself invalidate an independently
eligible prior result. Known relevant source/domain invalidation and expiry
withdraw the class; accepted abstention also yields unknown. History retains
its original capture window and deadline and is never substituted for current
state. Bounded candidate diagnostics share the same JSON result record.

Configuration and model selection are startup-only. Restart waits for the old
writer to exit, establishes fresh unknown state and does not restore history.
One advisory output lock excludes overlapping writers. Same-directory atomic
replacement prevents partial JSON reads. A failed or stalled publisher does
not renew evidence; consumers still enforce expiry.

## Platform boundary

Linux is the implemented host route. Its runner owns nonblocking local input,
the suspend-aware clock, worker scheduling, file descriptors and atomic output.
The portable core imports no JSON, Linux, OpenSSL or thread types. There is no
plugin framework, management/health endpoint, context input, network listener,
radio control or default persistent cache/log. Runtime files are operator-owned
and separate from the installation tree.

OpenWrt and prplOS remain future packaging, ABI, permission and producer-interface
qualification targets. QEMU Linux validation does not establish support for a
particular AP, SDK, driver or firmware.
