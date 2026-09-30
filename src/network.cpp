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
using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCK = INVALID_SOCKET;

inline void close_socket(SocketHandle s) { closesocket(s); }
inline int get_last_socket_error() { return WSAGetLastError(); }
inline bool is_operation_pending(int err) { return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS; }

inline bool set_nonblocking(SocketHandle s) {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCK = -1;

inline void close_socket(SocketHandle s) { close(s); }
inline int get_last_socket_error() { return errno; }
inline bool is_operation_pending(int err) {
    return err == EWOULDBLOCK || err == EAGAIN || err == EINPROGRESS;
}

inline bool set_nonblocking(SocketHandle s) {
    return fcntl(s, F_SETFL, O_NONBLOCK) == 0;
}
#endif

inline void configure_stream_socket(SocketHandle s) {
    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag));
#endif
}

// Protocol Constants

constexpr uint8_t PROTOCOL_MAGIC[4] = {'C', '8', 'L', 'N'}; // Chip-8 LAN
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t  HEADER_SIZE = 8;

enum MessageType : uint8_t {
    MSG_HELLO_HOST = 1,
    MSG_HELLO_GUEST = 2,
    MSG_INPUT = 3,
    MSG_FRAME = 4
};

constexpr size_t PAYLOAD_INPUT_SIZE = 1;
constexpr size_t PAYLOAD_FRAME_SIZE = 257;
constexpr auto   CONNECTION_TIMEOUT = std::chrono::seconds(5);

using Clock = std::chrono::steady_clock;

}

// Connection Implementation

struct Connection::Impl {
    SocketHandle listener = INVALID_SOCK;
    SocketHandle peer = INVALID_SOCK;

    bool is_host = false;
    bool connecting = false;
    bool connected = false;
    bool handshaken = false;
    bool broken = false;
    bool fresh_frame = false;

    uint8_t held = 0;
    Frame latest_frame;

    std::string message = "Not connected";
    std::vector<uint8_t> rx_buffer;
    std::vector<uint8_t> tx_buffer;
    size_t tx_bytes_sent = 0;

    Clock::time_point last_activity = Clock::now();

#ifdef _WIN32
    bool winsock_initialized = false;

    Impl() {
        WSADATA data;
        winsock_initialized = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
        if (!winsock_initialized) {
            fail("Could not initialize networking");
        }
    }
#else
    Impl() = default;
#endif

    ~Impl() {
        disconnect_sockets();
#ifdef _WIN32
        if (winsock_initialized) {
            WSACleanup();
        }
#endif
    }

    void disconnect_sockets() {
        if (peer != INVALID_SOCK) {
            close_socket(peer);
            peer = INVALID_SOCK;
        }
        if (listener != INVALID_SOCK) {
            close_socket(listener);
            listener = INVALID_SOCK;
        }
    }

    void fail(const std::string& reason) {
        broken = true;
        handshaken = false;
        held = 0;
        message = reason;

        disconnect_sockets();
        rx_buffer.clear();
        tx_buffer.clear();
        tx_bytes_sent = 0;
    }

    void queue_packet(uint8_t type, const std::vector<uint8_t>& payload) {
        // Discard packet if a previous transmit is still in flight (non-blocking drops stale ticks)
        if (!tx_buffer.empty() || broken) {
            return;
        }

        const uint16_t length = static_cast<uint16_t>(payload.size());
        tx_buffer = {
            PROTOCOL_MAGIC[0], PROTOCOL_MAGIC[1], PROTOCOL_MAGIC[2], PROTOCOL_MAGIC[3],
            PROTOCOL_VERSION,
            type,
            static_cast<uint8_t>(length >> 8),
            static_cast<uint8_t>(length & 0xFF)
        };
        tx_buffer.insert(tx_buffer.end(), payload.begin(), payload.end());
        tx_bytes_sent = 0;
    }

    void on_connection_established() {
        connecting = false;
        connected  = true;
        configure_stream_socket(peer);

        last_activity = Clock::now();
        message = "Checking game version...";
        queue_packet(is_host ? MSG_HELLO_HOST : MSG_HELLO_GUEST, {});
    }

    // --- Packet Serialization & Decoding ---

