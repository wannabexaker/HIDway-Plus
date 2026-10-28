add_executable(hidway_tests
    test_common.c
    ${HIDWAY_COMMON}/hidway_kbd.c
    ${HIDWAY_COMMON}/hidway_motion.c
    ${HIDWAY_COMMON}/hidway_input_source.c
    ${HIDWAY_COMMON}/hidway_click_queue.c
    ${HIDWAY_COMMON}/keymap_sc2hid.c
    ${HIDWAY_COMMON}/crc16.c
    ${HIDWAY_COMMON}/cobs.c
    ${HIDWAY_COMMON}/serial.c
    ${HIDWAY_COMMON}/hidway_crypto.c
)

target_include_directories(hidway_tests PRIVATE ${HIDWAY_COMMON})
target_link_libraries(hidway_tests PRIVATE hidway_monocypher)

if (MSVC)
    target_compile_options(hidway_tests PRIVATE /W4)
else ()
    target_compile_options(hidway_tests PRIVATE -Wall -Wextra)
endif ()

add_test(NAME hidway_common COMMAND hidway_tests)
add_executable(hidway_input_tests test_input_state.c ${HIDWAY_COMMON}/hidway_input_state.c)
target_include_directories(hidway_input_tests PRIVATE ${HIDWAY_COMMON})
if(MSVC)
    target_compile_options(hidway_input_tests PRIVATE /W4 /WX)
else()
    target_compile_options(hidway_input_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME hidway_input COMMAND hidway_input_tests)

# Host-test the production output stage; only SDK headers are mocked.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ../firmware/src/hid_out.c)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../firmware/src/hid_out.c" HID_OUTPUT)
string(REPLACE "#include \"pico/time.h\"" "#include \"pico_mock.h\"" HID_OUTPUT "${HID_OUTPUT}")
string(REPLACE "#include \"tusb.h\"" "" HID_OUTPUT "${HID_OUTPUT}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hid_out_host.c" CONTENT "${HID_OUTPUT}" @ONLY)
add_executable(hidway_firmware_tests firmware/test_hid_out.c "${CMAKE_CURRENT_BINARY_DIR}/hid_out_host.c"
    ${HIDWAY_COMMON}/hidway_kbd.c ${HIDWAY_COMMON}/hidway_motion.c)
target_include_directories(hidway_firmware_tests PRIVATE firmware ../firmware/src ${HIDWAY_COMMON})
if(MSVC)
    target_compile_options(hidway_firmware_tests PRIVATE /W4 /WX)
else()
    target_compile_options(hidway_firmware_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME hidway_firmware COMMAND hidway_firmware_tests)
