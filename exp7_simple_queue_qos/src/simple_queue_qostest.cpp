#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <vector>
#include <thread>
#include <map>
#include <chrono>
#include <deque>
#include <iostream>
#include <string>
#include <fstream>

int period[4]={
  5,20,50,200
};
int sleep_time[4]={
  1,4,10,40
};
size_t qos[4]={
  7,25,63,250
};
const char* topic[4]={
  "topic_5ms","topic_20ms","topic_50ms","topic_100ms"
};
const char* topic_second[4]={
  "second_1","second_2","second_3","second_4"
};
int timer_cnt[4];

std::vector<void*> timer_vec;
std::map<const void*, std::pair<int,int>> pos_map;


#define BURST_IDX 2
#define BURST_TIME 20
#define WINDOW_SIZE 5

using TimePoint = std::chrono::steady_clock::time_point;

class FrequencyMonitor {
public:
    using SharedPtr = std::shared_ptr<FrequencyMonitor>;
    FrequencyMonitor(size_t window_size = 10)
        : window_size_(window_size) {
      start_time_ = std::chrono::steady_clock::now();
      log_file_.open(log_file_name_, std::ios::out | std::ios::app);
      if (!log_file_.is_open()) {
          throw std::runtime_error("Failed to open log file");
      }
    }

    void record_time(const void* key) {
        TimePoint now = std::chrono::steady_clock::now();
        timestamps_[key].push_back(now);
        if (timestamps_[key].size() > window_size_) {
            timestamps_[key].pop_front();
        }
        if(!has_enough_data(key)) return;
        double frequency = average_frequency(key);
        auto duration = now - start_time_;
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000;
        log_file_ << "record time:"  <<  milliseconds  <<  "." <<  microseconds 
                  << ",Pos: " << pos_map[key].first << "," << pos_map[key].second 
                  << ",Frequency: " << frequency << " Hz\n";
        log_file_.flush();
    }

    bool has_enough_data(const void* key){
      if(timestamps_.find(key) == timestamps_.end()) return false;
      return timestamps_[key].size() >= 2;
    }

    double average_frequency(const void* key){
        if (!has_enough_data(key)) return 0.0;
        if (timestamps_.find(key) == timestamps_.end()) return 0.0;
        auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(
            timestamps_[key].back() - timestamps_[key].front());
        size_t interval_count = timestamps_[key].size() - 1;
        return duration.count() > 0.0 ? interval_count / duration.count() : 0.0;
    }

private:
    size_t window_size_;
    std::map<const void*, std::deque<TimePoint>> timestamps_;
    TimePoint start_time_;
    std::ofstream log_file_;
    std::string log_file_name_ = "frequency_log.txt";
};



class PublisherNode : public rclcpp::Node
{
public:
  PublisherNode() : Node("mypublishernode")
  {
    freq_monitor = std::make_shared<FrequencyMonitor>(WINDOW_SIZE);
    for(int i=0;i<4;i++){
      mypublisher.push_back(this->create_publisher<std_msgs::msg::String>(topic[i], 10));
      mytimer.push_back(this->create_wall_timer(
        std::chrono::milliseconds(period[i]),
        [this,i]() {
          timer_cnt[i]++;
          freq_monitor->record_time(mytimer[i].get());
          auto message = std_msgs::msg::String();
          message.data = "Hello, ROS 2 with EventsExecutor!";
          RCLCPP_INFO(this->get_logger(), "Publishing: '%s' to '%s'", message.data.c_str(),topic[i]);
          mypublisher[i]->publish(message);
        }));
    }
    for(int i=0;i<4;i++){
      lftimer.push_back(this->create_wall_timer(
        std::chrono::milliseconds(qos[i]),
        [this]() {
          return;
        }));
    }
    timer_end = this->create_wall_timer(
        std::chrono::milliseconds(710),
        [this]() {
          RCLCPP_INFO(this->get_logger(), "System end\n###cnt:%d,%d,%d,%d",timer_cnt[0],timer_cnt[1],timer_cnt[2],timer_cnt[3]);
          rclcpp::shutdown();
        });
    
    // subscription node
    for(int i=0;i<4;i++){
      mysubscription.push_back(this->create_subscription<std_msgs::msg::String>(
        topic[i],
        10,
        [this,i](const std_msgs::msg::String::SharedPtr msg) {
          freq_monitor->record_time(mysubscription[i]->get_subscription_handle().get());
          RCLCPP_INFO(this->get_logger(), "Received: '%s' from '%s'", msg->data.c_str(),topic[i]);
          if(i==3)  cnt_++;
          std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time[i]));
          if(cnt_==BURST_IDX && i==3) std::this_thread::sleep_for(std::chrono::milliseconds(BURST_TIME));
        }));
    }
  }

  void* GetTimerKey(int x){
    return mytimer[x].get();
  }

  void* GetLfKey(int x){
    return lftimer[x].get();
  }

  // subscription node
  void* GetEventKey(int x){
    return mysubscription[x]->get_subscription_handle().get();
  }
private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> mypublisher;
  std::vector<rclcpp::TimerBase::SharedPtr> mytimer;
  std::vector<rclcpp::TimerBase::SharedPtr> lftimer;
  rclcpp::TimerBase::SharedPtr timer_end;

  // subscription node
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> mysubscription;
  int cnt_=0;

  // frequency monitor
  FrequencyMonitor::SharedPtr freq_monitor;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto mypublishernode = std::make_shared<PublisherNode>();

  auto event_queue = std::make_unique<rclcpp::experimental::executors::SimpleEventsQueue>();

  for(int i=0;i<2;i++)
    for(int j=0;j<4;j++){
      std::cout<<"++++++++"<<i<<","<<j<<"+++++++++"<<std::endl;
      void* key=nullptr;
      if(!i)  {
        key=mypublishernode->GetTimerKey(j);
        timer_vec.push_back(key);
      }
      else{
        key=mypublishernode->GetEventKey(j);
        std::cout<<"$$$$$$$$$sub"<<j<<"is:"<<key<<std::endl;
      }
      //if(j==0)  event_queue->register_event(timer_vec[j],key,true,{i,j},0);
      //else  event_queue->register_event(timer_vec[j],key,false,{i,j},0);
      if(key==nullptr)  continue;
      pos_map[static_cast<const void *>(key)] = {i,j};
    }

  bool flag = false;
  rclcpp::ExecutorOptions options;
  options.context = rclcpp::contexts::get_global_default_context();
  //auto executor = std::make_shared<rclcpp::experimental::executors::EventsExecutor>(std::move(event_queue),flag,options);
  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();

  executor->add_node(mypublishernode);

  executor->spin();

  rclcpp::shutdown();
  return 0;
}
