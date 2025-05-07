#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <thread>
#include <vector>

#define Sleep_5ms 1
#define Sleep_20ms 2
#define Sleep_50ms 5
#define Sleep_100ms 10

int cnt_5ms=0;
int cnt_20ms=0;
int cnt_50ms=0;
int cnt_100ms=0;

int subcnt_5ms=0;
int subcnt_20ms=0;
int subcnt_50ms=0;
int subcnt_100ms=0;

std::vector<int> vec_5ms,vec_20ms,vec_50ms,vec_100ms;

rclcpp::Clock my_clock;

rclcpp::Time start_time;
rclcpp::Time current_time;

class PublisherNode : public rclcpp::Node
{
public:
  PublisherNode() : Node("publisher_node")
  {
    start_time = my_clock.now();
    publisher_ = this->create_publisher<std_msgs::msg::String>("topic", 10);
    publisher_5ms = this->create_publisher<std_msgs::msg::String>("topic_5ms", 10);
    publisher_20ms = this->create_publisher<std_msgs::msg::String>("topic_20ms", 10);
    publisher_50ms = this->create_publisher<std_msgs::msg::String>("topic_50ms", 10);
    publisher_100ms = this->create_publisher<std_msgs::msg::String>("topic_100ms", 10);
    timer_5ms = this->create_wall_timer(
      std::chrono::milliseconds(5),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "###timer_5ms called###";
        RCLCPP_INFO(this->get_logger(), "Publishing: '%s'", message.data.c_str());
        cnt_5ms++;
        current_time=my_clock.now();
        rclcpp::Duration delta=current_time-start_time;
        vec_5ms.push_back(delta.nanoseconds()/1'000'000);
        publisher_5ms->publish(message);
      });
    timer_20ms = this->create_wall_timer(
      std::chrono::milliseconds(20),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "###timer_20ms called###";
        RCLCPP_INFO(this->get_logger(), "Publishing: '%s'", message.data.c_str());
        cnt_20ms++;
        current_time=my_clock.now();
        rclcpp::Duration delta=current_time-start_time;
        vec_20ms.push_back(delta.nanoseconds()/1'000'000);
        publisher_20ms->publish(message);
      });
    timer_50ms = this->create_wall_timer(
      std::chrono::milliseconds(50),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "###timer_50ms called###";
        RCLCPP_INFO(this->get_logger(), "Publishing: '%s'", message.data.c_str());
        cnt_50ms++;
        current_time=my_clock.now();
        rclcpp::Duration delta=current_time-start_time;
        vec_50ms.push_back(delta.nanoseconds()/1'000'000);
        publisher_50ms->publish(message);
      });
    timer_100ms = this->create_wall_timer(
      std::chrono::milliseconds(100),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "###timer_100ms called###";
        RCLCPP_INFO(this->get_logger(), "Publishing: '%s'", message.data.c_str());
        cnt_100ms++;
        current_time=my_clock.now();
        rclcpp::Duration delta=current_time-start_time;
        vec_100ms.push_back(delta.nanoseconds()/1'000'000);
        publisher_100ms->publish(message);
      });
    timer_end = this->create_wall_timer(
      std::chrono::milliseconds(2003),
      [this]() {
        auto message = std_msgs::msg::String();
        message.data = "###timer_end called###";
        RCLCPP_INFO(this->get_logger(), "Publishing: '%s'\n cnt_5ms:%d,cnt_20ms:%d,cnt_50ms:%d,cnt_100ms:%d\n", message.data.c_str(),cnt_5ms,cnt_20ms,cnt_50ms,cnt_100ms);
        int size=vec_5ms.size();
        for(int i=0;i<size;i++) std::cout<<"vec_5ms:"<<vec_5ms[i]<<", ";
        std::cout<<std::endl;
        size=vec_20ms.size();
        for(int i=0;i<size;i++) std::cout<<"vec_20ms:"<<vec_20ms[i]<<", ";
        std::cout<<std::endl;
        size=vec_50ms.size();
        for(int i=0;i<size;i++) std::cout<<"vec_50ms:"<<vec_50ms[i]<<", ";
        std::cout<<std::endl;
        size=vec_100ms.size();
        for(int i=0;i<size;i++) std::cout<<"vec_100ms:"<<vec_100ms[i]<<", ";
        std::cout<<std::endl;

        publisher_->publish(message);
        rclcpp::shutdown();
      });

  }

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_5ms;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_20ms;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_50ms;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_100ms;
  rclcpp::TimerBase::SharedPtr timer_5ms;
  rclcpp::TimerBase::SharedPtr timer_20ms;
  rclcpp::TimerBase::SharedPtr timer_50ms;
  rclcpp::TimerBase::SharedPtr timer_100ms;
  rclcpp::TimerBase::SharedPtr timer_end;
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
        RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
        rclcpp::shutdown();
      });
    subscription_5ms = this->create_subscription<std_msgs::msg::String>(
      "topic_5ms",
      10,
      [this](const std_msgs::msg::String::SharedPtr msg) {
        RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(Sleep_5ms));
      });
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "topic_20ms",
      10,
      [this](const std_msgs::msg::String::SharedPtr msg) {
        RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(Sleep_20ms));
      });
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "topic_50ms",
      10,
      [this](const std_msgs::msg::String::SharedPtr msg) {
        RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(Sleep_50ms));
      });
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "topic_100ms",
      10,
      [this](const std_msgs::msg::String::SharedPtr msg) {
        RCLCPP_INFO(this->get_logger(), "Received: '%s'", msg->data.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(Sleep_100ms));
      });

  }

private:
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_5ms;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_20ms;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_50ms;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_100ms;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // 创建 EventsExecutor
  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();


  // 创建节点
  auto publisher_node = std::make_shared<PublisherNode>();
  auto subscriber_node = std::make_shared<SubscriberNode>();


  // 将节点添加到执行器
  executor->add_node(publisher_node);
  executor->add_node(subscriber_node);


  // 开始执
  executor->spin();

  rclcpp::shutdown();
  return 0;
}
