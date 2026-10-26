cmake_minimum_required(VERSION 3.20)
project(mesh_verifier VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MESH_VERIFIER_BUILD_TESTS "Build the tests" ON)
option(MESH_VERIFIER_BUILD_TOOLS "Build the command-line tools" ON)

# The library: geometry, OBJ import, intersection verification and the web viewer. No graphics dependencies.
add_library(mesh_verifier
    src/import/obj_importer.cpp
    src/verification/predicates.cpp
    src/verification/triangle_intersection.cpp
    src/verification/intersection_verifier.cpp
    src/viewer/web_viewer.cpp)
target_include_directories(mesh_verifier PUBLIC include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mesh_verifier PRIVATE -Wall -Wextra -Wpedantic)
    # The exact predicates rely on each floating-point operation being rounded on its own: no fused contractions.
    set_source_files_properties(src/verification/predicates.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
elseif(MSVC)
    target_compile_options(mesh_verifier PRIVATE /W4 /fp:precise)
endif()

if(MESH_VERIFIER_BUILD_TOOLS)
    add_executable(mesh-verify tools/mesh_verify.cpp)
    target_link_libraries(mesh-verify PRIVATE mesh_verifier)
    add_executable(mesh-verifier-web tools/mesh_verifier_web.cpp)
    target_link_libraries(mesh-verifier-web PRIVATE mesh_verifier)
endif()

if(MESH_VERIFIER_BUILD_TESTS)
    include(CTest)
    include(FetchContent)
    FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.2 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    add_executable(mesh_verifier_tests
        tests/importer_tests.cpp
        tests/predicate_tests.cpp
        tests/intersection_tests.cpp
        tests/web_viewer_tests.cpp)
    target_link_libraries(mesh_verifier_tests PRIVATE mesh_verifier Catch2::Catch2WithMain)
    target_compile_definitions(mesh_verifier_tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(mesh_verifier_tests)
endif()
