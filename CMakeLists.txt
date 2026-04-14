cmake_minimum_required(VERSION 3.16)
project(CNC_Project VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 Qt 的宏和 UI 文件
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ==========================================
# 强制告诉 CMake 去哪里找 Qt6 库 (保持和你电脑的 MSYS2 路径一致)
# ==========================================
set(CMAKE_PREFIX_PATH "D:/msys64/ucrt64" CACHE PATH "MSYS2 UCRT64 Path")

# ==========================================
# 寻找 Qt6 核心库，以及修复后的 Svg 和 SvgWidgets 渲染库
# ==========================================
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg SvgWidgets)

# ==========================================
# dxflib 引擎头文件与源码集成
# (注：既然我们用了 Python+SVG 外挂，其实 C++ 里已经用不到 dxflib 了。
#  但为了防止你以后想切回纯 C++ 算法研究，这里先为你保留着)
# ==========================================
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dxflib)
file(GLOB DXFLIB_SOURCES "dxflib/*.cpp")

# ==========================================
# 把主程序文件、UI文件 和 dxflib 的源文件一起打包编译
# ==========================================
add_executable(${PROJECT_NAME} 
    main.cpp
    mainwindow.cpp
    mainwindow.h
    modern_cnc_dark.ui 
    ${DXFLIB_SOURCES}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/dxf2svg.py
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/dxf2svg.py
)

# ==========================================
# 将 Qt 的动态链接库完美绑定到程序上 (包含 Svg 和 SvgWidgets)
# ==========================================
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::SvgWidgets)
