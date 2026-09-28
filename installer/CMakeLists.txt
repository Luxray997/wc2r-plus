set(PAYLOAD_HEADER ${CMAKE_BINARY_DIR}/generated/payload.h)
add_custom_command(
    OUTPUT ${PAYLOAD_HEADER}
    COMMAND ${CMAKE_COMMAND} -DIN=$<TARGET_FILE:version_proxy> -DOUT=${PAYLOAD_HEADER}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
    DEPENDS version_proxy $<TARGET_FILE:version_proxy> ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
    COMMENT "Embedding version.dll into the installer"
    VERBATIM
)

configure_file(installer.rc.in ${CMAKE_BINARY_DIR}/generated/installer.rc @ONLY)

add_executable(installer WIN32
    installer.cpp
    ${PAYLOAD_HEADER}
    ${CMAKE_BINARY_DIR}/generated/installer.rc
)
set_target_properties(installer PROPERTIES
    OUTPUT_NAME "wc2r-plus-setup-${PROJECT_VERSION}"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
target_compile_definitions(installer PRIVATE UNICODE _UNICODE)
target_include_directories(installer PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(installer PRIVATE comctl32 shell32 ole32 version bcrypt delayimp)
target_link_options(installer PRIVATE /MANIFEST:NO)
target_link_options(installer PRIVATE /DEPENDENTLOADFLAG:0x800 /DELAYLOAD:version.dll
                    /DELAYLOAD:bcrypt.dll)
