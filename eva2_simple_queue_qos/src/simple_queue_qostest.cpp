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
#include <mutex>

// period in milliseconds
int period[4]={
  80, 100, 160, 120
};
// sleep time in microseconds
int sleep_time[4][4]={
  {2'300, 16'100, 0, 0},
  {20'600, 17'900, 6'600, 0},
  {1'700, 11'000, 6'600, 7'900},
  {10'200, 2'600, 0, 0}
};
// qos period in milliseconds
size_t qos[4]={
  120,150,240,180
};
size_t priority[4]={
  4,3,2,1
};
const char* topic[4][4]={
  {"chain1_1","","",""},
  {"chain2_1","chain2_2","",""},
  {"chain3_1","chain3_2","chain3_3",""},
  {"chain4_1","","",""}
};

int timer_cnt[4];

std::vector<void*> timer_vec;
std::map<const void*, std::pair<int,int>> pos_map;
std::vector<int64_t> finish_time_array[4][3];
std::vector<int64_t> activation_time_array[4];
size_t index_array[4][3];

#define BURST_IDX 2
#define BURST_TIME 58'300
#define WINDOW_SIZE 4
#define RUN_TIME 60'200

#define Record_Freq 1
#define Record_Latency 2

using TimePoint = std::chrono::steady_clock::time_point;

class FrequencyMonitor {
public:
    using SharedPtr = std::shared_ptr<FrequencyMonitor>;
    FrequencyMonitor(size_t window_size = 10)
        : window_size_(window_size) {
      start_time_ = std::chrono::steady_clock::now();
      frequency_log_file_.open(frequency_log_file_name_, std::ios::out | std::ios::app);
      if (!frequency_log_file_.is_open()) {
          throw std::runtime_error("Failed to open frequency log file");
      }
      latency_log_file_.open(latency_log_file_name_, std::ios::out | std::ios::app);
      if (!latency_log_file_.is_open()) {
          throw std::runtime_error("Failed to open latency log file");
      }
      eelatency_log_file_.open(eelatency_log_file_name_, std::ios::out | std::ios::app);
      if (!eelatency_log_file_.is_open()) {
          throw std::runtime_error("Failed to open latency log file");
      }
    }

    void record_time(const void* key, size_t type) {
        TimePoint now = std::chrono::steady_clock::now();
        auto duration = now - start_time_;
        auto pos = pos_map[key];
        
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

        if(type == Record_Freq){
          // if(!has_enough_data(key)) {
          //   // have no timepoint data, so push back a zero timepoint
          //   TimePoint zerotp = TimePoint{};
          //   timestamps_[key].push_back(zerotp);
          // }
          timestamps_[key].push_back(now);
          if (timestamps_[key].size() > window_size_) {
              timestamps_[key].pop_front();
          }    
          double frequency = average_frequency(key);
          // record frequency data
          
          frequency_log_file_ << "record time:"  <<  milliseconds  <<  "." <<  microseconds % 1000
                    << ",Pos: " << pos_map[key].first << "," << pos_map[key].second 
                    << ",Frequency: " << frequency << " Hz\n";
          frequency_log_file_.flush();

        }else if(type==Record_Latency){
          std::unique_lock<std::mutex> lock(mutex_);
          // record relative latency data
          size_t index = index_array[pos.first][pos.second];
          if(pos.first==2 && pos.second==3){
            std::cout<<index<<std::endl;
          }
          auto latency_in_milliseconds = milliseconds - finish_time_array[pos.first][pos.second][index];
          latency_log_file_ << "record time:" << milliseconds << "." << microseconds % 1000
                            << ",Pos: " << pos.first << "," << pos.second 
                            << ",Latency: " << latency_in_milliseconds << " ms\n";
          latency_log_file_.flush();

          // record end-to-end latency data
          latency_in_milliseconds = milliseconds - activation_time_array[pos.first][index];
          eelatency_log_file_ << "record time:" << milliseconds << "." << microseconds % 1000
                              << ",idx:" << index << "#" << activation_time_array[pos.first][index]
                              << ",Pos: " << pos.first << "," << pos.second 
                              << ",Latency: " << latency_in_milliseconds << " ms";
          
          // increase index
          if(pos.first==2 && pos.second==3) {
            std::cout<<index_array[pos.first][pos.second]<<std::endl;
          }
          index_array[pos.first][pos.second]++;
          
          eelatency_log_file_ << ",index after:" << index_array[pos.first][pos.second] << std::endl;
          latency_log_file_.flush();
        }
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
    std::ofstream frequency_log_file_;
    std::ofstream latency_log_file_;
    std::ofstream eelatency_log_file_;
    std::string frequency_log_file_name_ = "frequency_log.txt";
    std::string latency_log_file_name_ = "latency_log.txt";
    std::string eelatency_log_file_name_ = "eelatency_log.txt";
    mutable std::mutex mutex_;
};

class PublisherNode : public rclcpp::Node
{
public:
  PublisherNode() : Node("mypublishernode")
  {
    freq_monitor = std::make_shared<FrequencyMonitor>(WINDOW_SIZE);
    for(int i=0;i<4;i++){
      mypublisher[i].push_back(this->create_publisher<std_msgs::msg::String>(topic[i][0], 10));
      mytimer.push_back(this->create_wall_timer(
        std::chrono::milliseconds(period[i]),
        [this,i]() {
          timer_cnt[i]++;
          freq_monitor->record_time(mytimer[i].get(),Record_Freq);
          auto message = std_msgs::msg::String();
          message.data = "Hello, ROS 2 with EventsExecutor!";
          RCLCPP_INFO(this->get_logger(), "Publishing: '%s' to '%s'", message.data.c_str(),topic[i][0]);
          mypublisher[i][0]->publish(message);
          std::this_thread::sleep_for(std::chrono::microseconds(sleep_time[i][0]));
          if(i==2 && (index_array[i][0]%10==9))  std::this_thread::sleep_for(std::chrono::microseconds(BURST_TIME));
          freq_monitor->record_time(mytimer[i].get(),Record_Latency);
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
        std::chrono::milliseconds(RUN_TIME),
        [this]() {
          RCLCPP_INFO(this->get_logger(), "System end\n###cnt:%d,%d,%d,%d",timer_cnt[0],timer_cnt[1],timer_cnt[2],timer_cnt[3]);
          rclcpp::shutdown();
        });
    
    // subscription node
    for(int i=0;i<4;i++){
      for(int j=0;j<3;j++){
        if(topic[i][j][0]=='\0')  break;
        if(topic[i][j+1][0]!='\0')  mypublisher[i].push_back(this->create_publisher<std_msgs::msg::String>(topic[i][j+1], 10));
        mysubscription[i].push_back(this->create_subscription<std_msgs::msg::String>(
        topic[i][j],
        10,
        [this,i,j](const std_msgs::msg::String::SharedPtr msg) {
          freq_monitor->record_time(mysubscription[i][j]->get_subscription_handle().get(),Record_Freq);
          auto message = std_msgs::msg::String();
          message.data = "Hello, ROS 2 with EventsExecutor!";
          RCLCPP_INFO(this->get_logger(), "Received: '%s' from '%s'", msg->data.c_str(),topic[i][j]);
          //if(i==3)  cnt_++;
          std::this_thread::sleep_for(std::chrono::microseconds(sleep_time[i][j+1]));
          if(topic[i][j+1][0]!='\0')  mypublisher[i][j+1]->publish(message);
          freq_monitor->record_time(mysubscription[i][j]->get_subscription_handle().get(),Record_Latency);
          //if(cnt_==BURST_IDX && i==3) std::this_thread::sleep_for(std::chrono::milliseconds(BURST_TIME));
        }));
      }
      
    }
  }

  void* GetTimerKey(int x){
    return mytimer[x].get();
  }

  void* GetLfKey(int x){
    return lftimer[x].get();
  }

  // subscription node
  void* GetEventKey(int x, int y){
    if(y>mysubscription[x].size())  return nullptr;
    return mysubscription[x][y-1]->get_subscription_handle().get();
  }
private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> mypublisher[4];
  std::vector<rclcpp::TimerBase::SharedPtr> mytimer;
  std::vector<rclcpp::TimerBase::SharedPtr> lftimer;
  rclcpp::TimerBase::SharedPtr timer_end;

  // subscription node
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> mysubscription[4];
  int cnt_=0;

  // frequency monitor
  FrequencyMonitor::SharedPtr freq_monitor;
};


int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // calculate activate time
  size_t chain_num = sizeof(period) / sizeof(period[0]);
  for(int i=0;i<chain_num;i++){
    int64_t period_ = period[i];
    int j=0;
    int64_t start = 0;
    size_t period_num = (RUN_TIME + 1) / period_ - 1;
    for(int p=0;p<period_num;p++) {
      int j=0;
      start += period_;
      activation_time_array[i].push_back(start);
      while(sleep_time[i][j]){
        finish_time_array[i][j].push_back(start + sleep_time[i][j]);
        j++;
      }
    }   
  }

  auto mypublishernode = std::make_shared<PublisherNode>();

  auto event_queue = std::make_unique<rclcpp::experimental::executors::SimpleEventsQueue>();

    for(int i=0;i<4;i++)
      for(int j=0;j<4;j++){
        std::cout<<"++++++++"<<i<<","<<j<<"+++++++++"<<std::endl;
        void* key=nullptr;
        if(!j)  {
          key=mypublishernode->GetTimerKey(i);
          timer_vec.push_back(key);
        }
        else{
          key=mypublishernode->GetEventKey(i,j);
          if(key == nullptr)  break;
          std::cout<<"$$$$$$$$$sub"<<i<<","<<j<<"is:"<<key<<std::endl;
        }
        //if(j==0)  event_queue->register_event(timer_vec[j],key,true,{i,j},0);
        //else  event_queue->register_event(timer_vec[j],key,false,{i,j},0);
        if(key==nullptr)  continue;
        pos_map[static_cast<const void *>(key)] = {i,j};
      }

  bool flag = false;
  rclcpp::ExecutorOptions options;
  options.context = rclcpp::contexts::get_global_default_context();
  auto executor = std::make_shared<rclcpp::experimental::executors::EventsExecutor>(std::move(event_queue),flag,options);
  // auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();

  executor->add_node(mypublishernode);

  executor->spin();

  rclcpp::shutdown();
  return 0;
}
