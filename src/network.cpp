#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif
#include "network.h"
#include <algorithm>
#include <chrono>
#include <vector>

namespace lan {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
int socket_error() { return WSAGetLastError(); }
bool pending(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
bool nonblocking(Socket s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on) == 0; }
#else
using Socket = int;
constexpr Socket invalid = -1;
void close_socket(Socket s) { close(s); }
int socket_error() { return errno; }
bool pending(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS; }
bool nonblocking(Socket s) { return fcntl(s, F_SETFL, O_NONBLOCK) == 0; }
#endif
using Clock = std::chrono::steady_clock;
constexpr uint8_t hello_host = 1, hello_guest = 2, input_message = 3, frame_message = 4;
constexpr size_t header_size = 8;
}

struct Connection::Impl {
    Socket listener = invalid, peer = invalid;
    bool is_host = false, connecting = false, connected = false, handshaken = false;
    bool broken = false, fresh_frame = false;
    uint8_t held = 0;
    Frame frame;
    std::string message = "Not connected";
    std::vector<uint8_t> rx, tx;
    size_t sent = 0;
    Clock::time_point last_received = Clock::now();
#ifdef _WIN32
    bool winsock = false;
    Impl() {
        WSADATA data;
        winsock = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!winsock) fail("Could not initialize networking");
    }
#endif
    ~Impl() {
        if (peer != invalid) close_socket(peer);
        if (listener != invalid) close_socket(listener);
#ifdef _WIN32
        if (winsock) WSACleanup();
#endif
    }
    void fail(const std::string& reason) {
        broken = true; handshaken = false; held = 0; message = reason;
        if (peer != invalid) { close_socket(peer); peer = invalid; }
        if (listener != invalid) { close_socket(listener); listener = invalid; }
        rx.clear(); tx.clear(); sent = 0;
    }
    void queue(uint8_t type, const std::vector<uint8_t>& payload) {
        // A partially sent message must finish; discard new snapshots until it does.
        // Callers publish fresh state every tick, so stale snapshots never pile up.
        if (!tx.empty() || broken) return;
        tx = {'C', '8', 'L', 'N', 1, type,
              static_cast<uint8_t>(payload.size() >> 8), static_cast<uint8_t>(payload.size())};
        tx.insert(tx.end(), payload.begin(), payload.end());
        sent = 0;
    }
    void established() {
        connecting = false; connected = true;
        int on = 1;
        setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
#ifdef SO_NOSIGPIPE
        setsockopt(peer, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        last_received = Clock::now();
        message = "Checking game version...";
        queue(is_host ? hello_host : hello_guest, {});
    }
    void parse() {
        while (rx.size() >= header_size) {
            const size_t length = (size_t(rx[6]) << 8) | rx[7];
            const uint8_t type = rx[5];
            const uint8_t expected = !handshaken ? (is_host ? hello_guest : hello_host)
                                               : (is_host ? input_message : frame_message);
            const size_t expected_length = !handshaken ? 0 : (is_host ? 1 : 257);
            if (rx[0] != 'C' || rx[1] != '8' || rx[2] != 'L' || rx[3] != 'N' ||
                rx[4] != 1 || type != expected || length != expected_length) {
                fail("Incompatible or invalid Pong connection"); return;
            }
            if (rx.size() < header_size + length) return;
            if (!handshaken) {
                handshaken = true; message = "Connected";
            } else if (is_host) {
                if (rx[8] > 3) { fail("Invalid paddle input"); return; }
                held = rx[8];
            } else {
                if (rx[8] > 3) { fail("Invalid frame flags"); return; }
                frame.beep = (rx[8] & 1) != 0;
                frame.paused = (rx[8] & 2) != 0;
                for (size_t i = 0; i < frame.pixels.size(); ++i)
                    frame.pixels[i] = (rx[9 + i / 8] >> (7 - i % 8)) & 1;
                fresh_frame = true;
            }
            last_received = Clock::now();
            rx.erase(rx.begin(), rx.begin() + header_size + length);
        }
    }
};

