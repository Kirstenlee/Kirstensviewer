include_guard()

include(Apr)
include(Boost)
include(Expat)
include(Glm)
include(xxHash)
include(ZLib)

set(LLCOMMON_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llcommon
    ${APRUTIL_INCLUDE_DIR}
    ${APR_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
    )

set(LLCOMMON_LIBRARIES llcommon
    ${BOOST_FIBER_LIBRARY} 
    ${BOOST_CONTEXT_LIBRARY} 
    ${BOOST_THREAD_LIBRARY} 
    ${BOOST_SYSTEM_LIBRARY} )


if(LLCOMMON_LINK_SHARED)
    add_definitions(-DLL_COMMON_LINK_SHARED=1)
endif()
