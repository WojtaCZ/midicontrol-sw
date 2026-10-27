# Organ Stops per Song Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A "Stops" tab on the status page where the organist picks the Johannus Opus 910 stops for each song, saved to `stops.conf` in the PlayFolder. At song start the player sends "all stops off" and then the song's stops before the first note.

**Architecture:** There are two new pure core modules:
- `organ`: the `[ORGAN]` config, the Opus 910 defaults, stop number → program, and the MIDI preamble for a song.
- `stops_file`: the `stops.conf` format, the atomic save, the `/api/stops` JSON and the per-song store.

`PlaybackEngine` gains a preamble that it plays through its `Timebase` before the song, and an option to drop Program Changes. Both are unit-testable with the existing `FakeClock`. `http` gains request bodies and a PUT route. `WebServer` owns the store. The page gets a cached Stops tab. `CommHandler::doPlay` only looks up the song's entry and fills `PlaybackOptions`.

**Tech Stack:**
- C++17, CMake.
- The built-in test harness (`tests/test_framework.hpp`).
- Inline HTML/CSS/JS in `src/web_page.cpp`.
- No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-27-organ-stops-design.md`; mockup `docs/superpowers/specs/2026-10-27-organ-stops-mockup.html`

## Global Constraints

- Project language is English: comments, identifiers, UI strings. Stop and group names are the English ones from the spec table.
- Core library (`midicontrol_core`) has no external deps. Every new core file goes into `midicontrol_core` and its test into `midicontrol_tests` in `CMakeLists.txt`.
- The web thread never takes CommHandler or Player locks.
- The page is self-contained: no `http://`, `https://`, `<script src`, `<link`, `@import`. It never uses `innerHTML` (`tests/test_web_page.cpp` enforces this).
- The `.mid` files are never written.
- No entry for a song → the organ is not touched. An entry with zero stops → all-off only.
- Defaults:
  - `Channel=12`, `ProgramOffset=-1`
  - `AllOffSysex=F0 00 4A 4F 48 41 53 7F F7`
  - `AllOffDelayMs=100`, `StopDelayMs=20`
  - `StopsEnabled=true`, `DropSongProgramChanges=true`
  - `Groups=ACCESSORIES:1-7, PEDAL:8-16, GREAT:17-29, SWELL:30-42`
- Stops file: `<PlayFolder>/stops.conf`, section `[SONGS]`, lines `<CODE>=<ascending numbers separated by spaces>`.
- Build/test: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ./build/midicontrol_tests`.

## Review Focus

1. A hand-edited `stops.conf` with lower-case codes (`406a=…`), duplicates, unsorted or out-of-range numbers, comments, or junk lines must load leniently: codes normalised with `songs::sanitizeCode`, numbers deduplicated and sorted, bad items dropped with a warning. It must never throw (Task 2 test `stops_file_parse_is_lenient`).
2. A PUT body that arrives in a later TCP read than the request head must wait for the whole body (`Incomplete`). A Content-Length beyond the limit gives 413 (Task 4 `http_parse_body_split_and_limits`).
3. A save that cannot be written (read-only folder) must return 500 with the error text and leave the old file intact. The page must show the error and keep the unsaved edits (Task 2 `stops_store_save_failure_keeps_file`; Task 6 page code).
4. Stop or a new song during the preamble: the engine returns `Stopped` before sending any song event (Task 3 `engine_preamble_stop_aborts_before_song`).
5. A stops entry for a code that no longer has a `.mid` must survive a save of another song (Task 2 `stops_file_keeps_unknown_songs`). A PUT for a code with no `.mid` in the folder → 400 (Task 5 test).

---

### Task 1: `organ` module: `[ORGAN]` config, Opus 910 defaults, programs, preamble

**Files:**
- Create: `include/midicontrol/organ.hpp`, `src/organ.cpp`, `tests/test_organ.cpp`
- Modify: `include/midicontrol/playback.hpp` (add `MidiStep` only, see Interfaces)
- Modify: `include/midicontrol/config.hpp`, `src/config.cpp` (add `Config::organ`, parse `[ORGAN]`), `tests/test_config.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces, in `playback.hpp` (namespace `mc`):
  ```cpp
  struct MidiStep { std::vector<uint8_t> bytes; double delayAfterSeconds = 0.0; };
  ```
