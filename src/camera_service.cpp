#include <iostream>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include "common.hpp"
#include <csignal>
#include <mqueue.h>


namespace
{
    volatile std::sig_atomic_t stop_requested = 0;
    
    void hande_signal(int)
    {
        stop_requested = 1;
    }

    uint64_t now_ns()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

        return static_cast<uint64_t>(ns);
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

    bool wait_for_semaphore(sem_t* semaphore, const char* semaphore_name)
    {
        while(sem_wait(semaphore) == -1)
        {
            if(errno == EINTR)
            {
                if(stop_requested)
                {
                    return false;
                }
                continue;
            }
            std::cerr<<"sem_wait failed for "<<semaphore_name<<": "<<std::strerror(errno)<<'\n';
            std::exit(EXIT_FAILURE);
        }
        return true;
    }

    bool poll_command(mqd_t command_queue,cockpit::CommandMessage &command)
    {
        const ssize_t bytes_received = mq_receive(command_queue,reinterpret_cast<char*>(&command),sizeof(command),nullptr);

        if(bytes_received == -1)
        {
            if(errno == EAGAIN)
            {
                return false;
            }

            if(errno == EINTR)
            {
                return false;
            }
            std::cerr<<"mq_received failed:"<<std::strerror(errno)<<'\n';
            exit(EXIT_FAILURE);
        }

        if(static_cast<ssize_t>(bytes_received) != sizeof(command))
        {
            std::cerr<<"Received invalid command size: "<<bytes_received<<"bytes\n";
            return false;
        }
        return true;
    }

    void handle_command(const cockpit::CommandMessage &command, bool &camera_enabled, bool &shutdown_requested)
    {
        std::cout<<"Receieved command :"<<command_to_string(command.type)<<'\n';

        switch(command.type)
        {
            case cockpit::CommandType::START_CAMERA:
                camera_enabled = true;
                std::cout<<"camera state: RUNNING \n";
                break;
            case cockpit::CommandType::STOP_CAMERA:
                camera_enabled = false;
                std::cout<<"camera state: STOOPED\n";
                break;
            case cockpit::CommandType::SHUTDOWN:
                shutdown_requested = true;
                std::cout<<"shutdown requested \n";
                break;
            default:
                std::cout<<"Unknown command \n";
                break;
        }
    }
}


int main()
{
    std::signal(SIGINT, hande_signal);
    std::signal(SIGTERM, hande_signal);
    using namespace cockpit;
    
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1)
    {
        std::cerr << "Camera service: shm_open failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (ftruncate(fd, sizeof(FrameBuffer)) == -1)
    {
        std::cerr << "Camera service: ftruncate failed: " << std::strerror(errno) << '\n';
        close(fd);
        return 1;
    }
    void *mapped_memory = mmap(nullptr, sizeof(FrameBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (mapped_memory == MAP_FAILED)
    {
        std::cerr << "Camera service: mmap failed : " << std::strerror(errno) << '\n';
        close(fd);
        return 1;
    }
    sem_t *buffer_empty = sem_open(SEM_BUFFER_EMPTY, O_CREAT, 0666, 1);
    if (buffer_empty == SEM_FAILED)
    {
        std::cerr << "Camera service: sem_open buffer_empty failed : " << std::strerror(errno) << '\n';
        munmap(mapped_memory, sizeof(FrameBuffer));
        close(fd);
        return 1;
    }
    sem_t *buffer_ready = sem_open(SEM_FRAME_READY, O_CREAT, 0666, 0);
    if (buffer_ready == SEM_FAILED)
    {
        std::cerr << "Camera service: sem_open buffer_ready failed : " << std::strerror(errno) << '\n';
        sem_close(buffer_empty);
        munmap(mapped_memory, sizeof(FrameBuffer));
        close(fd);
        return 1;
    }

    FrameBuffer *frame = static_cast<FrameBuffer *>(mapped_memory);

      
    mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = COMMAND_QUEUE_MESSAGES;
    attr.mq_msgsize = sizeof(CommandMessage);
    attr.mq_curmsgs = 0;

    mqd_t command_queue = mq_open(CAMERA_COMMAND_QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK,0666, &attr);

    if(command_queue == static_cast<mqd_t>(-1))
    {
        std::cerr<<"mq_open failed :"<<std::strerror(errno)<<'\n';
        exit(EXIT_FAILURE);
    }

    bool camera_enabled = false;
    bool shutdown_requested = false;
    uint32_t frame_id{0};

    std::cout<<" Camera service : waiting for commands ... \n";
    std::cout<<"Camera state: STOPPED\n";

    while(!shutdown_requested && !stop_requested)
    {
        CommandMessage command{};
        while(poll_command(command_queue,command))
        {
            handle_command(command,camera_enabled,shutdown_requested);
        }

        if(shutdown_requested || stop_requested)
        {
            break;
        }

        if(!camera_enabled)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if(!wait_for_semaphore(buffer_empty,"buffer_empty"))
        {
            break;
        }
        frame->frame_id = frame_id;
        frame->timestamp_ns = now_ns();
        frame->width = 800;
        frame->height = 480;
        frame->payload_size = FRAME_PAYLOAD_SIZE;

        std::memset(frame -> payload, static_cast<int>(frame_id % 256),FRAME_PAYLOAD_SIZE);

        if (sem_post(buffer_ready) == -1)
        {
            std::cerr << "Camera Service: sem_post frame_ready failed: " << std::strerror(errno) << "\n";
            break;
        }

        std::cout<<"Produced frame "<<frame_id<< '\n';
        ++frame_id;
    }
    
    sem_close(buffer_ready);
    sem_close(buffer_empty);
    munmap(mapped_memory, sizeof(FrameBuffer));
    close(fd);
    mq_close(command_queue);

    std::cout << "Camera service: exiting. \n";

    return 0;
}
