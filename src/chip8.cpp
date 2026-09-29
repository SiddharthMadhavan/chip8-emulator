#include "chip8.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <cstring>
#include <random>

uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

uint8_t schip_fontset[160] = {
    0x3C, 0x7E, 0xE7, 0xC3, 0xC3, 0xC3, 0xC3, 0xE7, 0x7E, 0x3C, // 0
    0x18, 0x38, 0x58, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, // 1
    0x3E, 0x7F, 0xC3, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFF, 0xFF, // 2
    0x3C, 0x7E, 0x06, 0x06, 0x1E, 0x1E, 0x06, 0x06, 0x7E, 0x3C, // 3
    0x06, 0x0E, 0x1E, 0x36, 0x66, 0xC6, 0xFF, 0xFF, 0x06, 0x06, // 4
    0xFF, 0xFF, 0xC0, 0xC0, 0xFC, 0xFE, 0x03, 0x03, 0x7E, 0x3C, // 5
    0x3E, 0x7C, 0xC0, 0xC0, 0xFC, 0xFE, 0xC3, 0xC3, 0x7E, 0x3C, // 6
    0xFF, 0xFF, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x60, // 7
    0x3C, 0x7E, 0xC3, 0xC3, 0x7E, 0x7E, 0xC3, 0xC3, 0x7E, 0x3C, // 8
    0x3C, 0x7E, 0xC3, 0xC3, 0x7F, 0x3F, 0x03, 0x03, 0x3E, 0x7C, // 9
    0x3C, 0x7E, 0xC3, 0xC3, 0xFF, 0xFF, 0xC3, 0xC3, 0xC3, 0xC3, // A
    0xFC, 0xFE, 0xC3, 0xC3, 0xFC, 0xFE, 0xC3, 0xC3, 0xFE, 0xFC, // B
    0x3E, 0x7F, 0xC1, 0xC0, 0xC0, 0xC0, 0xC1, 0x7F, 0x3E, 0x00, // C
    0xFC, 0xFE, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xFE, 0xFC, // D
    0xFF, 0xFF, 0xC0, 0xC0, 0xFC, 0xFC, 0xC0, 0xC0, 0xFF, 0xFF, // E
    0xFF, 0xFF, 0xC0, 0xC0, 0xFC, 0xFC, 0xC0, 0xC0, 0xC0, 0xC0  // F
};

void write_u16(std::ofstream& file, uint16_t value) {
    uint8_t bytes[2] = {
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF)
    };

    file.write(reinterpret_cast<const char*>(bytes), 2);
}

void read_u16(std::ifstream& file, uint16_t& value) {
    uint8_t bytes[2];

    file.read(reinterpret_cast<char*>(bytes), 2);

    value =
        (static_cast<uint16_t>(bytes[0]) << 8) |
        bytes[1];
}

Chip8::Chip8(){
    initialise();
}


void Chip8::initialise(){
    pc = 0x200;
    opcode = 0;
    index = 0;
    sp = 0;

    memset(display, 0, sizeof(display));
    memset(stack, 0, sizeof(stack));
    memset(v, 0, sizeof(v));
    memset(memory, 0, sizeof(memory));
    memset(key, 0, sizeof(key));

    load_fonts();
    delay_timer = 0;
    sound_timer = 0;
    draw_flag = false;
    high_res = false;
    memset(rpl, 0, sizeof(rpl));
}

void Chip8::load_fonts(){
    for(int i=0; i<80; i++) memory[i + 0x50] = chip8_fontset[i]; // Note : not starting from 0
    for(int i=0; i<160; i++) memory[i + 0xA0] = schip_fontset[i];
}

void Chip8::load_rom(const std::string& filename){
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if(!file.is_open()){
        std::cerr << "Failed to open ROM: " << filename << std::endl;
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if(size > (4096-512)){ // 512 reserved for fonts/interpreter
        std::cerr << "ROM too large to fit in memory" << std::endl;
        return;
    }

    file.read((char*)(memory+512),size);
    file.close();

    std::cout << "Loaded ROM: " << filename << std::endl;
}

bool Chip8::save_state(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);

    if (!file)
        return false;

    file.write(
        reinterpret_cast<const char*>(memory),
        sizeof(memory)
    );

    file.write(
        reinterpret_cast<const char*>(v),
        sizeof(v)
    );

    write_u16(file, index);
    write_u16(file, pc);

    for (int i = 0; i < 16; i++)
        write_u16(file, stack[i]);

    file.put(static_cast<char>(sp));
    file.put(static_cast<char>(delay_timer));
    file.put(static_cast<char>(sound_timer));
    file.put(static_cast<char>(high_res));

    file.write(reinterpret_cast<const char*>(rpl), sizeof(rpl));

    file.write(
        reinterpret_cast<const char*>(display), // Note
        sizeof(display)
    );

    return true;
}

