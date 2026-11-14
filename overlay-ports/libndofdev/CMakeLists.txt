cmake_minimum_required(VERSION 3.20)
project(libndofdev C CXX)

# Windows build of 3Dconnexion's libndofdev (DirectInput 8 backend) as a static library.
add_library(libndofdev STATIC
    src/ndofdev.c
    src/ndofdev_win.cpp)
target_compile_definitions(libndofdev PRIVATE WIN32 _LIB _CRT_SECURE_NO_DEPRECATE)
target_include_directories(libndofdev PRIVATE src)
target_link_libraries(libndofdev PRIVATE dinput8 dxguid)

install(TARGETS libndofdev ARCHIVE DESTINATION lib)
install(FILES src/ndofdev_external.h DESTINATION include)
