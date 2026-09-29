# Chip-8 Emulator

A Chip-8 emulator built in C++ with SDL2 graphics and audio support.

> **Note to Participants:** 
> This codebase is intentionally incomplete and contains implementation defects across opcode handling, memory management, timing control, and rendering pipeline. Please consult the **Problem Statement** document for your exact submission guidelines and evaluation criteria.

## Features

- All 35 Chip-8 opcodes implemented
- 64x32 pixel display with SDL2 rendering
- Keyboard input support
- Sound effects (beep tone)
- 60 FPS rendering

## Architecture

Chip-8 is a virtual machine from the 1970s designed to make programming video games easier on early microcomputers.

### System Specifications

- **Memory**: 4 KB (4096 bytes)
  - `0x000-0x1FF`: Reserved for interpreter and fonts
  - `0x200-0xFFF`: Program/ROM space
- **Registers**:
  - 16 8-bit general-purpose registers (V0-VF)
  - VF doubles as a flag register for arithmetic operations
  - 16-bit index register (I)
  - 16-bit program counter (PC)
  - 8-bit stack pointer (SP)
- **Display**: 64x32 pixels, monochrome
- **Timers**: 
  - Delay timer (counts down at 60 Hz)
  - Sound timer (beeps when > 0, counts down at 60 Hz)
- **Stack**: 16 levels for subroutine calls
- **Keypad**: 16-key hexadecimal input

## Dependencies

- SDL2 library
- SDL2_ttf library (menus and text)

## LAN multiplayer Pong

Run the updated emulator from its project folder on both computers. Both must
be connected to the same Wi-Fi/LAN.

1. On the first computer, choose **Host Pong (LAN)**. It waits on TCP port **24808**.
2. Find that computer's Wi-Fi IPv4 address: on Windows, run `ipconfig` and read
   the IPv4 Address under the active Wireless LAN adapter (for example, `192.168.1.20`).
3. On the second computer, choose **Join Pong (LAN)**, type that address, and press Enter.
4. The match starts when the connection is ready. Both players use **W/S**:
   the host moves the left paddle and the guest moves the right paddle.

The host uses **Space** to pause/resume and **F2** to restart. **Esc** leaves the
session on either computer. Host focus loss pauses the match; press Space after
returning. Guest focus loss releases its paddle. Save/load and speed changes
remain available only in offline games. The host needs `roms/Pong.ch8`; the
guest receives the host's display and sound status without executing a ROM.

If Windows Firewall prompts, allow the emulator on your private network.
If joining fails, check the host address, TCP port 24808, and whether the Wi-Fi
router isolates clients (common on guest networks). No port forwarding is needed.
An inactive connection times out after five seconds; return to the menu to host/join again.
For a one-computer test, run two instances and join `127.0.0.1`.

Build on Windows with MinGW and SDL2/SDL2_ttf installed in the same toolchain:
`make`. Winsock is linked automatically. `make test` runs loopback network,
ROM-streaming, and headless SDL menu/session tests (ports 24808 through 24810).
Close any hosted game before running tests.

### Installation

**Arch Linux:**
```bash
sudo pacman -S sdl2
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libsdl2-dev
```

**macOS:**
```bash
brew install sdl2
```

## Building

1. Clone the repo
```bash
git clone https://github.com/TatHack-Tathva/chip8-emulator.git
```

2. Make it
```bash
make
```

## Usage
```bash
./chip8 <path-to-rom-file>
```

**Example:**
```bash
./chip8 roms/PONG.ch8
```

## Keyboard Mapping

The original Chip-8 keypad is mapped to keyboard keys:
```
Chip-8 Keypad:          QWERTY Keyboard:
┌─┬─┬─┬─┐               ┌─┬─┬─┬─┐
│1│2│3│C│               │1│2│3│4│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│4│5│6│D│               │Q│W│E│R│
├─┼─┼─┼─┤      =        ├─┼─┼─┼─┤
│7│8│9│E│               │A│S│D│F│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│A│0│B│F│               │Z│X│C│V│
└─┴─┴─┴─┘               └─┴─┴─┴─┘
```

**Controls:**
- `ESC` - Quit emulator
- Keyboard keys as mapped above

### Game-Specific Controls

**PONG:**
- Left paddle: `1` (up), `Q` (down)
- Right paddle: `4` (up), `R` (down)

**TETRIS:**
- `Q` - Rotate
- `W` - Drop
- `E` - Move right
- `A` - Move left

## Implementation Details

### Instruction Set

The emulator implements all 35 Chip-8 instructions, including:
- **Arithmetic**: ADD, SUB, AND, OR, XOR, shift operations
- **Graphics**: Draw sprites with XOR mode, collision detection
- **Flow control**: Jump, call/return subroutines, conditional skips
- **Memory**: Load/store registers, BCD conversion
- **Timers**: Delay and sound timer operations
- **Input**: Key press detection (blocking and non-blocking)

### Display

Graphics are rendered using SDL2:
- Each Chip-8 pixel is scaled 10× for visibility (640×320 window)
- XOR-based sprite drawing for collision detection
- 60 FPS rendering

### Audio

Simple square wave generation at 440 Hz (musical note A) plays when `sound_timer > 0`.

## Resources

- [Chip-8 ROMs Archive](https://github.com/kripod/chip8-roms)
