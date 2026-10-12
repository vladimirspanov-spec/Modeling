cmake_minimum_required(VERSION 3.5)

project(Modeling VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_library(calendar_core
    src/domain/calendarevent.h
    src/services/calendarservice.h
    src/services/calendarservice.cpp
    src/services/calendarsetup.h
    src/services/calendarsetup.cpp
    src/simulation/simulationengine.h
    src/simulation/simulationengine.cpp
)
target_include_directories(calendar_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(calendar_core PUBLIC Qt${QT_VERSION_MAJOR}::Core)

set(PROJECT_SOURCES
        main.cpp
        widget.cpp
        widget.h
        simulationwindow.cpp
        simulationwindow.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Modeling
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET Modeling APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(Modeling SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(Modeling
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(Modeling PRIVATE calendar_core Qt${QT_VERSION_MAJOR}::Widgets)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(calendarservice_test tests/calendarservice_test.cpp)
    target_link_libraries(calendarservice_test PRIVATE calendar_core Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME calendarservice COMMAND calendarservice_test)
    add_executable(simulationengine_test tests/simulationengine_test.cpp)
    target_link_libraries(simulationengine_test PRIVATE calendar_core Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME simulationengine COMMAND simulationengine_test)
    add_executable(calendarwindow_test tests/calendarwindow_test.cpp simulationwindow.cpp widget.cpp widget.h)
    target_link_libraries(calendarwindow_test PRIVATE calendar_core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME calendarwindow COMMAND calendarwindow_test)
    set_tests_properties(calendarwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Modeling)
endif()
set_target_properties(Modeling PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Modeling
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Modeling)
endif()