- Produces, in `organ.hpp` (namespace `mc::organ`):
  ```cpp
  struct Stop { int number; std::string name; std::string st; std::optional<int> program; };
  struct StopGroup { std::string label; int first; int last; };
  struct OrganConfig {
      bool stopsEnabled = true;
      int channel = 12;                       // 1..16
      int programOffset = -1;
      std::vector<uint8_t> allOffSysex;       // default F0 00 4A 4F 48 41 53 7F F7
      int allOffDelayMs = 100;
      int stopDelayMs = 20;
      bool dropSongProgramChanges = true;
      std::vector<StopGroup> groups;          // ranges contiguous from 1, no overlap
      std::vector<Stop> stops;                // stops[i].number == i + 1
      int stopCount() const { return static_cast<int>(stops.size()); }
  };
  OrganConfig defaultOrgan();                                     // Opus 910 table from the spec
  OrganConfig parseOrgan(const std::map<std::string, std::string>& section);  // throws std::runtime_error("config: ...")
  int programFor(const OrganConfig& organ, int stopNumber);       // override, else octal(st) + offset
  std::vector<MidiStep> buildPreamble(const OrganConfig& organ, const std::vector<int>& stops);
  ```
- Produces, in `config.hpp`: `mc::organ::OrganConfig organ = mc::organ::defaultOrgan();` member of `Config`.

- [ ] **Step 1: Write the failing tests** in `tests/test_organ.cpp`:
  ```cpp
  TEST(organ_defaults_are_opus_910) {
      auto o = mc::organ::defaultOrgan();
      CHECK_EQ(o.stopCount(), 42);
      CHECK_EQ(o.groups.size(), 4u);
      CHECK_EQ(o.groups[0].label, std::string("ACCESSORIES")); CHECK_EQ(o.groups[0].last, 7);
      CHECK_EQ(o.groups[3].label, std::string("SWELL")); CHECK_EQ(o.groups[3].first, 30);
      CHECK_EQ(o.stops[0].name, std::string("Swell to Great")); CHECK_EQ(o.stops[0].st, std::string("60"));
      CHECK_EQ(o.stops[7].name, std::string("Double Bass 16'")); CHECK_EQ(o.stops[7].st, std::string("01"));
      CHECK_EQ(o.stops[41].name, std::string("MIDI to Swell")); CHECK_EQ(o.stops[41].st, std::string("47"));
      CHECK(o.allOffSysex == (std::vector<uint8_t>{0xF0,0x00,0x4A,0x4F,0x48,0x41,0x53,0x7F,0xF7}));
  }
  TEST(organ_program_is_octal_st_plus_offset) {
      auto o = mc::organ::defaultOrgan();
      CHECK_EQ(mc::organ::programFor(o, 8), 0);    // ST01
      CHECK_EQ(mc::organ::programFor(o, 15), 7);   // ST10 = 8 decimal
      CHECK_EQ(mc::organ::programFor(o, 17), 11);  // ST14 = 12
      CHECK_EQ(mc::organ::programFor(o, 1), 47);   // ST60 = 48
      CHECK_EQ(mc::organ::programFor(o, 42), 38);  // ST47 = 39
  }
  TEST(organ_parse_overrides) {
      auto o = mc::organ::parseOrgan({{"Channel","3"},{"ProgramOffset","0"},{"Program17","19"},
                                      {"Name1","Coupler"},{"ST2","70"},{"StopDelayMs","5"},{"DropSongProgramChanges","false"}});
      CHECK_EQ(o.channel, 3); CHECK_EQ(mc::organ::programFor(o, 8), 1);
      CHECK_EQ(mc::organ::programFor(o, 17), 19);  // override wins
      CHECK_EQ(mc::organ::programFor(o, 2), 56);   // ST70 octal
      CHECK_EQ(o.stops[0].name, std::string("Coupler")); CHECK_FALSE(o.dropSongProgramChanges);
  }
  TEST(organ_parse_rejects_bad_values) {
      using mc::organ::parseOrgan;
      CHECK_THROWS(parseOrgan({{"Channel","17"}}));
      CHECK_THROWS(parseOrgan({{"ST3","18"}}));            // 8 is not an octal digit
      CHECK_THROWS(parseOrgan({{"Program5","128"}}));
      CHECK_THROWS(parseOrgan({{"AllOffSysex","F0 ZZ F7"}}));
      CHECK_THROWS(parseOrgan({{"Groups","A:1-7, B:9-12"}})); // gap at 8
      CHECK_THROWS(parseOrgan({{"ProgramOffset","-2"}}));     // ST01 would map to -1
  }
  TEST(organ_preamble_bytes_and_delays) {
      auto o = mc::organ::defaultOrgan();
      auto p = mc::organ::buildPreamble(o, {8, 17});
      CHECK_EQ(p.size(), 3u);
      CHECK(p[0].bytes == o.allOffSysex); CHECK(std::abs(p[0].delayAfterSeconds - 0.100) < 1e-9);
      CHECK(p[1].bytes == (std::vector<uint8_t>{0xCB, 0})); CHECK(std::abs(p[1].delayAfterSeconds - 0.020) < 1e-9);
      CHECK(p[2].bytes == (std::vector<uint8_t>{0xCB, 11}));
      CHECK_EQ(mc::organ::buildPreamble(o, {}).size(), 1u);  // saved empty = all-off only
  }
  ```
  In `tests/test_config.cpp`, add `config_organ_section`: `parseConfig` with `[ORGAN]\nChannel=5` → `cfg.organ.channel == 5`. Without `[ORGAN]` → `cfg.organ.stopCount() == 42`.
  If `CHECK_THROWS` doesn't exist in `test_framework.hpp`, add it next to `CHECK`, in the same style.

