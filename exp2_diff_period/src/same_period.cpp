#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <thread>
#include <vector>
#include <fstream>
#include <string>

//period in milliseconds
int period_[4]={
  200,200,200,200
};
// sleep time in microseconds
int sleep_[4]={
  2000,2000,2000,2000
};
const char* topic_[4]={
  "topic_5ms","topic_20ms","topic_50ms","topic_200ms"
};
// control the utilization
int var;
int cnt_[4];

class PublisherNode : public rclcpp::Node
{
public:
  PublisherNode() : Node("publisher_node")
  {
    for(int i=0;i<4;i++){
      publisher_.push_back(this->create_publisher<std_msgs::msg::String>(topic_[i], 10));
      timer_.push_back(this->create_wall_timer(
        std::chrono::milliseconds(period_[i]),
        [this,i]() {
          auto message = std_msgs::msg::String();
          message.data = topic_[i];
          cnt_[i]++;
          RCLCPP_INFO(this->get_logger(), "Publish: '%s'", message.data.c_str());
          publisher_[i]->publish(message);
        }));
    }
    timer_end = this->create_wall_timer(
      std::chrono::milliseconds(2099),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "timer_end";
        RCLCPP_INFO(this->get_logger(), "end\n");
        std::ofstream outfile("data.txt",std::ios::app);
        outfile << var*4 << "%:" <<cnt_[0]<<","<<cnt_[1]<<","<<cnt_[2]<<","<<cnt_[3]<<std::endl;
        rclcpp::shutdown();
      });
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> publisher_;
  std::vector<rclcpp::TimerBase::SharedPtr> timer_;
  rclcpp::TimerBase::SharedPtr timer_end;
};

class SubscriberNode : public rclcpp::Node
{
public:
  SubscriberNode() : Node("subscriber_node")
  {
    for(int i=0;i<4;i++){
      subscription_.push_back(this->create_subscription<std_msgs::msg::String>(
        topic_[i],
        10,
        [this,i](const std_msgs::msg::String::SharedPtr msg) {
          std::this_thread::sleep_for(std::chrono::microseconds(sleep_[i]*var));
          RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
        }));    
    }
  }

private:
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  var=std::atoi(argv[1]);
  
  // 创建 EventsExecutor
  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();

  // 创建节点
  auto publisher_node = std::make_shared<PublisherNode>();
  auto subscriber_node = std::make_shared<SubscriberNode>();

  // 将节点添加到执行器
  executor->add_node(publisher_node);
  executor->add_node(subscriber_node);

  // 开始执行
  executor->spin();

  rclcpp::shutdown();
  return 0;
}
