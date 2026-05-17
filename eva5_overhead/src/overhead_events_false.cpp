// Overhead baseline: events executor with separate=false + SimpleEventsQueue.
// This is the fair apples-to-apples baseline for the proposed mechanism: both configs use
// separate=false (so timer events DO flow through the queue), the only difference is that
// SimpleEventsQueue lacks the token-bucket / dynamic-buffer logic. The cost difference vs
// overhead_proposed isolates the per-activation overhead added by the proposed mechanism.
// Burst is disabled to keep the system non-overloaded.
// At end-of-run, prints total timer activations per chain so per-activation overhead can be
// computed by dividing the queue's accumulated ns counters (written to
// $RCLCPP_EXP_OVERHEAD_LOG_DIR/simple_overhead.txt) by the activation count.

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <vector>
#include <thread>
#include <map>
#include <chrono>
#include <iostream>
#include <fstream>
#include <cstdlib>

int period[4] = {80, 100, 160, 120};
uint64_t sleep_time[4][4] = {
  {2'300, 16'100, 0, 0},
  {20'600, 17'900, 6'600, 0},
  {1'700, 11'000, 6'600, 7'900},
  {10'200, 2'600, 0, 0}
};
const char * topic[4][4] = {
  {"chain1_1", "", "", ""},
  {"chain2_1", "chain2_2", "", ""},
  {"chain3_1", "chain3_2", "chain3_3", ""},
  {"chain4_1", "", "", ""}
};

int timer_cnt[4] = {0, 0, 0, 0};
#define RUN_TIME 60'200  // ms

static void busy_run_for(uint64_t target_us)
{
  struct timespec start{}, now{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
  uint64_t elapsed_us = 0;
  while (elapsed_us < target_us) {
    for (volatile int i = 0; i < 1000; ++i) {}
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    elapsed_us = (now.tv_sec - start.tv_sec) * 1000000ULL +
      (now.tv_nsec - start.tv_nsec) / 1000;
  }
}

class PublisherNode : public rclcpp::Node
{
public:
  PublisherNode()
  : Node("overhead_node")
  {
    for (int i = 0; i < 4; i++) {
      mypublisher[i].push_back(this->create_publisher<std_msgs::msg::String>(topic[i][0], 10));
      mytimer.push_back(this->create_wall_timer(
        std::chrono::milliseconds(period[i]),
        [this, i]() {
          timer_cnt[i]++;
          auto message = std_msgs::msg::String();
          message.data = "tick";
          busy_run_for(sleep_time[i][0]);
          // burst injection intentionally disabled for non-overloaded overhead measurement
          mypublisher[i][0]->publish(message);
        }));
    }
    timer_end = this->create_wall_timer(
      std::chrono::milliseconds(RUN_TIME),
      [this]() {
        RCLCPP_INFO(this->get_logger(),
          "Run end. cnt: %d, %d, %d, %d",
          timer_cnt[0], timer_cnt[1], timer_cnt[2], timer_cnt[3]);
        rclcpp::shutdown();
      });

    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 3; j++) {
        if (topic[i][j][0] == '\0') {break;}
        if (topic[i][j + 1][0] != '\0') {
          mypublisher[i].push_back(
            this->create_publisher<std_msgs::msg::String>(topic[i][j + 1], 10));
        }
        mysubscription[i].push_back(
          this->create_subscription<std_msgs::msg::String>(
            topic[i][j], 10,
            [this, i, j](const std_msgs::msg::String::SharedPtr /*msg*/) {
              busy_run_for(sleep_time[i][j + 1]);
              if (topic[i][j + 1][0] != '\0') {
                auto m = std_msgs::msg::String();
                m.data = "tick";
                mypublisher[i][j + 1]->publish(m);
              }
            }));
      }
    }
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> mypublisher[4];
  std::vector<rclcpp::TimerBase::SharedPtr> mytimer;
  rclcpp::TimerBase::SharedPtr timer_end;
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> mysubscription[4];
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<PublisherNode>();

  auto event_queue =
    std::make_unique<rclcpp::experimental::executors::SimpleEventsQueue>();

  bool flag = false;  // separate=false: timer events flow through the events queue
                      //                  (apples-to-apples vs proposed)
  rclcpp::ExecutorOptions options;
  options.context = rclcpp::contexts::get_global_default_context();
  auto executor =
    std::make_shared<rclcpp::experimental::executors::EventsExecutor>(
      std::move(event_queue), flag, options);
  executor->add_node(node);
  executor->spin();

  const char * env_dir = std::getenv("RCLCPP_EXP_OVERHEAD_LOG_DIR");
  std::string dir = env_dir ? env_dir : "/tmp/rclcpp_exp_overhead";
  std::ofstream f(dir + "/events_false_activations.txt", std::ios::out);
  if (f.is_open()) {
    f << "run_time_ms=" << RUN_TIME << "\n";
    for (int i = 0; i < 4; i++) {
      f << "chain" << (i + 1) << "_timer_cnt=" << timer_cnt[i] << "\n";
    }
    int total = timer_cnt[0] + timer_cnt[1] + timer_cnt[2] + timer_cnt[3];
    f << "total_timer_cnt=" << total << "\n";
  }

  rclcpp::shutdown();
  return 0;
}
