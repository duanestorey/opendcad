cmake_minimum_required(VERSION 3.16)
project(OpenDCAD VERSION 0.1 LANGUAGES CXX)

# Use modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --------------------------------------------------------------------------------------
# OpenCascade — prefer imported component targets; otherwise fall back to legacy vars
# --------------------------------------------------------------------------------------
set(_occt_components
  TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKSTL
  TKPrim TKBO TKFillet TKOffset TKShHealing
  TKSTEP TKSTEPBase TKSTEPAttr TKXSBase
)
find_package(OpenCASCADE QUIET COMPONENTS ${_occt_components})
if(NOT OpenCASCADE_FOUND)
  message(STATUS "OpenCASCADE component targets not available; retrying without components…")
  find_package(OpenCASCADE REQUIRED)
endif()

# Helper to apply OCCT include/link + libs to a target
function(link_occt target)
  if(TARGET OpenCASCADE::TKernel)
    target_link_libraries(${target} PRIVATE
      OpenCASCADE::TKernel
      OpenCASCADE::TKMath
      OpenCASCADE::TKG2d
      OpenCASCADE::TKG3d
      OpenCASCADE::TKGeomBase
      OpenCASCADE::TKGeomAlgo
      OpenCASCADE::TKBRep
      OpenCASCADE::TKTopAlgo
      OpenCASCADE::TKSTL
      OpenCASCADE::TKPrim
      OpenCASCADE::TKBO
      OpenCASCADE::TKFillet
      OpenCASCADE::TKOffset
      OpenCASCADE::TKShHealing
      OpenCASCADE::TKSTEP
      OpenCASCADE::TKSTEPBase
      OpenCASCADE::TKSTEPAttr
      OpenCASCADE::TKXSBase
    )
  else()
    # Legacy variables path
    if(DEFINED OpenCASCADE_INCLUDE_DIR)
      target_include_directories(${target} PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    elseif(DEFINED OpenCASCADE_INCLUDE_DIRS)
      target_include_directories(${target} PRIVATE ${OpenCASCADE_INCLUDE_DIRS})
    endif()

    # Make sure the linker can actually find libTKernel, etc.
    if(DEFINED OpenCASCADE_LIBRARY_DIR)
      target_link_directories(${target} PRIVATE ${OpenCASCADE_LIBRARY_DIR})
    elseif(DEFINED OpenCASCADE_LIBRARY_DIRS)
      target_link_directories(${target} PRIVATE ${OpenCASCADE_LIBRARY_DIRS})
    elseif(APPLE)
      # Homebrew fallback (arm64)
      target_link_directories(${target} PRIVATE /opt/homebrew/lib)
    endif()

    # Link the libraries (these may be bare names on some Find modules)
    target_link_libraries(${target} PRIVATE ${OpenCASCADE_LIBRARIES})
  endif()
endfunction()

# --------------------------------------------------------------------------------------
# ANTLR4 C++ runtime (from Homebrew/system package)
# --------------------------------------------------------------------------------------
find_package(antlr4-runtime CONFIG REQUIRED)

# ======================================================================================
# ANTLR TOOLING (auto-generate lexer/parser during the build)
# ======================================================================================

# 1) Find the ANTLR tool (accept both names)
find_program(ANTLR4_CMD NAMES antlr4 antlr)
if (NOT ANTLR4_CMD)
  message(FATAL_ERROR
    "ANTLR tool not found. Install with your package manager, "
    "or provide a JAR and adjust the build.")
endif()

# 2) Where grammars live and where to place generated sources
set(GRAMMAR_DIR ${CMAKE_SOURCE_DIR}/grammar)
set(GEN_DIR     ${CMAKE_BINARY_DIR}/generated/antlr)
file(MAKE_DIRECTORY ${GEN_DIR})

# 3) Grammar files
set(GRAMMARS
  ${GRAMMAR_DIR}/OpenDCAD.g4
)

# 4) Expected generated files
set(GENERATED_SOURCES)
foreach(g ${GRAMMARS})
  get_filename_component(gn ${g} NAME_WE)
  list(APPEND GENERATED_SOURCES
    ${GEN_DIR}/${gn}Lexer.cpp
    ${GEN_DIR}/${gn}Parser.cpp
    ${GEN_DIR}/${gn}BaseListener.cpp
    ${GEN_DIR}/${gn}Listener.cpp
    ${GEN_DIR}/${gn}BaseVisitor.cpp
    ${GEN_DIR}/${gn}Visitor.cpp
  )
endforeach()

# 5) Generate the C++ sources
add_custom_command(
  OUTPUT  ${GENERATED_SOURCES}
  COMMAND ${ANTLR4_CMD}
          -Dlanguage=Cpp
          -visitor -listener
          -package ${PROJECT_NAME}
          -o ${GEN_DIR}
          ${GRAMMARS}
  DEPENDS ${GRAMMARS}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Generating ANTLR4 C++ sources"
  VERBATIM
)

# 6) A target representing the generation step
add_custom_target(generate_antlr ALL DEPENDS ${GENERATED_SOURCES})

# --------------------------------------------------------------------------------------
# Shared source files (used by both main executable and tests)
# --------------------------------------------------------------------------------------
set(OPENDCAD_LIB_SOURCES
  src/core/output.cpp
  src/core/Timer.cpp
  src/geometry/Shape.cpp
  src/geometry/ShapeRegistry.cpp
  src/parser/Value.cpp
  src/parser/Environment.cpp
  src/parser/Evaluator.cpp
  src/export/StepExporter.cpp
  src/export/StlExporter.cpp
)

# --------------------------------------------------------------------------------------
# Main executable
# --------------------------------------------------------------------------------------
add_executable(opendcad
  src/main.cpp
  ${OPENDCAD_LIB_SOURCES}
)

# Warnings
if (MSVC)
  target_compile_options(opendcad PRIVATE /W4 /permissive-)
else()
  target_compile_options(opendcad PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Include directories
target_include_directories(opendcad PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/geometry
  ${CMAKE_SOURCE_DIR}/src/parser
  ${CMAKE_SOURCE_DIR}/src/export
  ${GEN_DIR}
)

# Link the runtime + OpenCascade
target_link_libraries(opendcad PRIVATE antlr4_shared)
link_occt(opendcad)

# ANTLR generated sources
target_sources(opendcad PRIVATE ${GENERATED_SOURCES})
add_dependencies(opendcad generate_antlr)

# --------------------------------------------------------------------------------------
# Viewer (GLFW + OpenGL) — optional
# --------------------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw3)
  if (NOT GLFW_FOUND)
    pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw)
  endif()
endif()

if(GLFW_FOUND)
  add_executable(opendcad_viewer
    src/viewer/viewer.cpp
  )

  target_include_directories(opendcad_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/viewer
  )

  if (APPLE)
    target_link_libraries(opendcad_viewer PRIVATE
      PkgConfig::GLFW
      "-framework OpenGL"
      "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    )
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(opendcad_viewer PRIVATE PkgConfig::GLFW OpenGL::GL)
  endif()

  target_compile_features(opendcad_viewer PRIVATE cxx_std_20)
  target_compile_options(opendcad_viewer PRIVATE -Wall -Wextra -Wpedantic)
  link_occt(opendcad_viewer)
else()
  message(STATUS "GLFW not found — skipping opendcad_viewer")
endif()

# --------------------------------------------------------------------------------------
# Tests (Google Test)
# --------------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
# Prevent overriding parent project compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(opendcad_tests
  tests/test_value.cpp
  tests/test_environment.cpp
  tests/test_shape.cpp
  tests/test_shape_registry.cpp
  tests/test_evaluator.cpp
  ${OPENDCAD_LIB_SOURCES}
)

target_include_directories(opendcad_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/geometry
  ${CMAKE_SOURCE_DIR}/src/parser
  ${CMAKE_SOURCE_DIR}/src/export
  ${GEN_DIR}
)

target_link_libraries(opendcad_tests PRIVATE
  GTest::gtest_main
  antlr4_shared
)
link_occt(opendcad_tests)

target_sources(opendcad_tests PRIVATE ${GENERATED_SOURCES})
add_dependencies(opendcad_tests generate_antlr)

include(GoogleTest)
gtest_discover_tests(opendcad_tests)
