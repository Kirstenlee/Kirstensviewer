# -*- cmake -*-
# Construct the viewer version from indra/newview/VIEWER_VERSION.txt and the SVN working copy.
#
# VIEWER_VERSION.txt, one value per line:
#   1: YEAR.MONTH.PATCH   e.g. 26.10.1   (hand-edited)
#   2: release label      e.g. Alpha 1.31
#   3: codename           e.g. Hradr
#
# Full version: YEAR.MONTH.BUILD.PATCH, e.g. 26.10.4105.1
#   BUILD is the highest SVN revision in the working copy (automatic).
#   PATCH is the third number in VIEWER_VERSION.txt.
#
# The environment variable "revision" overrides the SVN revision, for builds outside
# a working copy.
#
# LLVersionInfo maps these onto its fields as MAJOR.MINOR.PATCH.BUILD, so the
# PATCH field carries the build number and the BUILD field carries the patch level.

if (NOT DEFINED VIEWER_SHORT_VERSION) # will be true in indra/, false in indra/newview/
    set(VIEWER_VERSION_BASE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/newview/VIEWER_VERSION.txt")

    if (NOT EXISTS ${VIEWER_VERSION_BASE_FILE})
        message(SEND_ERROR "Cannot get viewer version from '${VIEWER_VERSION_BASE_FILE}'")
    endif (NOT EXISTS ${VIEWER_VERSION_BASE_FILE})

    file(STRINGS ${VIEWER_VERSION_BASE_FILE} VIEWER_VERSION_LINES)
    list(LENGTH VIEWER_VERSION_LINES VIEWER_VERSION_LINE_COUNT)
    if (VIEWER_VERSION_LINE_COUNT LESS 3)
        message(SEND_ERROR "'${VIEWER_VERSION_BASE_FILE}' needs three lines: version, label, codename")
    endif (VIEWER_VERSION_LINE_COUNT LESS 3)

    list(GET VIEWER_VERSION_LINES 0 VIEWER_VERSION_LINE)
    list(GET VIEWER_VERSION_LINES 1 VIEWER_LABEL)
    list(GET VIEWER_VERSION_LINES 2 VIEWER_CODENAME)

    if (NOT VIEWER_VERSION_LINE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
        message(SEND_ERROR "First line of '${VIEWER_VERSION_BASE_FILE}' must be YEAR.MONTH.PATCH, got '${VIEWER_VERSION_LINE}'")
    endif ()
    string(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+.*" "\\1" VIEWER_VERSION_MAJOR "${VIEWER_VERSION_LINE}")
    string(REGEX REPLACE "^[0-9]+\\.([0-9]+)\\.[0-9]+.*" "\\1" VIEWER_VERSION_MINOR "${VIEWER_VERSION_LINE}")
    string(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" VIEWER_VERSION_PATCH "${VIEWER_VERSION_LINE}")

    # Build number: the environment override first, then the working copy. svnversion
    # prints "4095:4105" for a mixed working copy, so the highest number after ':' wins.
    if (DEFINED ENV{revision})
        set(VIEWER_VERSION_REVISION "$ENV{revision}")
        message(STATUS "Revision (from environment): ${VIEWER_VERSION_REVISION}")
    else (DEFINED ENV{revision})
        find_program(VIEWER_SVNVERSION_EXECUTABLE NAMES svnversion
                     HINTS "C:/Program Files/TortoiseSVN/bin" "C:/Program Files/SlikSvn/bin")
        if (VIEWER_SVNVERSION_EXECUTABLE)
            execute_process(COMMAND "${VIEWER_SVNVERSION_EXECUTABLE}" -n "${CMAKE_CURRENT_SOURCE_DIR}"
                            OUTPUT_VARIABLE VIEWER_SVN_RAW
                            RESULT_VARIABLE VIEWER_SVN_RESULT
                            ERROR_QUIET
                            OUTPUT_STRIP_TRAILING_WHITESPACE)
            if (VIEWER_SVN_RESULT EQUAL 0)
                string(REGEX REPLACE "^.*:" "" VIEWER_VERSION_REVISION "${VIEWER_SVN_RAW}")
                string(REGEX REPLACE "[^0-9]" "" VIEWER_VERSION_REVISION "${VIEWER_VERSION_REVISION}")
            endif (VIEWER_SVN_RESULT EQUAL 0)
        endif (VIEWER_SVNVERSION_EXECUTABLE)
    endif (DEFINED ENV{revision})

    if ("${VIEWER_VERSION_REVISION}" STREQUAL "")
        message(STATUS "Revision not available from environment or SVN: will use 0")
        set(VIEWER_VERSION_REVISION 0)
    endif ("${VIEWER_VERSION_REVISION}" STREQUAL "")

    set(VIEWER_VERSION_HOTFIX "${VIEWER_VERSION_PATCH}")
    set(VIEWER_SHORT_VERSION "${VIEWER_VERSION_MAJOR}.${VIEWER_VERSION_MINOR}.${VIEWER_VERSION_REVISION}")
    set(VIEWER_FULL_VERSION "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_HOTFIX}")
    set(VIEWER_RC_VERSION "${VIEWER_VERSION_MAJOR},${VIEWER_VERSION_MINOR},${VIEWER_VERSION_REVISION},${VIEWER_VERSION_HOTFIX}")
    message(STATUS "Building '${VIEWER_CHANNEL}' ${VIEWER_LABEL} (${VIEWER_CODENAME}), version ${VIEWER_FULL_VERSION}")

    # Re-run configure when the version file or the working copy changes, so the generated
    # version resources follow each commit.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VIEWER_VERSION_BASE_FILE}")
    find_program(VIEWER_SVN_EXECUTABLE NAMES svn
                 HINTS "C:/Program Files/TortoiseSVN/bin" "C:/Program Files/SlikSvn/bin")
    if (VIEWER_SVN_EXECUTABLE)
        execute_process(COMMAND "${VIEWER_SVN_EXECUTABLE}" info --show-item wc-root "${CMAKE_CURRENT_SOURCE_DIR}"
                        OUTPUT_VARIABLE VIEWER_WC_ROOT
                        ERROR_QUIET
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (VIEWER_WC_ROOT AND EXISTS "${VIEWER_WC_ROOT}/.svn/wc.db")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VIEWER_WC_ROOT}/.svn/wc.db")
        endif ()
    endif (VIEWER_SVN_EXECUTABLE)

    # Generated installer defines, so the Inno Setup script uses the same values.
    file(WRITE "${CMAKE_BINARY_DIR}/viewer_version.iss"
        "#define MyAppVersion \"${VIEWER_FULL_VERSION}\"\n"
        "#define MyAppBuild \"${VIEWER_VERSION_REVISION}\"\n"
        "#define MyAppLabel \"${VIEWER_LABEL}\"\n"
        "#define MyAppCodename \"${VIEWER_CODENAME}\"\n")

    set(VIEWER_CHANNEL_VERSION_DEFINES
        "LL_VIEWER_CHANNEL=\"${VIEWER_CHANNEL}\""
        "LL_VIEWER_VERSION_MAJOR=${VIEWER_VERSION_MAJOR}"
        "LL_VIEWER_VERSION_MINOR=${VIEWER_VERSION_MINOR}"
        "LL_VIEWER_VERSION_PATCH=${VIEWER_VERSION_REVISION}"
        "LL_VIEWER_VERSION_BUILD=${VIEWER_VERSION_HOTFIX}"
        "LL_VIEWER_LABEL=\"${VIEWER_LABEL}\""
        "LL_VIEWER_CODENAME=\"${VIEWER_CODENAME}\""
        "LLBUILD_CONFIG=\"${CMAKE_BUILD_TYPE}\""
        )
endif (NOT DEFINED VIEWER_SHORT_VERSION)
