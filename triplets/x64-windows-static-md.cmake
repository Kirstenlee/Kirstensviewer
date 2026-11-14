set(VCPKG_TARGET_TRIPLET "x64-windows-static-md")
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v145)

# S24: static libraries on the dynamic CRT (/MD), for code that links into the
# viewer's /MD Release config (the CEF wrapper and dullahan). Release only,
# like the other triplets.
set(VCPKG_BUILD_TYPE "release" CACHE STRING "Force Release build" FORCE)
set(VCPKG_BUILD_CONFIGURATIONS "release" CACHE STRING "Only build release configuration" FORCE)
