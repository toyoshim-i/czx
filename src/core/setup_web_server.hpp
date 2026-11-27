#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace czx::core {

class SetupWebServer {
public:
    SetupWebServer() = default;
    ~SetupWebServer();

    SetupWebServer(const SetupWebServer&) = delete;
    SetupWebServer& operator=(const SetupWebServer&) = delete;

    bool start(const std::string& data_dir, uint16_t preferred_port = 8088);
    void stop();

    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }
    uint16_t port() const noexcept { return port_.load(std::memory_order_acquire); }
    int upload_count() const noexcept { return upload_count_.load(std::memory_order_acquire); }
    std::vector<std::string> urls() const;
    std::string last_event() const;

private:
    std::string data_dir_;
    int listen_fd_ = -1;
    std::atomic<uint16_t> port_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_flag_{false};
    std::atomic<int> upload_count_{0};
    mutable std::mutex mtx_;
    std::string last_event_ = "Waiting for uploads";
    std::thread worker_;

    void serve_loop();
    void handle_client(int client_fd);
    void set_last_event(std::string msg);
};

}  // namespace czx::core