    void decode_frame_payload(const uint8_t* payload) {
        const uint8_t flags = payload[0];
        if (flags > 3) {
            fail("Invalid frame flags");
            return;
        }

        latest_frame.beep = (flags & 1) != 0;
        latest_frame.paused = (flags & 2) != 0;

        // Unpack 256 bytes into 2048 individual display pixels (1 bit per pixel)
        const uint8_t* packed_pixels = &payload[1];
        for (size_t i = 0; i < latest_frame.pixels.size(); ++i) {
            latest_frame.pixels[i] = (packed_pixels[i / 8] >> (7 - (i % 8))) & 1;
        }
        fresh_frame = true;
    }

    void parse_incoming_packets() {
        while (rx_buffer.size() >= HEADER_SIZE) {
            // Validate protocol header magic and version
            const bool valid_magic = (rx_buffer[0] == PROTOCOL_MAGIC[0] && rx_buffer[1] == PROTOCOL_MAGIC[1] && rx_buffer[2] == PROTOCOL_MAGIC[2] && rx_buffer[3] == PROTOCOL_MAGIC[3] && rx_buffer[4] == PROTOCOL_VERSION);

            const uint8_t type = rx_buffer[5];
            const size_t  length = (static_cast<size_t>(rx_buffer[6]) << 8) | rx_buffer[7];
            const uint8_t expected_type = !handshaken ? (is_host ? MSG_HELLO_GUEST : MSG_HELLO_HOST) : (is_host ? MSG_INPUT : MSG_FRAME);
            const size_t  expected_len  = !handshaken ? 0 : (is_host ? PAYLOAD_INPUT_SIZE : PAYLOAD_FRAME_SIZE);

            if (!valid_magic || type != expected_type || length != expected_len) {
                fail("Incompatible or invalid Pong connection");
                return;
            }

            // Await full packet payload before parsing
            if (rx_buffer.size() < HEADER_SIZE + length) {
                return;
            }

            const uint8_t* payload = rx_buffer.data() + HEADER_SIZE;

            if (!handshaken) {
                handshaken = true;
                message    = "Connected";
            } else if (is_host) {
                if (payload[0] > 3) {
                    fail("Invalid paddle input");
                    return;
                }
                held = payload[0];
            } else {
                decode_frame_payload(payload);
                if (broken) return;
            }

            last_activity = Clock::now();
            rx_buffer.erase(rx_buffer.begin(), rx_buffer.begin() + HEADER_SIZE + length);
        }
    }

    // --- Sub-polling Stages ---

    void poll_listener() {
        if (listener == INVALID_SOCK) return;

        peer = accept(listener, nullptr, nullptr);
        if (peer == INVALID_SOCK) {
            if (!is_operation_pending(get_last_socket_error())) {
                fail("Could not accept connection");
            }
            return;
        }

        // Host only accepts one guest; close listener once connected
        close_socket(listener);
        listener = INVALID_SOCK;

        if (!set_nonblocking(peer)) {
            fail("Could not configure connection");
            return;
        }

        on_connection_established();
    }

    void poll_connecting_socket() {
        if (!connecting) return;

        fd_set writable, errors;
        FD_ZERO(&writable);
        FD_ZERO(&errors);
        FD_SET(peer, &writable);
        FD_SET(peer, &errors);

        timeval no_wait{};
        const int result = select(static_cast<int>(peer + 1), nullptr, &writable, &errors, &no_wait);
        if (result < 0) {
            fail("Connection failed");
            return;
        }
        if (result == 0) return; // Still connecting

        int sock_err = 0;
#ifdef _WIN32
        int opt_len = sizeof(sock_err);
#else
        socklen_t opt_len = sizeof(sock_err);
#endif
        if (getsockopt(peer, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sock_err), &opt_len) != 0 || sock_err != 0) {
            fail("Connection refused: start Host Pong first");
            return;
        }

