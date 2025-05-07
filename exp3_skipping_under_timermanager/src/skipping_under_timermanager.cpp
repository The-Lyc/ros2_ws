#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <thread>

class PublisherNode : public rclcpp::Node
{
public:
  PublisherNode() : Node("publisher_node")
  {
    publisher_ = this->create_publisher<std_msgs::msg::String>("topic", 10);
    timer_5ms = this->create_wall_timer(
      std::chrono::milliseconds(5),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "timer_5ms";
        cnt_5ms++;
        RCLCPP_INFO(this->get_logger(), "'%s'", message.data.c_str());
      });
    timer_10ms = this->create_wall_timer(
      std::chrono::milliseconds(10),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "timer_10ms";
        cnt_10ms++;
        RCLCPP_INFO(this->get_logger(), "'%s'", message.data.c_str());
      });
    timer_50ms = this->create_wall_timer(
      std::chrono::milliseconds(50),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "timer_50ms";
        cnt_50ms++;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        RCLCPP_INFO(this->get_logger(), "'%s'", message.data.c_str());
      });
    timer_end = this->create_wall_timer(
      std::chrono::milliseconds(252),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "timer_end";
        RCLCPP_INFO(this->get_logger(), "'%s'\ncnt_5ms:%d\ncnt_10ms:%d\ncnt_50ms:%d\n", message.data.c_str(),cnt_5ms,cnt_10ms,cnt_50ms);
        rclcpp::shutdown();
      });
  }

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_5ms;
  rclcpp::TimerBase::SharedPtr timer_10ms;
  rclcpp::TimerBase::SharedPtr timer_50ms;
  rclcpp::TimerBase::SharedPtr timer_end;

  int cnt_5ms=0;
  int cnt_10ms=0;
  int cnt_50ms=0;
};

class SubscriberNode : public rclcpp::Node
{
public:
  SubscriberNode() : Node("subscriber_node")
  {
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "topic",
      10,
      [this](const std_msgs::msg::String::SharedPtr msg) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
      });
  }

private:
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // 创建 EventsExecutor
  auto executor = std::make_shared<rclcpp::experimental::executors::EventsExecutor>();

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