- [ ] **Step 2: Run them and confirm they fail** (compile error: `organ.hpp` missing). Add the test file to `midicontrol_tests` first.

- [ ] **Step 3: Implement** `organ.hpp` / `organ.cpp` and the `Config::organ` parse (in `parseConfig`, when section `ORGAN` exists).
  - **Keys:** exactly the spec's `[ORGAN]` table: `StopsEnabled`, `Channel`, `ProgramOffset`, `AllOffSysex`, `AllOffDelayMs`, `StopDelayMs`, `DropSongProgramChanges`, `Groups`, `NameN`, `STN`, `ProgramN`.
  - **Number range:** `N` must be within the stop count. Unknown keys are ignored, as the rest of `parseConfig` does.
  - **Stop count:** the last group's `last`.
  - **Changed `Groups`:** stops without default data get name `"Stop N"` and no ST. Such a stop needs `ProgramN`, otherwise throw.
  - **Validation:** after parsing, every stop must map to a program in 0..127, otherwise throw.
  - **Preamble channel byte:** `0xC0 | (channel - 1)`. The last stop step also carries `stopDelayMs`.

- [ ] **Step 4: Run** `cmake --build build -j && ./build/midicontrol_tests`. Expected: all pass.

- [ ] **Step 5: Commit** `git add include/midicontrol/organ.hpp src/organ.cpp tests/test_organ.cpp include/midicontrol/playback.hpp include/midicontrol/config.hpp src/config.cpp tests/test_config.cpp tests/test_framework.hpp CMakeLists.txt && git commit -m "Organ stops: [ORGAN] config, Opus 910 defaults, preamble bytes"`

---

### Task 2: `stops_file` module: `stops.conf`, atomic save, store, API JSON

