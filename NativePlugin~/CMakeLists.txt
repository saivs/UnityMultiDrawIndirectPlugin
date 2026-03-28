cmake_minimum_required(VERSION 3.20)
project(GfxPluginMDI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
    src/MultiDrawIndirect.cpp
    src/MDIBackend_D3D12.cpp
    src/MDIBackend_D3D11.cpp
    src/MDIBackend_Vulkan.cpp
    src/MDIBackend_GLES.cpp
)

set(HEADERS
    src/MDILog.h
    src/MDIBackend.h
    src/MDIBackend_D3D12.h
    src/MDIBackend_D3D11.h
    src/MDIBackend_Vulkan.h
    src/MDIBackend_GLES.h
    src/MDIBackend_Stub.h
    src/vulkan/vulkan.h
    src/gles/gles_minimal.h
    src/Unity/IUnityInterface.h
    src/Unity/IUnityGraphics.h
    src/Unity/IUnityGraphicsD3D11.h
    src/Unity/IUnityGraphicsD3D12.h
    src/Unity/IUnityGraphicsVulkan.h
    src/Unity/IUnityRenderingExtensions.h
)

add_library(GfxPluginMDI SHARED ${SOURCES} ${HEADERS})

target_include_directories(GfxPluginMDI PRIVATE src)

# Static CRT — eliminates VCRUNTIME140.dll dependency
set_property(TARGET GfxPluginMDI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(GfxPluginMDI PRIVATE d3d11 d3d12 dxgi)

# Output to package Plugins folder
get_filename_component(PLUGINS_DIR "${CMAKE_SOURCE_DIR}/../Plugins/x86_64" ABSOLUTE)

add_custom_command(TARGET GfxPluginMDI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGINS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:GfxPluginMDI>"
        "${PLUGINS_DIR}/"
    COMMENT "Copying GfxPluginMDI.dll to package Plugins folder"
)
