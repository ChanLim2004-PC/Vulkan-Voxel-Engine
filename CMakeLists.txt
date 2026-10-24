cmake_minimum_required(VERSION 3.14)
project(voxel)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(FASTGLTF_ENABLE_TESTS OFF CACHE INTERNAL "")
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE INTERNAL "")

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(
    sdl
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
set(GLM_TEST_ENABLE OFF CACHE BOOL "Enable GLM tests" FORCE)

FetchContent_Declare(
    vkbootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        v1.4.355
)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.0.2
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        master
)

add_subdirectory("./vendor/fastgltf-0.7.2")
set(VK_BOOTSTRAP_BUILD_EXAMPLES OFF CACHE BOOL "Disable vk-bootstrap examples" FORCE)
set(VK_BOOTSTRAP_TESTS OFF CACHE BOOL "Disable vk-bootstrap tests" FORCE)

FetchContent_MakeAvailable(sdl glm vkbootstrap vma fmt stb imgui)

add_executable(${PROJECT_NAME} 
    ./src/main.cpp
    ./src/engine/vk_engine.cpp
    ./src/engine/vk_images.cpp
    ./src/engine/vk_initializers.cpp
    ./src/engine/vk_pipeline.cpp
    ./src/engine/vk_descriptors.cpp
    ./src/engine/vk_loader.cpp
    ./src/camera/camera.cpp
    ./src/engine/pipelines/cull_pipeline.cpp
    ./src/meshes/voxel/mc_alogo_data.cpp
    ./src/meshes/voxel/chunk.cpp
    ./src/meshes/culling.cpp
    ./src/engine/pipelines/prefix_pipeline.cpp
    ./src/engine/pipelines/voxel_count_pipeline.cpp
    ./src/raytracing/acceleration_structure.cpp
)

target_sources(${PROJECT_NAME} PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ./src
    ${stb_SOURCE_DIR}             
    ${vma_SOURCE_DIR}/include     
    ${imgui_SOURCE_DIR}           
    ${imgui_SOURCE_DIR}/backends  
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3-shared
    glm::glm
    vk-bootstrap
    fmt::fmt
    fastgltf::fastgltf
    VulkanMemoryAllocator
)
