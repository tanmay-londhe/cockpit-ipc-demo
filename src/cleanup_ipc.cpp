#include <iostream>
#include <cerrno>
#include <cstring>
#include <semaphore.h>
#include <sys/mman.h>
#include "common.hpp"
#include <mqueue.h>

namespace
{
    void unlink_shared_memory(const char* name)
    {
        if(shm_unlink(name) == -1)
        {
            if(errno == ENOENT)
            {
                std::cout << "Cleanup: shared memory not found : " << name << '\n';
            }
        else
            {
                std::cerr << "Cleanup: shm_unlink failed for " << name << " : " << std::strerror(errno) << '\n';
            }

        }
        else
        {
            std::cout<<"Cleanup : Removed shared memory"<<name<<'\n';
        }
    }

    void unlink_semaphore(const char* name)
    {
        if(sem_unlink(name) == -1)
        {
            if (errno == ENOENT)
            {
                std::cout << "Cleanup: shared semaphore not found : " << name << '\n';
            }
            else
            {
                std::cerr << "Cleanup : sem_unlink failed for " << name << ": " << std::strerror(errno) << '\n';
            }
        }
        else
        {
            std::cout<<"Cleanup : Removed shared semaphore "<<name<<'\n';
        }
    }

    void unlink_message_queue(const char* name)
    {
        if(mq_unlink(name) == -1)
        {
            if(errno = ENOENT)
            {
                std::cout<<"Cleanup : message queue not found :"<<name<<'\n';
            }
            else
            {
                std::cerr<<"Cleanup : mq_unlink failed for "<<name<<":"<<std::strerror(errno)<<'\n';
            }
        }
        else
        {
            std::cout<<"Cleanup : Removed message queue "<<name<<'\n';
        }
    }
}

int main()
{
    errno = 0;
    using namespace cockpit;
    std::cout<<"Cleaning up cockpit IPC objects ... \n";

    unlink_shared_memory(SHM_NAME);

    unlink_semaphore(SEM_BUFFER_EMPTY);
    unlink_semaphore(SEM_FRAME_READY);

    unlink_message_queue(CAMERA_COMMAND_QUEUE_NAME);
    unlink_message_queue(DISPLAY_COMMAND_QUEUE_NAME);

    std::cout<<"Cleanup complete";
    return 0;
}
