// Common code for the chain-composition experiment (Reviewer 1's R2-scope question).
// All four binaries (comp_3ns1s, comp_2ns2s, comp_1ns3s, comp_0ns4s) define the four
// per-chain parameter arrays as CONFIG_* macros and include this header. Everything
// else (workload generator, frequency monitor, executor selection) is shared.
//
// Settings strictly match paper Table I + Section VI.A:
//   period       = {80,   100,   160,   120}     ms
//   sleep_time   = {2.3+16.1,  20.6+17.9+6.6,  1.7+11.0+6.6+7.9,  6.2+6.6}  ms
//   burst        = every 10th instance of C3 incurs an extra 58.3 ms in its timer cb
//   run_time     = 60.2 s
//   priorities   = NS-tier(2) / S-tier(1); within tier same value, tie-broken by pointer
//   QoS period   = native period for NS chains; 1.25 * native for S chains
//
// Executor is selected at runtime via env var EVA6_EXEC ∈
// {proposed, events_false, events_true, default}.

#ifndef EVA6_COMPOSITION_COMMON_HPP_
#define EVA6_COMPOSITION_COMMON_HPP_

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <rclcpp/experimental/executors/events_executor/events_executor.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Per-composition macros expected from including .cpp:
//   COMP_NAME              "3ns1s"  /  "2ns2s"  /  "1ns3s"  /  "0ns4s"
//   COMP_QOS_C1 ... C4     QoS periods in ms (native for NS, 1.25 * native for S)
//   COMP_PRIO_C1 ... C4    priority value (2 for NS, 1 for S)
//   COMP_SKIP_C1 ... C4    0 for non-skippable, 1 for skippable  (used for reporting only)

