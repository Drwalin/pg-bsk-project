cmake_minimum_required(VERSION 3.4.0)
project(pg-bsk-project VERSION 0.1.0)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

file(GLOB_RECURSE portable8439_sources "portable8439/src/*.c")
file(GLOB_RECURSE app_sources "src/*.cpp" "src/*.c")
add_library(portable8439_bsk STATIC ${portable8439_sources})

if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
endif()

include("secp256k1_bsk.cmake")

add_subdirectory("rpclib")

OPTION(LPG_GUI_BUILD_EXAMPLES "" OFF)
add_subdirectory("lpg-gui")

add_executable(pg-bsk-project ${app_sources})

target_link_libraries(pg-bsk-project PUBLIC secp256k1_bsk)
target_link_libraries(pg-bsk-project PUBLIC portable8439_bsk)
target_link_libraries(pg-bsk-project PUBLIC rpc)
target_link_libraries(pg-bsk-project PUBLIC lpg_gui)


target_include_directories(pg-bsk-project PUBLIC "src")
target_include_directories(pg-bsk-project PUBLIC "portable8439/src")
target_include_directories(pg-bsk-project PUBLIC "secp256k1/include")
target_include_directories(pg-bsk-project PUBLIC "rpclib/include")
target_include_directories(pg-bsk-project PUBLIC "dpp")
target_include_directories(pg-bsk-project PUBLIC "lpg-gui/axxegro/include")
target_include_directories(pg-bsk-project PUBLIC "lpg-gui/include")

add_custom_command(TARGET pg-bsk-project POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/resources/ ${CMAKE_CURRENT_BINARY_DIR}
)


set(CMAKE_EXPORT_COMPILE_COMMANDS 1)