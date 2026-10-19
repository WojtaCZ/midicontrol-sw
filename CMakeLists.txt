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
    src/display.cpp
    src/songs.cpp
    src/playback.cpp
    src/dispatch.cpp
    src/firmware_image.cpp
    src/protocol/names.cpp
    src/status.cpp
    src/status_json.cpp
    src/http.cpp
    src/web_page.cpp
    src/songcheck.cpp
    src/console.cpp
    src/log.cpp
    src/process.cpp
)
target_include_directories(midicontrol_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
    tests/test_display.cpp
    tests/test_songs.cpp
    tests/test_playback.cpp
    tests/test_dispatch.cpp
    tests/test_firmware_image.cpp
    tests/test_names.cpp
    tests/test_status.cpp
    tests/test_status_json.cpp
    tests/test_http.cpp
    tests/test_web_page.cpp
    tests/test_songcheck.cpp
    tests/test_console.cpp
    tests/test_log.cpp
    tests/test_process.cpp
)
target_link_libraries(midicontrol_tests PRIVATE midicontrol_core)
target_include_directories(midicontrol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(midicontrol_tests
    PRIVATE MC_SONGS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/songs")
add_test(NAME unit COMMAND midicontrol_tests)

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
        src/serial_port.cpp
        src/web_server.cpp
        src/console_exec.cpp
        src/control_socket.cpp
        src/instance.cpp
        src/sd_notify.cpp
    )
    # Version shown on the status page: git hash and build date at configure time.
    execute_process(COMMAND git rev-parse --short HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE MC_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE MC_GIT_RESULT)
    if(NOT MC_GIT_RESULT EQUAL 0 OR MC_GIT_HASH STREQUAL "")
        set(MC_GIT_HASH "unknown")
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

    # ---- Install: binary, example config, systemd user unit ----
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
