cmake_minimum_required(VERSION 3.21)
project(qemu-gui-manager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent DBus Gui Network Widgets)
qt_standard_project_setup()

# Everything but the windows, so that tests can use it
qt_add_library(qgm_core STATIC
    src/core/argsfile.cpp
    src/core/argsfile.h
    src/core/firmware.cpp
    src/core/firmware.h
    src/core/guestagent.cpp
    src/core/guestagent.h
    src/core/hostdevices.cpp
    src/core/hostdevices.h
    src/core/hostmemory.cpp
    src/core/hostmemory.h
    src/core/importer.cpp
    src/core/importer.h
    src/core/optionvalue.cpp
    src/core/optionvalue.h
    src/core/paths.cpp
    src/core/paths.h
    src/core/qemubuilder.cpp
    src/core/qemubuilder.h
    src/core/qemuinfo.cpp
    src/core/qemuinfo.h
    src/core/qmpclient.cpp
    src/core/qmpclient.h
    src/core/vmconfig.cpp
    src/core/vmcloner.cpp
    src/core/vmcloner.h
    src/core/vmconfig.h
    src/core/vmhardware.cpp
    src/core/vmhardware.h
    src/core/vmrunner.cpp
    src/core/vmrunner.h
    src/core/vmstore.cpp
    src/core/vmstore.h
    src/core/vmtemplate.cpp
    src/core/vmtemplate.h
)
target_include_directories(qgm_core PUBLIC src)
target_link_libraries(qgm_core PUBLIC
    Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Network)
target_compile_options(qgm_core PRIVATE -Wall -Wextra)

qt_add_executable(qemu-gui-manager
    src/main.cpp
    src/ui/argseditor.cpp
    src/ui/argseditor.h
    src/ui/banner.cpp
    src/ui/banner.h
    src/ui/clonedialog.cpp
    src/ui/clonedialog.h
    src/ui/icons.cpp
    src/ui/icons.h
    src/ui/importdialog.cpp
    src/ui/importdialog.h
    src/ui/logview.cpp
    src/ui/logview.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/memorymonitor.cpp
    src/ui/memorymonitor.h
    src/ui/newvmdialog.cpp
    src/ui/newvmdialog.h
    src/ui/preferencesdialog.cpp
    src/ui/preferencesdialog.h
    src/ui/qemubuilddialog.cpp
    src/ui/qemubuilddialog.h
    src/ui/qemudocs.cpp
    src/ui/qemudocs.h
    src/ui/referencepanel.cpp
    src/ui/referencepanel.h
    src/ui/settingspages.cpp
    src/ui/settingspages.h
    src/ui/textdialog.cpp
    src/ui/textdialog.h
    src/ui/uiconfig.cpp
    src/ui/uiconfig.h
    src/ui/usbaccess.cpp
    src/ui/usbaccess.h
    src/ui/vmdetails.cpp
    src/ui/vmdetails.h
    src/ui/vmpane.cpp
    src/ui/vmpane.h
    src/ui/vmwindow.cpp
    src/ui/vmwindow.h
    src/ui/widgets.cpp
    src/ui/widgets.h
)
qt_add_resources(qemu-gui-manager icons PREFIX /icons BASE data
    FILES data/qemu-gui-manager.svg)
target_link_libraries(qemu-gui-manager PRIVATE qgm_core Qt6::DBus Qt6::Widgets)
target_compile_definitions(qemu-gui-manager PRIVATE QGM_VERSION="${PROJECT_VERSION}")
target_compile_options(qemu-gui-manager PRIVATE -Wall -Wextra)

include(GNUInstallDirs)
install(TARGETS qemu-gui-manager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/qemu-gui-manager.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/qemu-gui-manager.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(BUILD_TESTING "Build the unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(name
            argsfile
            firmware
            guestagent
            hostdevices
            hostmemory
            importer
            optionvalue
            qemubuilder
            qemuinfo
            qmpclient
            vmcloner
            vmconfig
            vmhardware
            vmrunner
            vmstore
            vmtemplate)
        qt_add_executable(test_${name} tests/test_${name}.cpp)
        target_link_libraries(test_${name} PRIVATE qgm_core Qt6::Test)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
endif()