        on_connection_established();
    }

    void flush_outgoing_traffic() {
        if (tx_buffer.empty()) return;

        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const int count = send(peer, reinterpret_cast<const char*>(tx_buffer.data() + tx_bytes_sent), static_cast<int>(tx_buffer.size() - tx_bytes_sent), flags);

        if (count > 0) {
            tx_bytes_sent += count;
            if (tx_bytes_sent == tx_buffer.size()) {
                tx_buffer.clear();
                tx_bytes_sent = 0;
            }
        }
        else if (count == 0 || !is_operation_pending(get_last_socket_error())) {
            fail("Player disconnected");
        }
    }

    void receive_incoming_traffic() {
        for (int iteration = 0; iteration < 8; ++iteration) {
            char buffer[1024];
            const int count = recv(peer, buffer, sizeof(buffer), 0);

            if (count == 0) {
                fail("Player disconnected");
                return;
            }
            if (count < 0) {
                if (!is_operation_pending(get_last_socket_error())) {
                    fail("Connection lost");
                }
                return;
            }

            rx_buffer.insert(rx_buffer.end(), buffer, buffer + count);
            parse_incoming_packets();

            if (broken) return;
        }
    }
};


Connection::Connection() : impl(std::make_unique<Impl>()) {}
Connection::~Connection() = default;

bool Connection::host(uint16_t listen_port) {
    auto& s = *impl;
    if (s.broken || s.listener != INVALID_SOCK || s.peer != INVALID_SOCK) {
        return false;
    }

    s.is_host  = true;
    s.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port        = htons(listen_port);

    if (s.listener == INVALID_SOCK || !set_nonblocking(s.listener) ||
        bind(s.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(s.listener, 1) != 0) {
        s.fail("Cannot host: port unavailable or blocked");
        return false;
    }

    s.message = "Waiting for player on port " + std::to_string(listen_port);
    return true;
}

bool Connection::join(const std::string& ipv4, uint16_t remote_port) {
    auto& s = *impl;
    if (s.broken || s.listener != INVALID_SOCK || s.peer != INVALID_SOCK) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port   = htons(remote_port);

    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1) {
        s.fail("Enter a valid IPv4 address, e.g. 192.168.1.20");
        return false;
    }

    s.peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.peer == INVALID_SOCK || !set_nonblocking(s.peer)) {
        s.fail("Could not create connection");
        return false;
    }

    s.last_activity = Clock::now();
    if (connect(s.peer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
        s.on_connection_established();
    } else if (is_operation_pending(get_last_socket_error())) {
        s.connecting = true;
        s.message = "Connecting to " + ipv4 + "...";
    } else {
        s.fail("Connection failed: check host and address");
        return false;
    }

    return true;
}

void Connection::poll() {
    auto& s = *impl;
    if (s.broken) return;

    s.poll_listener();
    if (s.peer == INVALID_SOCK) return;

    if (Clock::now() - s.last_activity > CONNECTION_TIMEOUT) {
        s.fail("Connection timed out");
        return;
    }

    s.poll_connecting_socket();
    if (s.broken) return;

    s.flush_outgoing_traffic();
    if (s.broken) return;

    s.receive_incoming_traffic();
}

void Connection::send_input(uint8_t held) {
    if (ready() && !impl->is_host) {
        impl->queue_packet(MSG_INPUT, {static_cast<uint8_t>(held & 3)});
    }
}

void Connection::send_frame(const Frame& frame) {
    if (!ready() || !impl->is_host) return;

    std::vector<uint8_t> payload(PAYLOAD_FRAME_SIZE, 0);
    payload[0] = (frame.beep ? 1 : 0) | (frame.paused ? 2 : 0);

    // Pack 2048 boolean pixels into 256 bytes (8 pixels per byte)
    for (size_t i = 0; i < frame.pixels.size(); ++i) {
        if (frame.pixels[i]) {
            payload[1 + (i / 8)] |= static_cast<uint8_t>(1 << (7 - (i % 8)));
        }
    }

    impl->queue_packet(MSG_FRAME, payload);
}

bool Connection::ready() const {
    return impl->handshaken && !impl->broken;
}

bool Connection::failed() const {
    return impl->broken;
}

const std::string& Connection::status() const {
    return impl->message;
}

uint8_t Connection::input() const {
    return impl->held;
}

bool Connection::take_frame(Frame& frame) {
    if (!impl->fresh_frame) return false;
    frame = impl->latest_frame;
    impl->fresh_frame = false;
    return true;
}

} // namespace lan