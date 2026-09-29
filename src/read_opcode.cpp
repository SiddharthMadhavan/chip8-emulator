#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstdint>

int main() {
    std::ifstream file("../roms/Pong.ch8", std::ios::binary);

    if (!file) {
        std::cerr << "Error: Could not open Pong.ch8\n";
        return 1;
    }

    uint8_t highByte, lowByte;

    while (file.read(reinterpret_cast<char*>(&highByte), 1) &&
           file.read(reinterpret_cast<char*>(&lowByte), 1)) {

        uint16_t opcode =
            (static_cast<uint16_t>(highByte) << 8) | lowByte;

        std::cout << "0x"
                  << std::uppercase
                  << std::hex
                  << std::setw(4)
                  << std::setfill('0')
                  << opcode
                  << '\n';
    }

    return 0;
}
