#pragma once

#include <cstddef>
#include <cstdint>

namespace cockpit
{
    constexpr const char *SHM_NAME = "/cockpit_frame_buffer";
    constexpr const char *SEM_BUFFER_EMPTY = "/cockpit_buffer_empty";
    constexpr const char *SEM_FRAME_READY = "/cockpit_frame_ready";

    constexpr const char* CAMERA_COMMAND_QUEUE_NAME = "/cockpit_camera_cmd_queue";
    constexpr const char* DISPLAY_COMMAND_QUEUE_NAME = "/cockpit_display_cmd_queue";
    constexpr long COMMAND_QUEUE_MESSAGES = 10;

    constexpr int BENCHMARK_FRAME_COUNT = 1000;
    constexpr bool ENABLE_PAYLOAD_CHECKSUM = true;
    constexpr int FRAME_LOG_INTERVAL = 100;

    constexpr std::size_t FRAME_PAYLOAD_SIZE = 1024 * 1024;

    struct FrameBuffer
    {
        uint32_t frame_id;
        uint64_t timestamp_ns;
        uint32_t width;
        uint32_t height;
        uint32_t payload_size;
        char payload[FRAME_PAYLOAD_SIZE];
    };

    enum class CommandType : uint32_t 
    {
        START_CAMERA = 1,
        STOP_CAMERA = 2,
        SHUTDOWN = 3
    };

    struct CommandMessage
    {
        CommandType type;
        uint64_t timestamp_ns;
    };
}
