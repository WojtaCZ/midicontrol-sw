# Status web page and always-on service: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A read-only status page on port 50118 served by the running player, plus an unattended systemd service (notify + real-liveness watchdog, single instance) that terminal commands attach to over a Unix socket.

**Architecture:** Pure, unit-tested core pieces (`status` registry and dot rules, JSON, HTTP parsing/routing, embedded page, song-file check, console command parsing, `[WEB]` config, logger) feed a thin app layer (CommHandler instrumentation, a single-thread blocking web server, a Unix control socket server + thin client, flock single instance, sd_notify without libsystemd). Every thread writes the registry; the web thread only reads copies taken under the registry's own short lock.

**Tech Stack:** C++17, CMake, POSIX sockets/flock/termios, built-in test harness (`tests/test_framework.hpp`), RtMidi (app only).

**Spec:** `docs/superpowers/specs/2026-10-19-status-web-design.md`

## Global Constraints

- C++17, `-Wall -Wextra`, **no warnings**.
- Core library (`midicontrol_core`) has **no external dependencies**; hardware/OS socket code stays in the app target. New core sources go in `midicontrol_core`, new tests in `midicontrol_tests` (both lists in `CMakeLists.txt`).
- Event levels: DEBUG, INFO, WARN, ERROR. DEBUG goes to the registry only; INFO to stdout; WARN/ERROR to stderr. All non-DEBUG also go to the registry.
- Per-node ring: **200** events. Problems list (WARN + ERROR from all nodes): **50**.
- Web: port **50118**, bind **0.0.0.0**, enabled **true** by default; `[WEB]` keys `Enabled`, `Port`, `Bind`. Bind failure → ERROR and retry every **10 s**.
- Page polls `GET /api/status` about **once a second**; one self-contained HTML document, no external requests (no CDN, no fonts, no `http(s)://` URLs).
- Base poll: `GET_STATUS` every **5 s** while connected; no answer (NAK or timeout) shows "not answered".
- Controller STATUS_RESP freshness: **≤ 15 s**. WARN/ERROR "recent" window for Software dot: **10 min**.
- Song folder rescan: every **60 s**, on the web thread, only changed (mtime/size) files re-parsed; each problem is one WARN when first found.
- Lock `$XDG_RUNTIME_DIR/midicontrol.lock` (flock, exclusive); control socket `$XDG_RUNTIME_DIR/midicontrol.sock`, mode **0600**. Serial port opened with `TIOCEXCL`.
- systemd unit: `Restart=always`, `StartLimitIntervalSec=0`, `RestartSec=5`, `Type=notify`, `WatchdogSec=30`. `READY=1` once config is loaded and the web thread started (not waiting for the base).
- Routine traffic (KEEPALIVE, KEEPALIVE_ACK, the 5 s GET_STATUS/STATUS_RESP poll and their ACKs) updates counters/fields only, never events.
- Event text uses `MsgType` names from `protocol/types.hpp`, e.g. `RX REPLAY_ANCHOR id=8`.
- Interactive CLI output (prompt, `list` reply) stays plain console output, not events.
- Project language English.

## Build note (this container)

RtMidi is not installed and the container has no network, so the `midicontrol` app target is skipped by CMake here. Verification for app-layer tasks is: core + tests build and pass, **plus** a syntax/compile check of every app source against a stub header:

```bash
STUB=/tmp/claude-1000/-workspace/b48e7e56-2191-4af5-a465-f27d5789a2fc/scratchpad/rtmidi-stub
for f in src/main.cpp src/comm.cpp src/firmware_update.cpp src/player.cpp src/midi_ports.cpp \
         src/serial_port.cpp src/web_server.cpp src/control_socket.cpp src/instance.cpp src/sd_notify.cpp; do
  [ -f "$f" ] && g++ -std=c++17 -Wall -Wextra -Werror -Iinclude -I"$STUB" -DMC_GIT_HASH='"x"' -DMC_BUILD_DATE='"x"' -fsyntax-only "$f" || echo "FAIL $f"
done
```

`$STUB/RtMidi.h` declares just what `player.cpp`/`midi_ports.cpp` use (`RtMidiOut`, `RtMidiIn`, `RtMidiError`). Hardware tests are deferred (see "Bench checklist").

## Review Focus

1. **Arbitrary bytes in event text** (song codes from the wire, file names with spaces or Latin-2 bytes, `072 - Lide muj.mid`) → the JSON stays valid: control chars become `\u00XX`, invalid UTF-8 bytes become `\ufffd`. Test in Task 3.
2. **Malformed or hostile HTTP** (no CRLF terminator, > 8 KiB headers, `GET` with a query string, `POST`, absolute-form or `..` paths) → 400/413/405/404, never a crash or hang; query string ignored. Tests in Task 4.
3. **Web port already taken / invalid Bind address** → ERROR event once per failure streak, retry every 10 s, playback untouched. Covered by the bench checklist; the core part (config validation of `Port` 1..65535) is tested in Task 7.
4. **Client disconnects mid-command (firmware update) or a second client issues `update` concurrently** → the update continues; a concurrent `update` is refused with "busy". Logic in Task 11, bench-checked.
5. **Base unplugged at start, or lost and replugged** → Base dot yellow "waiting", reconnect counter increments, Controller dot grey. Dot rules tested in Task 2 with those exact field states.

