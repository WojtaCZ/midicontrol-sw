# Step 3.2 fuzz findings

Harness: `tests/fuzz/` (targets.cpp shared by libFuzzer binaries and the seeded `mc_fuzz_fallback`, ctest `fuzz_fallback`, ~1.3 s).
Build: `cmake -S . -B <dir> -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DMIDICONTROL_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug
-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined,fuzzer-no-link -g -O1 -fno-sanitize-recover=undefined"`; run `<dir>/tests/fuzz/mc_fuzz_<target> <corpus> -max_total_time=100`.
Crash repros go to `tests/fuzz/regress/<target>/` (replayed by the fallback). Corpora are kept outside git.

Run 2026-11-15, clang 23.1.4, ASan+UBSan+leak, 100 s each, max_len 4200 (diff/decode/parser re-run 100 s at 300 with CRC-fixup):

| target | executions | result |
|---|---|---|
| parser_feed (PC Parser::feed + round trip) | 2.26 M, +9.7 M | clean |
| decode_frame (+ valid-frame round trip) | 20.5 M, +14.5 M | clean |
| parse_request (default and 64-byte limit) | 16.0 M | clean |
| smf_parse | 2.08 M | clean |
| parse_config | 0.98 M | clean |
| parse_log_line | 29.5 M | clean |
| sanitize_code (result must satisfy validCode) | 13.7 M | clean |
| decode_record (decode/encode identity) | 29.9 M | clean |
| sysex_parse (bonus) | 33.6 M | clean |
| stops_parse (bonus) | 0.94 M | clean |
| fw_differential (PC vs midicontrol-fw/lib/protocol: decodeFrame, encode bytes, streaming parser) | 1.78 M, +1.35 M | clean, no divergence |

No crashes, UB, leaks, timeouts or PC/firmware divergences: **no findings (no F3-n)**.

Notes / limits
- Exceptions (std::exception) from smf::parse, parseConfig, stops::parse are treated as the documented error path.
- The firmware codec is C++ in namespace `protocol` (not C); it is compiled from the main checkout path
  `midicontrol-fw/lib/protocol` (override with `-DMIDICONTROL_FW_PROTOCOL_DIR=`); the target is skipped if absent.
- Not covered: firmware-side parsers other than the protocol lib (menu, bootloader), HTTP route handlers, comm.cpp (needs ports).

## Questions
- Q3-F1: the top-level CMakeLists adds `add_subdirectory(tests/fuzz)` unconditionally (fallback test always built, ~3 small TUs);
  libFuzzer binaries only with MIDICONTROL_FUZZ=ON. Provisional: keep unconditional so the normal suite keeps the targets alive.
