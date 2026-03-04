cmake_minimum_required(VERSION 3.10)
project(mySmartHostUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 1. 寻找 Qt5 基础及扩展组件
# 将所有需要的模块放在一个 find_package 中是最推荐的做法
find_package(Qt5 5.14 REQUIRED COMPONENTS 
    Widgets 
    SerialPort 
    Network
)

# 2. 定义源文件
add_executable(mySmartHostUI
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
)

# 3. 链接库文件
# 确保 Qt5::Network 被正确链接，否则编译时会报 "QTcpSocket" 未定义错误
target_link_libraries(mySmartHostUI PRIVATE 
    Qt5::Widgets 
    Qt5::SerialPort
    Qt5::Network
)