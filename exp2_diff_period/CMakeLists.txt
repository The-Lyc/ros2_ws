cmake_minimum_required(VERSION 3.8)
project(diff_period)

# 默认使用 C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
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
add_executable(diff_period src/same_period.cpp)

# 链接依赖库
ament_target_dependencies(diff_period
  "rclcpp"
  "std_msgs"
  "rclcpp_components"
  "rmw_fastrtps_cpp"
)

ament_package()

