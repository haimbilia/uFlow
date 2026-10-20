# GamePad HOME from ordinary vWii — investigation

Requested behavior: pressing **Wii U GamePad HOME**, while a Wii or GameCube
game launched by uFlow is running, exits that game and returns to uFlow. This
must not require a Wii Remote, the game's HOME menu, or GamePad gameplay support.

Status: experimental investigation, not an implemented exit feature. Production
uFlow, WiiFlow, IOS/cIOS and the native installer have not been changed by this
experiment. The probe is a separate foreground diagnostic, not a background
listener. Its timed return must not be reported as a successful HOME exit.

## Evidence from local firmware and source

`tools/inspect-gamepad-transport.py` checks plaintext ELF32 segments in local
firmware and reports GamePad/I2C diagnostic strings and aligned register
literals. No firmware is decrypted, patched or redistributed. It rejects
malformed segment bounds and keeps the input files unchanged.

The local HAI r569 `artifacts/hai-inspect/fw.img` has an enlarged OHCI1 segment
at `0x138b0000` (42,504 file bytes). Inspected IOS58 v6432, IOS80 v7200 and
IOS249 v65535 do not contain the same diagnostic strings or I2C3 register
literals. This is evidence of a different implementation, not proof that
ordinary vWii cannot access the hardware.

HAI r569 disassembly identifies:

| Routine | Address | Observation |
|---|---|---|
| Transport setup | `0x138b2568` | Initializes two buses: bus 0 at 400 kHz and bus 1 at 40 kHz; creates a thread and a 5,000-us timer. |
| I2C master setup | `0x138b5b7c` | Bus 1 uses the ARM register alias `0x0d000570`. |
| Input packet polling | `0x138b5ce0` | Writes register selector `0xf0` to device `0x70`, then reads 11 bytes. |
| Packet validation | within poll routine | CRC over the first nine bytes, initial `0xffff`, reflected polynomial `0x8408`, little-endian wire CRC in bytes 9–10. Rejects repeated sequence values after a short tolerance. |
| Shared input buffer | `0x138bb004` / PPC `0x938bb004` | Nine validated payload bytes; big-endian button word in bytes 2–3, HOME mask `0x0002`. |

The independent packet decoder and event tests are in
`wii/gamepad-probe/packet.hpp` and `tests/gamepad_probe_test.cpp`. They check
single-bit packet corruption, malformed lengths, initial held HOME, repeated
frames, counter wrap and shutdown isolation. They do not prove that ordinary
vWii supplies packets.

Nintendont's pinned `loader/source/ppc/PADReadGC/source/PADReadGC.c` already
branches to `DoExit` on GamePad HOME when its Wii VC input buffer is available.
The normal uFlow route uses `CMPTLaunchTitle` and the DWFA forwarder; it does
not establish that input buffer. An ordinary app thread is replaced at game
launch. A persistent listener would need integration into the running IOS/cIOS
or Nintendont runtime, and must survive/handle IOS reloads.

## Console probes

Build with `tools/build-gamepad-probe.ps1`. It produces a Wii DOL plus a
Boot2vWii-based Wii U launcher for the **already installed** Open Homebrew
Channel (`00010001-4f484243`). No channel or IOS installation is performed.

1. Place the resulting WUHB at `sd:/wiiu/apps/uFlow-GamePad-Probe.wuhb` on a
   verified card, checking complete readback. Launch it from Wii U Menu.
2. Send `wii/gamepad-probe/boot.dol` using `wiiload` to Homebrew Channel's IP.
   Network sending requires no Wii Remote. HBC must have its network active.
3. Once the probe appears, press/release GamePad HOME several times during
   the 30-second window. HOME only logs an event; it never initiates an exit.
4. The probe attempts an automatic return using built-in HCVA. The console's
   RESET button ends sampling early. Inspect
   `sd:/wiiu/apps/uFlow/gamepad-probe.log` after returning to Wii U mode.

The default passive probe logs IOS version, Wii U identification and AHBPROT access. With
hardware access, it checks libwiidrc's known HAI profile literals and observes
I2C3 control/interrupt status. It does **not** read a hardware FIFO, write a
hardware register, reload IOS, install firmware, or modify game/save/settings
files. A missing profile is logged as `UNAVAILABLE`, not as a successful scan
with zero presses. Unknown buffers are never followed.

### First hardware result (2026-09-26)

