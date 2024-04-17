#include "rclcpp/rclcpp.hpp"
#include "rclcpp/node_interfaces/node_clock.hpp"
#include "rttest/rttest.h"
#include "rttest/utils.hpp"
#include "std_msgs/msg/string.hpp"

#include <fstream>

const int period = 10000000;
const int times = 100;

struct timespec start_time;
struct timespec expected_time;
struct timespec now_time;
struct timespec latency;
struct timespec T;

std::ofstream outFile;

long long int cnt,res;

using std::placeholders::_1;
        
class TimerTestNode : public rclcpp::Node {
public:
    TimerTestNode() : Node("timer_test_node") {
        clock_gettime(CLOCK_MONOTONIC,&expected_time);
        clock_gettime(CLOCK_MONOTONIC,&start_time);
        // subtract_timespecs(&start_time,&expected_time,&latency);
        std::cout<<"^^^"<<timespec_to_uint64(&latency)<<std::endl;
        // 创建一个定时器，周期为1秒
        timer_ = create_wall_timer(std::chrono::nanoseconds(T.tv_nsec), std::bind(&TimerTestNode::timer_callback, this));
        publisher_H = this->create_publisher<std_msgs::msg::String>("topic1", 100);
        // Subscriptions
        subscription_H = this->create_subscription<std_msgs::msg::String>(
            "topic1", 100, std::bind(&TimerTestNode::topic_callback_H, this, _1));
    }

private:
    void call_back(){
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void topic_callback_H(const std_msgs::msg::String::SharedPtr msg)
    {
        RCLCPP_INFO(this->get_logger(), "Topic callback H: Start");   
        call_back();
        return;            
    }

    void timer_callback() {
        // 记录定时器回调的时间点
        // auto callback_time = now();
        
        // 计算定时器回调的延迟
        // std::cout<<"@"<<timespec_to_uint64(&expected_time)<<std::endl;
        
        
        res++;
        if(res>=times)   {
            outFile.close();

            clock_gettime(CLOCK_MONOTONIC,&now_time);
            subtract_timespecs(&now_time,&start_time,&latency);

            cnt = timespec_to_uint64(&latency)/T.tv_nsec;
            std::cout<<"cnt:"<<cnt<<std::endl;
            std::cout<<"res:"<<res<<std::endl;
            rclcpp::shutdown();
            outFile.close();
        }
        clock_gettime(CLOCK_MONOTONIC,&now_time);
        subtract_timespecs(&now_time,&expected_time,&latency);
        clock_gettime(CLOCK_MONOTONIC,&expected_time);

        outFile<<timespec_to_uint64(&latency)<<std::endl;
        // add_timespecs(&expected_time, &T, &expected_time);
        
        // 打印延迟情况
        // RCLCPP_INFO(get_logger(), "Timer callback delay: %ld milliseconds", delay);

        auto message = std_msgs::msg::String();
        message.data = "hello";
        publisher_H->publish(message);
    }

    rclcpp::TimerBase::SharedPtr timer_;
    // struct timespec expected, wakeup, wakeup_latency;
    // vector<uint64_t> wakeup_latencies(params.rt.iterations);
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_H;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_H;
};

int main(int argc, char* argv[]) {
    // std::cout << __cplusplus << std::endl;
    
    T.tv_sec = 0;
    T.tv_nsec = period;

    std::cout<<"*"<<timespec_to_uint64(&T)<<std::endl;
    
    outFile.open("../result/res");

    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<TimerTestNode>());
    rclcpp::shutdown();
    outFile.close();

    clock_gettime(CLOCK_MONOTONIC,&now_time);
    subtract_timespecs(&now_time,&expected_time,&latency);

    cnt = timespec_to_uint64(&latency)/T.tv_nsec;
    outFile<<"cnt:"<<cnt<<std::endl;
    outFile<<"res:"<<res<<std::endl;

    return 0;
}
