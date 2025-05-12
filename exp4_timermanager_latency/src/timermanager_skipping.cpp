#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <vector>
#include <thread>

int period[4]={
  5,20,50,200
};
int sleep_time[4]={
  1,4,10,40
};
const char* topic[4]={
  "topic_5ms","topic_20ms","topic_50ms","topic_100ms"
};
char* topic_second[4]={
  "second_1","second_2","second_3","second_4"
};
int timer_cnt[4];

std::vector<void*> timer_vec;

class PublisherNode : public rclcpp::Node
{
public:
  PublisherNode() : Node("mypublishernode")
  {
    for(int i=0;i<4;i++){
      mypublisher.push_back(this->create_publisher<std_msgs::msg::String>(topic[i], 10));
      mytimer.push_back(this->create_wall_timer(
        std::chrono::milliseconds(period[i]),
        [this,i]() {
          timer_cnt[i]++;
          auto message = std_msgs::msg::String();
          message.data = "Hello, ROS 2 with EventsExecutor!";
          RCLCPP_INFO(this->get_logger(), "Publishing: '%s' to '%s'", message.data.c_str(),topic[i]);
          mypublisher[i]->publish(message);
        }));
    }
    timer_end = this->create_wall_timer(
        std::chrono::milliseconds(410),
        [this]() {
          RCLCPP_INFO(this->get_logger(), "System end\n###cnt:%d,%d,%d,%d",timer_cnt[0],timer_cnt[1],timer_cnt[2],timer_cnt[3]);
          rclcpp::shutdown();
        });
  }

  void* GetEventKey(int x){
    return mytimer[x].get();
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> mypublisher;
  std::vector<rclcpp::TimerBase::SharedPtr> mytimer;
  rclcpp::TimerBase::SharedPtr timer_end;
};

class SubscriberNode : public rclcpp::Node
{
public:
  SubscriberNode() : Node("subscriber_node")
  {
    for(int i=0;i<4;i++){
      mysubscription.push_back(this->create_subscription<std_msgs::msg::String>(
        topic[i],
        10,
        [this,i](const std_msgs::msg::String::SharedPtr msg) {
          RCLCPP_INFO(this->get_logger(), "Received: '%s' from '%s'", msg->data.c_str(),topic[i]);
          std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time[i]));
        }));
    }
  }

  void* GetEventKey(int x){
    return mysubscription[x]->get_subscription_handle().get();
  }

private:
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> mysubscription;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // 创建节点
  auto mypublishernode = std::make_shared<PublisherNode>();
  auto subscriber_node = std::make_shared<SubscriberNode>();

  // 创建 EventsExecutor
  auto event_queue = std::make_unique<rclcpp::experimental::executors::SimpleEventsQueue>();
  for(int i=0;i<2;i++)
    for(int j=0;j<4;j++){
      std::cout<<"++++++++"<<i<<","<<j<<"+++++++++"<<std::endl;
      void* key=nullptr;
      if(!i)  {
        key=mypublishernode->GetEventKey(j);
        timer_vec.push_back(key);
      }
      else{
        key=subscriber_node->GetEventKey(j);
        std::cout<<"$$$$$$$$$sub"<<j<<"is:"<<key<<std::endl;
      }
      event_queue->register_event(timer_vec[j],key,false,{i,j},0);
    }
  bool flag = false;
  rclcpp::ExecutorOptions options;
  options.context = rclcpp::contexts::get_global_default_context();
  auto executor = std::make_shared<rclcpp::experimental::executors::EventsExecutor>(std::move(event_queue),flag,options);

  // 将节点添加到执行器
  executor->add_node(mypublishernode);
  executor->add_node(subscriber_node);

  std::cout<<"--------------------------"<<std::endl;
  // 开始执
  executor->spin();

  rclcpp::shutdown();
  return 0;
}
