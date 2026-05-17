cmake_minimum_required(VERSION 3.8)
project(eva5_overhead)
add_compile_options(-g -O2)

# Enable per-method overhead instrumentation inside QosPromisedQueue and SimpleEventsQueue
add_compile_definitions(EXP_OVERHEAD)
add_compile_definitions(EXP_QOS)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

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

add_executable(overhead_proposed src/overhead_proposed.cpp)
ament_target_dependencies(overhead_proposed
  "rclcpp"
  "std_msgs"
  "rclcpp_components"
  "rmw_fastrtps_cpp"
)

add_executable(overhead_events_false src/overhead_events_false.cpp)
ament_target_dependencies(overhead_events_false
  "rclcpp"
  "std_msgs"
  "rclcpp_components"
  "rmw_fastrtps_cpp"
)

install(TARGETS
  overhead_proposed
  overhead_events_false
  DESTINATION lib/${PROJECT_NAME})

ament_package()