static constexpr int    PERIOD[4]            = {80, 100, 160, 120};
// Timer + subsequent sub callbacks; per paper Table I.
// All values in microseconds. Index 0 is timer callback, 1..3 are sub callbacks.
static constexpr uint64_t SLEEP_TIME[4][4]   = {
  {2'300,  16'100,    0,     0},   // C1: timer 2.3 + 1 sub 16.1
  {20'600, 17'900, 6'600,    0},   // C2: timer 20.6 + 2 subs 17.9, 6.6
  {1'700,  11'000, 6'600, 7'900},  // C3: timer 1.7 + 3 subs 11.0, 6.6, 7.9
  {6'200,   6'600,    0,     0},   // C4: timer 6.2 + 1 sub 6.6
};
static constexpr size_t QOS[4]                = {COMP_QOS_C1, COMP_QOS_C2,
                                                 COMP_QOS_C3, COMP_QOS_C4};
static constexpr size_t PRIORITY[4]           = {COMP_PRIO_C1, COMP_PRIO_C2,
                                                 COMP_PRIO_C3, COMP_PRIO_C4};
static constexpr int    SKIPPABLE[4]          = {COMP_SKIP_C1, COMP_SKIP_C2,
                                                 COMP_SKIP_C3, COMP_SKIP_C4};

static constexpr const char * TOPIC[4][4] = {
  {"chain1_1", "",         "",         ""},
  {"chain2_1", "chain2_2", "",         ""},
  {"chain3_1", "chain3_2", "chain3_3", ""},
  {"chain4_1", "",         "",         ""}
};

#define COMP_BURST_TIME_US 58'300   // 58.3 ms burst injection
#define COMP_RUN_TIME_MS   60'200   // ~60 s total
#define COMP_WINDOW_SIZE   4

// Globals — kept simple. All per-instance latency/frequency analysis is
// now done offline from callbacks.csv, so we only keep the raw counters
// and the key→(chain_i, callback_j) map needed at runtime.
inline int timer_cnt[4] = {0, 0, 0, 0};
inline std::map<const void *, std::pair<int, int>> pos_map;

inline void busy_run_for(uint64_t target_us)
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

// Selects how a callback "consumes" its WCET so that each executor matches
// the original eva package that produced paper Fig.9–12. The choice is set
// once at startup by run_with_executor() based on EVA6_EXEC.
//   - sleep_for : eva1 (proposed), eva2 (events_false), eva3 (default)
//   - busy_run  : eva4 (events_true)
// Different CPU-consumption strategies expose meaningfully different timer
// drift behavior under overload (sleep wake-up latency vs hot CPU).
inline bool & use_busy_run_global()
{
  static bool b = false;
  return b;
}

inline void consume_wcet_us(uint64_t target_us)
{
  if (target_us == 0) {return;}
  if (use_busy_run_global()) {
    busy_run_for(target_us);
  } else {
    std::this_thread::sleep_for(std::chrono::microseconds(target_us));
  }
}

using TimePoint = std::chrono::steady_clock::time_point;

// ─── Raw callback record table ────────────────────────────────────────────────
//
// During the run we capture one row per callback execution:
//     (chain_i, callback_j, t_start_us, t_end_us)
// All rows accumulate in an in-memory vector; at shutdown we dump a single
// callbacks.csv file. All downstream analyses (count / frequency / latency)
// derive from this one table, removing the previous bug-prone per-event
// indexing and three-file output.
//
// "Theoretical trigger time" of an instance is derived offline as
//     idx * period  (after a small startup-offset alignment),
// so the latency definition the user asked for —
//     latency = real_finish_time - idx * theoretical_period
// is computed in the Python analyzer, not at run time.

struct CallbackRecord
{
  int chain_i;
  int callback_j;
  uint64_t t_start_us;
  uint64_t t_end_us;
};

class CallbackLogger
{
public:
  using SharedPtr = std::shared_ptr<CallbackLogger>;

  explicit CallbackLogger(const std::string & out_dir)
  : out_dir_(out_dir)
  {
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    start_time_ = std::chrono::steady_clock::now();
    records_.reserve(16384);
  }

  // Called at the very start of a callback body.
  void record_start(const void * key)
  {
    uint64_t t_us = now_us();
    std::unique_lock<std::mutex> lk(mutex_);
    pending_[key] = t_us;
  }

  // Called at the very end of a callback body. Pairs with the most recent
  // record_start(key) and appends a row to the in-memory table.
  void record_end(const void * key)
  {
    uint64_t t_end = now_us();
    auto pos_it = pos_map.find(key);
    if (pos_it == pos_map.end()) {return;}
    std::unique_lock<std::mutex> lk(mutex_);
    auto p_it = pending_.find(key);
    if (p_it == pending_.end()) {return;}
    uint64_t t_start = p_it->second;
    pending_.erase(p_it);
    records_.push_back(
      {pos_it->second.first, pos_it->second.second, t_start, t_end});
  }

  // Write callbacks.csv at end of run.
  void dump_csv()
  {
    std::ofstream f(out_dir_ + "/callbacks.csv", std::ios::out);
    if (!f.is_open()) {return;}
    f << "chain,j,t_start_us,t_end_us\n";
    for (const auto & r : records_) {
      f << r.chain_i << "," << r.callback_j << ","
        << r.t_start_us << "," << r.t_end_us << "\n";
    }
  }

private:
  uint64_t now_us()
  {
    auto d = std::chrono::steady_clock::now() - start_time_;
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
  }

  std::string out_dir_;
  TimePoint start_time_;
  std::map<const void *, uint64_t> pending_;
  std::vector<CallbackRecord> records_;
  std::mutex mutex_;
};

class PublisherNode : public rclcpp::Node
{
public:
  explicit PublisherNode(CallbackLogger::SharedPtr logger)
  : Node("comp_node"), cb_logger_(logger)
  {
    for (int i = 0; i < 4; i++) {
      mypublisher[i].push_back(
        this->create_publisher<std_msgs::msg::String>(TOPIC[i][0], 10));
      mytimer.push_back(
        this->create_wall_timer(
          std::chrono::milliseconds(PERIOD[i]),
          [this, i]() {
            timer_cnt[i]++;
            cb_logger_->record_start(mytimer[i].get());
            auto msg = std_msgs::msg::String();
            msg.data = "tick";
            mypublisher[i][0]->publish(msg);
            consume_wcet_us(SLEEP_TIME[i][0]);
            // Burst injection: every 10th C3 instance, timer cb takes an
            // extra 58.3 ms (so total timer cb time = 60 ms, matching paper).
            // timer_cnt is incremented at the very start of the callback, so
            // it equals 1 on the first invocation; the 10th, 20th, 30th ...
            // invocations satisfy timer_cnt % 10 == 0.
            if (i == 2 && timer_cnt[i] > 0 && (timer_cnt[i] % 10 == 0)) {
              consume_wcet_us(COMP_BURST_TIME_US);
            }
            cb_logger_->record_end(mytimer[i].get());
          }));
    }
    for (int i = 0; i < 4; i++) {
      lftimer.push_back(
        this->create_wall_timer(
          std::chrono::milliseconds(QOS[i]),
          []() {return;}));
    }
    timer_end = this->create_wall_timer(
      std::chrono::milliseconds(COMP_RUN_TIME_MS),
      [this]() {
        RCLCPP_INFO(
          this->get_logger(),
          "Run end. cnt: %d, %d, %d, %d",
          timer_cnt[0], timer_cnt[1], timer_cnt[2], timer_cnt[3]);
        rclcpp::shutdown();
      });

    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 3; j++) {
        if (TOPIC[i][j][0] == '\0') {break;}
        if (TOPIC[i][j + 1][0] != '\0') {
          mypublisher[i].push_back(
            this->create_publisher<std_msgs::msg::String>(TOPIC[i][j + 1], 10));
        }
        mysubscription[i].push_back(
          this->create_subscription<std_msgs::msg::String>(
            TOPIC[i][j], 10,
            [this, i, j](const std_msgs::msg::String::SharedPtr /*m*/) {
              void * key = mysubscription[i][j]->get_subscription_handle().get();
              cb_logger_->record_start(key);
              auto msg = std_msgs::msg::String();
              msg.data = "tick";
              if (TOPIC[i][j + 1][0] != '\0') {
                mypublisher[i][j + 1]->publish(msg);
              }
              consume_wcet_us(SLEEP_TIME[i][j + 1]);
              cb_logger_->record_end(key);
            }));
      }
    }
  }

  void * GetTimerKey(int x) {return mytimer[x].get();}
  void * GetLfKey(int x) {return lftimer[x].get();}
  void * GetEventKey(int x, int y)
  {
    if (y > static_cast<int>(mysubscription[x].size())) {return nullptr;}
    return mysubscription[x][y - 1]->get_subscription_handle().get();
  }

