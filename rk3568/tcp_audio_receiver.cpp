#include "edgeaudio/tcp_audio_receiver.h"

#include "edgeaudio/pcm16_stream_assembler.h"

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace edgeaudio {
namespace {

int make_server(int port, int backlog) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket: " + std::string(std::strerror(errno)));

    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        const std::string message = "setsockopt: " + std::string(std::strerror(errno));
        close(fd);
        throw std::runtime_error(message);
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(fd, backlog) < 0) {
        const std::string message = "bind/listen: " + std::string(std::strerror(errno));
        close(fd);
        throw std::runtime_error(message);
    }
    return fd;
}

}  // namespace

TcpAudioReceiver::TcpAudioReceiver(int port, int backlog) : server_fd_(make_server(port, backlog)) {}

TcpAudioReceiver::~TcpAudioReceiver() { stop(); }

int TcpAudioReceiver::accept_client() {
    const int client = accept(server_fd_, nullptr, nullptr);
    if (client >= 0) return client;
    if (errno == EINTR || errno == EBADF || errno == EINVAL) return -1;
    throw std::runtime_error("accept: " + std::string(std::strerror(errno)));
}

bool TcpAudioReceiver::receive_client(int client_fd, const SampleCallback& callback,
                                      const StopPredicate& should_stop) {
    if (client_fd < 0 || !callback) return false;
    struct ClientLease {
        int fd;
        ~ClientLease() {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
    } client{client_fd};
    Pcm16StreamAssembler assembler;
    std::vector<std::uint8_t> bytes(8192);
    bool clean_eof = false;

    while (true) {
        const ssize_t count = recv(client_fd, bytes.data(), bytes.size(), 0);
        if (count > 0) {
            std::vector<std::int16_t> samples;
            assembler.feed(bytes.data(), static_cast<std::size_t>(count), &samples);
            if (!samples.empty()) callback(samples);
            continue;
        }
        if (count == 0) {
            clean_eof = true;
            break;
        }
        if (errno == EINTR) {
            if (should_stop && should_stop()) break;
            continue;
        }
        break;
    }

    // A residual byte is discarded at the client boundary. It cannot be
    // joined with a new client without corrupting the new PCM stream.
    assembler.reset();
    return clean_eof;
}

void TcpAudioReceiver::stop() {
    if (server_fd_ < 0) return;
    shutdown(server_fd_, SHUT_RDWR);
    close(server_fd_);
    server_fd_ = -1;
}

}  // namespace edgeaudio
