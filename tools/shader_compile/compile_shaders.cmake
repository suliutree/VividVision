cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SHADER_INPUT)
  message(FATAL_ERROR "SHADER_INPUT is required")
endif()
if(NOT DEFINED SHADER_OUTPUT)
  message(FATAL_ERROR "SHADER_OUTPUT is required")
endif()
if(NOT DEFINED GLSLANG_VALIDATOR)
  message(FATAL_ERROR "GLSLANG_VALIDATOR is required")
endif()

execute_process(
  COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_INPUT} -o ${SHADER_OUTPUT}
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "Shader compile failed for ${SHADER_INPUT}: ${err}\n${out}")
endif()
