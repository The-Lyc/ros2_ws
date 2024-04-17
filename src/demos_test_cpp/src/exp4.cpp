/*
event executor
*/

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/node_interfaces/node_clock.hpp"
#include "rttest/rttest.h"
#include "rttest/utils.hpp"
#include "std_msgs/msg/string.hpp"
#include "rclcpp/experimental/executors/events_executor/events_executor.hpp"
#include "rclcpp/experimental/timers_manager.hpp"

#include <fstream>

using rclcpp::experimental::TimersManager;

const int period = 100000000;
const int times = 1000;

struct timespec start_time;
struct timespec last_time;
struct timespec now_time;
struct timespec latency;
struct timespec T;

std::ofstream outFile;
std::ofstream overFile;

long long int cnt,res,sl;

using std::placeholders::_1;

class EventNode : public rclcpp::Node {
public :
    EventNode() : Node("event_node") {
        auto context = std::make_shared<rclcpp::Context>();
        manager_ = std::make_shared<rclcpp::experimental::TimersManager>(context);

        clock_gettime(CLOCK_MONOTONIC,&last_time);
        clock_gettime(CLOCK_MONOTONIC,&start_time);

        timer_ = create_wall_timer(std::chrono::nanoseconds(T.tv_nsec), std::bind(&EventNode::timer_callback, this));

        manager_->add_timer(timer_);
        manager_->start();

        // Publisher
        publisher_H = this->create_publisher<std_msgs::msg::String>("topic1", 0);

        // Subscription
        subscription_H = this->create_subscription<std_msgs::msg::String>(
            "topic1", 0, std::bind(&EventNode::topic_callback_H, this, _1));
    }
private :
    void call_back(){
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sl++;
        // overFile<<sl<<": over"<<std::endl; 
    }

    void topic_callback_H(const std_msgs::msg::String::SharedPtr msg)
    {
        // RCLCPP_INFO(this->get_logger(), "Topic callback H: Start");   
        call_back();
        return;            
    }

    void timer_callback() {       
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
        subtract_timespecs(&now_time,&last_time,&latency);
        clock_gettime(CLOCK_MONOTONIC,&last_time);

        outFile<<timespec_to_uint64(&latency)<<std::endl;

        auto message = std_msgs::msg::String();
        message.data = "hello";
        publisher_H->publish(message);
        // call_back();
    }
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::experimental::TimersManager::SharedPtr manager_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_H;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_H;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    T.tv_sec = 0;
    T.tv_nsec = period;
    
    outFile.open("../result/res2");
    overFile.open("../result/over");

    auto node = std::make_shared<EventNode>();

    
    rclcpp::experimental::executors::EventsExecutor executor(std::make_unique<
      rclcpp::experimental::executors::SimpleEventsQueue>(),true,rclcpp::ExecutorOptions());
    executor.add_node(node);
    executor.spin();
    executor.remove_node(node);
    rclcpp::shutdown();

    return 0;
}