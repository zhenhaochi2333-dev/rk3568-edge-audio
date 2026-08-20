#pragma once

#include "audio_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace edgeaudio {

class YamnetRknnModel {
public:
    YamnetRknnModel(std::string backend, std::string model_path);
    ~YamnetRknnModel();

    YamnetRknnModel(const YamnetRknnModel&) = delete;
    YamnetRknnModel& operator=(const YamnetRknnModel&) = delete;

    const std::string& backend() const { return backend_; }
    const std::string& error() const { return error_; }
    bool ready() const;

    bool infer(const std::vector<std::int16_t>& samples, bool speech,
               std::vector<float>* frame_scores, double* inference_ms);

private:
    bool load_rknn();

    std::string backend_;
    std::string model_path_;
    std::string error_;
    std::size_t output_count_ = 0;
    std::size_t score_output_index_ = 0;
    std::size_t score_element_count_ = 0;
    std::uint32_t input_element_count_ = 0;
    int input_format_ = 0;
    bool context_ready_ = false;
#if EDGEAUDIO_HAS_RKNN
    void* context_ = nullptr;
#endif
};

}  // namespace edgeaudio
