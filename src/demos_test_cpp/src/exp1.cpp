#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "example_interfaces/srv/add_two_ints.hpp"

using std::placeholders::_1;
using std::placeholders::_2;

#define Start 1000

class MyNode : public rclcpp::Node
{
public:
    MyNode() : Node("my_node")
    {
        // rclcpp::Clock ros_clock(RCL_ROS_TIME);
        // rclcpp::Time now = ros_clock.now();

        // RCLCPP_INFO(this->get_logger(), "t1: %ld.%09ld", now.seconds(), now.nanoseconds() % 1000000000);

        // Timer for the first message queue:start
        timer_queue_1 = this->create_wall_timer(
                            std::chrono::milliseconds(Start),
                            std::bind(&MyNode::timer_callback_queue_1, this));
        // Timer for the second message queue:1500ms
        timer_queue_2 = this->create_wall_timer(
                            std::chrono::milliseconds(Start+1500),
                            std::bind(&MyNode::timer_callback_queue_2, this));
        // Timer 1 for T1:250ms
        timer_t1_1 = this->create_wall_timer(
                            std::chrono::milliseconds(Start+250),
                            std::bind(&MyNode::timer_callback_t1_1, this));
        // Timer 2 for T1:250ms
        
        timer_t1_2 = this->create_wall_timer(
                            std::chrono::milliseconds(Start+250),
                            std::bind(&MyNode::timer_callback_t1_2, this));
        // Timer 1 for T2:2250ms
        timer_t2_1 = this->create_wall_timer(
                            std::chrono::milliseconds(Start+2250),
                            std::bind(&MyNode::timer_callback_t2_1, this));
        // Timer 2 for T2:2250ms
        timer_t2_2 = this->create_wall_timer(
                            std::chrono::milliseconds(Start+2250),
                            std::bind(&MyNode::timer_callback_t2_2, this));           
        // Publishers
        publisher_H = this->create_publisher<std_msgs::msg::String>("topic1", 10);
        
        publisher_M = this->create_publisher<std_msgs::msg::String>("topic2", 10);

        publisher_L = this->create_publisher<std_msgs::msg::String>("topic3", 10);


        // Subscriptions
        subscription_H = this->create_subscription<std_msgs::msg::String>(
            "topic1", 10, std::bind(&MyNode::topic_callback_H, this, _1));
        subscription_M = this->create_subscription<std_msgs::msg::String>(
            "topic2", 10, std::bind(&MyNode::topic_callback_M, this, _1));
        subscription_L = this->create_subscription<std_msgs::msg::String>(
            "topic2", 10, std::bind(&MyNode::topic_callback_L, this, _1));


        // Services
        service_SH = this->create_service<example_interfaces::srv::AddTwoInts>(
            "add_two_ints_group1", std::bind(&MyNode::service_callback_SH, this, _1, _2));
        service_SM = this->create_service<example_interfaces::srv::AddTwoInts>(
            "add_two_ints_group2", std::bind(&MyNode::service_callback_SM, this, _1, _2));
        service_SL = this->create_service<example_interfaces::srv::AddTwoInts>(
            "add_two_ints_group3", std::bind(&MyNode::service_callback_SL, this, _1, _2));

        // Clients
        client_SH = this->create_client<example_interfaces::srv::AddTwoInts>("add_two_ints_group1");
        client_SM = this->create_client<example_interfaces::srv::AddTwoInts>("add_two_ints_group2");
        client_SL = this->create_client<example_interfaces::srv::AddTwoInts>("add_two_ints_group3");
        
    }

private:
    void timer_callback_queue_1()
    {
        RCLCPP_INFO(this->get_logger(), "sending queue 1");

        // Publish to topic1,2,3
        auto message = std_msgs::msg::String();
        message.data = "queue 1";
        
        publisher_H->publish(message);
        publisher_M->publish(message);
        publisher_L->publish(message);
        // RCLCPP_INFO(this->get_logger(), "sending queue 1::topic ok");
        // Send service request to Group 1,3
        send_service_request(client_SH, 1, 2);
        // RCLCPP_INFO(this->get_logger(), "service 1::ok");

        send_service_request(client_SL, 1, 2);
        // RCLCPP_INFO(this->get_logger(), "sending queue 1::service ok");
        
        publisher_H->publish(message);
        publisher_M->publish(message);
        publisher_L->publish(message);
        send_service_request(client_SH, 1, 2);
        send_service_request(client_SL, 1, 2);

        timer_queue_1->cancel();
        return;
    }