The passive probe ran under ordinary vWii IOS58 v6432, received through
Homebrew Channel at `192.168.1.147`, and automatically returned through HCVA.
AHBPROT hardware access was available. Both HAI profile locations held
`0x9abcdef0`, so no shared GamePad input buffer was exposed. The I2C3
registers read `control=0x611e0003`, `interrupt-enable=0`, `status=1`.
The log is `artifacts/gamepad-probe/console-probe.log`. This validates the
diagnostic and return channel, not HOME detection or a background listener.

### Opt-in direct-input query (v2)

Sending the DOL with exactly one explicit argument, `--query`, enables an experimental
read of device `0x70`, register selector `0xf0`. It is restricted to ordinary
IOS58 v6432 with AHBPROT access, no recognized HAI buffer, and the exact bus
configuration observed above. The probe never changes control/clock/interrupt
configuration. It does write the bus command FIFO and acknowledges completion
status to issue the input read. Its initial FIFO count check may consume one
old byte; a nonempty FIFO stops the test rather than being drained. This
foreground-only test assumes the inspected IOS58 has no competing I2C3 driver;
it must not be moved into a game without solving ownership first.

Each transfer waits at most 200 polling intervals (500 us each, plus scheduler
overhead). Bus errors, timeout, unexpected FIFO counts or a bad packet checksum
stop further queries immediately. Only CRC-valid packets reach the HOME-edge
detector. HOME still only writes a log; the automatic 30-second return remains
separate. Host mocks exercise the production transport, including the exact
device/register command sequence, blocked controllers, pending FIFO data,
timeouts, errors, packet length and CRC.

The first v2 network launch remained passive because `wiiload` supplies the
explicit argument string starting at `argv[0]`, without the executable name.
The parser now accepts both that form and conventional `argv[1]`, with host
regression coverage. That passive run is not counted as a bus-query test.

The corrected v2 hardware query completed both transactions, with the expected
11-byte FIFO length, but returned `ff ff ff ff ff ff ff ff ff ff ff`. CRC
validation rejected it immediately. No valid input packet was observed, and
the probe returned through HCVA. The user did not press buttons during that
run; the invalid packet was captured before a button press could matter.

### HAI mode-bit experiment (v3)

`--query-hai-mode` adds one narrowly scoped experiment: clear controller bit 1,
matching the mode argument in HAI's `0x138b5b7c` initialization, while preserving
the observed divider/timing. The original `0x611e0003` control value is restored
immediately on a query error and at test completion. Host tests cover restoration
on timeout, explicit completion, and refusing an unexpected controller state.

Hardware readback confirmed `0x611e0001` during the experiment and restoration
to `0x611e0003` afterward. The first query again returned eleven `ff` bytes and
was rejected by CRC validation. The user pressed/released HOME while this test
was displayed, but the query had already stopped at the first invalid response;
the probe did **not** continuously sample those presses. This rules out that
one mode-bit change as sufficient to obtain an initial valid packet. It does
not prove that GamePad input is impossible in ordinary vWii.

All runs are preserved in the ignored `artifacts/gamepad-probe/console-probe.log`.
The restart/return in these runs was the fixed timer, never a HOME event.

It is intentionally separate from the release installer: this is a research
tool, not a shipped runtime feature. Once a listener is validated, the matched
runtime dependencies must be included in `tools/sync-native-payload.py` and
the native installer, with fresh-install/component-selection tests.

## Next decision

The passive bus observation and bounded transaction completed, but valid input
is still missing. Investigate pre-transition GamePad/DRH setup and compare the
complete HAI initialization sequence before further console experiments. The
local `nn_cmpt` exports `CMPTAcctGetDrcCtrlEnabled` and
`CMPTAcctSetDrcCtrlEnabled`; these access persistent `wii_acct.ctrl.drc`
settings. The separate Cafe diagnostic now temporarily enables the flag for
`CMPTExPrepareLaunch(2, &OHBC_title, 8)`, restores and reads back the original
value, then calls `CMPTExLaunch()`. This is built and host-tested, but has not
yet been tested on the console. The launcher was installed and fully read back
over FTP at `192.168.1.147` after verifying the existing production app SHA256
`8a4c3078814763e67af5a6b4082598270d87496ef67da80d5dcaabd4050417bd`.
Experimental launcher SHA256:
`1447738e694ba9a158f0095e71266cc1a80f6f7b873a5e1ab88038c4eae2d808`.
The installer and production runtime remain unchanged. Hardware launch and
setting-restoration results are recorded below.

