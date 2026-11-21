cmake_minimum_required(VERSION 3.16)
project(midicontrol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# ---------------------------------------------------------------------------
# Core library: pure logic with NO hardware dependencies. Unit-testable
# anywhere. (protocol, SysEx codec, SMF reader, verse scan, config loader,
# indicator encoding, song library, playback sequencing, packet dispatch)
# ---------------------------------------------------------------------------
add_library(midicontrol_core
    src/protocol/crc8.cpp
    src/protocol/codec.cpp
    src/protocol/parser.cpp
    src/sysex.cpp
    src/smf.cpp
    src/verse.cpp
    src/config.cpp
    src/organ.cpp
    src/stops_file.cpp
    src/display.cpp
    src/songs.cpp
    src/playback.cpp
    src/dispatch.cpp
    src/firmware_image.cpp
    src/firmware_upload.cpp
    src/protocol/names.cpp
    src/status.cpp
    src/status_json.cpp
    src/http.cpp
    src/web_page.cpp
    src/songcheck.cpp
    src/console.cpp
    src/log.cpp
    src/process.cpp
    src/log_line.cpp
    src/fault_decode.cpp
    src/diag_codes.cpp
    src/diag_store.cpp
    src/serial_port.cpp
    src/log_reader.cpp
    src/diag_poller.cpp
    src/diag_api.cpp
    src/device_info.cpp
    src/startup_reset.cpp
    src/recording.cpp
)
find_package(Threads REQUIRED)
target_include_directories(midicontrol_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(midicontrol_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Tests: link only the core library, so they build and run without hardware.
# ---------------------------------------------------------------------------
enable_testing()
add_executable(midicontrol_tests
    tests/test_main.cpp
    tests/test_crc8.cpp
    tests/test_codec.cpp
    tests/test_parser.cpp
    tests/test_sysex.cpp
    tests/test_smf.cpp
    tests/test_config.cpp
    tests/test_organ.cpp
    tests/test_stops_file.cpp
    tests/test_display.cpp
    tests/test_songs.cpp
    tests/test_playback.cpp
    tests/test_dispatch.cpp
    tests/test_device_info.cpp
    tests/test_firmware_image.cpp
    tests/test_firmware_upload.cpp
    tests/test_names.cpp
    tests/test_status.cpp
    tests/test_status_json.cpp
    tests/test_http.cpp
    tests/test_web_page.cpp
    tests/test_songcheck.cpp
    tests/test_console.cpp
    tests/test_log.cpp
    tests/test_process.cpp
    tests/test_log_line.cpp
    tests/test_fault_decode.cpp
    tests/test_diag_codes.cpp
    tests/test_diag_store.cpp
    tests/test_port_select.cpp
    tests/test_serial_lock.cpp
    tests/test_log_reader.cpp
    tests/test_diag_poller.cpp
    tests/test_diag_api.cpp
    tests/test_stall_monitor.cpp
    tests/test_startup_reset.cpp
    tests/test_recording.cpp
    tests/test_songcode.cpp
)
target_link_libraries(midicontrol_tests PRIVATE midicontrol_core)
target_include_directories(midicontrol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(midicontrol_tests
    PRIVATE MC_SONGS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/songs")
add_test(NAME unit COMMAND midicontrol_tests)
add_test(NAME sync_system_doc COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/sync_system_doc_test.sh)

# ---------------------------------------------------------------------------
# Application: needs RtMidi (playback) + pthreads + the serial layer. Built
# only when RtMidi is available so the core/tests still build everywhere.
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

find_package(PkgConfig QUIET)
set(RTMIDI_FOUND FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(RTMIDI rtmidi)
    if(RTMIDI_FOUND)
        message(STATUS "RtMidi found via pkg-config (${RTMIDI_VERSION}) - building app target")
    endif()
endif()

if(RTMIDI_FOUND)
    add_executable(midicontrol
        src/main.cpp
        src/comm.cpp
        src/firmware_update.cpp
        src/player.cpp
        src/midi_ports.cpp
        src/web_server.cpp
        src/console_exec.cpp
        src/control_socket.cpp
        src/instance.cpp
        src/sd_notify.cpp
    )
    # Version shown on the status page: git hash and build date at configure time.
    # MC_GIT_HASH can be given on the command line (-DMC_GIT_HASH=..., used by the Docker build, no .git there).
    if(NOT MC_GIT_HASH)
    execute_process(COMMAND git rev-parse --short HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE MC_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE MC_GIT_RESULT)
    if(NOT MC_GIT_RESULT EQUAL 0 OR MC_GIT_HASH STREQUAL "")
        set(MC_GIT_HASH "unknown")
    endif()
    endif()
    string(TIMESTAMP MC_BUILD_DATE "%Y-%m-%d")
    target_compile_definitions(midicontrol PRIVATE
        MC_GIT_HASH="${MC_GIT_HASH}" MC_BUILD_DATE="${MC_BUILD_DATE}")
    target_link_libraries(midicontrol PRIVATE midicontrol_core Threads::Threads ${RTMIDI_LIBRARIES})
    target_include_directories(midicontrol PRIVATE ${RTMIDI_INCLUDE_DIRS})
    target_compile_options(midicontrol PRIVATE ${RTMIDI_CFLAGS_OTHER})
    if(RTMIDI_LIBRARY_DIRS)
        target_link_directories(midicontrol PRIVATE ${RTMIDI_LIBRARY_DIRS})
    endif()

    # CommHandler tests: real comm/player threads against a fake base on a pty.
    add_executable(midicontrol_comm_tests
        tests/test_main.cpp
        tests/test_comm_organ.cpp
        src/comm.cpp
        src/console_exec.cpp
        src/firmware_update.cpp
        src/player.cpp
        src/midi_ports.cpp
    )
    target_link_libraries(midicontrol_comm_tests PRIVATE midicontrol_core Threads::Threads
                          ${RTMIDI_LIBRARIES} util)
    target_include_directories(midicontrol_comm_tests PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/tests ${RTMIDI_INCLUDE_DIRS})
    target_compile_definitions(midicontrol_comm_tests
        PRIVATE MC_SONGS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/songs")
    target_compile_options(midicontrol_comm_tests PRIVATE ${RTMIDI_CFLAGS_OTHER})
    if(RTMIDI_LIBRARY_DIRS)
        target_link_directories(midicontrol_comm_tests PRIVATE ${RTMIDI_LIBRARY_DIRS})
    endif()
    add_test(NAME comm COMMAND midicontrol_comm_tests)

    # ---- Install: binary, example config, systemd user unit ----
    # Developer use only: production runs from the Docker image (docs/deployment.md).
    include(GNUInstallDirs)
    install(TARGETS midicontrol RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES config.conf
            DESTINATION ${CMAKE_INSTALL_DATADIR}/midicontrol
            RENAME config.conf.example)
    configure_file(packaging/midicontrol.service.in
                   ${CMAKE_CURRENT_BINARY_DIR}/midicontrol.service @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/midicontrol.service
            DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
else()
    message(WARNING "RtMidi not found - skipping the 'midicontrol' app target "
                    "(core library and tests will still build).")
endif()

# Fuzz targets + seeded fallback (tests/fuzz); libFuzzer binaries only with -DMIDICONTROL_FUZZ=ON.
add_subdirectory(tests/fuzz)
