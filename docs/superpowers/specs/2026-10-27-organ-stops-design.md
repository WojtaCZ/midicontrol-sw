# Organ stops per song: design

Date: 2026-10-27 · Repo: `midicontrolpy-sw` (C++ PC player) · Organ: Johannus Opus 910

## Intent

Each song needs a particular set of organ stops (rejstříky). The organist decides once which stops each song gets. From then on, MIDIControl sets them on the organ by itself when the song starts, so nobody has to touch the stop tabs.

The organist makes the choice on a phone or PC, through the MIDIControl web page. The small screens on the base and the remote are not used for this, and neither is MIDI.

## Goals

1. A **Stops** page in the existing web UI. It lists every song in the play folder, shows the 42 stops for the selected song, and saves that song's set.
2. **At song start the player sets the organ's stops**: it sends "all stops off", then switches on the song's stops, then plays the song.
3. A song's stops live in **one plain text file** in the play folder, written by the page and readable or fixable by hand. The `.mid` files are never modified.

## Non-goals

- Setting stops from the base or the remote (their menus and OLED screens).
- Reading the stop state back from the organ. It is not possible.
- Changing stops in the middle of a song, or per verse.
- Login or permissions. The page stays open to the local network, like the status page (the user agreed).
- Editing stop names or group names from the web page. They come from the config file (below).

## Anti-goals (a failure even if it "works")

- A song file gets rewritten, or a save from the page can corrupt or truncate the stops file (for example two phones saving at once, or a power loss mid-write).
- Playback stutters or is delayed by the web page (same rule as the status page: the web thread never takes CommHandler or Player locks).
- A song with no saved stops gets its stops cleared. "Not set" must leave the organ as it is (see below).
- Stops "jump" or look strange on the page. The user rejected a raised "on" tab in the mockups.

## Facts about the organ

- The Johannus Opus 910 has **42 stops**, in four physical groups on the console. The page uses the **English names** from the manual (`docs/Opus 810 tm 1410 usermanual.pdf`, "Own Registrations Opus 910 / 1010") and from the schematic stop list (`docs/Opus 910 schematics.pdf`, page 8, "STOPLIST"). The page numbers them 1–42 in the user's console order. The schematic's own number for each stop is `STnn`:

  | Group (label) | No. | Stop | ST |
  |---|---|---|---|
  | ACCESSORIES | 1 | Swell to Great | 60 |
  | | 2 | Great to Pedal | 61 |
  | | 3 | Swell to Pedal | 62 |
  | | 4 | Tremulant Great | 63 |
  | | 5 | Tremulant Swell | 64 |
  | | 6 | Chorus | 65 |
  | | 7 | Manual Bass | 66 |
  | PEDAL | 8 | Double Bass 16' | 01 |
  | | 9 | Subbass 16' | 02 |
  | | 10 | Octave 8' | 03 |
  | | 11 | Gedackt 8' | 04 |
  | | 12 | Bass Flute 4' | 05 |
  | | 13 | Mixture III | 06 |
  | | 14 | Contra Trumpet 16' | 07 |
  | | 15 | Trumpet 8' | 10 |
  | | 16 | MIDI to Pedal | 11 |
  | GREAT | 17 | Bourdon 16' | 14 |
  | | 18 | Open Diapason 8' | 15 |
  | | 19 | Rohrflute 8' | 16 |
  | | 20 | Gamba 8' | 17 |
  | | 21 | Octave 4' | 20 |
  | | 22 | Open Flute 4' | 21 |
  | | 23 | Twelfth 2 2/3' | 22 |
  | | 24 | Superoctave 2' | 23 |
  | | 25 | Conical Flute 2' | 24 |
  | | 26 | Cornet IV | 25 |
  | | 27 | Mixture IV-VI | 26 |
  | | 28 | Trumpet 8' | 27 |
  | | 29 | MIDI to Great | 30 |
  | SWELL | 30 | Open Diapason 8' | 33 |
  | | 31 | Stopped Flute 8' | 34 |
  | | 32 | Viola 8' | 35 |
  | | 33 | Celeste 8' | 36 |
  | | 34 | Octave 4' | 37 |
  | | 35 | Flute 4' | 40 |
  | | 36 | Flute Twelfth 2 2/3' | 41 |
  | | 37 | Waldflute 2' | 42 |
  | | 38 | Tierce 1 3/5' | 43 |
  | | 39 | Rauschpfeife II-IV | 44 |
  | | 40 | Cromorne 8' | 45 |
  | | 41 | Oboe 8' | 46 |
  | | 42 | MIDI to Swell | 47 |

