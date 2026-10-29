cmake_minimum_required(VERSION 3.16)

project(plain VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core Gui Widgets Network WebEngineCore WebEngineWidgets)

add_executable(plain
    src/main.cpp
    src/Browser.h
    src/Browser.cpp
    src/WebView.h
    src/WebView.cpp
    src/AdBlocker.h
    src/AdBlocker.cpp
    src/Theme.h
    src/Theme.cpp
    src/Panel.h
    src/Panel.cpp
    src/Quirks.h
    src/Quirks.cpp
)

target_compile_definitions(plain PRIVATE
    PLAIN_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS
)

target_compile_options(plain PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(plain PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    Qt6::WebEngineCore Qt6::WebEngineWidgets)

option(PLAIN_BUILD_TESTS "Build the behaviour tests" OFF)
if(PLAIN_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(test_plain
        tests/test_plain.cpp
        src/Browser.h src/Browser.cpp
        src/WebView.h src/WebView.cpp
        src/Theme.h src/Theme.cpp
        src/Panel.h src/Panel.cpp
        src/Quirks.h src/Quirks.cpp
        src/AdBlocker.h src/AdBlocker.cpp)
    target_compile_definitions(test_plain PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(test_plain PRIVATE
        Qt6::Widgets Qt6::Network Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::Test)
    add_test(NAME test_plain COMMAND test_plain)
endif()

include(GNUInstallDirs)
install(TARGETS plain RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES plain.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/plain.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