**Files:**
- Create: `include/midicontrol/stops_file.hpp`, `src/stops_file.cpp`, `tests/test_stops_file.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `mc::organ::OrganConfig`, `buildPreamble` (Task 1); `mc::songs::sanitizeCode`, `naturalLess`; `mc::status::jsonEscape` (from `status_json.hpp`)
- Produces (namespace `mc::stops`):
  ```cpp
  inline constexpr const char* kFileName = "stops.conf";
  std::string filePath(const std::string& playFolder);                  // playFolder + "/stops.conf"
  using SongStops = std::map<std::string, std::vector<int>>;            // present = set; empty vector = all off
  SongStops parse(const std::string& text, int stopCount, std::vector<std::string>* warnings);
  std::string serialize(const SongStops& s);                            // header comment, [SONGS], natural order
  SongStops load(const std::string& path, int stopCount, std::vector<std::string>* warnings); // missing file = {}
  void save(const std::string& path, const SongStops& s);               // temp in same dir, fsync, rename; throws std::runtime_error
  // Body of PUT: {"stops":[...]} -> set, {"stops":null} -> nullopt inner; nullopt outer = invalid.
  std::optional<std::optional<std::vector<int>>> parseBody(const std::string& body, int stopCount);
  std::string toJson(const mc::organ::OrganConfig& organ, const std::vector<std::string>& songCodes, const SongStops& s);
  std::string entryJson(const std::string& code, const std::optional<std::vector<int>>& stops); // {"code":"401","stops":[..]|null}

  struct SongPlan { std::vector<MidiStep> preamble; bool dropProgramChanges = false; std::string text; };
  // text: "3 4 5" | "all off" | "not set" | "disabled"; preamble empty unless set and stopsEnabled
  SongPlan planFor(const mc::organ::OrganConfig& organ, const SongStops& s, const std::string& code);

  class Store {  // the web thread's handle; saves serialised by a mutex
  public:
      Store(std::string path, int stopCount);
      SongStops get() const;
      // Replace (set) or remove (nullopt) one song; load-modify-save. Throws on write failure, file unchanged.
      void put(const std::string& code, const std::optional<std::vector<int>>& stops);
  };
  ```
- `toJson` shape (schema 1):
  ```json
  {"schema":1,"groups":[{"label":"ACCESSORIES","first":1,"last":7},…],"names":{"1":"Swell to Great",…},"songs":[{"code":"401","stops":[3,4]},{"code":"403","stops":null}]}
  ```
  Songs come in the order of `songCodes`.

- [ ] **Step 1: Write the failing tests** in `tests/test_stops_file.cpp`:
  - `stops_file_roundtrip`: `serialize({{"401",{3,4,5}},{"406B",{}}})` contains `"[SONGS]"`, `"401=3 4 5"` and `"406B="`. `parse` of that text gives the same map.
  - `stops_file_parse_is_lenient`: the text below parses to `{"406A",{5,7}}` and `{"401",{1}}`, with `warnings.size() >= 3`, and does not throw:
    ```
    # c
    [SONGS]
    406a = 7 5 5 99 x
    bad line
    401=1
    ../x=2
    ```
  - `stops_file_keeps_unknown_songs`: after `Store::put("401",{{1}})` on a file holding `999=2`, `get()` has both.
  - `stops_store_put_null_removes`: `put("401", std::nullopt)` → the key is absent.
  - `stops_store_save_failure_keeps_file`: the dir has the file and then mode `0555` → `put` throws (`CHECK_THROWS`) and the file content is unchanged. Skip when `geteuid()==0`.
  - `stops_parse_body`:
    - `{"stops":[3,1,3]}` → `{1,3}`
    - `{"stops":null}` → set, inner nullopt
    - ` { "stops" : [ ] } ` → empty vector
    - `{"stops":[0]}`, `{"stops":[43]}`, `{"stops":["3"]}`, `{}`, `garbage` → outer nullopt
  - `stops_json_shape`: `toJson(defaultOrgan(), {"401","403"}, {{"401",{3}}})` contains `"\"schema\":1"`, `"{\"label\":\"PEDAL\",\"first\":8,\"last\":16}"`, `"\"8\":\"Double Bass 16'\""`, `"{\"code\":\"401\",\"stops\":[3]}"` and `"{\"code\":\"403\",\"stops\":null}"`.
  - `stops_plan_for_song`, with `o = defaultOrgan()`:
    - `planFor(o, {{"401",{8}}}, "401")`: preamble size 2, `dropProgramChanges` true, text `"8"`
    - for `"403"`: preamble empty, drop false, text `"not set"`
    - for a set `{}` entry: preamble size 1, text `"all off"`
    - with `o.stopsEnabled=false`: preamble empty, text `"disabled"`.

  Use a temp dir from `mkdtemp` under `/tmp`.

- [ ] **Step 2: Run them and confirm they fail** (missing header).
- [ ] **Step 3: Implement.**
  - The JSON body parser is hand-written and strict, accepting only the two shapes above (whitespace allowed). There is no general JSON parser.
  - `save` writes `<path>.tmp.<pid>`, then `fsync`, then `rename`, then `fsync`s the directory. On any failure it removes the temp file and throws a `std::runtime_error` naming the path and `strerror`.
- [ ] **Step 4: Run tests.** Expected: all pass.
- [ ] **Step 5: Commit** `git commit -m "Organ stops: stops.conf format, atomic save, store, API JSON"`

---

### Task 3: PlaybackEngine preamble and Program Change filter

**Files:**
- Modify: `include/midicontrol/playback.hpp` (`PlaybackOptions`), `src/playback.cpp` (`run`), `tests/test_playback.cpp`

**Interfaces:**
- Consumes: `MidiStep` (Task 1)
- Produces:
  ```cpp
  struct PlaybackOptions {
      double gapSeconds = 0.0;
      std::vector<MidiStep> preamble;      // sent before the song, through the Timebase
      bool dropProgramChanges = false;     // skip 0xCn channel events from the file
  };
  ```

- [ ] **Step 1: Write the failing tests** in `tests/test_playback.cpp`, using the existing `FakeClock`, `Capture` and `multiVerse()`:
  ```cpp
  TEST(engine_sends_preamble_before_song) {
      Capture cap; PlaybackOptions o;
      o.preamble = {{{0xF0,0x00,0x4A,0x4F,0x48,0x41,0x53,0x7F,0xF7}, 0.1}, {{0xCB,0}, 0.02}};
      PlaybackEngine engine(multiVerse(), "401", o, cap.callbacks());
      FakeClock clock;
      CHECK(engine.run(clock) == PlaybackEngine::Result::Finished);
      CHECK(cap.midi[0] == o.preamble[0].bytes);
      CHECK(cap.midi[1] == o.preamble[1].bytes);
      CHECK_EQ(clock.passStarts, 2);                       // preamble pass + song pass
      CHECK(std::abs(clock.waits[0] - 0.1) < 1e-9);
      CHECK(std::abs(clock.waits[1] - 0.12) < 1e-9);
      CHECK_EQ(cap.noteOns(), 4);
  }
  TEST(engine_preamble_stop_aborts_before_song) {
      Capture cap; PlaybackOptions o; o.preamble = {{{0xF0,0xF7}, 0.1}, {{0xCB,0}, 0.02}};
      PlaybackEngine engine(multiVerse(), "401", o, cap.callbacks());
      FakeClock clock; clock.hook = [](int, double) { return false; };
      CHECK(engine.run(clock) == PlaybackEngine::Result::Stopped);
      CHECK_EQ(cap.noteOns(), 0);
      CHECK(cap.anchors.empty());
  }
  TEST(engine_drops_program_changes_when_asked) {
      // multiVerse() plus a {0xC0, 0} channel event at t=0; with dropProgramChanges no 0xC0 reaches midi,
      // without it exactly one does.
  }
  ```
  Write the last test with that body: build the file, run it twice and count `(b[0]&0xF0)==0xC0`.
  Also check that `engine_plays_sections_as_authored` still expects `passStarts == 1`, i.e. no preamble means no extra pass.

- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement in `PlaybackEngine::run`.**
  - When `_opts.preamble` is non-empty, run it before the start display and `announce()`:
    1. `tb.startPass()`, `t = 0`.
    2. For each step: if `_stop`, return `Stopped`. Otherwise `_cb.midi(bytes)`, then `t += delayAfterSeconds`, then `if (!tb.waitUntil(t))` return `Stopped`.
    3. On `Stopped`, set `_running` false and send no all-notes-off (no note has played).
  - In the event loop, skip `Channel` events whose status nibble is `0xC0` when `dropProgramChanges` is set.
  - Update the `Callbacks::midi` comment to say "MIDI message (channel or SysEx)".
- [ ] **Step 4: Run tests.** Expected: all pass, including the old playback tests.
- [ ] **Step 5: Commit** `git commit -m "Playback: preamble before the song and optional Program Change filter"`

---

### Task 4: HTTP request bodies, PUT route, stops API routing

**Files:**
- Modify: `include/midicontrol/http.hpp`, `src/http.cpp`, `tests/test_http.cpp`

**Interfaces:**
- Produces:
  ```cpp
  inline constexpr size_t kMaxBody = 4096;
  struct Request { std::string method, path, query; std::string contentType /* lower-cased, as sent */; std::string body; };
  struct StopsApi {
      std::function<std::string()> get;                                                 // GET /api/stops
      std::function<Response(const std::string& code, const std::string& body)> put;    // PUT /api/stops/<code>
  };
  Response route(const Request& req, const std::string& html, const std::function<std::string()>& statusJson,
                 const DiagHandler& diagJson = nullptr, const StopsApi* stops = nullptr);
  ```
- Rules:
  - `parseRequest` stays `Incomplete` until `Content-Length` bytes of body are present.
  - Head plus body over `maxSize`, or `Content-Length > kMaxBody` → `TooLarge`. A non-numeric `Content-Length` → `Bad`.
  - Header names match case-insensitively.
  - `route`:
    - `PUT /api/stops/<code>` with `stops` set: `contentType` must start with `application/json`, otherwise 415. Then it calls `stops->put(code, body)`, where `code` is the raw path segment and the handler validates it.
    - `GET /api/stops` → 200 `application/json`.
    - Any other non-GET → 405, as now.
  - Add reason phrase `415 Unsupported Media Type`.

- [ ] **Step 1: Write the failing tests** in `tests/test_http.cpp`:
  - `http_parse_body_split_and_limits`:
    - the head with `Content-Length: 5` and only 2 body bytes → `Incomplete`; all 5 → `Ok` with `body=="abcde"`
    - `content-type: Application/JSON` → `contentType=="application/json"`
    - `Content-Length: 5000` → `TooLarge`
    - `Content-Length: x` → `Bad`
  - `http_route_stops`, with a fake `StopsApi` that records its args:
    - `GET /api/stops` → 200
    - `PUT /api/stops/401` with json → the handler gets `"401"` and the body
    - the same with `text/plain` → 415
    - `PUT /api/status` → 405
    - `PUT /api/stops/401` with `stops == nullptr` → 404
    - `POST /` → 405, as the existing test has it.
- [ ] **Step 2: Run them and confirm they fail.**
- [ ] **Step 3: Implement.** The existing 8 KiB `kMaxRequest` still bounds head + body.
- [ ] **Step 4: Run tests.** Expected: all pass, including `http_route`.
- [ ] **Step 5: Commit** `git commit -m "HTTP: request bodies and the stops API routes"`

---

### Task 5: Wire it up: WebServer store, `doPlay`, config docs

**Files:**
- Modify:
  - `include/midicontrol/web_server.hpp`, `src/web_server.cpp`
  - `src/main.cpp:227`
  - `src/comm.cpp` (`doPlay`, around `opts.gapSeconds` at ~1094)
  - `config.conf`, `docs/status-web-and-service.md`, `CLAUDE.md`, `TODO.md`

**Interfaces:**
- Consumes: `mc::stops::Store`, `toJson`, `entryJson`, `parseBody`, `load`, `planFor`, `filePath` (Task 2); `mc::http::StopsApi` (Task 4); `Config::organ` (Task 1).
- Produces: the `WebServer` ctor gains `const mc::organ::OrganConfig& organ` after `playFolder`. It keeps a copy and a `mc::stops::Store _stops{filePath(playFolder), organ.stopCount()}`.

- [ ] **Step 1: Write the failing test** in `tests/test_stops_file.cpp`: `stops_put_handler_validates`. This exercises the PUT logic as a free function, so the WebServer stays thin:
  ```cpp
  // in stops_file.hpp:
  mc::http::Response handlePut(Store& store, const mc::organ::OrganConfig& organ,
                               const std::vector<std::string>& songCodes,
                               const std::string& rawCode, const std::string& body);
  ```
  It asserts:
  - unknown code (not in `songCodes`) → 400
  - `rawCode` `"../x"` → 400
  - bad body → 400
  - `"401"` + `{"stops":[2,1]}` → 200 with body `{"code":"401","stops":[1,2]}`, and the store has it
  - a store whose save throws (read-only dir, skip as root) → 500, with the error text in the body.

  Codes are normalised with `sanitizeCode`, so `406a` is accepted as `406A`.
- [ ] **Step 2: Run it and confirm it fails.**
- [ ] **Step 3: Implement `handlePut`** in `stops_file.cpp`, then wire everything:
  - **`WebServer::readConn`** builds a `StopsApi`:
    - `get` → `toJson(_organ, mc::songs::listSongs(_playFolder), _stops.get())`
    - `put` → `handlePut(_stops, _organ, listSongs(_playFolder), code, body)`
    - It passes `&api` to `route`.
  - **`main.cpp`** passes `config.organ`.
  - **`doPlay`**, right after the warm-up block and before `_midiFinder`:
    1. Call `load(filePath(_config.playFolder), _config.organ.stopCount(), &warnings)`, and log each warning with `mc::log::warn`.
    2. Call `planFor(_config.organ, songs, cmd.code)`.
    3. Copy `preamble` and `dropProgramChanges` into `opts`. Move the `PlaybackOptions opts` declaration up.
    4. Log `mc::log::info(Node::Software, "Stops for " + cmd.code + ": " + plan.text)`.
    5. Set `_status.set(Node::Software, "stops", plan.text)`.
  - **`config.conf`**: add a commented `[ORGAN]` section listing every key with its default and a one-line meaning, as in the spec table.
  - **`docs/status-web-and-service.md`**: add a "Stops tab and `stops.conf`" section covering:
    - what the tab does
    - the file format
    - not set vs all off
    - the song-start sequence
    - the `/api/stops` GET/PUT
    - the open items from the spec (verify the offset, the all-off SysEx).
  - **`CLAUDE.md`**: add the module-list entries for `organ` and `stops_file`. Mention `PUT /api/stops/<code>` in the status-page line (it is no longer strictly read-only), and `[ORGAN]` in the Config sentence.
  - **`TODO.md`**: replace "Per-song organ stops" with a short "verify at the organ" note, pointing to the spec's open items.
- [ ] **Step 4: Run** `cmake --build build -j && ./build/midicontrol_tests`. Expected: all pass, and the `midicontrol` app target links (rtmidi is present in the container).
- [ ] **Step 5: Commit** `git commit -m "Organ stops: web store, song-start preamble, config and docs"`

---

### Task 6: Stops tab on the status page

**Files:**
- Modify: `src/web_page.cpp`, `tests/test_web_page.cpp`

**Interfaces:**
- Consumes: `GET /api/stops` JSON and `PUT /api/stops/<code>` (Tasks 2, 4, 5)

- [ ] **Step 1: Write the failing test** `web_page_has_stops_tab` in `tests/test_web_page.cpp`. It checks that the page contains:
  - `"Stops"`, `"/api/stops"`, `"application/json"`, `"PUT"`
  - `"not set"`, `"all off"`, `"unsaved changes"`, `"Not set"`.

  The existing `web_page_self_contained` (no `innerHTML`, no external URLs) must keep passing.
- [ ] **Step 2: Run it and confirm it fails.**
- [ ] **Step 3: Implement** the tab after the mockup `docs/superpowers/specs/2026-10-27-organ-stops-mockup.html`, adapted to the page.
  - **Tab registration:** add `["stops","Stops"]` to `TABS`. Add a `stops` builder to `render()`'s `builders`.
  - **Caching:** the section is built once and cached, like `DG.sec` for Diagnostics, so the 1 s status poll does not rebuild it or lose edits.
  - **Loading:** `GET /api/stops` loads once when the tab is first opened, and again after each save. It never polls.
  - **DOM:** build it with `el()`/`textContent` only. No `innerHTML`.
  - **Data:** groups, names and songs come from the API, nothing hard-coded.
  - **Behaviour, as in mockup 6:**
    - dark panel, gold group labels, 1:3 tabs that toggle on any click, "on" shown by colour only
    - "not set" heading badge with dimmed tabs
    - Save enabled only when dirty
    - All off; Not set (with `confirm`, then a PUT with `{"stops":null}`)
    - sortable song table; `confirm` before discarding unsaved changes
    - phone layout: below 520 px the groups wrap, tabs shrink and names are hidden.
  - **Save errors:** a failed save (non-2xx) shows the response text next to the buttons and keeps the edits.
  - **Colours:** the panel keeps its own colours in both themes.
- [ ] **Step 4: Run tests,** then run the app against the scratch config and check it in a browser:
  - toggle, save, reload: still there
  - Not set, reload: "not set"
  - phone width.

  Expected: tests pass, and `stops.conf` in the PlayFolder holds the saved lines.
- [ ] **Step 5: Commit** `git commit -m "Status page: Stops tab"`

---

## Bench check (after Task 6, needs the base + organ, the user decides when)

- Play a song with stops set while watching the base's MIDI out, or the organ. Expected order: the all-off SysEx, 100 ms, then one Program Change per stop on channel 12, then the first note. The song's own channel-1 Program Change is not sent.
- Spec open items 1 and 2: the program numbers of Double Bass (ST01) and Swell to Great (ST60), and whether all-off works on the 910.