- **The ST numbers are octal.** Their digits never include 8 or 9 (01–07, 10, 11, 14–17, 20–27, 30 …). Their decimal values are therefore not contiguous: Pedal 1–9, Great 12–24, Swell 27–39, Accessories 48–54. The recording fits this: its programs 2–5, 13, 14, 16–18, 20, 27, 28, 31–34 fall inside these runs (Pedal, Great, Swell, left to right).
- **Stops are toggled by a Program Change on MIDI channel 12.** The recording `songs/rejstriky.mid` contains only Program Changes on channel 12, and https://www.zerkelhome.com/organ/ confirms this for Johannus organs. A toggle has no on/off meaning, and the state cannot be read back.
- **All stops off:** SysEx `F0 00 4A 4F 48 41 53 7F F7`. The same page says to wait at least 10 ticks after it before switching on the first stop.
- **Program number (assumption): program = (ST read as octal) + `ProgramOffset`, with default offset −1.** So Double Bass ST01 → program 0, and Swell to Great ST60 → program 47. Johannus counts programs from 0 (zerkelhome), which is the reason for −1. Offset 0 would also fit the recording. This is **not yet verified at the organ** (open item 1), so the offset is configurable and every stop can be overridden.
- **The songs' Program Change 0 on channel 1** comes from whoever recorded them on a different organ. The user doesn't know what it does on the Opus 910.

## The page (agreed with the user, mockup `2026-10-27-organ-stops-mockup.html` next to this file)

- **Top: the stops panel for the selected song.** A heading reads "Song 401 · 16 of 42 on". Below it is a dark panel holding the four groups side by side, with a wide horizontal gap between groups. Each group has a **gold label with black text** above it, as wide as the group.
- **A stop tab** is an upright rectangle with a 1:3 aspect (short side horizontal). The stop number is big, with the name in small text under it. **A click anywhere on the tab toggles it.** "On" is shown by colour only (lit, warm background with a gold outline). The tab does not move. "Off" is greyer and dimmer.
- On a narrow (phone) screen the groups wrap onto separate rows, the tabs shrink and the names are hidden, so only the numbers show. The numbers are what the organist goes by; the names are secondary.
- Under the panel are **Save** and **All off** buttons. All off only clears the tabs on the page; nothing is sent to the organ.
- **"Not set" is visible and reversible** *(my call, after the builder check)*: a song without an entry shows "not set" in the heading and its tabs are drawn dimmed. **Save is enabled only after a tab was changed**, so just opening a song and pressing Save cannot turn "not set" into "all off". A small **"Not set"** button removes the song's entry (PUT with `null`), after a confirmation.
- **Below: the song list**, a table with Song, Stops on (count) and Stops (the numbers, or "not set"). Clicking a row selects that song. Clicking a column heading sorts by it (click again to reverse).
- The user will tweak the look later. Build it to match the mockup, not polished beyond it.

## Decisions

- **Stored as a separate file, not as MIDI in the song.** Because the commands toggle, a song needs a *target set*, not a list of presses. The `.mid` files stay untouched, so a re-recorded song keeps its stops, and the page never writes a file that might be playing. (User agreed.)
- **Part 1 (page + file) and part 2 (send at song start) both ship in this version.** The all-off SysEx makes part 2 simple, so the state tracking or power-cycling discussed in `TODO.md` isn't needed. (User agreed.)
- **"Not set" is different from "all off".**
  - A song with **no entry** in the file is not touched: no SysEx, no Program Changes.
  - A song **saved with zero stops** gets "all off" and nothing else.

## How it works

### The stops file

- **Name and place:** `stops.conf` in the PlayFolder.
- **Format:** the same INI style as `config.conf`, with one section `[SONGS]` and one line per song: `<CODE>=<numbers separated by spaces>`, ascending, each in 1–42. An empty value means "saved with zero stops". The keys are song codes, upper case as `songs` sanitises them.
- **Writing:** only the web page writes it. Each save writes the whole file to a temporary file in the same folder, `fsync`s it and `rename`s it over the old one. Saves are serialised inside the process.
- **Updating:** a save changes one song's line. It keeps every other line, including lines for songs that no longer exist in the folder.
- **Reading:** the player reads the file at each song start. A missing file means every song is "not set".
- **Bad lines:** a malformed line, or a number out of range, is skipped and logged as a warning. It never stops a song from playing.

### Organ layout in `config.conf`

There is a new section `[ORGAN]`. Every key is optional, and the built-in defaults are the Opus 910 values above:

