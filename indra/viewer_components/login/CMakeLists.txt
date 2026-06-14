# -*- cmake -*-

project(login)

include(00-Common)
include(LLCommon)
include(LLMath)
include(LLXML)
include(LLCoreHttp)
include(LLMessage)

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    )
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLXML_SYSTEM_INCLUDE_DIRS}
    )

set(login_SOURCE_FILES
    lllogin.cpp
    )

set(login_HEADER_FILES
    lllogin.h
    )

set_source_files_properties(${login_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND 
    login_SOURCE_FILES 
    ${login_HEADER_FILES} 
    )

add_library(lllogin 
            ${login_SOURCE_FILES}
            )

target_link_libraries(lllogin
    ${LLMESSAGE_LIBRARIES}
    ${LLCOREHTTP_LIBRARIES}
    ${LLCOMMON_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLXML_LIBRARIES}
    )
    
