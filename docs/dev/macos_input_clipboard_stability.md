# macOS Input and Clipboard Stability Notes

This document records the current macOS input and clipboard behavior that must
remain stable while the implementation is refactored.

## Scope

The notes cover the macOS client path for:

- Keyboard event translation from the server protocol into macOS key events.
- Input-method-aware keymap generation, especially Korean IME input.
- Transactional clipboard transfers between a client and the server.
- Local macOS pasteboard change detection.

The goal is to make the implicit cross-layer contracts explicit before moving
logic between classes.

## Relevant Layers

`OSXScreen`
: Owns macOS event taps, pasteboard polling, local clipboard application, and
  platform-level clipboard grab events.

`OSXKeyState`
: Builds macOS keymaps, tracks the active input source group, maps Deskflow
  key IDs to native key events, and changes input source groups when required.

`Client`
: Owns client-side clipboard ownership state, sent-cache state, local clipboard
  revision tracking, and high-level input/clipboard calls received from
  `ServerProxy`.

`ServerProxy`
: Converts client actions into protocol messages, receives server messages,
  translates modifier mappings, and owns client-side transactional clipboard
  transfer queues.

`Server` and `ClientProxy1_9`
: Own server-side clipboard ownership, revision broadcast, transfer assembly,
  acknowledgments, cancellations, and retry behavior for protocol 1.9+
  transactional clipboard transfers.

## Keyboard Invariants

- A non-modifier `KeyID` is protocol data, not a modifier table index. It must be
  returned unchanged by `ServerProxy::translateKey()`.
- Modifier translation values may be clamped only when used as indexes into
  modifier translation tables.
- A `KeyModifierMask` is a bitmask. It must not be clamped to the modifier ID
  range.
- Preserve unrelated modifier mask bits such as Caps Lock and Num Lock while
  remapping shift/control/alt/meta/super/altgr bits.
- Invalid modifier mapping input must fail safe by clamping the table index, not
  by corrupting the returned key ID or bitmask.

## macOS IME Keymap Invariants

- The server can send a Latin physical `KeyID` with a language code such as
  `ko`. The client must preserve the physical key intent while selecting the
  matching input source group.
- Some macOS input methods, including Korean 2-Set input mode, do not expose
  `kTISPropertyUnicodeKeyLayoutData` on the input-mode source.
- For input-method groups without native Unicode key layout data, use an
  ASCII-capable keyboard layout as the fallback key resource.
- Do not use the current Hangul layout resource as the fallback for Latin
  physical key IDs. That can cause the keymap to synthesize Option-modified
  keystrokes to match Latin characters, which breaks IME composition.
- The current input source group remains responsible for composition; the
  fallback layout is only used to map physical keys.

Expected log evidence for Korean input includes:

```text
using ASCII-capable keyboard layout 'com.apple.keylayout.ABC' for IME fallback
using fallback uchr resource for group <korean-group>
recv key down id=0x..., mask=0x..., button=0x..., lang="ko"
language ko has group id <korean-group>
```

## Clipboard Transfer Invariants

- Transactional clipboard transfer IDs are opaque identifiers. A value such as
  `2147483649` is a client-originated transfer ID with the high bit set, not a
  payload size.
- The payload size is carried in the transfer start action data and logged as
  `size=<bytes>`.
- A client clipboard payload is committed as successfully sent only after the
  matching server acknowledgment is received.
- Timeout or invalid cancellation of an outgoing client transfer must invalidate
  the client sent-cache for that clipboard ID. The next local clipboard grab must
  be allowed to resend the same payload.
- Superseded transfers are not failures. They are normal ownership/revision
  replacement and must not force a failed-send cache reset.
- Keepalive extension used during clipboard transfer must be restored after the
  final outgoing transfer becomes idle, including final failure paths.
- Server acknowledgment confirms that the server accepted the clipboard data. It
  does not prove that every target client has already applied it.

Expected log evidence for a local macOS text copy is:

```text
clipboard changed
sending clipboard <id> changed
sending clipboard <id> seqnum=<seq>
starting clipboard transfer <transfer-id> to server, size=<bytes>
finished sending clipboard transfer <transfer-id> to server; waiting for acknowledgment
clipboard transfer <transfer-id> to server was acknowledged
```

Expected log evidence for a final outgoing failure is:

```text
clipboard transfer <transfer-id> to server timed out
cancelling clipboard transfer <transfer-id> to server, reason=2
forgot clipboard <id> sent cache after failed transfer, reason=2
```

## macOS Pasteboard Detection

macOS does not provide a stable public global pasteboard-changed notification
equivalent to polling `NSPasteboard`/`Pasteboard` change state. The current
implementation polls through `OSXScreen::checkClipboards()`.

Polling is the correctness path. Event-triggered fast checks, such as checking
immediately after observing Command-C or Command-X, may be added only as a
latency optimization. Polling must remain as the fallback because context-menu
copy, application copy buttons, and programmatic clipboard writes do not
necessarily pass through a Deskflow-observable keyboard shortcut.

Private macOS notifications should not be used for product behavior.

## Refactoring Plan

1. Extract server-side modifier/key translation from `ServerProxy`.
   - Keep it protocol-only.
   - Preserve current regression coverage for non-modifier keys, modifier masks,
     and invalid mapping input.

2. Extract macOS input source and key resource selection from `OSXKeyState`.
   - Make the fallback selection explicit and testable.
   - Separate "which input source group should be active" from "which layout
     resource maps physical keys".

3. Introduce a client clipboard state object.
   - Move `m_ownClipboard`, `m_sentClipboard`, `m_timeClipboard`,
     `m_dataClipboard`, and `m_clipboardRevision` behind a narrow API.
   - Represent "sent and acknowledged" separately from "queued/sent but not
     acknowledged".

4. Introduce explicit clipboard transfer completion callbacks.
   - `ServerProxy` should report acknowledged, superseded, timed-out, and invalid
     outcomes to `Client` through named methods instead of mutating higher-level
     state indirectly.

5. Add transfer latency logging around stable milestones.
   - Local platform detection.
   - Client enqueue.
   - Transfer start/end flush.
   - Server acknowledgment.
   - Remote client apply, when visible on that side.

6. Keep platform-specific optimizations local.
   - macOS pasteboard polling interval and optional shortcut-triggered fast
     checks belong in `OSXScreen`.
   - Protocol retry and acknowledgment policy belong outside `OSXScreen`.

## Refactoring Done Criteria

- Existing keyboard and clipboard regression tests pass.
- Manual macOS verification can be performed from logs without guessing.
- Korean IME input still uses an ASCII-capable fallback key resource.
- Regular key IDs and modifier masks remain unchanged unless explicitly mapped.
- Clipboard resend works after timeout without reconnecting the client.
- Local text clipboard transfer logs show milliseconds between transfer start
  and server acknowledgment under normal network conditions.
