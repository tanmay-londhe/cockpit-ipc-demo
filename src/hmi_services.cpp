#include "common.hpp"
#include <iostream>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cerrno>
#include <cstring>

namespace
{
    uint64_t now_ns()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    }
    
    const char* command_to_string(cockpit::CommandType type)
    {
        switch(type)
        {
            case cockpit::CommandType::START_CAMERA:
                return "START CAMERA";
            case cockpit::CommandType::STOP_CAMERA:
                return "STOP CAMERA";
            case cockpit::CommandType::SHUTDOWN:
                return "SHUTDOWN";
            default:
                return "UNKNOWN";
        }
    }

    bool send_command(mqd_t queue,cockpit::CommandType type)
    {
        cockpit::CommandMessage command{};
        command.type = type;
        command.timestamp_ns = now_ns();

        const int result = mq_send(queue,reinterpret_cast<const char*>(&command),sizeof(command),0);

        if(result == -1)
        {
            std::cerr<<"mq_send failed :"<<std::strerror(errno)<<'\n';
            return false;
        }
        std::cout<<"sent command :"<<command_to_string(type)<<'\n';
        return true;
    }

    void print_menu()
    {
        std::cout<<"\n HMI Service \n";
        std::cout<<"1. Start camera \n";
        std::cout<<"2. Stop cameara \n";
        std::cout<<"3. Shutdown System \n";
        std::cout<<"q. Quit HMI only \n";
        std::cout<<"Enter choice: ";
    }
}

int main()
{
    using namespace cockpit;
    std::cout<<"HMI service started ... \n";
    
    mqd_t camera_queue = mq_open(CAMERA_COMMAND_QUEUE_NAME, O_WRONLY);
    
    if(camera_queue == static_cast<mqd_t>(-1))
    {
        std::cerr<<"mq_open failed :"<<std::strerror(errno)<<'\n';
        exit(EXIT_FAILURE);
    }

    mqd_t display_queue = mq_open(DISPLAY_COMMAND_QUEUE_NAME, O_WRONLY);

    if(display_queue == static_cast<mqd_t>(-1))
    {
        std::cerr<<"mq_open failed :"<<std::strerror(errno)<<'\n';
        mq_close(camera_queue);
        exit(EXIT_FAILURE);   
    }
    bool running = true;

    while(running)
    {
        print_menu();
        char choice{};
        std::cin>>choice;

        switch(choice)
        {
            case '1':
                send_command(camera_queue,CommandType::START_CAMERA);
                break;
            case '2':
                send_command(camera_queue,CommandType::STOP_CAMERA);
                break;
            case '3':
                send_command(camera_queue,CommandType::SHUTDOWN);
                send_command(display_queue,CommandType::SHUTDOWN);
                break;
            case 'q':
            case 'Q':
                std::cout<<"Exiting HMI only";
                running = false;
                break;
            default:
                std::cout<<"Invalid choice \n";
                break;
        }
    }
    
    if(mq_close(camera_queue) == -1)
    {
        std::cerr<<"mq_close failed:"<<std::strerror(errno)<<'\n';
        exit(EXIT_FAILURE);
    }
    if(mq_close(display_queue) == -1)
    {
        std::cerr<<"mq_close failed:"<<std::strerror(errno)<<'\n';
        exit(EXIT_FAILURE);
    }

    std::cout<<"HMI service : stopped \n";
    return 0;

}