### Prepared-launch hardware result

The Cafe launcher reached OHBC successfully: screen check, work-buffer setup,
preparation and launch each returned zero. The original DRC setting was already
`1`, so this run did not change the setting and did not exercise restoration or
the recovery marker. No recovery marker remained. The local log is
`artifacts/gamepad-probe/console-launch-probe.log`.

The subsequent `--query-hai-mode` probe still ran on IOS58 v6432, with no known
HAI shared buffer. Its first query returned eleven `ff` bytes and failed CRC;
it immediately stopped querying and restored control to `0x611e0003`. The timed
HCVA return completed. Thus an already-enabled DRC account flag plus the Ex
ordinary-title launch is insufficient for this input query. No HOME event was
detected, but button presses after the first failed query were not sampled.
This is not evidence that the restoration path works on hardware, nor proof
that all other GamePad activation paths are impossible.

### Remaining timing difference (static analysis only)

HAI initialization at `0x138b5b7c` does more than clear mode bit 1. At the
40,000 Hz setting used for the input bus, it calculates the divider as
`floor(clockMHz * 1000000 / 40000) / 2 - 1`, with integer truncation, and
the timing byte as `floor(6250000 / 40000) = 156`. The programmed control is
`(divider << 16) | (156 << 8) | 1`. The observed ordinary-vWii control
`0x611e0003` has a zero timing byte; clearing bit 1 alone did not reproduce
HAI's setup. This is a concrete difference, not proof it explains the failure.

The clock helper at `0x138b7318` reaches IOS syscall `0x55`. In the inspected
HAI kernel its table entry is `0xffff5795`, which calls `0xffff5dec`; that
routine derives the integer MHz value from hardware revision and PLL registers
(`0x0d800214`, `0x0d800190`, `0x0d8001b0`, `0x0d8001b4`). Do not assume a
hard-coded clock frequency for a future timing experiment.

A fresh inventory of all 19 IOS58 content files found no aligned full-address
literal for the queried bus registers; the HAI OHCI1 module contains them.
This is only a literal scan and cannot exclude register addresses formed by
arithmetic or another bus owner. The reverse-engineering notes on
[Latte I2C](https://wiiubrew.org/wiki/Hardware/Latte_I2C) also identify the
controller as shared with SMC. Ownership and timing validation remain required
before expanding the experiment or embedding it in a game. No new clock,
global bus-enable, SMC, or firmware writes were performed in this step.

The local library validates a 64-byte-aligned work buffer of at least 12 MiB.
Preparation invokes MCP's prepare operation; launch uses the separate launch
operation and rereads the screen selection. The diagnostic therefore leaves
the screen selection unchanged. No evidence yet establishes that the DRC flag
is captured during preparation for ordinary vWii, or that a later MCP stage
does not reread it. A negative result cannot rule out enabling GamePad input by
another pre-transition method.

Before changing a disabled flag, the diagnostic exclusively creates and flushes
`gamepad-probe-drc.restore` on the existing uFlow SD directory. Restoration is
attempted even when enabling/preparation fails. Failed restoration blocks the
mode switch and leaves the marker. Reopening the diagnostic recovers a valid
marker before another test; an invalid marker blocks all setting changes.
Already-enabled settings are not rewritten. `gamepad-launch-probe.log` records
each stage. A power interruption between setting changes still requires reopening
the diagnostic to recover; this is not an automatic recovery service.

`tests/gamepad_prepare_test.cpp` covers original-on/off, failed reads, failed
journaling, failed enabling, failed preparation, failed restoration and failed
marker cleanup. These are host control-flow tests, not evidence that the Wii U
APIs or SD flush behave correctly on hardware. The Cafe Makefile is derived
from pinned Boot2vWii's public-domain Makefile; its license is retained beside it.

A production input poll requires exclusive ownership and must not steal
another driver's FIFO or reset the GamePad transport during gameplay. If valid
input becomes available, prototype a listener in the actual
game runtime and test clean exit/save completion before enabling HOME-to-uFlow.
The existing one-shot uFlow request handles the final Wii U Menu-to-uFlow step.

References: [libwiidrc](https://github.com/FIX94/libwiidrc),
[Nintendont](https://github.com/FIX94/Nintendont),
[Boot2vWii](https://github.com/WiiDatabase/Boot2vWii).