bool Chip8::load_state(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);

    if (!file)
        return false;

    file.read(
        reinterpret_cast<char*>(memory),
        sizeof(memory)
    );

    file.read(
        reinterpret_cast<char*>(v),
        sizeof(v)
    );

    read_u16(file, index);
    read_u16(file, pc);

    for (int i = 0; i < 16; i++)
        read_u16(file, stack[i]);

    sp = static_cast<uint8_t>(file.get());
    delay_timer = static_cast<uint8_t>(file.get());
    sound_timer = static_cast<uint8_t>(file.get());

    file.read(
        reinterpret_cast<char*>(display),
        sizeof(display)
    );

    std::memset(key, 0, sizeof(key));

    opcode = 0;
    draw_flag = true;

    return true;
}


void Chip8::emulate_cycle(){
    opcode = memory[pc] << 8 | memory[pc+1]; // 16-bit instruction

    switch(opcode & 0xF000){ // Gets only the first 4 bits
        case 0x0000:
            switch(opcode & 0x00FF){
                case 0x00E0: // Clears the display
                    std::memset(display, 0, sizeof(display));
                    draw_flag = true;
                    pc += 2;
                    break;
                case 0x00EE: // Returns from subroutine
                    if(sp == 0){
                        std::cerr << "Underflow!" << std::endl;
                        break;
                    }
                    pc = stack[--sp];
                    pc += 2;
                    break;
                case 0x00FB: { // Scroll right 4 pixels
                    int width = high_res ? 128 : 64;
                    int height = high_res ? 64 : 32;
                    for (int y = 0; y < height; y++) {
                        for (int x = width - 1; x >= 4; x--) display[y * width + x] = display[y * width + x - 4];
                        for (int x = 0; x < 4; x++) display[y * width + x] = 0;
                    }
                    draw_flag = true;
                    pc += 2;
                    break;
                }
                case 0x00FC: { // Scroll left 4 pixels
                    int width = high_res ? 128 : 64;
                    int height = high_res ? 64 : 32;
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width - 4; x++) display[y * width + x] = display[y * width + x + 4];
                        for (int x = width - 4; x < width; x++) display[y * width + x] = 0;
                    }
                    draw_flag = true;
                    pc += 2;
                    break;
                }
                case 0x00FD: // Exit interpreter
                    std::exit(0);
                    break;

                case 0x00FE: // Disable high-res
                    high_res = false;
                    std::memset(display, 0, sizeof(display));
                    draw_flag = true;
                    pc += 2;
                    break;

                case 0x00FF: // Enable high-res
                    high_res = true;
                    std::memset(display, 0, sizeof(display));
                    draw_flag = true;
                    pc += 2;
                    break;

                default:
                    if ((opcode & 0x00F0) == 0x00C0) { // Scroll down N lines (don't know what this does)
                        int n = opcode & 0x000F;
                        int width = high_res ? 128 : 64;
                        int height = high_res ? 64 : 32;
                        for (int y = height - 1; y >= n; y--) {
                            for (int x = 0; x < width; x++) display[y * width + x] = display[(y - n) * width + x];
                        }
                        for (int y = 0; y < n; y++) {
                            for (int x = 0; x < width; x++) display[y * width + x] = 0;
                        }
                        draw_flag = true;
                        pc += 2;
                    } 
                    else {
                        std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                        pc += 2;
                    }
            }
            break;
        case 0x1000: // 1XXX = Jump to address XXX
            pc = opcode & 0x0FFF;
            break;
        case 0x2000: // 2XXX = Call subroutine at XXX
            if(sp >= 16){
                std::cerr << "Overflow!" << std::endl;
                sp = 0;
                break;
            }
            stack[sp] = pc;
            sp++;
            pc = opcode & 0x0FFF;
            break;
        case 0x3000: // 3XNN = Skip next instruction if v[x] = NN
            if(v[(opcode & 0x0F00) >> 8] == (opcode & 0x00FF)) pc += 4;
            else pc += 2;
            break;
        case 0x4000:  // 4XNN - Skip next instruction if v[x] != NN
            if (v[(opcode & 0x0F00) >> 8] != (opcode & 0x00FF)) pc += 4;
            else pc += 2;
            break;
        case 0x5000:  // 5XY0 - Skip next instruction if v[x] == v[y]
            if (v[(opcode & 0x0F00) >> 8] == v[(opcode & 0x00F0) >> 4]) pc += 4;
            else pc += 2;
            break;
        case 0x6000: // 6XNN = set v[n] = NN
            v[(opcode & 0x0F00) >> 8] = opcode & 0x00FF;
            pc += 2;
            break;
        case 0x7000: //7XNN = add NN to v[x]
            v[(opcode & 0x0F00) >> 8] += opcode & 0x00FF;
            pc += 2;
            break;
        case 0x8000: // Arithmetic operations
            {
                uint8_t x = (opcode & 0x0F00) >> 8;
                uint8_t y = (opcode & 0x00F0) >> 4;

                switch(opcode & 0x000F){ // Look only at last 4 bits
                    case 0x0000: // 8XY0: v[x] = v[y]
                        v[x] = v[y];
                        pc += 2;
                        break;

                    case 0x0001: // 8XY1: v[x] |= v[y]
                        v[x] |= v[y];
                        pc += 2;
                        break;

                    case 0x0002: // 8XY2: v[x] &= v[y]
                        v[x] &= v[y];
                        pc += 2;
                        break;

                    case 0x0003: // 8XY3: v[x] ^= v[y]
                        v[x] ^= v[y];
                        pc += 2;
                        break;

                    case 0x0004: { // 8XY4: v[x] += v[y], v[F] = carry
                        uint16_t sum = v[x] + v[y];
                        uint8_t carry = (sum > 0xFF) ? 1 : 0;
                        v[x] = sum & 0xFF;  // Write result first
                        v[0xF] = carry;     // Set flag last
                        pc += 2;
                        break;
                    }

                    case 0x0005: { // 8XY5: v[x] -= v[y], v[F] = NOT(borrow)
                        uint8_t not_borrow = (v[x] >= v[y]) ? 1 : 0;
                        v[x] -= v[y];       // Write result first
                        v[0xF] = not_borrow;// Set flag last
                        pc += 2;
                        break;
                    }

                    case 0x0006: { // 8XY6: v[x] >>= 1, v[F] = LSB
                        uint8_t lsb = v[x] & 0x1;
                        v[x] >>= 1;         // Shift first
                        v[0xF] = lsb;       // Set flag last
                        pc += 2;
                        break;
                    }

                    case 0x0007: { // 8XY7: v[x] = v[y] - v[x], v[F] = NOT(borrow)
                        uint8_t not_borrow = (v[y] >= v[x]) ? 1 : 0;
                        v[x] = v[y] - v[x]; // Write result first
                        v[0xF] = not_borrow;// Set flag last
                        pc += 2;
                        break;
                    }

                    case 0x000E: { // 8XYE: v[x] <<= 1, v[F] = MSB
                        uint8_t msb = (v[x] >> 7) & 0x1;
                        v[x] <<= 1;         // Shift first
                        v[0xF] = msb;       // Set flag last
                        pc += 2;
                        break;
                    }

                    default:
                        std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                        pc += 2;
                        break;
                }
            }
            break;
        case 0x9000: // 9XY0 = Skip next instr. if v[x] != v[y]
            if (v[(opcode & 0x0F00) >> 8] != v[(opcode & 0x00F0) >> 4])
                pc += 4;
            else
                pc += 2;
            break;
        case 0xA000: // AXXX = set index to XXX
            index = opcode & 0x0FFF;
            pc += 2;
            break;
        case 0xB000: // BXXX = jump to address XXX + v[0]
            pc = (opcode & 0xFFF) + v[0];
            break;
        case 0xC000:{  // CXNN - v[x] = random_byte & NN
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dist(0, 255);
            v[(opcode & 0x0F00) >> 8] = dist(gen) & (opcode & 0x00FF);
            pc += 2;
        }
            break;
        case 0xD000: { 
            uint8_t x = v[(opcode & 0x0F00) >> 8], y = v[(opcode & 0x00F0) >> 4];
            uint8_t height = opcode & 0x000F;
            uint8_t width_bytes = (height == 0) ? 2 : 1; 
            if (height == 0) height = 16; // N=0 triggers 16x16 mode
            
            int screen_width = high_res ? 128 : 64;
            int screen_height = high_res ? 64 : 32;

            v[0xF] = 0; 
            for(int y_line = 0; y_line < height; y_line++) {
                for(int byte = 0; byte < width_bytes; byte++) {
                    uint8_t pixel = memory[index + (y_line * width_bytes) + byte];
                    
                    for(int x_line = 0; x_line < 8; x_line++) {
                        if((pixel & (0x80 >> x_line)) != 0) {
                            int screen_x = (x & (screen_width - 1)) + (byte * 8) + x_line;
                            int screen_y = (y & (screen_height - 1)) + y_line;
                            if (screen_x >= screen_width || screen_y >= screen_height) continue;
                            
                            int screen_index = screen_x + (screen_y * screen_width);
                            if(display[screen_index] == 1) v[0xF] = 1; 
                            display[screen_index] ^= 1;
                        }
                    }
                }
            }
            draw_flag = true;
            pc += 2;
        }
        break;
        case 0xE000: 
            switch(opcode & 0x00FF){
                case 0x009E: // EX9E = skip next instr. if key[v[x]] is pressed
                    if(key[v[(opcode & 0x0F00) >> 8]] != 0) pc += 4;
                    else pc += 2;
                    break;
                case 0x00A1: // EXA1 = skip next instr. if key[v[x]] is not pressed
                    if(key[v[(opcode & 0x0F00) >> 8]] == 0) pc += 4;
                    else pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        case 0xF000: // Timers and memory operations
            switch(opcode & 0x00FF){
                case 0x0007: // FX07 - v[x] = delay_timer
                    v[(opcode & 0x0F00) >> 8] = delay_timer;
                    pc += 2;
                    break;
                case 0x000A:{ // FX0A - wait for key press, store in v[x]
                    bool key_pressed = false;
                    for(int i=0; i<16; i++){
                        if(key[i] != 0){
                            v[(opcode & 0x0F00) >> 8] = i;
                            key_pressed = true;
                            break;
                        }
                    }
                    if(!key_pressed)
                        break;

                    pc += 2;
                }
                    break;
                case 0x0015: // FX15 - delay_timer = v[x]
                    delay_timer = v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x0018: // FX18 - sound_timer = v[x]
                    sound_timer = v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x001E: // FX1E - index += v[x]
                    index += v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x0029: // FX29 - index = location of sprite for digit v[x]
                    index = (v[(opcode & 0x0F00) >> 8] * 5) + 0x50;
                    pc += 2;
                    break;
                case 0x0030: // FX30 - point I to 10-byte font sprite
                    index = (v[(opcode & 0x0F00) >> 8] * 10) + 0xA0;
                    pc += 2;
                    break;
                case 0x0033:{ // FX33 - store BCD representation of v[x] at index
                    uint8_t value = v[(opcode & 0x0F00) >> 8];
                    memory[index] = value/100;
                    memory[index+1] = (value/10)%10;
                    memory[index+2] = value%10;
                    pc += 2;
                }
                    break;
                case 0x0055: // FX55 - store v[0] to v[x] in memory starting from index
                    for(int i=0; i<=((opcode & 0x0F00) >> 8); i++){
                        memory[index+i] = v[i];
                    }
                    pc += 2;
                    break;
                case 0x0065: // FX65 - Fill v[0] to v[x] from memory starting at index
                    for(int i=0; i<=((opcode & 0x0F00) >> 8); i++){
                        v[i] = memory[index+i];
                    }
                    pc += 2;
                    break;
                case 0x0075: // FX75 - Store V0..VX in RPL flags
                    for(int i = 0; i <= ((opcode & 0x0F00) >> 8) && i < 8; i++) {
                        rpl[i] = v[i];
                    }
                    pc += 2;
                    break;
                case 0x0085: // FX85 - Read V0..VX from RPL flags
                    for(int i = 0; i <= ((opcode & 0x0F00) >> 8) && i < 8; i++) {
                        v[i] = rpl[i];
                    }
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        default:
            std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
            pc += 2;
            break;
    }
}

void Chip8::update_timers() {
    if (delay_timer > 0) delay_timer--;
    if (sound_timer > 0) {
        if (sound_timer == 1) std::cout << "BEEP!\n";
        sound_timer--;
    }
}