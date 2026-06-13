cmake_minimum_required(VERSION 3.16)

project(CockpitIpcDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(hmi_services
    src/hmi_services.cpp
)
add_executable(camera_service
    src/camera_service.cpp
)

add_executable(display_manager
    src/display_manager.cpp
)

add_executable(cleanup_ipc
    src/cleanup_ipc.cpp
)

target_link_libraries(hmi_services pthread rt)
target_link_libraries(camera_service pthread rt)
target_link_libraries(display_manager pthread rt)
target_link_libraries(cleanup_ipc pthread rt)
