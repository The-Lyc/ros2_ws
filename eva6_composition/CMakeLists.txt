cmake_minimum_required(VERSION 3.8)
project(eva6_composition)
add_compile_options(-g -O2)

add_compile_definitions(EXP_QOS)
# Keep EXP_OVERHEAD off here — runtime overhead study is in eva1 already.

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)

set(COMP_TARGETS
  comp_3ns1s
  comp_2ns2s
  comp_1ns3s
  comp_0ns4s)

foreach(tgt ${COMP_TARGETS})
  add_executable(${tgt} src/${tgt}.cpp)
  ament_target_dependencies(${tgt}
    rclcpp
    std_msgs
    rclcpp_components
    rmw_fastrtps_cpp)
endforeach()

install(TARGETS ${COMP_TARGETS}
  DESTINATION lib/${PROJECT_NAME})

ament_package()