| Key | Default | Meaning |
|---|---|---|
| `StopsEnabled` | `true` | `false` = never send stops (the page still works) |
| `Channel` | `12` | MIDI channel (1–16) for the stop Program Changes |
| `ProgramOffset` | `-1` | program = octal value of the stop's ST number + offset |
| `ST1` … `ST42` | the ST column above | the stop's schematic number (octal digits) |
| `Program1` … `Program42` | (none) | per-stop program number (decimal, 0–127), overrides the ST + offset rule for that stop |
| `DropSongProgramChanges` | `true` | while playing a song **that has stops set**, drop every Program Change in the song file (they come from another organ) *(my call: harmless if they do nothing, protects the stops if they do)* |
| `AllOffSysex` | `F0 00 4A 4F 48 41 53 7F F7` | hex bytes |
| `AllOffDelayMs` | `100` | wait after the all-off SysEx |
| `StopDelayMs` | `20` | wait between Program Changes and after the last one |
| `Groups` | `ACCESSORIES:1-7, PEDAL:8-16, GREAT:17-29, SWELL:30-42` | group labels and ranges (the total stop count follows from the ranges) |
| `Name1` … `Name42` | the names in the table above | stop names |

Document the section in `config.conf` (the example) and in `docs/status-web-and-service.md`.

### At song start (`CommHandler::doPlay`)

1. Run the existing order unchanged up to the warm-up: power on, warm-up wait, then supersede/stop checks.
2. If the song has an entry in `stops.conf` and `StopsEnabled` is true:
   - Send the all-off SysEx, then wait `AllOffDelayMs`.
   - Send one Program Change per stop on `Channel`, in ascending order, with `StopDelayMs` between them and after the last one.
   - Send everything on the **same MIDI output port** as the song.
3. Start the song.

Further rules:

- **Stop / supersede:** a stop or a new song during this sequence ends it, the same as during the warm-up (`waitInterruptible`).
- **Logging:** log one INFO line, e.g. `Stops for 401: 3 4 5 6 14`, or `Stops for 401: not set`. Set a Software status field `stops` with the same text.
- **Add-verse and replays** do not resend stops. Neither does anything that happens after the song starts.

### Web API

The `http` module gains a body (Content-Length, small size limit) and two non-GET routes:

- **`GET /api/stops`** returns `{"schema":1,"groups":[{"label":"PEDAAL","first":8,"last":16},…],"names":{"1":"…",…},"songs":[{"code":"401","stops":[3,4,5]},{"code":"403","stops":null},…]}`.
  - `songs` holds the songs in the PlayFolder, sorted, using the same list the status page already scans.
  - `null` = not set.
- **`PUT /api/stops/<CODE>`** with body `{"stops":[…]}` saves the song's set.
  - The body `{"stops":null}` removes the song's entry.
  - Unknown song code, numbers out of range or a bad body → 400.
  - Success returns the song's saved entry.
- Requests must have `Content-Type: application/json`. A plain cross-site form then can't post, since that would need a CORS preflight, which is not answered.

### Web page

- The Stops page is a new **"Stops" tab** in the existing status page (`web_page.cpp`): same top bar, inline CSS/JS, no external requests, works in light and dark.
- The stops panel keeps its own dark wooden colours in both themes.
- **Save** sends the PUT and shows "Saved ✓", or the error.
- **Unsaved changes:** if they exist when another song is picked, ask before discarding them.
- The page's text is English (project rule), and so are the stop and group names (from the English manual, as the user asked).

## Testing

Unit tests, with no time and no ports:

- Stops file parse and serialise, including bad lines, empty values and keeping unknown songs.
- Atomic save through a temp dir.
- `[ORGAN]` config keys and defaults.
- The MIDI bytes built for a song's set: SysEx, PCs, channel, octal ST + offset, `ProgramN` overrides, not-set vs empty.
- The song's own Program Changes are dropped only when the song has stops set and `DropSongProgramChanges` is true.
- HTTP body parsing and the two routes (incl. 400/405/415 cases).
- The page contains the Stops tab and stays self-contained.

The sequencing in `doPlay` should be testable with the injected clock and outputs, as `PlaybackEngine` is. Bench check: stops arrive at the organ before the first note.

## Open items (need someone at the organ)

1. **Verify the program numbers.** In a MIDI monitor on the organ's MIDI OUT, press Double Bass 16' (ST01). Program 0 means offset −1 (the default); program 1 means set `ProgramOffset=0`. Then press Swell to Great (ST60): 47 (or 48 with offset 0) confirms the octal rule for the accessories. If a stop doesn't fit, set its `ProgramN`.
2. **Verify that the all-off SysEx works on the Opus 910**, and that 100 ms after it is enough.
3. `songs/rejstriky.mid` is a recording, not a song. Its name ("REJSTRIKY", 9 characters) is not a valid song code, so the song check will flag it. Move it out of the play folder (e.g. to `docs/`) once it's no longer needed.

## Left to the builder

- Module and class names.
- Where the sequence's MIDI sending lives: in `Player`, before the engine starts, or as a small sender sharing the output port.
- How the tests inject time.
- How the page code is organised.
- The exact styling, within the mockup.
