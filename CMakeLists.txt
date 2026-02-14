cmake_minimum_required(VERSION 3.25)
project(VividVision LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VV_BUILD_TESTS "Build tests" ON)
option(VV_BUILD_VALIDATION "Enable Vulkan validation layers at runtime" ON)
option(VV_USE_SYSTEM_VULKAN "Use system Vulkan package" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)

CPMAddPackage(
  NAME glm
  GITHUB_REPOSITORY g-truc/glm
  GIT_TAG 1.0.1
  OPTIONS
    "GLM_BUILD_TESTS OFF"
)

CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG v1.14.1
  OPTIONS
    "SPDLOG_BUILD_SHARED OFF"
    "SPDLOG_BUILD_EXAMPLE OFF"
    "SPDLOG_BUILD_TESTS OFF"
)

CPMAddPackage(
  NAME glfw
  GITHUB_REPOSITORY glfw/glfw
  GIT_TAG 3.4
  OPTIONS
    "GLFW_BUILD_EXAMPLES OFF"
    "GLFW_BUILD_TESTS OFF"
    "GLFW_BUILD_DOCS OFF"
)

CPMAddPackage(
  NAME assimp
  GITHUB_REPOSITORY assimp/assimp
  GIT_TAG v5.4.3
  OPTIONS
    "ASSIMP_BUILD_TESTS OFF"
    "ASSIMP_BUILD_ASSIMP_TOOLS OFF"
)

CPMAddPackage(
  NAME VMA
  GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
  GIT_TAG v3.1.0
)

if(VV_USE_SYSTEM_VULKAN)
  find_package(Vulkan REQUIRED)
else()
  message(FATAL_ERROR "VV_USE_SYSTEM_VULKAN=OFF is not yet supported. Install Vulkan SDK/MoltenVK and rerun.")
endif()

add_subdirectory(engine)

if(VV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
