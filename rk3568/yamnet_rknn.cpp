#include "edgeaudio/yamnet_rknn.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

#if EDGEAUDIO_HAS_RKNN
#include "rknn_api.h"
#endif

namespace edgeaudio {
namespace {
constexpr std::size_t kYamnetFrames = 6;
constexpr std::size_t kYamnetClasses = 521;
constexpr std::size_t kYamnetScores = kYamnetFrames * kYamnetClasses;
}

YamnetRknnModel::YamnetRknnModel(std::string backend, std::string model_path)
    : backend_(std::move(backend)), model_path_(std::move(model_path)) {
    if (backend_ == "mock") {
        context_ready_ = true;
    } else if (backend_ == "rknn") {
#if EDGEAUDIO_HAS_RKNN
        context_ready_ = load_rknn();
#else
        error_ = "RKNN runtime not compiled in";
#endif
    } else {
        error_ = "unsupported YAMNet backend: " + backend_;
    }
}

YamnetRknnModel::~YamnetRknnModel() {
#if EDGEAUDIO_HAS_RKNN
    if (context_) {
        rknn_destroy(static_cast<rknn_context>(reinterpret_cast<std::uintptr_t>(context_)));
        context_ = nullptr;
    }
#endif
}

bool YamnetRknnModel::ready() const { return context_ready_; }

bool YamnetRknnModel::infer(const std::vector<std::int16_t>& samples, bool speech,
                            std::vector<float>* frame_scores, double* inference_ms) {
    if (!frame_scores || !inference_ms || samples.size() != kYamnetWindowSamples || !ready()) {
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    frame_scores->assign(kYamnetScores, 0.0f);
    if (backend_ == "mock") {
        for (std::size_t frame = 0; frame < kYamnetFrames; ++frame) {
            (*frame_scores)[frame * kYamnetClasses + (speech ? 0U : 497U)] = speech ? 0.90f : 0.95f;
            (*frame_scores)[frame * kYamnetClasses + (speech ? 497U : 0U)] = 0.08f;
        }
        *inference_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return true;
    }

#if EDGEAUDIO_HAS_RKNN
    std::vector<float> input(kYamnetWindowSamples);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        input[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    rknn_input input_desc{};
    input_desc.index = 0;
    input_desc.type = RKNN_TENSOR_FLOAT32;
    input_desc.fmt = static_cast<rknn_tensor_format>(input_format_);
    input_desc.size = input.size() * sizeof(float);
    input_desc.buf = input.data();
    auto context = static_cast<rknn_context>(reinterpret_cast<std::uintptr_t>(context_));
    if (rknn_inputs_set(context, 1, &input_desc) != RKNN_SUCC ||
        rknn_run(context, nullptr) != RKNN_SUCC) {
        error_ = "rknn input/run failed";
        return false;
    }

    std::vector<rknn_output> outputs(output_count_);
    for (auto& output : outputs) output.want_float = 1;
    if (rknn_outputs_get(context, static_cast<uint32_t>(outputs.size()), outputs.data(), nullptr) != RKNN_SUCC) {
        error_ = "rknn_outputs_get failed";
        return false;
    }
    struct OutputLease {
        rknn_context context;
        std::vector<rknn_output>& outputs;
        ~OutputLease() { rknn_outputs_release(context, static_cast<uint32_t>(outputs.size()), outputs.data()); }
    } lease{context, outputs};

    const auto* raw = static_cast<const float*>(outputs[score_output_index_].buf);
    if (!raw) {
        error_ = "YAMNet score output buffer is null";
        return false;
    }
    frame_scores->assign(raw, raw + score_element_count_);
    *inference_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return true;
#else
    (void)speech;
    return false;
#endif
}

#if EDGEAUDIO_HAS_RKNN
bool YamnetRknnModel::load_rknn() {
    std::ifstream file(model_path_, std::ios::binary | std::ios::ate);
    if (!file) {
        error_ = "cannot open YAMNet model: " + model_path_;
        return false;
    }
    const auto file_size = file.tellg();
    if (file_size <= 0) {
        error_ = "YAMNet model is empty: " + model_path_;
        return false;
    }
    file.seekg(0);
    std::vector<unsigned char> data(static_cast<std::size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(data.data()), file_size)) {
        error_ = "cannot read YAMNet model: " + model_path_;
        return false;
    }

    rknn_context context = 0;
    if (rknn_init(&context, data.data(), data.size(), 0, nullptr) != RKNN_SUCC) {
        error_ = "rknn_init failed";
        return false;
    }
    struct ContextLease {
        rknn_context context;
        ~ContextLease() {
            if (context) rknn_destroy(context);
        }
    } lease{context};
    auto fail = [this](const std::string& message) {
        error_ = message;
        return false;
    };

    rknn_input_output_num io{};
    if (rknn_query(lease.context, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)) != RKNN_SUCC) {
        return fail("rknn_query input/output count failed");
    }
    if (io.n_input != 1 || io.n_output == 0) {
        return fail("unexpected YAMNet tensor count: input=" + std::to_string(io.n_input) +
                    " output=" + std::to_string(io.n_output));
    }

    rknn_tensor_attr input{};
    input.index = 0;
    if (rknn_query(lease.context, RKNN_QUERY_INPUT_ATTR, &input, sizeof(input)) != RKNN_SUCC) {
        return fail("rknn_query input attributes failed");
    }
    if (input.n_elems != kYamnetWindowSamples || input.type != RKNN_TENSOR_FLOAT32) {
        return fail("unexpected YAMNet input: elements=" + std::to_string(input.n_elems) +
                    " type=" + std::to_string(input.type));
    }
    if (input.n_dims != 2 || input.dims[0] != 1 || input.dims[1] != kYamnetWindowSamples) {
        return fail("unexpected YAMNet input shape; expected [1,48000]");
    }
    input_element_count_ = input.n_elems;
    input_format_ = static_cast<int>(input.fmt);

    rknn_tensor_type score_type = RKNN_TENSOR_FLOAT32;
    for (uint32_t index = 0; index < io.n_output; ++index) {
        rknn_tensor_attr output{};
        output.index = index;
        if (rknn_query(lease.context, RKNN_QUERY_OUTPUT_ATTR, &output, sizeof(output)) != RKNN_SUCC) {
            return fail("rknn_query output attributes failed for index " + std::to_string(index));
        }
        std::cout << "[INFO] YAMNet output[" << index << "] name=" << output.name
                  << " elems=" << output.n_elems << " type=" << output.type
                  << " fmt=" << output.fmt << " dims=";
        for (uint32_t dimension = 0; dimension < output.n_dims; ++dimension) {
            if (dimension != 0) std::cout << "x";
            std::cout << output.dims[dimension];
        }
        std::cout << "\n";
        const bool expected_score_shape =
            (output.n_dims == 2 && output.dims[0] == kYamnetFrames && output.dims[1] == kYamnetClasses) ||
            (output.n_dims == 3 && output.dims[0] == 1 && output.dims[1] == kYamnetFrames &&
             output.dims[2] == kYamnetClasses);
        if (expected_score_shape && output.n_elems == kYamnetScores && score_element_count_ == 0) {
            score_output_index_ = index;
            score_element_count_ = output.n_elems;
            score_type = output.type;
        }
    }
    if (score_element_count_ != kYamnetScores) {
        return fail("YAMNet score tensor with 6x521 elements was not found");
    }
    if (score_type != RKNN_TENSOR_FLOAT32 && score_type != RKNN_TENSOR_FLOAT16 &&
        score_type != RKNN_TENSOR_INT8 && score_type != RKNN_TENSOR_UINT8) {
        return fail("unsupported YAMNet score tensor type: " + std::to_string(score_type));
    }
    output_count_ = io.n_output;
    context_ = reinterpret_cast<void*>(static_cast<std::uintptr_t>(lease.context));
    lease.context = 0;
    std::cout << "[INFO] RKNN model loaded input_elems=" << input_element_count_
              << " score_output_index=" << score_output_index_ << " outputs=" << output_count_ << "\n";
    return true;
}
#endif

}  // namespace edgeaudio
