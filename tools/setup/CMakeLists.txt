add_executable(HIDway-Setup WIN32 setup.c setup.rc)
target_compile_definitions(HIDway-Setup PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(HIDway-Setup PRIVATE user32 shell32)
foreach(script hidway-setup-gui.ps1 hidway-setup-core.ps1 hidway-uninstall.ps1 hidway-pi-setup.ps1 hidway-updates.ps1 hidway-update-core.ps1 hidway-updater.psm1)
    configure_file(${script} ${script} COPYONLY)
endforeach()
configure_file(../../LICENSE LICENSE COPYONLY)
add_custom_target(hidway_setup_payload ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/hidway-setup-gui.ps1
        ${CMAKE_CURRENT_SOURCE_DIR}/hidway-setup-core.ps1
        ${CMAKE_CURRENT_SOURCE_DIR}/hidway-uninstall.ps1
        ${CMAKE_CURRENT_SOURCE_DIR}/hidway-pi-setup.ps1
        ${CMAKE_CURRENT_SOURCE_DIR}/hidway-updates.ps1
        ${CMAKE_CURRENT_SOURCE_DIR}/hidway-update-core.ps1
        ${CMAKE_CURRENT_SOURCE_DIR}/hidway-updater.psm1
        ${CMAKE_SOURCE_DIR}/LICENSE "$<TARGET_FILE_DIR:HIDway-Setup>"
    DEPENDS HIDway-Setup VERBATIM)
add_test(NAME hidway_setup_bootstrap COMMAND HIDway-Setup --smoke-test)
# The launcher ends a hung wizard itself after 120 s; CTest's limit sits above that.
set_tests_properties(hidway_setup_bootstrap PROPERTIES TIMEOUT 180)
add_test(NAME hidway_setup_windows COMMAND "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
    -NoProfile -STA -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/test_setup_win.ps1"
    -Work "${CMAKE_CURRENT_BINARY_DIR}/setup-tests")
add_test(NAME hidway_updates COMMAND "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
    -NoProfile -STA -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/tests/test_updates_win.ps1"
    -Work "${CMAKE_CURRENT_BINARY_DIR}/update-tests")
if(MSVC)
    target_compile_options(HIDway-Setup PRIVATE /W4 /WX /utf-8)
    target_link_options(HIDway-Setup PRIVATE /MANIFEST:NO)
    set_property(TARGET HIDway-Setup PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
endif()
