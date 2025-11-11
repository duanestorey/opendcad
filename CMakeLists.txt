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

# --- ANTLR4 C++ runtime (from Homebrew/system package) ---
find_package(antlr4-runtime CONFIG REQUIRED)

# --- Your executable sources ---
add_executable(opendcad
    src/opendcad.cpp
    src/output.cpp
)

# Warnings
if (MSVC)
    target_compile_options(opendcad PRIVATE /W4 /permissive-)
else()
    target_compile_options(opendcad PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

# Link the runtime
target_link_libraries(opendcad PRIVATE antlr4_shared)

# ======================================================================================
# ANTLR TOOLING (auto-generate lexer/parser during the build)
# ======================================================================================

# 1) Find the ANTLR tool (accept both names)
find_program(ANTLR4_CMD NAMES antlr4 antlr)
if (NOT ANTLR4_CMD)
  message(FATAL_ERROR
    "ANTLR tool not found. Install with Homebrew (brew install antlr), "
    "or provide a JAR and adjust the build.")
endif()

# 2) Where grammars live and where to place generated sources
set(GRAMMAR_DIR ${CMAKE_SOURCE_DIR}/grammar)
set(GEN_DIR     ${CMAKE_BINARY_DIR}/generated/antlr)
file(MAKE_DIRECTORY ${GEN_DIR})

# 3) List your grammar files here
set(GRAMMARS
  ${GRAMMAR_DIR}/OpenDCAD.g4
  # add more .g4 files here if you split lexer/parser grammars
)

# 4) Expected generated files (for each grammar name)
set(GENERATED_SOURCES)
foreach(g ${GRAMMARS})
  get_filename_component(gn ${g} NAME_WE)  # grammar name without extension
  list(APPEND GENERATED_SOURCES
    ${GEN_DIR}/${gn}Lexer.cpp
    ${GEN_DIR}/${gn}Parser.cpp
    ${GEN_DIR}/${gn}BaseListener.cpp
    ${GEN_DIR}/${gn}Listener.cpp
    ${GEN_DIR}/${gn}BaseVisitor.cpp
    ${GEN_DIR}/${gn}Visitor.cpp
  )
endforeach()

# 5) Command to generate the C++ sources
add_custom_command(
  OUTPUT  ${GENERATED_SOURCES}
  COMMAND ${ANTLR4_CMD}
          -Dlanguage=Cpp
          -visitor -listener
          -package ${PROJECT_NAME}           # optional: namespaces generated code
          -o ${GEN_DIR}
          ${GRAMMARS}
  DEPENDS ${GRAMMARS}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Generating ANTLR4 C++ sources"
  VERBATIM
)

# 6) A target representing the generation step
add_custom_target(generate_antlr ALL DEPENDS ${GENERATED_SOURCES})

# 7) Compile the generated sources and include their headers
target_sources(opendcad PRIVATE ${GENERATED_SOURCES})
target_include_directories(opendcad PRIVATE ${GEN_DIR})
add_dependencies(opendcad generate_antlr)

# Find OpenCascade (Homebrew build has no component system)
find_package(OpenCASCADE REQUIRED)

# Include dirs
include_directories(${OpenCASCADE_INCLUDE_DIR})

# Link the entire OCCT library set
target_link_libraries(opendcad PRIVATE ${OpenCASCADE_LIBRARIES})



# --------------------------------------------------------------------------------------
# Future dependencies (commented for now)
# --------------------------------------------------------------------------------------
# find_package(OpenCASCADE REQUIRED)
# target_link_libraries(opendcad PRIVATE ${OpenCASCADE_LIBRARIES})
# target_include_directories(opendcad PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# --------------------------------------------------------------------------------------
# install (optional)
# --------------------------------------------------------------------------------------
# install(TARGETS opendcad DESTINATION bin)


# --- Viewer (GLFW + OpenGL) ---
find_package(PkgConfig REQUIRED)
# Homebrew installs glfw as pkgconfig "glfw3" or "glfw"; try both:
pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw3)
if (NOT GLFW_FOUND)
  pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw)
endif()

add_executable(opendcad_viewer
  viewer/viewer_main.cpp
)

# macOS OpenGL framework + GLFW
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