cmake_minimum_required(VERSION 3.20)
project(AeroEngine VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

find_package(Vulkan REQUIRED)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

add_subdirectory(external/volk)

add_subdirectory(external/vk-bootstrap)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS $ENV{VULKAN_SDK}/Bin)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please check Vulkan SDK installation.")
endif()

set(SHADER_SOURCES 
    ${CMAKE_SOURCE_DIR}/shaders/colored_triangle.vert
    ${CMAKE_SOURCE_DIR}/shaders/colored_triangle.frag
    ${CMAKE_SOURCE_DIR}/shaders/mesh.vert
    ${CMAKE_SOURCE_DIR}/shaders/mesh.frag
    ${CMAKE_SOURCE_DIR}/shaders/culling.comp
)

set(SPIRV_FILES "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPIRV_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${FILE_NAME}.spv)
    
    add_custom_command(
        OUTPUT ${SPIRV_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPIRV_OUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${FILE_NAME} to SPIR-V..."
    )
    list(APPEND SPIRV_FILES ${SPIRV_OUT})
endforeach()

add_custom_target(CompileShaders DEPENDS ${SPIRV_FILES})

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

add_executable(${PROJECT_NAME} 
    src/main.cpp
    src/aero_engine.cpp
    src/aero_engine.h
    src/vk_types.h
 "src/vk_context.cpp" "src/vk_context.h" "src/vk_types.cpp" "src/vk_initializers.cpp" "src/vk_initializers.h" "src/vk_pipelines.h" "src/vk_pipelines.cpp"
 ${IMGUI_SOURCES}
 "src/gltf_loader.h" "src/gltf_loader.cpp" "src/camera.h" "src/camera.cpp" "src/Core/Window.h" "src/Core/Window.cpp" "src/Core/KeyCodes.h" "src/RHI/VulkanDevice.h" "src/RHI/VulkanDevice.cpp" "src/Renderer/SceneRenderer.h" "src/Renderer/SceneRenderer.cpp" "src/Resource/asset_manager.h" "src/Resource/asset_manager.cpp")

target_compile_definitions(AeroEngine PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)

 add_dependencies(AeroEngine CompileShaders)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    external/glm
    external/vma/include
    external/stb
    external/cgltf
    external/imgui

    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    volk::volk
    Vulkan::Vulkan
    vk-bootstrap
)

add_custom_command(TARGET AeroEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:AeroEngine>/shaders
    COMMENT "Copying compiled shaders to executable directory..."
)