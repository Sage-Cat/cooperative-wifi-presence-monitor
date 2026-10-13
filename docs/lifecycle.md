# Result lifecycle and failure decisions

These are planned decision rules. They constrain how a reviewed presence model
may be used; they do not establish that such a model has been trained or
validated.

![Current-result states](diagrams/result_state.svg)

## Current, candidate, diagnostic and historical state

The current result is `present`, `absent` or `unknown`. A categorical result
requires valid evidence, eligible declared conditions, a supported exact
feature/source/model combination, a permitted model output and unexpired
capture-based age. Model abstention can yield `unknown` even with valid
evidence and eligible conditions; `decision_reasons` records that separately.

A candidate window is assessed independently of the current result. Failure of
a new candidate does not retroactively invalidate an older complete window. The
older result may remain current only while its original conditions and deadline
still hold. Candidate rejection diagnostics remain distinct from the current
result's evidence assessment. They occupy one bounded diagnostics section in
the same result record; there is no separate health endpoint or file.

Historical `last_known` retains its original class, window and expiry. It may
be shown as history after current state becomes unknown, but never used as the
current estimate. The default history is at most one separate in-memory
`last_known` result. Startup selects a static model/profile; model/profile
changes require a controlled stop/start. Restart begins unknown under a unique
runtime/session token and does not automatically restore prior history.

## Transition decisions

| Event | Current-result decision | Candidate/acquisition consequence |
|---|---|---|
| Startup, no reviewed bundle or required facts unknown | Unknown | Wait for valid configuration and observations; do not supply a default class |
| New complete compatible window, accepted current-generation model output | Replace with its estimated class after publication-time recheck | Record its capture window, fixed deadline and provenance |
| Model abstains on an otherwise eligible new window | Publish unknown with `model_abstained` | Do not fall back to an earlier categorical estimate |
| New candidate has gaps, bad records, late input or an unsupported reduced subset | Keep prior estimate only if independently eligible; otherwise unknown | Reject candidate and expose all applicable diagnostic reasons; original deadline unchanged |
| Receiver disappears after an eligible complete window | Retain only to that window's original deadline if no applicability invalidation is known | New reduced-input inference requires an independently supported subset |
| Known source-epoch/feature/domain invalidation | Withdraw immediately; unknown | Advance the affected in-process generation and rebuild compatible windows; model/profile changes use controlled stop/start |
| No replacement before age exceeds the fixed maximum | Unknown with `evidence_expired` | Keep last-known history; wait for eligible replacement |
| Controlled stop/start, or a clock discontinuity invalidating the current mapping | Unknown for the new runtime/session token or affected state | Select static model/profile at startup; reject old-token completions and incompatible windows |
| Publisher fails or stops updating | Consumer independently withdraws class when its own validity test fails | Never treat a readable old file as fresh evidence |

## Five causes of lost observational support

| Cause | Permitted current decision | Recovery |
|---|---|---|
| Gaps in a new candidate | Keep an independently valid prior estimate only to its original deadline; otherwise unknown | Reject the window; assemble new complete eligible evidence |
| Node loss | Keep prior estimate only while its exact window remains eligible and unexpired; no absent inference | Continue only with a separately supported exact subset or wait for the node |
| Incompatible processing | Reject a mismatched new candidate; retain prior only if independently eligible. A proven mismatch affecting current evidence withdraws it to unknown | Establish compatible processing and new qualified evidence; model/profile changes use controlled stop/start |
| Changed environment | Known relevant invalidation or unresolved required applicability yields unknown. Unobserved changes are a stated limitation, not detectable by claim | Require new eligible evidence and reviewed applicability; do not infer absence |
| Aging past the fixed evidence deadline | Unknown with `evidence_expired` | Obtain a new eligible window; receipt or inference time does not renew age |

A transport pause or end-of-stream after a valid window is a no-new-data event.
It does not renew or automatically erase that window. A source declaration that
proves a relevant epoch or configuration changed is an invalidation event, not
a mere transport pause. A source-only reboot or clock reset always breaks new
window assembly across that boundary; it withdraws an old estimate only when
its retained mapping or applicability is invalidated. No inference of absence
follows from these events.

## Acceptance order

1. Preserve source facts and assemble one bounded, generation-specific window.
2. Qualify integrity, required metadata, exact subset/features, declared domain
   conditions and time alignment. Unsupported/unresolved requirements prevent
   submission to the inference worker.
3. Submit an immutable job to the single running slot. The coordinator retains
   at most one latest pending window and requalifies it before dispatch; a
   relevant invalidation clears pending work. Keep timer processing responsive
   while executing the frozen pipeline/model.
4. On completion, reject an old runtime/session token or generation, an
   unexpected job/window ID, or a duplicate completion; re-evaluate
   known invalidations and capture-time freshness at publication time.
5. Publish an accepted class or model abstention atomically. A failed candidate
   may leave an independently eligible current result unchanged; it never
   changes that result's evidence clock.
6. Process timer events without depending on new records; the consumer enforces
   its own deadline too.

All applicable current decision/evidence/domain reasons remain visible in their
respective fields; candidate-only reasons remain in bounded diagnostics. One
failure does not conceal another. There is no universal gap tolerance, subset
model, environment-change detector, expiry threshold or safety criterion in
this design. Domain rules use portable values and immutable owned payload
views; the application coordinator returns plain jobs/results to the outer
runner, which owns JSON, clock, thread and publisher details.

## Time boundary

[The interface contract](interfaces.md#time-and-expiry-contract) defines `A =
max_i(now - e_i + epsilon_i)` and `valid_until = min_i(e_i - epsilon_i +
A_max)` for a common declared time basis. The existing boundary is inclusive:
`A <= A_max` is eligible; `A > A_max` is expired. The local timer withdraws at
the first representable tick beyond the deadline. Clock precision and
uncertainty must already be included in the reviewed bounds; a coarse timer
does not justify displaying a class beyond its validity. Consumers evaluate
freshness at their own use time.

This is a necessary age condition, not proof that a fresh estimate is correct.
No receipt timestamp, inference completion, restart or repeated publication may
reset capture age. Unknown or unbounded mappings permit no categorical validity
horizon on that route.

## Interrupted input and recovery

![Gap, expiry and recovery sequence](diagrams/failure_recovery.svg)

Recovery requires new eligible evidence under a supported bundle. Restoring
packet delivery alone does not repair incompatible features or a changed room.
A returning node must establish its epoch and time mapping before cooperative
use. A separately reviewed subset model may provide a replacement, but subset
selection or missing-feature zero filling is not performed automatically.
