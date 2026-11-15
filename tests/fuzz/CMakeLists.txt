# Fuzzing (audit step 3.2). Included from the top-level CMakeLists.
#  * mc_fuzz_fallback + ctest "fuzz_fallback": seeded-random driver, always built, runs in seconds.
#  * libFuzzer binaries (mc_fuzz_<target>): -DMIDICONTROL_FUZZ=ON with clang. Configure a separate
#    build dir with  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined,fuzzer-no-link -g -O1"
#  * fw_differential: needs the firmware protocol lib; MIDICONTROL_FW_PROTOCOL_DIR (default: sibling
#    midicontrol-fw/lib/protocol). Skipped when absent.
set(MC_FUZZ_FW_DIR "${MIDICONTROL_FW_PROTOCOL_DIR}")
if(NOT MC_FUZZ_FW_DIR)
    foreach(_up .. ../..)
        get_filename_component(_mc_ws "${CMAKE_SOURCE_DIR}/${_up}" ABSOLUTE)
        if(EXISTS "${_mc_ws}/midicontrol-fw/lib/protocol/src/codec.cpp")
            set(MC_FUZZ_FW_DIR "${_mc_ws}/midicontrol-fw/lib/protocol")
        endif()
    endforeach()
endif()

set(MC_FUZZ_SRC targets.cpp)
set(MC_FUZZ_DEFS)
set(MC_FUZZ_HAVE_FW FALSE)
if(EXISTS "${MC_FUZZ_FW_DIR}/src/codec.cpp" AND EXISTS "${MC_FUZZ_FW_DIR}/inc/protocol/codec.hpp")
    set(MC_FUZZ_HAVE_FW TRUE)
    set(MC_FUZZ_DEFS MC_FUZZ_FW_CODEC)
    # Firmware sources compiled into a static lib; only fw_codec.cpp sees its headers.
    add_library(mc_fuzz_fwproto STATIC
        ${MC_FUZZ_FW_DIR}/src/codec.cpp ${MC_FUZZ_FW_DIR}/src/crc8.cpp ${MC_FUZZ_FW_DIR}/src/parser.cpp
        fw_codec.cpp)
    target_include_directories(mc_fuzz_fwproto PRIVATE ${MC_FUZZ_FW_DIR}/inc)
    target_include_directories(mc_fuzz_fwproto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    list(APPEND MC_FUZZ_SRC differential.cpp)
else()
    message(STATUS "fuzz: firmware protocol lib not found (${MC_FUZZ_FW_DIR}); differential target skipped")
endif()

add_library(mc_fuzz_targets STATIC ${MC_FUZZ_SRC})
target_link_libraries(mc_fuzz_targets PUBLIC midicontrol_core)
target_include_directories(mc_fuzz_targets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mc_fuzz_targets PUBLIC ${MC_FUZZ_DEFS})
if(MC_FUZZ_HAVE_FW)
    target_link_libraries(mc_fuzz_targets PUBLIC mc_fuzz_fwproto)
endif()

add_executable(mc_fuzz_fallback fallback_main.cpp)
target_link_libraries(mc_fuzz_fallback PRIVATE mc_fuzz_targets)
target_compile_definitions(mc_fuzz_fallback PRIVATE MC_FUZZ_REGRESS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/regress")
add_test(NAME fuzz_fallback COMMAND mc_fuzz_fallback 20000 1)

if(MIDICONTROL_FUZZ AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(_names parser_feed decode_frame parse_request smf_parse parse_config parse_log_line
               sanitize_code decode_record sysex_parse stops_parse)
    if(MC_FUZZ_HAVE_FW)
        list(APPEND _names fw_differential)
    endif()
    foreach(n ${_names})
        add_executable(mc_fuzz_${n} libfuzzer_main.cpp)
        target_compile_definitions(mc_fuzz_${n} PRIVATE MC_FUZZ_NAME="${n}")
        target_link_libraries(mc_fuzz_${n} PRIVATE mc_fuzz_targets)
        target_link_options(mc_fuzz_${n} PRIVATE -fsanitize=fuzzer)
    endforeach()
endif()
