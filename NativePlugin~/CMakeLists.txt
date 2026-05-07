cmake_minimum_required(VERSION 3.20)
project(GfxPluginMDI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/MultiDrawIndirect.cpp
    src/MDIBackend_Vulkan.cpp
    src/MDIBackend_GLES.cpp
)

set(HEADERS
    src/MDILog.h
    src/MDIBackend.h
    src/MDIBackend_Vulkan.h
    src/MDIBackend_GLES.h
    src/MDIBackend_Stub.h
    src/vulkan/vulkan.h
    src/gles/gles_minimal.h
    src/Unity/IUnityInterface.h
    src/Unity/IUnityGraphics.h
    src/Unity/IUnityGraphicsVulkan.h
    src/Unity/IUnityRenderingExtensions.h
)

if (WIN32)
    list(APPEND SOURCES
        src/MDIBackend_D3D12.cpp
        src/MDIBackend_D3D11.cpp
    )
    list(APPEND HEADERS
        src/MDIBackend_D3D12.h
        src/MDIBackend_D3D11.h
        src/Unity/IUnityGraphicsD3D11.h
        src/Unity/IUnityGraphicsD3D12.h
    )
endif()

if (APPLE)
    enable_language(OBJCXX)
    list(APPEND SOURCES src/MDIBackend_Metal.mm)
    list(APPEND HEADERS
        src/MDIBackend_Metal.h
        src/Unity/IUnityGraphicsMetal.h
    )
endif()

if (APPLE)
    # Unity loads native plugins on macOS as bundles
    add_library(GfxPluginMDI MODULE ${SOURCES} ${HEADERS})
    set_target_properties(GfxPluginMDI PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        MACOSX_BUNDLE_BUNDLE_NAME "GfxPluginMDI"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.saivs.gfxpluginmdi"
        OUTPUT_NAME "GfxPluginMDI"
    )
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set_target_properties(GfxPluginMDI PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    set_source_files_properties(src/MDIBackend_Metal.mm PROPERTIES
        COMPILE_FLAGS "-fno-objc-arc"
    )
    target_link_libraries(GfxPluginMDI PRIVATE
        "-framework Foundation"
        "-framework Metal"
    )
else()
    add_library(GfxPluginMDI SHARED ${SOURCES} ${HEADERS})
endif()

target_include_directories(GfxPluginMDI PRIVATE src)

if (MSVC)
    # Static CRT — eliminates VCRUNTIME140.dll dependency
    set_property(TARGET GfxPluginMDI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if (WIN32)
    target_link_libraries(GfxPluginMDI PRIVATE d3d11 d3d12 dxgi)
endif()

# Output to package Plugins folder
get_filename_component(PLUGINS_ROOT "${CMAKE_SOURCE_DIR}/../Plugins" ABSOLUTE)

if (APPLE)
    set(PLUGINS_DIR "${PLUGINS_ROOT}/macOS")
    add_custom_command(TARGET GfxPluginMDI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGINS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PLUGINS_DIR}/GfxPluginMDI.bundle"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:GfxPluginMDI>"
            "${PLUGINS_DIR}/GfxPluginMDI.bundle"
        COMMENT "Copying GfxPluginMDI.bundle to package Plugins/macOS folder"
    )
else()
    set(PLUGINS_DIR "${PLUGINS_ROOT}/x86_64")
    add_custom_command(TARGET GfxPluginMDI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGINS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:GfxPluginMDI>"
            "${PLUGINS_DIR}/"
        COMMENT "Copying GfxPluginMDI to package Plugins folder"
    )
endif()