    void timer_callback_queue_2()
    {
        RCLCPP_INFO(this->get_logger(), "sending queue 2");

        // Send service request to Group 2
        send_service_request(client_SM, 3, 4);
        send_service_request(client_SM, 3, 4);

        // Publish to topic1
        auto message = std_msgs::msg::String();
        message.data = "queue 2";
        publisher_H->publish(message);

        timer_queue_2->cancel();
        std::cout<<timer_queue_2->is_canceled()<<std::endl;
    }

    void callback_500ms()
    {
        RCLCPP_INFO(this->get_logger(), "Pending 500ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        return;
    }

    void timer_callback_t1_1()
    {
        RCLCPP_INFO(this->get_logger(), "timer_t1_1 :Start");
        callback_500ms();
        timer_t1_1->cancel();
        return;
    }

    void timer_callback_t1_2()
    {
        RCLCPP_INFO(this->get_logger(), "timer_t1_2 :Start");
        callback_500ms();
        timer_t1_2->cancel();
        return;
    }

    void timer_callback_t2_1()
    {
        RCLCPP_INFO(this->get_logger(), "timer_t2_1 :Start");
        callback_500ms();
        timer_t2_1->cancel();
        return;
    }

    void timer_callback_t2_2()
    {
        RCLCPP_INFO(this->get_logger(), "timer_t2_2 :Start");
        callback_500ms();
        timer_t2_2->cancel();
        return;
    }
    
    
    void topic_callback_H(const std_msgs::msg::String::SharedPtr msg)
    {
        RCLCPP_INFO(this->get_logger(), "Topic callback H: Start");   
        callback_500ms();
        return;            
    }
    
    void topic_callback_M(const std_msgs::msg::String::SharedPtr msg)
    {
        RCLCPP_INFO(this->get_logger(), "Topic callback M: Start");   
        callback_500ms();
        return;            
    }

    void topic_callback_L(const std_msgs::msg::String::SharedPtr msg)
    {
        RCLCPP_INFO(this->get_logger(), "Topic callback L: Start");   
        callback_500ms();
        return;            
    }

    void service_callback_SH(const example_interfaces::srv::AddTwoInts::Request::SharedPtr request,
                                 example_interfaces::srv::AddTwoInts::Response::SharedPtr response)
    {
        response->sum = request->a + request->b;
        RCLCPP_INFO(this->get_logger(), "Service callback SH: Start");
        callback_500ms();
        return;
    }

    void service_callback_SM(const example_interfaces::srv::AddTwoInts::Request::SharedPtr request,
                                 example_interfaces::srv::AddTwoInts::Response::SharedPtr response)
    {
        response->sum = request->a + request->b;
        RCLCPP_INFO(this->get_logger(), "Service callback SM: Start");
        callback_500ms();
        return;
    }

    void service_callback_SL(const example_interfaces::srv::AddTwoInts::Request::SharedPtr request,
                                 example_interfaces::srv::AddTwoInts::Response::SharedPtr response)
    {
        response->sum = request->a + request->b;
        RCLCPP_INFO(this->get_logger(), "Service callback SL: Start");
        callback_500ms();
        return;
    }

    void send_service_request(std::shared_ptr<rclcpp::Client<example_interfaces::srv::AddTwoInts>> client, long a, long b)
    {
        auto request = std::make_shared<example_interfaces::srv::AddTwoInts::Request>();
        request->a = a;
        request->b = b;
        auto result = client->async_send_request(request);
        // Note: Here we are not handling the response. In a real application, you should handle the response properly.
    }

rclcpp::TimerBase::SharedPtr timer_queue_1;
rclcpp::TimerBase::SharedPtr timer_queue_2;
rclcpp::TimerBase::SharedPtr timer_t1_1;
rclcpp::TimerBase::SharedPtr timer_t1_2;
rclcpp::TimerBase::SharedPtr timer_t2_1;
rclcpp::TimerBase::SharedPtr timer_t2_2;
rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_H;
rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_M;
rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_L;

rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_H;
rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_M;
rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_L;

rclcpp::Service<example_interfaces::srv::AddTwoInts>::SharedPtr service_SH;
rclcpp::Service<example_interfaces::srv::AddTwoInts>::SharedPtr service_SM;
rclcpp::Service<example_interfaces::srv::AddTwoInts>::SharedPtr service_SL;

rclcpp::Client<example_interfaces::srv::AddTwoInts>::SharedPtr client_SH;
rclcpp::Client<example_interfaces::srv::AddTwoInts>::SharedPtr client_SM;
rclcpp::Client<example_interfaces::srv::AddTwoInts>::SharedPtr client_SL;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<MyNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}