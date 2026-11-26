#include "platform/pack_job.h"
#include "art/pack_builder.h"
namespace mtv {
Upscaler upscaler_in(const std::filesystem::path& base) {
#ifdef _WIN32
    const char* name = "realesrgan-ncnn-vulkan.exe";
#else
    const char* name = "realesrgan-ncnn-vulkan";
#endif
    return {base / "upscaler" / name, base / "upscaler" / "models"};
}
PackJob::~PackJob() { cancel(); join(); }
void PackJob::start(const std::filesystem::path& data_dir, const Upscaler& up, const std::filesystem::path& out_pack,
                    const std::filesystem::path& work) {
    join();
    cancel_ = false; done_ = 0; total_ = 0;
    { std::lock_guard l(m_); error_.reset(); }
    started_ = true;
    running_ = true;
    worker_ = std::thread([this, data_dir, up, out_pack, work] {
        auto r = art::build_pack(data_dir, up.runner, up.models, out_pack, work,
                                 [this](art::PackProgress p) { done_ = p.done; total_ = p.total; }, cancel_);
        std::error_code ec;
        std::filesystem::remove_all(work, ec); // the intermediate pictures are not kept, whatever the outcome
        if (!r.has_value()) { std::lock_guard l(m_); error_ = r.error().reason; }
        running_ = false;
    });
}
std::optional<std::string> PackJob::error() const { std::lock_guard l(m_); return error_; }
}