private:
  std::vector<rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> mypublisher[4];
  std::vector<rclcpp::TimerBase::SharedPtr> mytimer;
  std::vector<rclcpp::TimerBase::SharedPtr> lftimer;
  rclcpp::TimerBase::SharedPtr timer_end;
  std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> mysubscription[4];
  CallbackLogger::SharedPtr cb_logger_;
};

// ---- Executor configuration switch ---------------------------------------
// Returns 0 on success.
inline int run_with_executor(
  const std::string & exec_name,
  std::shared_ptr<PublisherNode> & node,
  std::vector<void *> & timer_keys_out)
{
  // Match the original eva package's CPU-consumption strategy for each executor:
  //   eva1/eva2/eva3 use sleep_for; eva4 uses busy_run_for. This is critical —
  //   sleep_for exposes wake-up latency that causes baselines to skip timer
  //   activations (paper Fig.9 effect), while busy_run_for keeps the CPU hot
  //   and avoids that drift.
  use_busy_run_global() = (exec_name == "events_true");

  rclcpp::ExecutorOptions options;
  options.context = rclcpp::contexts::get_global_default_context();

  if (exec_name == "proposed") {
    auto q = std::make_unique<rclcpp::experimental::executors::QosPromisedQueue>();
    for (int i = 0; i < 4; i++) {
      timer_keys_out.push_back(node->GetTimerKey(i));
      q->register_priority(node->GetTimerKey(i), PRIORITY[i]);
    }
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        void * key = (j == 0) ? node->GetTimerKey(i) : node->GetEventKey(i, j);
        if (key == nullptr) {break;}
        pos_map[key] = {i, j};
        q->register_qos_event(key);
      }
    }
    for (int i = 0; i < 4; i++) {
      auto consumer = node->GetTimerKey(i);
      auto producer = node->GetLfKey(i);
      q->register_token_producer(consumer, consumer, true);
      q->register_token_producer(producer, consumer, false);
    }
    auto exec = std::make_shared<rclcpp::experimental::executors::EventsExecutor>(
      std::move(q), /*separate=*/false, options);
    exec->add_node(node);
    exec->spin();
    return 0;
  } else if (exec_name == "events_false") {
    auto q = std::make_unique<rclcpp::experimental::executors::SimpleEventsQueue>();
    for (int i = 0; i < 4; i++) {
      timer_keys_out.push_back(node->GetTimerKey(i));
      for (int j = 0; j < 4; j++) {
        void * key = (j == 0) ? node->GetTimerKey(i) : node->GetEventKey(i, j);
        if (key == nullptr) {break;}
        pos_map[key] = {i, j};
      }
    }
    auto exec = std::make_shared<rclcpp::experimental::executors::EventsExecutor>(
      std::move(q), /*separate=*/false, options);
    exec->add_node(node);
    exec->spin();
    return 0;
  } else if (exec_name == "events_true") {
    auto q = std::make_unique<rclcpp::experimental::executors::SimpleEventsQueue>();
    for (int i = 0; i < 4; i++) {
      timer_keys_out.push_back(node->GetTimerKey(i));
      for (int j = 0; j < 4; j++) {
        void * key = (j == 0) ? node->GetTimerKey(i) : node->GetEventKey(i, j);
        if (key == nullptr) {break;}
        pos_map[key] = {i, j};
      }
    }
    auto exec = std::make_shared<rclcpp::experimental::executors::EventsExecutor>(
      std::move(q), /*separate=*/true, options);
    exec->add_node(node);
    exec->spin();
    return 0;
  } else if (exec_name == "default") {
    for (int i = 0; i < 4; i++) {
      timer_keys_out.push_back(node->GetTimerKey(i));
      for (int j = 0; j < 4; j++) {
        void * key = (j == 0) ? node->GetTimerKey(i) : node->GetEventKey(i, j);
        if (key == nullptr) {break;}
        pos_map[key] = {i, j};
      }
    }
    auto exec = std::make_shared<rclcpp::executors::SingleThreadedExecutor>(options);
    exec->add_node(node);
    exec->spin();
    return 0;
  }

  std::cerr << "Unknown EVA6_EXEC: " << exec_name << "\n";
  return 1;
}

