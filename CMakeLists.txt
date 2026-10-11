cmake_minimum_required(VERSION 3.21)
project(VcpkgCompileTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

# ---- PAT 模組：有私有實作就用私有的，否則用公開的空殼 ----
set(PAT_PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/secret/PatProvider_private.cpp")
if(EXISTS "${PAT_PRIVATE}")
    set(PAT_SRC "${PAT_PRIVATE}")
    message(STATUS "PAT: using private PatProvider_private.cpp")
else()
    set(PAT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/secret/PatProvider_stub.cpp")
    message(WARNING "PAT: private provider not found -> stub (reads env VCPKG_COMPILE_PAT). Run tools/gen-pat.ps1")
endif()

qt_add_executable(VcpkgCompileTool WIN32
    src/main.cpp
    src/Config.h
    src/MainWindow.cpp src/MainWindow.h
    src/VcpkgManager.cpp src/VcpkgManager.h
    src/GitHubClient.cpp src/GitHubClient.h
    src/BuildController.cpp src/BuildController.h
    secret/PatProvider.h
    ${PAT_SRC}
)

qt_add_resources(VcpkgCompileTool "app_resources" PREFIX "/" FILES resources/build.yml)

target_include_directories(VcpkgCompileTool PRIVATE src secret)
target_link_libraries(VcpkgCompileTool PRIVATE Qt6::Widgets Qt6::Network)

if(MSVC)
    target_compile_options(VcpkgCompileTool PRIVATE /utf-8 /W4 /permissive-)
endif()
