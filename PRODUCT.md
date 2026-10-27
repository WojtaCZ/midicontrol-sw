# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users
- **Organist** (primary): opens the status page about once a month, on a PC, phone or tablet on the church LAN. Sets the organ stops per song and checks that the system is healthy. Not technical; reads Czech.
- **Developer / maintainer**: bench work, firmware debugging, fault analysis. Uses the advanced views (English).

## Product Purpose
MIDIControl plays prepared MIDI songs on a church organ (Johannus Opus 910), driven by a handheld remote over BLE through a USB base unit. The PC service plays the files, switches the organ power, and serves an embedded status page. Success: the organist can see at a glance that everything works and set which stops each song uses, without meeting debug data.

## Operating Context
- The page is served by the player process itself (`http://<pc>:50118/`), LAN only, no internet: everything inline, no external requests.
- Three parts: the PC app ("Software"), the Base (USB, organ power relay, church display), the Controller/remote (BLE, battery).
- Polled JSON: `/api/status` (1 s), `/api/diag`, `/api/stops`, `PUT /api/stops/<code>`.

## Capabilities and Constraints
- Landing page in **Czech**: health of the three parts, now playing, organ power, remote battery, plus the per-song stops editor. Problems surface as one plain-language notice that leads to Advanced.
- Advanced section in **English**: the existing Software / Base / Controller / Diagnostics detail, unchanged in content.
- Single self-contained HTML string in `src/web_page.cpp`; no `innerHTML`, no `http(s)://` in the page (tests enforce this).
- Must work with light and dark OS themes, desktop and phone widths.

## Brand Commitments
- No logo or brand assets. User reference: UniFi network console (simple, sleek, modern).

## Product Principles
1. The organist never has to read debug data to know whether things work.
2. Stops editing is the one task on the landing page; it must be safe (unsaved-changes guard, "not set" vs "all off" stays explicit).
3. Advanced keeps every existing diagnostic; reorganise, never drop.