---

### Task 1: Protocol names, packet description, dispatcher duplicate hook, parser bad-frame count

**Files:**
- Create: `src/protocol/names.cpp`
- Modify: `include/midicontrol/protocol/types.hpp`, `include/midicontrol/dispatch.hpp`, `src/dispatch.cpp`, `include/midicontrol/protocol/parser.hpp`, `src/protocol/parser.cpp`, `CMakeLists.txt`
- Test: `tests/test_names.cpp` (new), `tests/test_dispatch.cpp`, `tests/test_parser.cpp`

**Interfaces:**
- Produces (in `mc::protocol`, declared in `types.hpp`):
  - `const char* msgTypeName(MsgType t);` — enum identifier (`"REPLAY_ANCHOR"`), `"0xNN"`-style fallback is not possible with `const char*`, so unknown → `"UNKNOWN"`.
  - `const char* nakReasonName(uint8_t reason);` — `"NOT_PLAYING"`, unknown → `"UNKNOWN"`.
  - `std::string describePacket(const Packet& p);` — `"<TYPE> id=<dec>"`; ACK adds ` for=<pid>`; NAK adds ` for=<pid> <REASON>`; other non-empty payloads add ` [AA BB ..]` uppercase hex, at most 16 bytes then ` ...`.
- Produces: `Dispatcher::Output::onDuplicate` — `std::function<void(const Packet&)>`, called (when set) for a retransmission that was re-answered without executing.
- Produces: `Parser::badFrames() const -> uint64_t` — count of FRAME_END frames that failed `decodeFrame` plus runaway-frame drops.

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_names.cpp
TEST(names_msg_type) {
    CHECK_EQ(std::string(protocol::msgTypeName(protocol::MsgType::REPLAY_ANCHOR)), "REPLAY_ANCHOR");
    CHECK_EQ(std::string(protocol::msgTypeName(static_cast<protocol::MsgType>(0x99))), "UNKNOWN");
    CHECK_EQ(std::string(protocol::nakReasonName(0x05)), "NOT_PLAYING");
}
TEST(names_describe_packet) {
    using protocol::Packet; using protocol::MsgType;
    CHECK_EQ(protocol::describePacket(Packet(3, 1, 8, MsgType::REPLAY_ANCHOR)), "REPLAY_ANCHOR id=8");
    CHECK_EQ(protocol::describePacket(Packet(1, 3, 9, MsgType::NAK, {8, 0x05})), "NAK id=9 for=8 NOT_PLAYING");
    CHECK_EQ(protocol::describePacket(Packet(1, 2, 17, MsgType::NOW_PLAYING_INFO, {0x40, 0x6A, 0x01, 0xFF, 0xFF})),
             "NOW_PLAYING_INFO id=17 [40 6A 01 FF FF]");
}
// tests/test_dispatch.cpp: dispatcher_reports_duplicate — same (source, packetId) PLAY_SONG twice → onDuplicate called once, sink.playSong called once.
// tests/test_parser.cpp: parser_counts_bad_frames — feed a valid frame with one CRC byte flipped → no packet, badFrames()==1; a valid frame afterwards still decodes.
```

- [ ] **Step 2:** `cmake --build build -j && ./build/midicontrol_tests` → compile failure (names undefined).
- [ ] **Step 3:** Implement the three pieces; add `src/protocol/names.cpp` and `tests/test_names.cpp` to `CMakeLists.txt`.
- [ ] **Step 4:** Build and run tests → all PASS, no warnings.
- [ ] **Step 5:** `git commit -m "core: message names, packet description, duplicate and bad-frame hooks"`

---

### Task 2: StatusRegistry, peer attribution and dot rules

**Files:**
- Create: `include/midicontrol/status.hpp`, `src/status.cpp`
- Test: `tests/test_status.cpp`

**Interfaces:**
- Produces (namespace `mc::status`):
  ```cpp
  enum class Level { Debug, Info, Warn, Error };
  enum class Node { Software = 0, Base = 1, Controller = 2 };
  enum class Dot { Green, Yellow, Red, Grey };
  const char* levelName(Level);  // "DEBUG" "INFO" "WARN" "ERROR"
  const char* nodeKey(Node);     // "software" "base" "controller"
  const char* dotName(Dot);      // "green" "yellow" "red" "grey"
  using Value = std::variant<std::monostate, bool, int64_t, std::string>;
  struct Event { int64_t timeMs; Level level; Node node; std::string text; };
  struct SongEntry { std::string code; std::string problem; };  // problem empty = ok
  struct NodeSnapshot {
      std::map<std::string, Value> fields;
      std::map<std::string, uint64_t> counters;
      std::vector<Event> events;  // oldest first
  };
  struct Snapshot {
      int64_t nowMs = 0;
      std::array<NodeSnapshot, 3> nodes;
      std::vector<Event> problems;  // oldest first
      std::vector<SongEntry> songs;
  };
  class StatusRegistry {
  public:
      using Clock = std::function<int64_t()>;  // wall time, ms since epoch
      explicit StatusRegistry(Clock now, size_t ringSize = 200, size_t problemsSize = 50);
      int64_t now() const;
      void event(Node, Level, std::string text);  // WARN/ERROR also -> problems and counters "warnings"/"errors" of that node
      void count(Node, const std::string& counter, uint64_t n = 1);
      void set(Node, const std::string& field, Value v);
      void setNow(Node, const std::string& field);  // set(field, now())
      void setSongs(std::vector<SongEntry>);
      Snapshot snapshot() const;  // copy under the lock, nowMs = now()
  };
  // Nodes a packet to/from `addr` counts towards: BASE→{Base}, REMOTE→{Controller},
  // BROADCAST→{Base, Controller}, anything else→{Base}.
  std::vector<Node> nodesForPeer(uint8_t addr);
  Dot evaluateDot(Node, const Snapshot&);
  ```
- Field contract read by `evaluateDot` (written by Task 9):
  - Base: `connected` (bool, serial open), `alive` (bool, keepalive answered within 6 s), `status_answered` (bool, last GET_STATUS answered).
  - Controller: `status_ms` (int64, time of last STATUS_RESP, on the **Base** node), `status_ble` (bool, Base node).
  - Software: `song_problems` (int64, Software node) plus WARN/ERROR events in `problems`.

Dot rules (from the spec, exactly):
- Software: Red if a Software ERROR in `problems` within 10 min of `nowMs`; else Yellow if a Software WARN within 10 min or `song_problems > 0`; else Green.
- Base: Yellow if `!connected`; Red if `connected && !alive`; Yellow if `status_answered == false`; else Green.
- Controller: Grey if base `!connected` or no `status_ms` or `nowMs - status_ms > 15000`; Green if `status_ble`; else Yellow.

- [ ] **Step 1: Write failing tests** (`tests/test_status.cpp`, fake clock `int64_t t = 1'000'000; StatusRegistry r([&]{ return t; });`)
  - `status_counters_per_node`: `count(Base,"tx")` twice, `count(Controller,"tx")` once → snapshot base `tx`==2, controller `tx`==1.
  - `status_ring_wraps_at_200`: 205 DEBUG events on Base → 200 kept, first text is `"e5"`, last `"e204"`.
  - `status_problems_keep_warnings`: one WARN then 300 DEBUG on Base → Base events 200 long, `problems.size()==1`, Base counter `warnings`==1; 60 ERRORs → problems 50.
  - `status_snapshot_is_a_copy`: take snapshot, add event, earlier snapshot unchanged.
  - `status_nodes_for_peer`: 0x02→{Base}, 0x03→{Controller}, 0xFF→{Base,Controller}, 0x01→{Base}.
  - `status_dot_base`: not connected → Yellow; connected, alive=false → Red; connected, alive, status_answered=false → Yellow; connected, alive, answered → Green.
  - `status_dot_controller`: base not connected → Grey; connected, no `status_ms` → Grey; `status_ms` 16 s old → Grey; 5 s old with `status_ble` true → Green; false → Yellow.
  - `status_dot_software`: fresh → Green; WARN now → Yellow; advance 11 min → Green; ERROR → Red; `song_problems`=1 → Yellow.
- [ ] **Step 2:** Run tests → compile failure.
- [ ] **Step 3:** Implement with one `std::mutex`; ring as `std::deque<Event>` per node.
- [ ] **Step 4:** Tests PASS.
- [ ] **Step 5:** `git commit -m "core: status registry and dot rules"`

---

### Task 3: JSON rendering

**Files:**
- Create: `include/midicontrol/status_json.hpp`, `src/status_json.cpp`
- Test: `tests/test_status_json.cpp`

**Interfaces:**
- Consumes: `mc::status::Snapshot`, `evaluateDot` (Task 2).
- Produces: `std::string mc::status::jsonEscape(const std::string& s);` (returns the quoted string) and `std::string mc::status::toJson(const Snapshot& s);`

Schema (version 1; future firmware data = more `fields` keys and counters on the same node, no shape change):

```json
{"schema":1,"now_ms":123,
 "nodes":{
   "software":{"dot":"green","fields":{"version":"4854c75"},"counters":{"songs_ended":3},
               "events":[{"t":120,"level":"INFO","text":"..."}]},
   "base":{...}, "controller":{...}},
 "problems":[{"t":120,"level":"WARN","node":"base","text":"..."}],
 "songs":[{"code":"523","problem":"empty display anchor"}]}
