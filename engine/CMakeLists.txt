file(GLOB_RECURSE VV_ENGINE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
file(GLOB_RECURSE VV_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER VV_ENGINE_SOURCES EXCLUDE REGEX ".*/DemoMain\\.cpp$")

add_library(vividvision_engine STATIC
  ${VV_ENGINE_HEADERS}
  ${VV_ENGINE_SOURCES}
)

target_include_directories(vividvision_engine
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${glm_SOURCE_DIR}
    ${VMA_SOURCE_DIR}/include
    ${assimp_SOURCE_DIR}/contrib/stb
)

target_link_libraries(vividvision_engine
  PUBLIC
    Vulkan::Vulkan
    glfw
    assimp
    spdlog::spdlog
)

target_compile_definitions(vividvision_engine
  PUBLIC
    GLFW_INCLUDE_NONE
    GLM_FORCE_RIGHT_HANDED
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

add_executable(vividvision_demo
  app/DemoMain.cpp
)

target_link_libraries(vividvision_demo
  PRIVATE
    vividvision_engine
)

if(APPLE)
  target_compile_definitions(vividvision_engine PUBLIC VV_PLATFORM_MACOS=1)
endif()
if(VV_BUILD_VALIDATION)
  target_compile_definitions(vividvision_engine PUBLIC VV_ENABLE_VALIDATION=1)
endif()

find_program(GLSLANG_VALIDATOR NAMES glslangValidator)
if(GLSLANG_VALIDATOR)
  set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
  file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

  set(VV_SHADERS
    ${CMAKE_SOURCE_DIR}/shaders/skin_pbr.vert
    ${CMAKE_SOURCE_DIR}/shaders/skin_pbr.frag
    ${CMAKE_SOURCE_DIR}/shaders/skin_shadow.vert
  )

  set(VV_SHADER_OUTPUTS)
  foreach(SHADER_FILE ${VV_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
    set(OUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
      OUTPUT ${OUT_FILE}
      COMMAND ${CMAKE_COMMAND}
              -DSHADER_INPUT=${SHADER_FILE}
              -DSHADER_OUTPUT=${OUT_FILE}
              -DGLSLANG_VALIDATOR=${GLSLANG_VALIDATOR}
              -P ${CMAKE_SOURCE_DIR}/tools/shader_compile/compile_shaders.cmake
      DEPENDS ${SHADER_FILE} ${CMAKE_SOURCE_DIR}/tools/shader_compile/compile_shaders.cmake
      COMMENT "Compiling shader ${SHADER_NAME}"
      VERBATIM
    )
    list(APPEND VV_SHADER_OUTPUTS ${OUT_FILE})
  endforeach()

  add_custom_target(vividvision_shaders ALL DEPENDS ${VV_SHADER_OUTPUTS})
  add_dependencies(vividvision_demo vividvision_shaders)
endif()