inline int eva6_main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  // Output directory: $EVA6_OUT_DIR/<composition>/<executor>/
  const char * exec_env = std::getenv("EVA6_EXEC");
  std::string exec_name = exec_env ? exec_env : "proposed";
  const char * out_root_env = std::getenv("EVA6_OUT_DIR");
  std::string out_root = out_root_env ? out_root_env : "/tmp/eva6_composition";
  std::string out_dir = out_root + "/" + COMP_NAME + "/" + exec_name;
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);

  auto logger = std::make_shared<CallbackLogger>(out_dir);
  auto node = std::make_shared<PublisherNode>(logger);

  std::vector<void *> timer_keys;
  std::cout << "=== eva6 composition=" << COMP_NAME
            << " exec=" << exec_name
            << " run_time=" << COMP_RUN_TIME_MS << "ms ==="
            << std::endl;
  int rc = run_with_executor(exec_name, node, timer_keys);

  // Persist activation counts and per-chain metadata for the summary script.
  {
    std::ofstream f(out_dir + "/activations.txt", std::ios::out);
    f << "composition=" << COMP_NAME << "\n";
    f << "executor=" << exec_name << "\n";
    f << "run_time_ms=" << COMP_RUN_TIME_MS << "\n";
    for (int i = 0; i < 4; i++) {
      f << "chain" << (i + 1)
        << " period=" << PERIOD[i]
        << " qos_period=" << QOS[i]
        << " priority=" << PRIORITY[i]
        << " skippable=" << SKIPPABLE[i]
        << " timer_cnt=" << timer_cnt[i] << "\n";
    }
    int total = timer_cnt[0] + timer_cnt[1] + timer_cnt[2] + timer_cnt[3];
    f << "total_timer_cnt=" << total << "\n";
  }

  // Dump the raw per-callback table that downstream analysis reads.
  logger->dump_csv();

  rclcpp::shutdown();
  return rc;
}

#endif  // EVA6_COMPOSITION_COMMON_HPP_