```

Events and problems are emitted **newest first**. `monostate` → `null`, bool → `true/false`, int64 → number.

- [ ] **Step 1: Write failing tests**
  - `json_escape_specials`: `jsonEscape("a\"b\\c\n\x01")` == `"\"a\\\"b\\\\c\\n\\u0001\""`.
  - `json_escape_utf8`: valid `"Lid\xC3\xA9"` passes through unchanged (quoted); lone `"\xFF"` → `"\"\\ufffd\""`; truncated `"\xC3"` → `"\"\\ufffd\""`.
  - `json_schema_keys`: snapshot with one Base field, one counter and one WARN event → output contains `"schema":1`, `"nodes":{"software":`, `"base":{"dot":"yellow"`, `"problems":[{"t":`, `"songs":[]`.
  - `json_events_newest_first`: events "a" then "b" → `"b"` appears before `"a"` in output.
- [ ] **Step 2:** Run → fail. **Step 3:** Implement (hand-written serializer, `std::ostringstream`). **Step 4:** PASS.
- [ ] **Step 5:** `git commit -m "core: status JSON rendering"`

---

### Task 4: HTTP request parsing and routing

**Files:**
- Create: `include/midicontrol/http.hpp`, `src/http.cpp`
- Test: `tests/test_http.cpp`

**Interfaces:**
- Produces (namespace `mc::http`):
  ```cpp
  inline constexpr size_t kMaxRequest = 8192;
  struct Request { std::string method; std::string path; };   // path without query
  enum class ParseResult { Incomplete, Ok, Bad, TooLarge };
  ParseResult parseRequest(const std::string& buffer, Request& out, size_t maxSize = kMaxRequest);
  struct Response { int status; std::string contentType; std::string body; };
  std::string serialize(const Response& r);   // HTTP/1.1, Content-Length, Connection: close, Cache-Control: no-store
  Response route(const Request& req, const std::string& html, const std::function<std::string()>& statusJson);
  Response errorResponse(int status);         // text/plain body "<code> <reason>\n"
  ```
- Rules: request complete when `\r\n\r\n` seen; `Incomplete` before that, `TooLarge` once buffer exceeds `maxSize` without terminator; `Bad` if the request line isn't `METHOD SP target SP HTTP/1.x` or the target doesn't start with `/`. `route`: non-GET → 405; `/` → 200 `text/html; charset=utf-8`; `/api/status` → 200 `application/json`; else 404.

- [ ] **Step 1: Write failing tests**
  - `http_parse_good`: `"GET /api/status?x=1 HTTP/1.1\r\nHost: a\r\n\r\n"` → Ok, method GET, path `/api/status`.
  - `http_parse_incomplete`: `"GET / HTTP/1.1\r\nHost"` → Incomplete.
  - `http_parse_too_large`: 9000 × `'a'` → TooLarge.
  - `http_parse_bad`: `"HELLO\r\n\r\n"` → Bad; `"GET http://x/ HTTP/1.1\r\n\r\n"` → Bad.
  - `http_route`: GET `/` → 200 html body; GET `/api/status` → 200 and the json callback's string; GET `/nope` → 404; POST `/` → 405.
  - `http_serialize`: `serialize({200,"text/plain","hi"})` starts with `"HTTP/1.1 200 OK\r\n"` and contains `"Content-Length: 2\r\n"` and ends with `"\r\n\r\nhi"`.
- [ ] **Step 2–4:** fail → implement → PASS.
- [ ] **Step 5:** `git commit -m "core: HTTP request parsing and routing"`

---

### Task 5: Embedded status page

**Files:**
- Create: `include/midicontrol/web_page.hpp`, `src/web_page.cpp`
- Test: `tests/test_web_page.cpp`

**Interfaces:**
- Consumes: JSON schema (Task 3), field/counter names (Task 9 table below).
- Produces: `const std::string& mc::web::statusPageHtml();`

Page per spec + mockup `.superpowers/brainstorm/64184-1792425780/content/layout-v2.html`: top bar "MIDIControl", `location.host`, "updated N s ago"; tabs Overview/Software/Base/Controller; each node tab has Status / Details / Counters / Recent events, in that order, generated by one JS render function per block so all tabs share structure. Light/dark via CSS custom properties and `prefers-color-scheme`. `fetch('/api/status')` every 1000 ms; on failure the top bar says "no connection" with a red dot and the last data stays. Selected tab kept in `location.hash`. All text inserted with `textContent` (never `innerHTML` with data). Details rows for unknown future `fields` keys are not needed now; the greyed "Firmware debug: later" row is static.

- [ ] **Step 1: Write failing test** `web_page_self_contained`: html contains `"<!doctype html>"`, `"/api/status"`, `"Overview"`, `"Software"`, `"Base"`, `"Controller"`, `"prefers-color-scheme"`; does **not** contain `"http://"`, `"https://"`, `"<script src"`, `"<link"`.
- [ ] **Step 2–4:** fail → write page as a raw string literal (`R"HTML(...)HTML"`) → PASS.
- [ ] **Step 5:** `git commit -m "core: embedded status page"`

---

### Task 6: Song-file check and incremental scanner

**Files:**
- Create: `include/midicontrol/songcheck.hpp`, `src/songcheck.cpp`
- Test: `tests/test_songcheck.cpp`

**Interfaces:**
- Produces (namespace `mc::songcheck`):
  ```cpp
  struct FileCheck { std::string file; std::string code; std::vector<std::string> problems; };
  FileCheck checkFile(const std::string& path);   // file = basename, code = stem upper-cased
  class Scanner {
  public:
      struct Result { std::vector<FileCheck> all; std::vector<std::pair<std::string,std::string>> newProblems; }; // (file, problem)
      Result rescan(const std::string& folder);  // only re-parses files whose mtime or size changed; forgets deleted files
  };
  ```
- Problem strings, exactly: `"cannot parse: <ParseError what()>"`, `"no display anchors"` (no valid display SysEx with a verse field anywhere), `"empty display anchor"` (a SysEx event with an empty body or a body that isn't a valid display message, before the first note), `"file name is not a valid song code"` (stem fails `songs::sanitizeCode`, or stem ≠ its sanitized form ignoring case). Files considered: regular files with `.mid` extension, any case.
- A (file, problem) pair is in `newProblems` only the first time it is seen (re-parsing an unchanged-problem file doesn't repeat it).

- [ ] **Step 1: Write failing tests**
  - `songcheck_real_files`: every file in `MC_SONGS_DIR` except `523.mid` has no problems; `523.mid` problems contain `"empty display anchor"`. (If another real file reports a problem, stop and report — it means the rule text needs adjusting, not the test.)
  - `songcheck_bad_name_and_garbage`: in a temp dir (`mkdtemp` under `/tmp`) write `"072 - Lide muj.mid"` with bytes `"garbage"` → problems contain `"file name is not a valid song code"` and one starting `"cannot parse: "`.
  - `songcheck_scanner_reports_once`: temp dir with a garbage `1.mid` → first `rescan` newProblems size 1; second `rescan` size 0; delete file → `all` empty.
- [ ] **Step 2–4:** fail → implement (`stat` for mtime/size, `smf::load`, `sysex::parse`) → PASS.
- [ ] **Step 5:** `git commit -m "core: song-file check and scanner"`

---

### Task 7: `[WEB]` config and console command parsing

**Files:**
- Modify: `include/midicontrol/config.hpp`, `src/config.cpp`, `config.conf`
- Create: `include/midicontrol/console.hpp`, `src/console.cpp`
- Test: `tests/test_config.cpp`, `tests/test_console.cpp` (new)

**Interfaces:**
- Produces in `Config`: `bool webEnabled = true; int webPort = 50118; std::string webBind = "0.0.0.0";` parsed from `[WEB]` `Enabled`, `Port` (1..65535, else `runtime_error`), `Bind` (non-empty string; address validity is checked at bind time).
- Produces (namespace `mc::console`):
  ```cpp
  struct Command {
      enum class Kind { Empty, Play, Stop, Verse, List, Record, On, Off, Status, Update, Quit, Usage, Unknown } kind;
      std::string arg;     // Play: code as typed; Update: path; Usage: usage text; Unknown: whole line
      std::string target;  // Update: "base" | "remote"
  };
  Command parse(const std::string& line);       // same rules as main.cpp interactive() today
  std::string absolutize(const std::string& line, const std::string& cwd);  // update path → absolute; other lines unchanged
  extern const char* const kCommandsHelp;       // the existing kCommands text
  inline constexpr char kEndOfReply = '\x1e';
  std::string endMarker(bool ok);               // "\x1eOK\n" / "\x1eFAIL\n"
  ```
- Parse rules (copied from `main.cpp`): trim spaces/tabs; command word case-insensitive; `verse`|`repeat` → Verse; `quit`|`exit` → Quit; `play` with empty arg → Unknown; `update` with target not base/remote or empty path → Usage (text = current update usage message); blank line → Empty.

- [ ] **Step 1: Write failing tests**
  - `config_web_defaults`: minimal `[FILES]` config → `webEnabled`, 50118, `"0.0.0.0"`.
  - `config_web_values`: `[WEB]\nEnabled=false\nPort=8080\nBind=127.0.0.1` → parsed.
  - `config_web_bad_port`: `Port=0` and `Port=70000` throw.
  - `console_parse`: `"  PLAY 406a "` → Play, arg `"406a"`; `"repeat"` → Verse; `"exit"` → Quit; `"update base"` → Usage; `"update Remote fw-update.bin"` → Update, target `"remote"`, arg `"fw-update.bin"`; `"play"` → Unknown; `""` → Empty.
  - `console_absolutize`: `absolutize("update base fw.bin","/home/a")` == `"update base /home/a/fw.bin"`; absolute path unchanged; `"play 401"` unchanged.
- [ ] **Step 2–4:** fail → implement → PASS. Document `[WEB]` in `config.conf` (commented, with defaults).
- [ ] **Step 5:** `git commit -m "core: [WEB] config and console command parsing"`

---

### Task 8: Logger

**Files:**
- Create: `include/midicontrol/log.hpp`, `src/log.cpp`
- Test: `tests/test_log.cpp`

**Interfaces:**
- Consumes: `StatusRegistry` (Task 2).
- Produces (namespace `mc::log`), global by design (one registry per process):
  ```cpp
  void setRegistry(mc::status::StatusRegistry* r);  // nullptr = console only
  void write(mc::status::Node, mc::status::Level, const std::string& text);
  void debug(Node, const std::string&); void info(Node, const std::string&);
  void warn(Node, const std::string&);  void error(Node, const std::string&);
  ```
  Console line = the text plus `"\n"` (no prefix — the journal adds time; WARN/ERROR prefixed `"warning: "` / `"error: "`). DEBUG never printed. A single mutex serialises console writes.

- [ ] **Step 1: Write failing test** `log_routes_levels`: redirect `std::cout`/`std::cerr` rdbufs to `ostringstream`s; with a registry set, `debug(Base,"d")`, `info(Software,"i")`, `warn(Base,"w")` → cout == `"i\n"`, cerr == `"warning: w\n"`, Base events `d`,`w`, Software events `i`. Restore rdbufs and `setRegistry(nullptr)` at the end.
- [ ] **Step 2–4:** fail → implement → PASS.
- [ ] **Step 5:** `git commit -m "core: logger feeding the status registry"`

---

### Task 9: CommHandler instrumentation and logging migration

**Files:**
- Modify: `include/midicontrol/comm.hpp`, `src/comm.cpp`, `include/midicontrol/firmware_update.hpp`, `src/firmware_update.cpp`, `src/serial_port.cpp`
- Verify: tests + stub compile (Build note)

**Interfaces:**
- Consumes: Tasks 1, 2, 8.
- Produces:
  - `CommHandler(const Config&, MidiPortFinder, mc::status::StatusRegistry& status);`
  - `struct BaseStatus { bool ble, relay, display, pc; };`
    `std::optional<BaseStatus> requestStatus(int timeoutMs = 1500);` — interactive `status`: sends GET_STATUS, blocks until STATUS_RESP or timeout. Replaces `main.cpp`'s `logPacket` STATUS print.
  - `bool threadsAlive(std::chrono::milliseconds maxAge) const;` — control heartbeat always checked; reader and keepalive heartbeats only while `_running`.
  - `FirmwareUpdater(CommHandler&, std::function<void(const std::string&)> say = {})` — every message it printed now goes through `say` (if set) **and** `mc::log` (INFO for progress/success, ERROR for failures, Software node). The progress callback is unchanged.
- `SerialPort::open` sets `ioctl(fd, TIOCEXCL)` after a successful open (failure → close, return false).

Fields/counters written (names are the contract with Tasks 2, 3, 5):

| Node | Fields | Counters |
|---|---|---|
| software | `version`, `build_date`, `started_ms`, `config_path`, `play_folder`, `song_count`, `song_problems`, `playback` ("idle"/"warming up"/"playing"), `song`, `section`, `last_song`, `last_sections` ("Predehra, Sloka 1"), `last_end` ("ended normally"/"stopped"/"stopped (base disconnected)"/"failed: <why>"), `activity` (e.g. "firmware update to base in progress", empty when none) | `songs_ended`, `songs_stopped`, auto `warnings`/`errors` |
| base | `connected`, `alive`, `device`, `vidpid` ("ABCD:1234"), `midi_port_found`, `midi_out`, `keepalive_ack_ms`, `connected_since_ms`, `reconnects`, `organ_power`, `status_ms`, `status_ble`, `status_relay`, `status_display`, `status_pc`, `status_answered` | `tx`, `rx`, `retries`, `gave_up`, `nak_sent`, `nak_received`, `crc_errors`, `duplicates` |
| controller | `last_rx_ms`, `last_cmd`, `last_cmd_reply`, `last_cmd_ms`, `last_tx`, `last_tx_result`, `last_tx_ms` | same packet counters + `cmd.<MSGTYPE>` per received command |

`main.cpp` sets `version`/`build_date`/`started_ms`/`config_path`/`play_folder` (Task 10); CommHandler owns the rest.

Instrumentation points:
- `send`/`sendWithAck`/retransmit in `checkTimeouts`/`sendReliable` writes: `tx` (+`retries` on retransmit) on `nodesForPeer(dest)`; NAK sent → `nak_sent`; a DEBUG event `"TX " + describePacket(pkt)` unless routine. ACK/NAK to REMOTE → `last_cmd_reply` = `"ACK"` / `"NAK <REASON>"`.
- `dispatchPacket`: `rx` on `nodesForPeer(source)`; NAK received → `nak_received`; DEBUG `"RX ..."` unless routine; source REMOTE → `last_rx_ms`, and for non-ACK/NAK types `last_cmd`, `last_cmd_ms`, `cmd.<NAME>`.
- Dispatcher `onDuplicate` → `duplicates` on the source's node. Parser `badFrames()` delta after each read → `crc_errors` on Base.
- Gave up in `checkTimeouts` → `gave_up` + WARN on the dest's node(s) (`"<TYPE> id=N to <node> not acknowledged after 3 retries"`); `last_tx_result="no answer"` for REMOTE. `sendWithAck` to REMOTE sets `last_tx`, `last_tx_ms`, `last_tx_result="pending"`; ACK/NAK in `handleAck` (look up `_pending[pid].dest` before erasing) sets `"ACK"`/`"NAK <REASON>"`.
- Routine = KEEPALIVE, KEEPALIVE_ACK, GET_STATUS/STATUS_RESP of the background poll, ACKs whose `for=` pid is a poll GET_STATUS pid or the pid of a poll STATUS_RESP. Track poll pids in a small set guarded by `_mutex`.
- Keepalive thread: every 5 s while connected, `send` (not `sendWithAck`) GET_STATUS and remember pid/time; no STATUS_RESP within 2 s, or a NAK for it → `status_answered=false`. Every 10 s call `_midiFinder` → `midi_port_found`, `midi_out`. `alive` updated each loop; base-lost transition → WARN Base `"base is not answering keepalives (no reply for 6 s)"`; recovery → INFO `"base is responding again"`.
- STATUS_RESP (`[ble, current, display, pc_alive]`) → `status_*` fields, `status_ms`, `status_answered=true`; if `requestStatus` is waiting, wake it.
- Link events on Base: open/reconnect INFO (`connected`, `device`, `vidpid`, `connected_since_ms`, reconnect → `reconnects`+1), serial lost WARN, waiting INFO.
- Playback: `doPlay` sets `playback` "warming up" then "playing", `song`, `section` on each anchor and appends to the section list; INFO `"play <code>: <n> sections"`. Finish → `songs_ended`, `last_end="ended normally"`; stop → `songs_stopped`, `"stopped"`; SerialLost → `"stopped (base disconnected)"`; `fail` → ERROR `"cannot play <code>: <why>"`, `last_end="failed: <why>"`. Organ on/off → `organ_power`, INFO.
- Heartbeats: `std::atomic<int64_t>` steady-ms stamps per loop; `controlLoop` waits with `wait_for(1 s)` so it ticks while idle.
- Every `std::cout`/`std::cerr` in `comm.cpp` and `firmware_update.cpp` moves to `mc::log` with the right node and level (keep the wording); no direct console writes remain there.

- [ ] **Step 1:** Implement; replace `std::cout`/`std::cerr` (`grep -n "std::c\(out\|err\)" src/comm.cpp src/firmware_update.cpp` → no hits).
- [ ] **Step 2:** Build + run unit tests → PASS, no warnings. Stub compile of `comm.cpp`, `firmware_update.cpp`, `serial_port.cpp` → no output.
- [ ] **Step 3:** `git commit -m "app: instrument CommHandler, route messages through the logger"`

---

### Task 10: Web server thread and song rescan

**Files:**
- Create: `include/midicontrol/web_server.hpp`, `src/web_server.cpp`
- Modify: `CMakeLists.txt` (app sources; `MC_GIT_HASH`/`MC_BUILD_DATE` via `execute_process(git rev-parse --short HEAD)` and `string(TIMESTAMP ... "%Y-%m-%d")` at configure time, `"unknown"` fallback), `src/main.cpp` (minimal wiring: registry, `log::setRegistry`, software fields, start/stop web server)

**Interfaces:**
- Consumes: Tasks 2–6, 8.
- Produces:
  ```cpp
  class WebServer {
  public:
      WebServer(mc::status::StatusRegistry&, std::string bind, int port, std::string playFolder);
      ~WebServer();          // stop()
      void start();          // spawns the thread; never throws
      void stop();
  };
  ```
- Thread loop: if not bound, try `socket/SO_REUSEADDR/bind/listen(8)` every 10 s (ERROR once per failure streak with `strerror`, DEBUG on repeats, INFO `"status page on http://<bind>:<port>/"` on success); `poll` the listen fd with a 1 s timeout; on accept, handle the connection inline: `SO_RCVTIMEO`/`SO_SNDTIMEO` 2 s, read until `parseRequest` is not Incomplete or 3 s total, respond with `route(...)` / `errorResponse(400|413)`, `send(..., MSG_NOSIGNAL)`, close. Every 60 s (and once at start) run `Scanner::rescan(playFolder)` → `setSongs`, `song_count`, `song_problems`, one WARN per new problem `"song <file>: <problem>"`. Whole loop body in `try { } catch (const std::exception&) { log::error } catch (...) { }`.
- Never touches `CommHandler` or the player.

- [ ] **Step 1:** Implement; wire in `main.cpp` when `config.webEnabled`.
- [ ] **Step 2:** Unit tests PASS; stub compile of `web_server.cpp`, `main.cpp` → clean.
- [ ] **Step 3:** Local smoke (no hardware needed): compile a scratch harness in the scratchpad linking `midicontrol_core` + `web_server.cpp` that starts `WebServer` on 127.0.0.1:50119 with the repo `songs/` folder; `curl -s localhost:50119/api/status | python -m json.tool` succeeds and lists the song problem for 523; `curl -s -o /dev/null -w '%{http_code}' localhost:50119/x` → 404; `nc -l 50119` squatter first → ERROR logged, harness keeps running, binds after the squatter exits.
- [ ] **Step 4:** `git commit -m "app: built-in status web server"`

---

### Task 11: Console executor, control socket, client, single instance

**Files:**
- Create: `include/midicontrol/console_exec.hpp`, `src/console_exec.cpp`, `include/midicontrol/control_socket.hpp`, `src/control_socket.cpp`, `include/midicontrol/instance.hpp`, `src/instance.cpp`
- Modify: `src/main.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 7, 9.
- Produces:
  ```cpp
  // In core console.hpp (Task 7 file, added here):
  using Reply = std::function<void(const std::string&)>;
  enum class ExecResult { Ok, Failed, Quit };
  using Executor = std::function<ExecResult(const std::string& line, const Reply& out)>;
  // console_exec: runs one parsed command against the handler; output via `out`.
  class ConsoleExecutor {
  public:
      ConsoleExecutor(CommHandler&, mc::status::StatusRegistry&);
      ExecResult execute(const std::string& line, const Reply& out, const std::string& who);
  };
  // instance: runtime paths and the lock.
  std::string runtimeDir();          // $XDG_RUNTIME_DIR, else /tmp/midicontrol-<uid> (created 0700)
  class InstanceLock { public: bool acquire(const std::string& path); ~InstanceLock(); };
  // control_socket
  class ControlServer {
  public:
      ControlServer(mc::console::Executor exec, std::string socketPath);
      void start(); void stop();     // unlinks a stale socket file before bind, chmod 0600
  };
  // Client: returns nullopt when no service answers on the socket.
  std::optional<int> runClient(const std::string& socketPath, const std::optional<std::string>& oneShot);
  ```
- Executor: same behaviour and wording as today's `interactive()`; Failed for Unknown/Usage, play NAK, `verse` while idle, `status` with no answer, update failure. `status` uses `requestStatus()`. Every non-empty command logs INFO Software `"<who>: <line>"` (`who` = `"console"`). `update` is guarded by a `try_lock` mutex (`"busy: a firmware update is already running"` → Failed), sets `activity` to `"firmware update to <target> in progress"` while running, clears it after; `FirmwareUpdater`'s `say` and progress go to `out`.
- Socket protocol: client sends one line per command; server runs `execute` on that client's thread and streams `out` text; before the end marker it ensures the output ended with `"\n"`; then `console::endMarker(ok)`. Writes use `MSG_NOSIGNAL`; a failed write marks the client dead and further output is discarded while the command runs to completion. At most 4 client threads; a 5th connection gets `"busy: too many clients\n"` + `endMarker(false)` and is closed. `Quit` from a client → just Ok (the client itself handles detaching; it never sends quit).
- Client: interactive → prints the banner and `kCommandsHelp`, prompt `"> "`, `quit`/`exit`/EOF detaches locally; lines go through `console::absolutize(line, getcwd)`. Output is printed as received up to the marker (`\x1e` at line start). One-shot → sends the line, prints, exits 0 for OK / 1 for FAIL or lost connection.
- `main.cpp` flow:
  1. Parse `--config`, `--headless`, new `--cmd "<line>"` (usage text updated).
  2. If `--cmd` given or interactive (not headless): `runClient(runtimeDir()+"/midicontrol.sock", cmd)`; if it returned a code, exit with it. If `--cmd` and no service: run standalone for that one command (lock, start handler, execute, shut down) and exit with its result.
  3. Acquire `InstanceLock` on `runtimeDir()+"/midicontrol.lock"`; on failure: ERROR `"another midicontrol instance is running"` and exit 1.
  4. Load config, registry, logger, handler, web server, control server, then (Task 12) READY.
  5. Headless: wait for SIGINT/SIGTERM. Interactive standalone: local loop calling the same `ConsoleExecutor` with `out` printing to `std::cout`.

- [ ] **Step 1:** Implement executor and move `interactive()` onto it (main shrinks); then instance lock; then control server and client.
- [ ] **Step 2:** Unit tests PASS; stub compile all app sources → clean.
- [ ] **Step 3:** Local socket smoke via a scratch harness (no RtMidi): `ControlServer` with a fake executor (echoes the line, `"fail"` → Failed, `"slow"` → 20 × 100 ms output lines) on a socket in the scratchpad, plus `runClient` in one-shot mode: `ok` → exit 0 and echo printed; `fail` → exit 1; killing the client during `slow` leaves the server serving the next client; socket mode is `srwx------`.
- [ ] **Step 4:** `git commit -m "app: control socket, attach client, single instance"`

---

### Task 12: systemd notify, liveness watchdog, unit file, docs

**Files:**
- Create: `include/midicontrol/sd_notify.hpp`, `src/sd_notify.cpp`
- Modify: `src/main.cpp`, `packaging/midicontrol.service.in`, `CLAUDE.md`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `CommHandler::threadsAlive` (Task 9).
- Produces: `bool mc::sdNotify(const std::string& state);` — `AF_UNIX`/`SOCK_DGRAM` `sendto` `$NOTIFY_SOCKET` (leading `@` → abstract namespace); returns false and does nothing when unset.
- `main.cpp`: after web + control servers start, `sdNotify("READY=1")`. A watchdog thread (started after the signal mask is set): every 5 s, if `handler.threadsAlive(std::chrono::seconds(10))` → `sdNotify("WATCHDOG=1")`; else WARN once per stall `"thread stalled; withholding watchdog"`. Stopped and joined before `handler.shutdown()`; `sdNotify("STOPPING=1")` at shutdown.
- Unit: `Type=notify`, `NotifyAccess=main`, `WatchdogSec=30`, `Restart=always`, `RestartSec=5`, `StartLimitIntervalSec=0` (in `[Unit]`). Header comment: `loginctl enable-linger $USER` is **required**; status page URL; attaching with `midicontrol` / `midicontrol --cmd "..."`.
- `CLAUDE.md`: new core modules (status, status_json, http, web_page, songcheck, console, log), app modules (web_server, control_socket, instance, sd_notify), `[WEB]` section, `--cmd`, socket/lock paths, the service notes, and that port 50118 clashes with the brainstorm companion server.

- [ ] **Step 1:** Implement and edit docs.
- [ ] **Step 2:** Unit tests PASS; stub compile all app sources → clean; `grep -n "Type=notify\|WatchdogSec=30\|Restart=always\|StartLimitIntervalSec=0" packaging/midicontrol.service.in` → 4 hits.
- [ ] **Step 3:** `git commit -m "service: sd_notify readiness and liveness watchdog"`

---

## Bench checklist (manual, deferred — not run in this session)

1. Build on the PC with RtMidi; stop any brainstorm companion server on 50118.
2. `midicontrol --headless`; open `http://<pc>:50118/`; play a song from the remote; watch counters/events on each tab; Problems shows 523's warning.
3. Unplug the base → Base yellow "waiting", Controller grey; replug → green, `reconnects` +1.
4. `kill -STOP <pid>` under systemd → restart within ~30 s (`journalctl --user -u midicontrol`).
5. `nc -l 50118` before start → ERROR logged, retry every 10 s, playback works; stop `nc` → page comes up.
6. `midicontrol` in a terminal attaches; `quit` detaches, service keeps running; `midicontrol --cmd "update base <file>-update.bin"` streams progress and exits 0; kill the client mid-update → update completes.
