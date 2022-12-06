cmake_minimum_required(VERSION 3.21)
execute_process(COMMAND vcpkg x-update-baseline)
include(cmake/warnings.cmake)
include(cmake/static_analysis.cmake)

project(hlmm VERSION 0.1.0)

add_executable(${PROJECT_NAME}
    "src/main.cpp"

    "src/bsp.cpp"
    "src/ConfigXML.cpp"
    "src/entities.cpp"
    "src/wad.cpp"
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_project_warnings(${PROJECT_NAME})

set(DAXA_INCLUDE_DIR "$<TARGET_FILE_DIR:hlmm>/../vcpkg_installed/x64-$<LOWER_CASE:$<PLATFORM_ID>>/include")
target_compile_definitions(${PROJECT_NAME} PRIVATE DAXA_SHADER_INCLUDE_DIR="${DAXA_INCLUDE_DIR}")

find_package(daxa CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    daxa::daxa
    glfw
    tinyxml2::tinyxml2
    nlohmann_json::nlohmann_json
    imguizmo::imguizmo
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    #     target_link_options(${PROJECT_NAME} PRIVATE "-Wl,/ENTRY:mainCRTStartup,/SUBSYSTEM:WINDOWS")
    # else()
    #     target_link_options(${PROJECT_NAME} PRIVATE /ENTRY:mainCRTStartup /SUBSYSTEM:WINDOWS)
    # endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE Dwmapi)
endif()
