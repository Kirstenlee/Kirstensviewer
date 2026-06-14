# Triplet identity
set(VCPKG_TARGET_TRIPLET "x64-windows-static-libxml")

# Architecture + linkage
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

# Toolset override (optional but valid)
set(VCPKG_PLATFORM_TOOLSET v145)

# ============================
#  Compiler Flags (Release)
# ============================

# C flags
set(VCPKG_C_FLAGS_RELEASE
    "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /GS- /Gy /Zc:inline /Zc:__cplusplus /wd4789"
    CACHE STRING "libxml2 C flags" FORCE
)

# C++ flags (libxml2 is C, but harmless to include)
set(VCPKG_CXX_FLAGS_RELEASE
    "/O2 /Ob2 /Ot /Oi /Oy /GL /arch:AVX /fp:fast /GS- /Gy /Zc:inline /Zc:__cplusplus /wd4789"
    CACHE STRING "libxml2 CXX flags" FORCE
)

# Linker flags
set(VCPKG_LINKER_FLAGS_RELEASE
    "/LTCG"
    CACHE STRING "Link-time code generation" FORCE
)