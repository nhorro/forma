cmake_minimum_required(VERSION 3.22)
project(forma VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FORMA_BUILD_TESTS "Build the forma unit tests" ON)
option(FORMA_BUILD_EXAMPLE "Build the SFML 3 / Box2D 3 playground" ON)

# Point these at a checkout to skip the download, e.g. -DFORMA_BOX2D_DIR=../box2d
set(FORMA_BOX2D_DIR "" CACHE PATH "Existing Box2D 3 source tree")
set(FORMA_CLIPPER2_DIR "" CACHE PATH "Existing Clipper2 CPP directory")

set(CLIPPER2_UTILS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_USINGZ "OFF" CACHE STRING "" FORCE)

include(FetchContent)

if(FORMA_BOX2D_DIR)
    add_subdirectory("${FORMA_BOX2D_DIR}" "${CMAKE_BINARY_DIR}/box2d" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(
        box2d
        GIT_REPOSITORY https://github.com/erincatto/box2d.git
        GIT_TAG v3.1.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(box2d)
endif()

if(FORMA_CLIPPER2_DIR)
    add_subdirectory("${FORMA_CLIPPER2_DIR}" "${CMAKE_BINARY_DIR}/clipper2" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(
        clipper2
        GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
        GIT_TAG Clipper2_2.0.1
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR CPP
    )
    FetchContent_MakeAvailable(clipper2)
endif()

# Clipper2 2.0.1 builds with -Werror and trips a false positive on GCC 12.
if(TARGET Clipper2)
    target_compile_options(Clipper2 PRIVATE -Wno-error)
endif()

add_library(forma
    src/predicates.cpp
    src/sample.cpp
    src/mesh.cpp
    src/clipper_ops.cpp
    src/box2d_export.cpp
    src/node_pool.cpp
    src/document.cpp
    src/skeleton.cpp
    src/ik.cpp
    src/clip.cpp
    src/decompose.cpp
)
target_include_directories(forma PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(forma PUBLIC box2d::box2d Clipper2)
target_compile_features(forma PUBLIC cxx_std_20)

if(FORMA_BUILD_TESTS)
    add_executable(forma_tests tests/test_forma.cpp)
    target_link_libraries(forma_tests PRIVATE forma)
endif()

if(FORMA_BUILD_EXAMPLE)
    find_package(SFML 3 QUIET COMPONENTS Graphics Window System)
    if(SFML_FOUND)
        add_library(forma_sfml src/sfml_draw.cpp)
        target_link_libraries(forma_sfml PUBLIC forma SFML::Graphics)
        target_compile_features(forma_sfml PUBLIC cxx_std_20)

        add_executable(forma_playground examples/playground.cpp)
        target_link_libraries(forma_playground PRIVATE forma_sfml)

        add_executable(forma_walk examples/walk.cpp)
        target_link_libraries(forma_walk PRIVATE forma_sfml)

        include(FetchContent)
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.91.9
            GIT_SHALLOW TRUE
        )
        FetchContent_GetProperties(imgui)
        if(NOT imgui_POPULATED)
            FetchContent_Populate(imgui)
        endif()
        add_library(forma_imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            editor/imgui_sfml.cpp
        )
        target_include_directories(forma_imgui PUBLIC ${imgui_SOURCE_DIR})
        target_link_libraries(forma_imgui PUBLIC SFML::Graphics)
        target_compile_features(forma_imgui PUBLIC cxx_std_20)

        add_executable(forma_editor editor/main.cpp)
        target_link_libraries(forma_editor PRIVATE forma_sfml forma_imgui)
    else()
        message(STATUS "SFML 3 not found — skipping forma_playground. Set SFML_DIR to the SFML cmake config.")
    endif()
endif()
