project(SLPlugin)

include(00-Common)
include(LLCommon)
include(LLPlugin)
include(Linking)
include(PluginAPI)
include(LLMessage)

include_directories(
    ${LLPLUGIN_INCLUDE_DIRS}
    ${LLMESSAGE_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
)

include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
)

# [subsystem:sources]
set(SLPlugin_SOURCE_FILES
    slplugin.cpp
)

# [subsystem:headers]
set_source_files_properties(${SLPlugin_HEADER_FILES}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

if (SLPlugin_HEADER_FILES)
    list(APPEND SLPlugin_SOURCE_FILES ${SLPlugin_HEADER_FILES})
endif ()

# [subsystem:resource_file]
if (WIN32)
    list(APPEND SLPlugin_SOURCE_FILES slplugin.rc)
    set_source_files_properties(slplugin.rc PROPERTIES LANGUAGE RC)
endif ()

# [subsystem:executable]
add_executable(SLPlugin WIN32 ${SLPlugin_SOURCE_FILES})

# [subsystem:linking]
if (WINDOWS)
    set_target_properties(SLPlugin PROPERTIES
        LINK_FLAGS_DEBUG "/NODEFAULTLIB:\"LIBCMTD\""
        LINK_FLAGS_RELEASE "/NODEFAULTLIB:\"LIBCMT\""
        LINK_FLAGS_RELWITHDEBINFO "/NODEFAULTLIB:\"LIBCMT\""
    )
endif ()

target_link_libraries(SLPlugin
    ${LLPLUGIN_LIBRARIES}
    ${LLMESSAGE_LIBRARIES}
    ${LLCOMMON_LIBRARIES}
    ${PLUGIN_API_WINDOWS_LIBRARIES}
    shell32
    rpcrt4
    advapi32
)

# [subsystem:signing_hook]
# Optional post-build signing step (disabled by default)
# add_custom_command(TARGET SLPlugin POST_BUILD
#     COMMAND signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 $<TARGET_FILE:SLPlugin>
#     COMMENT "Signing SLPlugin binary"
# )
