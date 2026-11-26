#pragma once
// Builds the upscale pack on a worker thread while the app shows progress (first start, or F12).
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
namespace mtv {
struct Upscaler { std::filesystem::path runner, models; };
// The upscaler shipped with the app: <base>/upscaler/realesrgan-ncnn-vulkan[.exe] and <base>/upscaler/models.
Upscaler upscaler_in(const std::filesystem::path& base);
class PackJob {
public:
    ~PackJob();
    void start(const std::filesystem::path& data_dir, const Upscaler& up, const std::filesystem::path& out_pack,
               const std::filesystem::path& work);
    bool running() const { return running_; }
    bool finished() const { return started_ && !running_; }
    size_t done() const { return done_; }
    size_t total() const { return total_; }
    void cancel() { cancel_ = true; }
    void join() { if (worker_.joinable()) worker_.join(); }
    // After finishing: nullopt on success, else why it failed (or "cancelled").
    std::optional<std::string> error() const;
private:
    std::thread worker_;
    std::atomic<bool> running_{false}, started_{false}, cancel_{false};
    std::atomic<size_t> done_{0}, total_{0};
    mutable std::mutex m_;
    std::optional<std::string> error_;
};
}
