#include <cerrno>
#include <chrono>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "common.hpp"
#include <csignal>
#include <mqueue.h>
#include <thread>
#include <ctime>

namespace
{
    volatile std::sig_atomic_t stop_requested = 0;
    uint64_t now_ns()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

        return static_cast<uint64_t>(ns);
    }

    void hande_signal(int)
    {
        stop_requested = 1;
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

    void handle_command(const cockpit::CommandMessage &command, bool &shutdown_requested)
    {
        std::cout<<"Receieved command :"<<command_to_string(command.type)<<'\n';

        switch(command.type)
        {
            case cockpit::CommandType::SHUTDOWN:
                shutdown_requested = true;
                std::cout<<"shutdown requested \n";
                break;
            default:
                std::cout<<"Unknown command \n";
                break;
        }
    }

    timespec make_timeout_from_now_ms(long timeout_ms)
    {
        timespec timeout{};

        if(clock_gettime(CLOCK_REALTIME, &timeout) == -1)
        {
            std::cerr<<"clock_gettime failed:"<<std::strerror(errno)<<'\n';
            exit(EXIT_FAILURE);
        }
        timeout.tv_nsec += timeout_ms *1000*1000;

        while(timeout.tv_nsec >= 1000*1000*1000)
        {
            timeout.tv_nsec += 1;
            timeout.tv_nsec -= 1000 * 1000 * 1000;
        }
        
        return timeout;
    }

    bool wait_for_frame_with_timeout(sem_t* buffer_ready, long timeout_ms)
    {
        while(true)
        {
            timespec timeout = make_timeout_from_now_ms(timeout_ms);

            if(sem_timedwait(buffer_ready, &timeout) == 0)
            {
                return true;
            }
            if(errno == ETIMEDOUT)
            {
                return false;
            }
            if(errno == EINTR)
            {
                if(stop_requested)
                {
                    return false;
                }
                continue;
            }
            std::cerr<<"sem_timedwait failed for buffer_ready :"<<std::strerror(errno)<<'\n';
            exit(EXIT_FAILURE);
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
}

int main()
{
    using namespace cockpit;

    std::signal(SIGINT, hande_signal);
    std::signal(SIGTERM, hande_signal);
    errno = 0;
    
    std::cout << "Display manager: starting ... \n";

    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd == -1)
    {
        std::cerr << "Display manager: shm_open failed: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (ftruncate(fd, sizeof(FrameBuffer)) == -1)
    {
        std::cerr << "Display manager: ftruncate failed: " << std::strerror(errno) << '\n';
        close(fd);
        return 1;
    }
    void *mapped_memory = mmap(nullptr, sizeof(FrameBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (mapped_memory == MAP_FAILED)
    {
        std::cerr << "Display manager: mmap failed : " << std::strerror(errno) << '\n';
        close(fd);
        return 1;
    }
    sem_t *buffer_empty = sem_open(SEM_BUFFER_EMPTY, O_CREAT, 0666, 1);
    if (buffer_empty == SEM_FAILED)
    {
        std::cerr << "Display manager: sem_open buffer_empty failed : " << std::strerror(errno) << '\n';
        munmap(mapped_memory, sizeof(FrameBuffer));
        close(fd);
        return 1;
    }
    sem_t *buffer_ready = sem_open(SEM_FRAME_READY, O_CREAT, 0666, 0);
    if (buffer_ready == SEM_FAILED)
    {
        std::cerr << "Display manager: sem_open buffer_ready failed : " << std::strerror(errno) << '\n';
        sem_close(buffer_empty);
        munmap(mapped_memory, sizeof(FrameBuffer));
        close(fd);
        return 1;
    }

    mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = COMMAND_QUEUE_MESSAGES;
    attr.mq_msgsize = sizeof(CommandMessage);
    attr.mq_curmsgs = 0;

    mqd_t command_queue = mq_open(DISPLAY_COMMAND_QUEUE_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0666, &attr);

    if(command_queue == static_cast<mqd_t>(-1))
    {
        std::cerr<<"mq_open failed :"<<std::strerror(errno)<<'\n';
        exit(EXIT_FAILURE);
    }

    FrameBuffer *frame = static_cast<FrameBuffer *>(mapped_memory);

    bool shutdown_requested = false;

    std::cout<<"Waiting for frames or shutdown ... \n";

    while(!shutdown_requested && !stop_requested)
    {
        CommandMessage command{};

        while(poll_command(command_queue,command))
        {
            handle_command(command, shutdown_requested);
        }

        if(shutdown_requested || stop_requested)
            break;

        const bool frame_available = wait_for_frame_with_timeout(buffer_ready, 100);
        
        if(!frame_available)
            continue;

        const uint32_t frame_id = frame->frame_id;
        const uint64_t timestamp_ns = frame->timestamp_ns;
        const uint32_t payload_size = frame->payload_size;

        std::cout<<"Display receieved frame "<<frame_id<<", timestamp_ns "<<timestamp_ns<<", payload_size = "<<payload_size<<'\n';

        if(sem_post(buffer_empty) == -1)
        {
            std::cerr<<"sempost failed "<<std::strerror(errno)<<'\n';
            break;
        }
    }

    sem_close(buffer_ready);
    sem_close(buffer_empty);
    munmap(mapped_memory, sizeof(FrameBuffer));
    close(fd);
    mq_close(command_queue);

    std::cout << "Display manager : exiting... \n";

    return 0;
}
