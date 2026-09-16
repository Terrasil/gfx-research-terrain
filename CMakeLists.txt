cmake_minimum_required(VERSION 3.25)

project(gfx-research-terrain VERSION 0.5.9 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/.cpm-cache" CACHE PATH "CPM source cache")
set(GFX_RESEARCH_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty" CACHE PATH "Third-party source directory")
set(GFX_RESEARCH_BASE_GIT_TAG main CACHE STRING "gfx-research-base Git revision")
set(GFX_RESEARCH_BASE_LOCAL_PATH "" CACHE PATH "Optional local gfx-research-base checkout")

if(GFX_RESEARCH_BASE_LOCAL_PATH)
    add_subdirectory("${GFX_RESEARCH_BASE_LOCAL_PATH}" "${CMAKE_BINARY_DIR}/gfx-research-base")
    set(GFX_RESEARCH_BASE_SOURCE_DIR "${GFX_RESEARCH_BASE_LOCAL_PATH}")
else()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake")
    CPMAddPackage(
        NAME gfx_research_base
        GITHUB_REPOSITORY Terrasil/gfx-research-base
        GIT_TAG "${GFX_RESEARCH_BASE_GIT_TAG}"
        SOURCE_DIR "${GFX_RESEARCH_THIRD_PARTY_DIR}/gfx-research-base"
        OPTIONS
            "GFX_RESEARCH_BASE_BUILD_EXAMPLE OFF"
    )
    set(GFX_RESEARCH_BASE_SOURCE_DIR "${gfx_research_base_SOURCE_DIR}")
endif()

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${GFX_RESEARCH_BASE_SOURCE_DIR}"
    OUTPUT_VARIABLE GFX_RESEARCH_BASE_RESOLVED_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GFX_RESEARCH_BASE_RESOLVED_REVISION)
    set(GFX_RESEARCH_BASE_RESOLVED_REVISION "${GFX_RESEARCH_BASE_GIT_TAG}")
endif()

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GFX_TERRAIN_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GFX_TERRAIN_GIT_COMMIT)
    set(GFX_TERRAIN_GIT_COMMIT "unknown")
endif()

add_executable(gfx-research-terrain
    src/main.cpp
    src/AdaptiveTerrainMesh.cpp
    src/TerrainResearchApp.cpp
    src/BenchmarkRunner.cpp
    src/ErrorController.cpp
    src/GpuTessellatedTerrain.cpp
    src/Heightfield.cpp
    src/Metrics.cpp
    src/PatchSet.cpp
    src/TerrainTextures.cpp
)

target_include_directories(gfx-research-terrain PRIVATE include)

target_compile_features(gfx-research-terrain PRIVATE cxx_std_20)
target_link_libraries(gfx-research-terrain PRIVATE gfx::research_base)

target_compile_definitions(gfx-research-terrain PRIVATE
    GFX_TERRAIN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GFX_TERRAIN_GIT_COMMIT="${GFX_TERRAIN_GIT_COMMIT}"
    GFX_RESEARCH_BASE_REVISION="${GFX_RESEARCH_BASE_RESOLVED_REVISION}"
    GFX_TERRAIN_BUILD_TYPE="$<CONFIG>"
    GFX_TERRAIN_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
    GFX_TERRAIN_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    GFX_TERRAIN_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
    GFX_TERRAIN_SYSTEM_PROCESSOR="${CMAKE_SYSTEM_PROCESSOR}"
)

gfx_research_configure_executable(gfx-research-terrain)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gfx-research-terrain PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(gfx-research-terrain PRIVATE /W4)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    target_compile_definitions(gfx-research-terrain PRIVATE
        GFX_TERRAIN_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    )
    add_test(
        NAME terrain-unit-static
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_unit_tests.py"
    )
    set_tests_properties(terrain-unit-static PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()
