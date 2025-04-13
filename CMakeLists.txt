cmake_minimum_required(VERSION 3.16)

project(SingleApplication)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

# SingleApplication base class
set(QAPPLICATION_CLASS QCoreApplication CACHE STRING "Inheritance class for SingleApplication")
set_property(CACHE QAPPLICATION_CLASS PROPERTY STRINGS QApplication QGuiApplication QCoreApplication)

# Libary target
add_library(${PROJECT_NAME} STATIC
    singleapplication.cpp
    singleapplication_p.cpp
    )

# Find dependencies
find_package(Qt6Network)
if(QAPPLICATION_CLASS STREQUAL QApplication)
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
elseif(QAPPLICATION_CLASS STREQUAL QGuiApplication)
    find_package(Qt6 COMPONENTS Gui REQUIRED)
else()
    find_package(Qt6 COMPONENTS Core REQUIRED)
endif()
add_compile_definitions(QAPPLICATION_CLASS=${QAPPLICATION_CLASS})

# Link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Network)
if(QAPPLICATION_CLASS STREQUAL QApplication)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets)
elseif(QAPPLICATION_CLASS STREQUAL QGuiApplication)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Gui)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE advapi32)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
