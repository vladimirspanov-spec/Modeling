cmake_minimum_required(VERSION 3.16)
project(Modeling VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_library(calendar_core
    src/services/calendarservice.cpp
    src/services/calendarsetup.cpp
    src/services/conflictresolver.cpp
    src/services/reminderservice.cpp
    src/simulation/simulationengine.cpp
    src/storage/jsoncodec.cpp
    src/storage/simulationstore.cpp
    src/reporting/simulationreport.cpp
    src/experiments/experimentrunner.cpp
)
target_include_directories(calendar_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(calendar_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)

add_library(calendar_ui
    widget.cpp widget.h simulationwindow.cpp simulationwindow.h
    officescene.cpp officescene.h src/ui/runtools.cpp src/ui/experimentdialog.cpp
)
target_link_libraries(calendar_ui PUBLIC calendar_core Qt${QT_VERSION_MAJOR}::Widgets)
add_executable(Modeling main.cpp)
target_link_libraries(Modeling PRIVATE calendar_ui)
add_executable(modeling_experiments src/experiments/experimentmain.cpp)
target_link_libraries(modeling_experiments PRIVATE calendar_core)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    foreach(name calendarservice simulationengine completion)
        add_executable(${name}_test tests/${name}_test.cpp)
        target_link_libraries(${name}_test PRIVATE calendar_core Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
    add_executable(calendarwindow_test tests/calendarwindow_test.cpp)
    target_link_libraries(calendarwindow_test PRIVATE calendar_ui Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME calendarwindow COMMAND calendarwindow_test)
    set_tests_properties(calendarwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
include(GNUInstallDirs)
install(TARGETS Modeling modeling_experiments RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
