cmake_minimum_required(VERSION 3.21)
execute_process(COMMAND vcpkg x-update-baseline)
include(cmake/warnings.cmake)
include(cmake/static_analysis.cmake)

project(half-mapper VERSION 0.1.0)
add_executable(${PROJECT_NAME}-old
    "src/bsp.cpp"
    "src/ConfigXML.cpp"
    "src/entities.cpp"
    "src/halfmapper.cpp"
    "src/VideoSystem.cpp"
    "src/wad.cpp"
)
target_compile_features(${PROJECT_NAME}-old PUBLIC cxx_std_20)
set_project_warnings(${PROJECT_NAME}-old)

find_package(SDL2 CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(GLEW REQUIRED)

target_link_libraries(${PROJECT_NAME}-old PRIVATE
    GLEW::GLEW
    SDL2::SDL2main
    SDL2::SDL2
    tinyxml2::tinyxml2
)

add_executable(${PROJECT_NAME}
    "rewrite/main.cpp"

    "rewrite/bsp.cpp"
    "rewrite/ConfigXML.cpp"
    "rewrite/entities.cpp"
    "rewrite/wad.cpp"
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_project_warnings(${PROJECT_NAME})

set(DAXA_INCLUDE_DIR "$<TARGET_FILE_DIR:half-mapper>/../vcpkg_installed/x64-$<LOWER_CASE:$<PLATFORM_ID>>/include")
target_compile_definitions(${PROJECT_NAME} PRIVATE DAXA_SHADER_INCLUDE_DIR="${DAXA_INCLUDE_DIR}")

find_package(daxa CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    daxa::daxa
    glfw
    tinyxml2::tinyxml2
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    #     target_link_options(${PROJECT_NAME} PRIVATE "-Wl,/ENTRY:mainCRTStartup,/SUBSYSTEM:WINDOWS")
    # else()
    #     target_link_options(${PROJECT_NAME} PRIVATE /ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS)
    # endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE Dwmapi)
endif()
