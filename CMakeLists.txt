cmake_minimum_required(VERSION 3.21)
project(qemu-gui-manager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent Gui Network Widgets)
qt_standard_project_setup()

# Everything but the windows, so that tests can use it
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
qt_add_library(qgm_core STATIC ${CORE_SOURCES})
target_include_directories(qgm_core PUBLIC src)
target_link_libraries(qgm_core PUBLIC
    Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Network)
target_compile_options(qgm_core PRIVATE -Wall -Wextra)

file(GLOB UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/ui/*.h)
qt_add_executable(qemu-gui-manager src/main.cpp ${UI_SOURCES})
target_link_libraries(qemu-gui-manager PRIVATE qgm_core Qt6::Widgets)
target_compile_options(qemu-gui-manager PRIVATE -Wall -Wextra)
install(TARGETS qemu-gui-manager RUNTIME DESTINATION bin)

option(BUILD_TESTING "Build the unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)
    foreach(source ${TEST_SOURCES})
        get_filename_component(name ${source} NAME_WE)
        qt_add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE qgm_core Qt6::Test)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
endif()
