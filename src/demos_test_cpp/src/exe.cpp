#include "rclcpp/rclcpp.hpp"

class TimerTestNode : public rclcpp::Node {
public:
    TimerTestNode() : Node("timer_test_node") {
        // 创建一个定时器，周期为1秒
        timer_ = create_wall_timer(std::chrono::seconds(1), std::bind(&TimerTestNode::timer_callback, this));
    }

private:
    void timer_callback() {
        // 记录定时器回调的时间点
        auto callback_time = now();

        // 计算定时器回调的延迟
        auto expected_time = timer_->get_clock()->now() + std::chrono::seconds(1);
        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(callback_time - expected_time).count();

        // 打印延迟情况
        RCLCPP_INFO(get_logger(), "Timer callback delay: %ld milliseconds", delay);
    }

    rclcpp::TimerBase::SharedPtr timer_;
    // struct timespec expected, wakeup, wakeup_latency;
    // vector<uint64_t> wakeup_latencies(params.rt.iterations);
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<TimerTestNode>());
    rclcpp::shutdown();
    return 0;
}
