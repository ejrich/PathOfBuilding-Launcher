cmake_minimum_required(VERSION 3.15)
project("Path of Building" C CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")

set(LAUNCHER_SOURCES
    "Launcher.cpp"
)

add_executable(PathOfBuilding ${LAUNCHER_SOURCES})
set_target_properties(PathOfBuilding PROPERTIES OUTPUT_NAME "Path of Building")

add_executable(PathOfBuilding-PoE2 ${LAUNCHER_SOURCES})
set_target_properties(PathOfBuilding-PoE2 PROPERTIES OUTPUT_NAME "Path of Building-PoE2")
target_compile_definitions(PathOfBuilding-PoE2 PRIVATE GAMEVERSION_2=1)
