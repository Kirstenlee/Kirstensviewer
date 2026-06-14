# -*- cmake -*-

include(00-Common)

project(cmake)

set(cmake_SOURCE_FILES
        CMakeLists.txt
        00-BuildOptions.cmake
        00-Common.cmake
        Apr.cmake
        Audio.cmake
        Boost.cmake
        BuildVersion.cmake
        CefPlugin.cmake
        CMakeCopyIfDifferent.cmake
        Curl.cmake

        DragDrop.cmake
        Expat.cmake
    FindOpenJPEG.cmake
    FindSCP.cmake
        FreeType.cmake
        GlExt.cmake
        Glh.cmake
        Glm.cmake

    Hunspell.cmake
    Icu4c.cmake
    Jpeg.cmake
    LLAppearance.cmake
    LLAudio.cmake
    LLCharacter.cmake
    LLCommon.cmake
    LLCoreHttp.cmake
    LLImage.cmake
    LLImageJ2COJ.cmake
    LLInventory.cmake
    LLLogin.cmake
    LLMath.cmake
    LLMeshOptimizer.cmake
    LLMessage.cmake
    LLConvexdecomposition.cmake
    LLPlugin.cmake
        LLPrimitive.cmake
    LLRender.cmake
    LLUI.cmake
    LLFileSystem.cmake
        LLWindow.cmake
    LLXML.cmake
        Linking.cmake
    MediaPluginBase.cmake
        MeshOptimizer.cmake
        Mikktspace.cmake
        Ndof.cmake
        NgHttp2.cmake
        OpenAL.cmake
        OpenGL.cmake
        DxRender.cmake
        OpenJPEG.cmake
        OpenSSL.cmake
        Png.cmake
        PluginAPI.cmake
        Prebuilt.cmake
        TemplateCheckModern.cmake
        TinyEXR.cmake
        TinyGLTF.cmake
        Tut.cmake
        Ui.cmake
        Variables.cmake
        ViewerMiscLibs.cmake
        LibVLCPlugin.cmake
        WebRTC.cmake
        XmlRpcEpi.cmake
        xxHash.cmake
        ZLib.cmake
        )

source_group("Shared Rules" FILES ${cmake_SOURCE_FILES})

set(master_SOURCE_FILES
    ../CMakeLists.txt
    )

source_group("Master Rules" FILES ${master_SOURCE_FILES})

set_source_files_properties(${cmake_SOURCE_FILES} ${master_SOURCE_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

add_library(cmake
            cmake_dummy.cpp
            ${cmake_SOURCE_FILES}
            ${master_SOURCE_FILES}
            )