Connection::Connection() : impl(new Impl) {}
Connection::~Connection() = default;
bool Connection::host(uint16_t listen_port) {
    auto& s = *impl;
    if (s.broken || s.listener != invalid || s.peer != invalid) return false;
    s.is_host = true;
    s.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(listen_port);
    if (s.listener == invalid || !nonblocking(s.listener) ||
        bind(s.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(s.listener, 1) != 0) {
        s.fail("Cannot host: port unavailable or blocked"); return false;
    }
    s.message = "Waiting for player on port " + std::to_string(listen_port);
    return true;
}
bool Connection::join(const std::string& ipv4, uint16_t remote_port) {
    auto& s = *impl;
    if (s.broken || s.listener != invalid || s.peer != invalid) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_port = htons(remote_port);
    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1) {
        s.fail("Enter a valid IPv4 address, e.g. 192.168.1.20"); return false;
    }
    s.peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.peer == invalid || !nonblocking(s.peer)) {
        s.fail("Could not create connection"); return false;
    }
    s.last_received = Clock::now();
    if (connect(s.peer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
        s.established();
    else if (pending(socket_error())) {
        s.connecting = true; s.message = "Connecting to " + ipv4 + "...";
    } else { s.fail("Connection failed: check host and address"); return false; }
    return true;
}
void Connection::poll() {
    auto& s = *impl;
    if (s.broken) return;
    if (s.listener != invalid) {
        s.peer = accept(s.listener, nullptr, nullptr);
        if (s.peer == invalid) {
            if (!pending(socket_error())) s.fail("Could not accept connection");
            return;
        }
        close_socket(s.listener); s.listener = invalid;
        if (!nonblocking(s.peer)) { s.fail("Could not configure connection"); return; }
        s.established();
    }
    if (s.peer == invalid) return;
    if (Clock::now() - s.last_received > std::chrono::seconds(5)) {
        s.fail("Connection timed out"); return;
    }
    if (s.connecting) {
        fd_set writable, errors;
        FD_ZERO(&writable); FD_ZERO(&errors);
        FD_SET(s.peer, &writable); FD_SET(s.peer, &errors);
        timeval wait{};
        const int result = select(static_cast<int>(s.peer + 1), nullptr, &writable, &errors, &wait);
        if (result < 0) { s.fail("Connection failed"); return; }
        if (result == 0) return;
        int error = 0;
#ifdef _WIN32
        int length = sizeof(error);
#else
        socklen_t length = sizeof(error);
#endif
        if (getsockopt(s.peer, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error) {
            s.fail("Connection refused: start Host Pong first"); return;
        }
        s.established();
    }
    if (!s.tx.empty()) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const int count = send(s.peer, reinterpret_cast<const char*>(s.tx.data() + s.sent),
                               static_cast<int>(s.tx.size() - s.sent), flags);
        if (count > 0) {
            s.sent += count;
            if (s.sent == s.tx.size()) { s.tx.clear(); s.sent = 0; }
        } else if (count == 0 || !pending(socket_error())) {
            s.fail("Player disconnected"); return;
        }
    }
    // Bound work per tick even if a peer floods the socket.
    for (int reads = 0; reads < 8; ++reads) {
        char buffer[1024];
        const int count = recv(s.peer, buffer, sizeof(buffer), 0);
        if (count == 0) { s.fail("Player disconnected"); return; }
        if (count < 0) {
            if (!pending(socket_error())) s.fail("Connection lost");
            return;
        }
        s.rx.insert(s.rx.end(), buffer, buffer + count);
        s.parse();
        if (s.broken) return;
    }
}
void Connection::send_input(uint8_t held) {
    if (ready() && !impl->is_host) impl->queue(input_message, {uint8_t(held & 3)});
}
void Connection::send_frame(const Frame& frame) {
    if (!ready() || !impl->is_host) return;
    std::vector<uint8_t> payload(257, 0);
    payload[0] = (frame.beep ? 1 : 0) | (frame.paused ? 2 : 0);
    for (size_t i = 0; i < frame.pixels.size(); ++i)
        if (frame.pixels[i]) payload[1 + i / 8] |= uint8_t(1 << (7 - i % 8));
    impl->queue(frame_message, payload);
}
bool Connection::ready() const { return impl->handshaken && !impl->broken; }
bool Connection::failed() const { return impl->broken; }
const std::string& Connection::status() const { return impl->message; }
uint8_t Connection::input() const { return impl->held; }
bool Connection::take_frame(Frame& frame) {
    if (!impl->fresh_frame) return false;
    frame = impl->frame; impl->fresh_frame = false; return true;
}
} // namespace lan
