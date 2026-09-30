#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace lan {
constexpr uint16_t port = 24808;
struct Frame {
    std::array<uint8_t, 2048> pixels{};
    bool beep = false;
    bool paused = false;
};

class Connection {
public:
    Connection();
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    bool host(uint16_t listen_port = port);
    bool join(const std::string& ipv4, uint16_t remote_port = port);
    void poll();
    void send_input(uint8_t held); // bit 0: up, bit 1: down
    void send_frame(const Frame& frame);
    bool ready() const;
    bool failed() const;
    const std::string& status() const;
    uint8_t input() const;
    bool take_frame(Frame& frame);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
