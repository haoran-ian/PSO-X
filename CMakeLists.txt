cmake_minimum_required(VERSION 3.10)

# 项目名称
project(ParticleSwarmOptimization)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 将 gsl 文件夹添加到系统包含路径
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/gsl)

# 包含 src 和 Functions 目录中的头文件
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/Functions)

# 设置源文件变量，包括 src 和 Functions 目录下的所有 cpp 文件
file(GLOB SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/Functions/*.cpp
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# 查找并链接 GSL 库
find_package(GSL REQUIRED)
include_directories(${GSL_INCLUDE_DIRS})

# 生成可执行文件
add_executable(pso ${SOURCES})
target_link_libraries(pso ${GSL_LIBRARIES} ${GSL_CBLAS_LIBRARIES})
