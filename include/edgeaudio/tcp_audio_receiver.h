#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace edgeaudio {

class TcpAudioReceiver {
public:
    using SampleCallback = std::function<void(const std::vector<std::int16_t>&)>;
    using StopPredicate = std::function<bool()>;

    explicit TcpAudioReceiver(int port, int backlog = 4);
    ~TcpAudioReceiver();

    TcpAudioReceiver(const TcpAudioReceiver&) = delete;
    TcpAudioReceiver& operator=(const TcpAudioReceiver&) = delete;

    int accept_client();
    bool receive_client(int client_fd, const SampleCallback& callback,
                        const StopPredicate& should_stop = {});
    void stop();

private:
    int server_fd_ = -1;
};

}  // namespace edgeaudio
