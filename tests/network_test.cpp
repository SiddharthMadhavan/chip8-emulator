#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "network.h"
#include "chip8.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class Predicate>
void until(lan::Connection& host, lan::Connection& guest, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        host.poll(); guest.poll();
        check(!host.failed() && !guest.failed(), "connection remains healthy");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(done(), "operation completes within two seconds");
}
int main() {
    constexpr uint16_t test_port = 24809;
    lan::Connection host;
    auto guest = std::make_unique<lan::Connection>();
    check(host.host(test_port), "listen");
    lan::Connection duplicate;
    check(!duplicate.host(test_port), "duplicate listener rejected");
    check(guest->join("127.0.0.1", test_port), "connect");
    until(host, *guest, [&] { return host.ready() && guest->ready(); });
    for (uint8_t held : {1, 2, 3, 0}) {
        guest->send_input(held);
        until(host, *guest, [&] { return host.input() == held; });
    }
    lan::Frame frame, received;
    for (size_t i = 0; i < frame.pixels.size(); ++i) frame.pixels[i] = (i % 7) < 3;
    frame.beep = true; frame.paused = true;
    host.send_frame(frame);
    bool got = false;
    until(host, *guest, [&] { if (!got) got = guest->take_frame(received); return got; });
    check(received.pixels == frame.pixels && received.beep && received.paused, "packed frame round trip");

    // Run the actual ROM on the host and verify the guest receives its display.
    Chip8 game;
    game.load_rom("roms/Pong.ch8");
    for (int tick = 0; tick < 180; ++tick) {
        guest->send_input(tick % 2 ? 1 : 2);
        host.poll(); guest->poll(); host.poll();
        game.key[1] = tick % 2;
        game.key[0xC] = (host.input() & 1) != 0;
        game.key[0xD] = (host.input() & 2) != 0;
        for (int i = 0; i < 10; ++i) game.emulate_cycle();
        game.update_timers();
        std::copy_n(game.display, frame.pixels.size(), frame.pixels.begin());
        frame.beep = false; frame.paused = false;
        host.send_frame(frame);
        got = false;
        until(host, *guest, [&] { if (!got) got = guest->take_frame(received); return got; });
        check(received.pixels == frame.pixels && !received.paused && !received.beep, "live Pong frame matches");
    }
    check(std::any_of(frame.pixels.begin(), frame.pixels.end(), [](uint8_t p) { return p != 0; }), "ROM draws game");
    guest.reset();
    for (int i = 0; i < 100 && !host.failed(); ++i) {
        host.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(host.failed() && host.input() == 0, "disconnect clears input");
    lan::Connection bad;
    check(!bad.join("not-an-ip") && bad.failed(), "invalid address rejected");
    lan::Connection refused;
    refused.join("127.0.0.1", test_port);
    const auto refusal_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!refused.failed() && std::chrono::steady_clock::now() < refusal_deadline) {
        refused.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(refused.failed(), "absent host fails without blocking");

    lan::Connection idle_host, idle_guest;
    check(idle_host.host(test_port), "host again after disconnect");
    check(idle_guest.join("127.0.0.1", test_port), "idle guest connects");
    until(idle_host, idle_guest, [&] { return idle_host.ready() && idle_guest.ready(); });
    const auto idle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!idle_host.failed() && std::chrono::steady_clock::now() < idle_deadline) {
        idle_host.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(idle_host.failed() && idle_host.input() == 0, "silent peer times out and releases input");

    // A raw peer checks stream fragmentation and protocol rejection.
    lan::Connection raw_host;
    check(raw_host.host(test_port + 1), "raw listener");
    auto raw = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET; addr.sin_port = htons(test_port + 1);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    check(connect(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "raw connect");
    const char hello[] = {'C', '8', 'L', 'N', 1, 2, 0, 0};
    for (int i = 0; i < 8; ++i) {
        check(send(raw, hello + i, 1, 0) == 1, "fragment sent");
        raw_host.poll();
        if (i < 7) check(!raw_host.ready(), "partial hello not accepted early");
    }
    for (int i = 0; i < 100 && !raw_host.ready(); ++i) {
        raw_host.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(raw_host.ready(), "fragmented hello accepted");
    const char malformed[] = {'C', '8', 'L', 'N', 1, 3, char(255), char(255)};
    check(send(raw, malformed, sizeof(malformed), 0) == sizeof(malformed), "bad header sent");
    for (int i = 0; i < 100 && !raw_host.failed(); ++i) {
        raw_host.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(raw_host.failed(), "oversized packet rejected");
#ifdef _WIN32
    closesocket(raw);
#else
    close(raw);
#endif
    std::cout << "PASS: handshake, input, frames, live ROM, disconnect, refusal, timeout, invalid address, fragmentation, malformed packet\n";
}
