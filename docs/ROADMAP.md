# HIDway roadmap

Source-based status, reviewed 2026-10-28. [ARCHITECTURE.md](ARCHITECTURE.md)
describes the implementation; [VALIDATION.md](VALIDATION.md) separates automated
checks, hardware observations and remaining acceptance work.

## Implemented foundation

Windows ARM capture and HUD; UDP state and optional authenticated encryption;
Linux relay, UART reconnect and timeout release; Pico HID and watchdog;
target USB-state reporting, WOL, managed installation and update rollback.

The input preview has been exercised with a managed relay and Pico 2 USB HID.
Actual input acceptance remains separate from installation and automated tests.

## Implemented input improvements

Per-source ownership and overlap tests; explicit Another-PC/This-PC routing;
scan-less virtual fallback and Unicode guard; stable DOWN identity across
layout changes; capture generations, queue failures and age cancellation;
fresh ARM sessions and ordered STATUS handling; waitable-clock sender;
bounded virtual clicks and optional hold-repeat; joint keyboard/mouse journals
through client, relay and Pico; receiver fault reporting; atomic configuration
saves and single-instance ownership. See [INPUT_SOURCES.md](INPUT_SOURCES.md).

## Current: complete actual-driver acceptance

Physical, Touchpad and Virtual / remote switches are implemented.
Validate the actual laptop touchpad and AnyDesk paths, remote Laptop → Pi →
Pico → gaming PC, and same-machine virtual → Pico → HID. Record driver
versions, foreground behaviour, duplicate suppression and target release
results. [INPUT_TESTING.md](INPUT_TESTING.md) lists acceptance cases.
Do not mark this complete from helper tests alone.

## Reliability and release quality

- Measure real packet timing, click pacing and combined modifier/click ordering
  under USB, UART and stream load. Automated FIFO tests do not prove arrival
  of every UDP edge on the target.
- Evaluate negotiated edge acknowledgment/retry if packet-loss measurements
  justify it; current protocol remains best effort.
- Move configuration to a stable per-user path in a future compatibility
  change; current launchers set cwd and saves are atomic.
- Require deliberate safe bind/peer/auth settings for serial-connected relays.
  Review update authenticity and deployment gates.
- Run failure injection and an extended hardware soak; refresh screenshots,
  setup instructions and packaging after behaviour is stable.
- Split Windows capture/state/UI into testable modules while preserving
  transport and firmware interfaces.

## Later capabilities

NKRO USB output (currently 6KRO), end-to-end keyboard LED feedback, hardware
arm controls, remote firmware maintenance, fuller setup automation and
additional desktop clients. Horizontal scrolling already exists. A native
phone client is separate from remote-control input delivered to Windows.
