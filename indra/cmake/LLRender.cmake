include_guard()

include(Variables)
include(FreeType)
include(Glh)

set(LLRENDER_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llrender
    ${GLH_INCLUDE_DIR}
    )

set(LLRENDER_LIBRARIES
    llrender
    )

