cmake_minimum_required(VERSION 3.24)
project(mesh_verifier VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MESH_VERIFIER_BUILD_TESTS "Build the tests" ON)
option(MESH_VERIFIER_BUILD_TOOLS "Build the command-line tools" ON)

include(FetchContent)
include(cmake/Embed.cmake)

# Third-party libraries, fetched at configure time:
#  - nlohmann/json: reading and writing JSON.
#  - valijson: checking JSON documents against the JSON Schema files in schema/ (header-only).
#  - cpp-httplib: the local HTTP server (header-only).
# SOURCE_SUBDIR names a directory that doesn't exist, so only the headers are used, not the projects' own builds.
FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(valijson GIT_REPOSITORY https://github.com/tristanpenman/valijson.git GIT_TAG v1.1.3 GIT_SHALLOW TRUE SOURCE_SUBDIR none)
FetchContent_Declare(httplib GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.54.1 GIT_SHALLOW TRUE SOURCE_SUBDIR none)
FetchContent_MakeAvailable(nlohmann_json valijson httplib)

# A local three.js package (npm's "three") for the GUI to serve, so it works offline; the page falls back to a CDN.
find_path(MESH_VERIFIER_THREE_DIR
    NAMES build/three.module.js
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/node_modules/three /usr/local/lib/node_modules/three /usr/lib/node_modules/three
    NO_DEFAULT_PATH
    DOC "A three.js package directory (with build/ and examples/) for the GUI to serve")
if(NOT MESH_VERIFIER_THREE_DIR)
    set(MESH_VERIFIER_THREE_DIR "")
endif()

# The JSON Schemas and the GUI page, embedded in the library.
set(MESH_VERIFIER_SCHEMAS
    schema/common.schema.json
    schema/transform-edit.schema.json
    schema/transforms.schema.json
    schema/reset.schema.json
    schema/intersections.schema.json
    schema/view.schema.json
    schema/state.schema.json
    schema/scene.schema.json
    schema/error.schema.json)
mv_embed(${CMAKE_CURRENT_BINARY_DIR}/generated/schema_files.cpp SchemaFiles ${MESH_VERIFIER_SCHEMAS})
mv_embed(${CMAKE_CURRENT_BINARY_DIR}/generated/page_files.cpp PageFiles src/server/gui_page.html)

find_package(Threads REQUIRED)

# The library: geometry and scenes, OBJ and GLB import, intersection verification, the view (what renderers draw),
# the web viewer and the GUI server. No graphics dependencies.
add_library(mesh_verifier
    src/util/schema.cpp
    src/scene/transform.cpp
    src/scene/scene.cpp
    src/scene/scene_io.cpp
    src/import/obj_importer.cpp
    src/import/gltf_importer.cpp
    src/verification/predicates.cpp
    src/verification/triangle_intersection.cpp
    src/verification/intersection_verifier.cpp
    src/view/view.cpp
    src/viewer/web_viewer.cpp
    src/server/gui_server.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/schema_files.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/page_files.cpp)
target_include_directories(mesh_verifier PUBLIC include PRIVATE src)
target_include_directories(mesh_verifier SYSTEM PRIVATE ${valijson_SOURCE_DIR}/include ${httplib_SOURCE_DIR})
target_link_libraries(mesh_verifier PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
if(WIN32)
    target_link_libraries(mesh_verifier PRIVATE ws2_32)
endif()
set_source_files_properties(src/server/gui_server.cpp PROPERTIES COMPILE_DEFINITIONS "MESH_VERIFIER_THREE_DIR=\"${MESH_VERIFIER_THREE_DIR}\"")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mesh_verifier PRIVATE -Wall -Wextra -Wpedantic)
    # The exact predicates rely on each floating-point operation being rounded on its own: no fused contractions.
    set_source_files_properties(src/verification/predicates.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
elseif(MSVC)
    target_compile_options(mesh_verifier PRIVATE /W4 /fp:precise /utf-8 /bigobj)
endif()

if(MESH_VERIFIER_BUILD_TOOLS)
    add_executable(mesh-verify tools/mesh_verify.cpp)
    target_link_libraries(mesh-verify PRIVATE mesh_verifier)
    add_executable(mesh-verifier-web tools/mesh_verifier_web.cpp)
    target_link_libraries(mesh-verifier-web PRIVATE mesh_verifier)
endif()

if(MESH_VERIFIER_BUILD_TESTS)
    include(CTest)
    find_package(Catch2 3.4 CONFIG QUIET)
    if(NOT Catch2_FOUND)
        FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.2 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    endif()
    add_executable(mesh_verifier_tests
        tests/importer_tests.cpp
        tests/predicate_tests.cpp
        tests/intersection_tests.cpp
        tests/web_viewer_tests.cpp
        tests/transform_tests.cpp
        tests/scene_tests.cpp
        tests/gltf_importer_tests.cpp
        tests/schema_tests.cpp
        tests/view_tests.cpp
        tests/gui_server_tests.cpp)
    target_link_libraries(mesh_verifier_tests PRIVATE mesh_verifier Catch2::Catch2WithMain)
    target_compile_definitions(mesh_verifier_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    if(MSVC)
        target_compile_options(mesh_verifier_tests PRIVATE /utf-8 /bigobj)
    endif()
    include(Catch)
    catch_discover_tests(mesh_verifier_tests)
    if(MESH_VERIFIER_BUILD_TOOLS)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_FOUND)
            add_test(NAME cli_tests COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.py $<TARGET_FILE:mesh-verify>)
        endif()
    endif()
endif()
