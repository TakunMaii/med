cmake_minimum_required(VERSION 3.20)
project(med C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(TREE_SITTER REQUIRED tree-sitter)

find_library(TREE_SITTER_C_LIB tree-sitter-c REQUIRED)

add_executable(med
  src/main.c
  src/common.c
  src/editor.c
  src/editor_command.c
  src/editor_syntax.c
  src/keymap.c
  src/lsp.c
  src/vulkan_app.c
  third_party/cjson/cJSON.c
)

target_include_directories(med PRIVATE
  ${GLFW_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
  ${TREE_SITTER_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson
)

target_compile_options(med PRIVATE
  ${GLFW_CFLAGS_OTHER}
  ${FREETYPE_CFLAGS_OTHER}
  ${TREE_SITTER_CFLAGS_OTHER}
  -Wall -Wextra -Wpedantic
)

target_link_libraries(med PRIVATE
  Vulkan::Vulkan
  ${GLFW_LIBRARIES}
  ${FREETYPE_LIBRARIES}
  ${TREE_SITTER_LIBRARIES}
  ${TREE_SITTER_C_LIB}
  m
)

set(SHADER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.frag
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cursor.vert
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cursor.frag
)
set(SHADER_SPV
  ${CMAKE_CURRENT_BINARY_DIR}/text.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/text.frag.spv
  ${CMAKE_CURRENT_BINARY_DIR}/cursor.vert.spv
  ${CMAKE_CURRENT_BINARY_DIR}/cursor.frag.spv
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/text.vert.spv
  COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.vert -o ${CMAKE_CURRENT_BINARY_DIR}/text.vert.spv
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.vert
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/text.frag.spv
  COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.frag -o ${CMAKE_CURRENT_BINARY_DIR}/text.frag.spv
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/text.frag
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cursor.vert.spv
  COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cursor.vert -o ${CMAKE_CURRENT_BINARY_DIR}/cursor.vert.spv
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cursor.vert
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/cursor.frag.spv
  COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cursor.frag -o ${CMAKE_CURRENT_BINARY_DIR}/cursor.frag.spv
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cursor.frag
)

add_custom_target(med_shaders DEPENDS ${SHADER_SPV})
add_dependencies(med med_shaders)

target_compile_definitions(med PRIVATE
  MED_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}"
  MED_DEFAULT_FONT="/usr/share/fonts/myfonts/CaskaydiaCove/CaskaydiaCoveNerdFontMono-Regular.ttf"
)

add_executable(med_editor_tests
  src/editor_tests.c
  src/common.c
  src/editor.c
  src/editor_command.c
  src/editor_syntax.c
  src/keymap.c
  src/lsp.c
  third_party/cjson/cJSON.c
)

target_include_directories(med_editor_tests PRIVATE
  ${GLFW_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
  ${TREE_SITTER_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson
)

target_compile_options(med_editor_tests PRIVATE
  ${GLFW_CFLAGS_OTHER}
  ${FREETYPE_CFLAGS_OTHER}
  ${TREE_SITTER_CFLAGS_OTHER}
  -Wall -Wextra -Wpedantic
)

target_link_libraries(med_editor_tests PRIVATE
  ${GLFW_LIBRARIES}
  ${TREE_SITTER_LIBRARIES}
  ${TREE_SITTER_C_LIB}
)

enable_testing()
add_test(NAME med_editor_tests COMMAND med_editor_tests)
