# Flow runtime model

`libflow.c` separates a reusable opened flow from each execution created from
it. This page describes that lifecycle and the branch behavior that applications
must account for.

## Opened flow

`kc_flow_open()` opens an existing `.flow` file and keeps a reusable flow
template.

`kc_flow_set()` and `kc_flow_unset()` append temporary overrides to that opened
flow. The overrides affect runs started afterward.

Closing the opened flow does not invalidate runs that already started.

## Run snapshots

Each `kc_flow_run()` snapshots the state needed by that execution before the
call returns. The snapshot includes the flow source, temporary overrides,
optional entry selection, and input.

Later modifications to the opened flow do not change an active run.

Multiple runs created from the same opened flow are independent. Each owns its
branch traversal, stop state, and terminal result.

## Branch semantics

A branch passes each step's output to the next step. Fan-out creates independent
branches. Branches do not merge back into one execution path.

Successful terminal branch outputs form the successful run result.

Child flows participate in the same branch-oriented execution model.

## Completion

A successfully started run delivers one terminal completion callback. The
callback receives the final status, successful output when present, and
contextual error text when present.

Release successful output with `kc_flow_free()`.

After terminal completion, release the run with `kc_flow_run_close()`.

## Cooperative stopping

`kc_flow_run_stop()` requests a cooperative stop for one run. It does not kill
the command that is currently executing.

The active step finishes normally. Before another step begins, the run observes
the stop request and completes with `KC_FLOW_ESTOP`.

Closing an active run requests the same cooperative stop and waits for the run
to finish before releasing it.

## Visualization

`libflow.c` is an execution engine. For visual graph representations of `.flow`
documents, see [fldot](https://github.com/kaisarcode/fldot).

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libflow.h) for the complete C interface.
