cmake_minimum_required(VERSION 3.8)
project(timermanager_skipping)
add_compile_options(-g)
# 默认使用 C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

# 定义一个 CMake 选项，默认关闭
option(RCLCPP_EXP_LATENCY "Enable experiment-latency in rclcpp" OFF)
option(RCLCPP_EXP_SEPARATE "set separate flag true" OFF)

# 如果选项开启，则添加对应的编译定义
if(RCLCPP_EXP_LATENCY)
  add_compile_definitions(RCLCPP_EXP_LATENCY)
endif()

if(RCLCPP_EXP_SEPARATE)
  add_compile_definitions(RCLCPP_EXP_SEPARATE)
endif()

# 查找 ROS 2 所需的包
find_package(ament_cmake REQUIRED)
find_package(example_interfaces REQUIRED)
find_package(rcl REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rcpputils REQUIRED)
find_package(rcutils REQUIRED)
find_package(rmw REQUIRED)
find_package(std_msgs REQUIRED)

# 添加可执行文件
add_executable(timermanager_skipping src/timermanager_skipping.cpp)

# 链接依赖库
ament_target_dependencies(timermanager_skipping
  "rclcpp"
  "std_msgs"
  "rclcpp_components"
  "rmw_fastrtps_cpp"
)

ament_package()

