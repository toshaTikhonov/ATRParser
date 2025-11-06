cmake_minimum_required(VERSION 3.16)

project(ATRParser VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt packages
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Core Widgets)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

# PC/SC Lite library
find_library(PCSCLITE_LIBRARY 
    NAMES pcsclite PCSC winscard
    PATHS 
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
        "C:/Program Files/PCSC"
)

if(NOT PCSCLITE_LIBRARY)
    message(FATAL_ERROR "PC/SC Lite library not found! Install it with: sudo apt-get install libpcsclite-dev")
endif()

message(STATUS "Found PC/SC library: ${PCSCLITE_LIBRARY}")

# Определяем пути к заголовкам
if(WIN32)
    set(PCSCLITE_INCLUDE_DIR "C:/Program Files/PCSC/include")
elseif(APPLE)
    set(PCSCLITE_INCLUDE_DIR "/System/Library/Frameworks/PCSC.framework/Headers")
else()
    set(PCSCLITE_INCLUDE_DIR "/usr/include/PCSC")
endif()

# Common source files
set(COMMON_SOURCES
    atrparser.cpp
    atrparser.h
    cardreader.cpp
    cardreader.h
)

# GUI Application
add_executable(atrparser_gui
    main.cpp
    ${COMMON_SOURCES}
)

target_link_libraries(atrparser_gui
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    ${PCSCLITE_LIBRARY}
)

target_include_directories(atrparser_gui PRIVATE ${PCSCLITE_INCLUDE_DIR})

# Console Application
add_executable(atrparser_console
    console_example.cpp
    ${COMMON_SOURCES}
)

target_link_libraries(atrparser_console
    Qt${QT_VERSION_MAJOR}::Core
    ${PCSCLITE_LIBRARY}
)

target_include_directories(atrparser_console PRIVATE ${PCSCLITE_INCLUDE_DIR})

# Install targets
install(TARGETS atrparser_gui atrparser_console
    RUNTIME DESTINATION bin
)

# Print build info
message(STATUS "Qt version: ${QT_VERSION_MAJOR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
