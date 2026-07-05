# Codex Development Harnesses

This document describes the local harnesses and verification patterns that help
agent-assisted development stay code-driven, reproducible, and easy to review.
Use these tools before relying on manual runtime checks or browser/UI testing.

## Goals

- Prefer code-path verification over assumptions.
- Keep verification scoped to the changed behavior first, then broaden when the
  change touches shared contracts.
- Leave enough command output or logs for a human maintainer to confirm runtime
  behavior when an issue cannot be proven by static code inspection.
- Avoid temporary workarounds. A harness should make the intended behavior
  observable, not hide a failing path.

## Baseline Checks

Run these checks after merge conflict resolution or any non-trivial code change:

```sh
git diff --check
git diff --name-only --diff-filter=U
rg -n '<<<<<<<|=======|>>>>>>>' src docs .github deploy translations
```

Expected result:

- `git diff --check` exits successfully.
- `git diff --name-only --diff-filter=U` prints nothing.
- The conflict marker search prints nothing.

## Build Harness

Use an existing configured build tree when one is available. In this workspace,
`build/codex-tests` is the preferred fast verification tree.

Targeted build:

```sh
cmake --build build/codex-tests --target ClipboardChunksTests ClipboardTransferTests --parallel
```

Full local build:

```sh
cmake --build build/codex-tests --parallel
```

If `build/codex-tests` is missing or stale, configure a new local test build
instead of changing release build settings:

```sh
cmake -S . -B build/codex-tests -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=OFF
```

## Test Harness

Run the smallest test set that covers the changed logic first:

```sh
ctest --test-dir build/codex-tests/src/unittests -R 'ClipboardChunksTests|ClipboardTransferTests' --output-on-failure
```

Then run the full unit test suite when the change touches shared libraries,
protocol behavior, configuration parsing, event dispatch, platform abstractions,
or GUI-facing settings:

```sh
ctest --test-dir build/codex-tests/src/unittests --output-on-failure
```

Use test names as behavior contracts. For example:

- `ClipboardChunksTests` covers legacy `DCLP` chunk assembly behavior.
- `ClipboardTransferTests` covers transactional clipboard transfer behavior.
- `ServerConfigTests` covers config parsing, writing, aliases, and physical
  layout round-trips.
- `ServerTests` covers server-side coordination behavior.

## Protocol Harnesses

Protocol changes should have focused unit tests for both valid and invalid
message flows. Prefer in-memory stream fixtures over network integration tests
unless socket behavior is the subject of the change.

For clipboard protocol work, cover these cases:

- Valid start/data/end sequencing.
- Invalid clipboard IDs.
- Invalid size headers.
- Declared size smaller than received data.
- Declared size larger than the configured receive limit.
- Mismatched clipboard ID or sequence in the middle of a transfer.
- Stale sequence or revision rejection.
- Superseded active transfer cancellation.
- Unmarshall failure before applying clipboard state.

Recommended mapping:

| Area | Harness |
| --- | --- |
| Legacy chunk assembly | `ClipboardChunksTests` |
| Protocol 1.9 transfer queue and receive assembler | `ClipboardTransferTests` |
| Client receive path | `ServerProxy` unit coverage through clipboard fixtures |
| Server receive path | `ClientProxy1_6`, `ClientProxy1_9`, and `ServerTests` |

## Runtime Log Harness

Use runtime logs when correctness depends on OS behavior, platform clipboard
backends, input capture portals, or GUI/core process coordination.

Preferred pattern:

1. Add a precise log at the decision point.
2. Include identifiers needed to prove the path: screen name, clipboard ID,
   sequence, revision, transfer ID, size, and reason.
3. Keep the log at a level that matches operator value:
   - `LOG_DEBUG` for expected diagnostic flow.
   - `LOG_INFO` for user-visible state changes.
   - `LOG_WARN` for ignored invalid input or recoverable protocol problems.
   - `LOG_ERR` for protocol corruption or unrecoverable state.
4. Ask the human tester to run the scenario and provide the relevant log lines.
5. Remove noisy temporary logs before finishing unless they remain useful
   diagnostics.

Example clipboard log fields:

```text
clipboard transfer <transferId> from "<screen>" completed
ignored screen "<screen>" update of clipboard <id> because sequence <old> was superseded by <new>
clipboard size exceeds limit, size: <bytes>, limit: <bytes>
```

## Manual Verification Handoff

Browser and UI testing can be intentionally left to a human tester. When doing
that, provide exact runtime checks instead of a vague request.

Good handoff:

```text
Run a client/server clipboard transfer with a payload larger than the configured
clipboard limit. Confirm the receiver logs "clipboard size exceeds limit" and
the previous clipboard contents remain unchanged.
```

Avoid:

```text
Please test clipboard.
```

## Merge Harness

After merging upstream changes:

```sh
git fetch upstream
git rev-list --left-right --count HEAD...upstream/master
git merge --no-commit --no-ff upstream/master
```

When conflicts occur:

1. Resolve behavior by code path, not by choosing one side wholesale.
2. Preserve local fixes that guard real runtime failures.
3. Preserve upstream validation and API updates when they improve the same path.
4. Add or update tests for the merged behavior.
5. Run baseline checks, targeted tests, then full tests.

For staged merge results:

```sh
git status --short --branch
git diff --cached --stat
```

Do not create a merge commit unless explicitly requested.

## Harness Design Rules

- A harness should fail for the bug it is meant to prevent.
- Keep fixtures deterministic and in-process when possible.
- Prefer structured protocol messages over ad hoc byte strings, unless the test
  is specifically validating malformed bytes.
- Test invalid inputs at the lowest layer that can reject them.
- Test successful integration at the first layer that applies state.
- Include regression tests when a merge combines two different implementations.
- Avoid sleeps and timing assumptions. Use event queues, explicit flush events,
  or one-shot timers only when the production behavior requires them.
- Keep platform-dependent tests isolated behind platform-specific targets.

## Development Checklist

Before handing work back:

- `git diff --check` passes.
- No unmerged paths remain.
- No conflict markers remain.
- Targeted tests for changed logic pass.
- Full tests pass when shared behavior was touched.
- Runtime-only claims are backed by log instructions.
- New harnesses are documented or discoverable from the relevant `CMakeLists.txt`.
- No commit was created unless the user explicitly requested it.
