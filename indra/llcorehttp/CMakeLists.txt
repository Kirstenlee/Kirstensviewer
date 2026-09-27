# -*- cmake -*-

# S24 :: websocketpp/ (header-only, vendored directly -- dropped from
# vcpkg's registry, locally patched for Boost 1.92's io_context API) sits
# under this directory and is already on the include path below via
# CMAKE_CURRENT_SOURCE_DIR. See websocketpp/S24_VENDORED_FROM.txt.
# Consumed by llwebsocketmgr.cpp/lljsonrpcws.cpp below (Phase 5, SLua/Luau
# external editor bridge - see project_slua_luau_introduction_2026_09_20.md).
# _WEBSOCKETPP_CPP11_STL_ is required: MSVC's __cplusplus macro reports the
# legacy pre-C++11 value unless /Zc:__cplusplus is passed (not set anywhere
# in this tree), which websocketpp's own C++11-detection depends on.

project(llcorehttp)

include(00-Common)
include(CURL)
include(OpenSSL)
include(NGHTTP2)
include(ZLIB)
include(LLCoreHttp)
include(LLMessage)
include(LLCommon)
include(Tut)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
    ${LLMESSAGE_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLCOREHTTP_INCLUDE_DIRS}
    )

set(llcorehttp_SOURCE_FILES
    bufferarray.cpp
    bufferstream.cpp
    httpcommon.cpp
    llhttpconstants.cpp
    httpheaders.cpp
    httpoptions.cpp
    httprequest.cpp
    httpresponse.cpp
    httpstats.cpp
    lljsonrpcws.cpp
    llwebsocketmgr.cpp
    _httplibcurl.cpp
    _httpopcancel.cpp
    _httpoperation.cpp
    _httpoprequest.cpp
    _httpopsetget.cpp
    _httpopsetpriority.cpp
    _httppolicy.cpp
    _httppolicyclass.cpp
    _httppolicyglobal.cpp
    _httpreplyqueue.cpp
    _httprequestqueue.cpp
    _httpservice.cpp
    _refcounted.cpp
    )

set(llcorehttp_HEADER_FILES
    CMakeLists.txt

    bufferarray.h
    bufferstream.h
    httpcommon.h
    llhttpconstants.h
    httphandler.h
    httpheaders.h
    httpoptions.h
    httprequest.h
    httpresponse.h
    httpstats.h
    lljsonrpcws.h
    llwebsocketmgr.h
    _httpinternal.h
    _httplibcurl.h
    _httpopcancel.h
    _httpoperation.h
    _httpoprequest.h
    _httpopsetget.h
    _httpopsetpriority.h
    _httppolicy.h
    _httppolicyclass.h
    _httppolicyglobal.h
    _httpreadyqueue.h
    _httpreplyqueue.h
    _httprequestqueue.h
    _httpservice.h
    _mutex.h
    _refcounted.h
    _thread.h
    )

set_source_files_properties(${llcorehttp_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llcorehttp_SOURCE_FILES ${llcorehttp_HEADER_FILES})

add_library (llcorehttp ${llcorehttp_SOURCE_FILES})
target_compile_definitions(llcorehttp PRIVATE _WEBSOCKETPP_CPP11_STL_)
target_link_libraries(
  llcorehttp
  ${CURL_LIBRARIES}
  ${OPENSSL_LIBRARIES}
  ${NGHTTP2_LIBRARIES}
  ${BOOST_THREAD_LIBRARY}
  ${BOOST_SYSTEM_LIBRARY}
  )

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "Build type.  One of: Release Release Release" FORCE)

