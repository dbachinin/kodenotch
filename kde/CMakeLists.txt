cmake_minimum_required(VERSION 3.21)

project(codenotch-plasma VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(BUILD_PLASMOID "Build and install the Plasma widget" ON)
set(PLASMA_MAJOR_VERSION "6" CACHE STRING "Plasma major version to target (5 or 6)")
set_property(CACHE PLASMA_MAJOR_VERSION PROPERTY STRINGS 5 6)
if(NOT PLASMA_MAJOR_VERSION MATCHES "^[56]$")
    message(FATAL_ERROR "PLASMA_MAJOR_VERSION must be 5 or 6")
endif()

if(BUILD_PLASMOID)
    find_package(ECM REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

    include(KDEInstallDirs)
    include(KDECMakeSettings)
    include(KDECompilerSettings NO_POLICY_SCOPE)

    if(PLASMA_MAJOR_VERSION EQUAL 6)
        find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Qml Test)
        find_package(Plasma 6.0 REQUIRED)
        set(QT_PACKAGE Qt6)
        set(PLASMA_PACKAGE_DIR package)
    else()
        find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network Qml Test)
        find_package(KF5Plasma 5.27 REQUIRED)
        set(QT_PACKAGE Qt5)
        set(PLASMA_PACKAGE_DIR package5)
    endif()

    plasma_install_package(${PLASMA_PACKAGE_DIR} com.github.hivinz.codenotch)

    add_library(codenotchplugin SHARED
        plugin/codenotchplugin.cpp
        plugin/codenotchplugin.h
        plugin/antigravitybackend.cpp
        plugin/antigravitybackend.h
        plugin/claudebackend.cpp
        plugin/claudebackend.h
        plugin/usagebackend.cpp
        plugin/usagebackend.h
    )
    target_link_libraries(codenotchplugin PRIVATE
        ${QT_PACKAGE}::Core ${QT_PACKAGE}::Network ${QT_PACKAGE}::Qml)
    target_compile_definitions(codenotchplugin PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

    install(TARGETS codenotchplugin
        DESTINATION ${KDE_INSTALL_QMLDIR}/com/github/hivinz/codenotch)
    install(FILES plugin/qmldir plugin/plugins.qmltypes
        DESTINATION ${KDE_INSTALL_QMLDIR}/com/github/hivinz/codenotch)
    install(FILES
        icons/kodenotch.svg
        icons/kodenotch-antigravity.svg
        icons/kodenotch-claude.svg
        icons/kodenotch-codex.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
    install(FILES metainfo/com.github.hivinz.codenotch.appdata.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
else()
    if(PLASMA_MAJOR_VERSION EQUAL 6)
        find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Test)
        set(QT_PACKAGE Qt6)
    else()
        find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network Test)
        set(QT_PACKAGE Qt5)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(usagebackendtest
        tests/usagebackendtest.cpp
        plugin/antigravitybackend.cpp
        plugin/antigravitybackend.h
        plugin/claudebackend.cpp
        plugin/claudebackend.h
        plugin/usagebackend.cpp
        plugin/usagebackend.h
    )
    target_include_directories(usagebackendtest PRIVATE plugin)
    target_link_libraries(usagebackendtest PRIVATE
        ${QT_PACKAGE}::Core ${QT_PACKAGE}::Network ${QT_PACKAGE}::Test)
    add_test(NAME usagebackendtest COMMAND usagebackendtest)
endif()
