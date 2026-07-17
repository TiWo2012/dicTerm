# dicTerm

A GPU-accelerated terminal emulator built with [raylib](https://www.raylib.com/) and C23 (C2y).

## Status

Early development – a minimal terminal that spawns a child shell, renders its
output to a raylib window, and forwards keyboard input.  ANSI escape sequence
parsing is functional; font rendering and rich terminal features are on the
roadmap.

## Dependencies

- **clang** 16+ (C23 / `-std=c2y`)
- **cmake** 3.20+
- **raylib** (system-installed; tested with 5.x)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The resulting binary is `build/dicTerm`.

### Build modes

| Mode | Tests auto-run? | Use case |
|------|----------------|----------|
| `Debug`   | Yes – tests run during `cmake --build` and block on failure | Development |
| `Release` | No – tests compile but are not executed | Packaging |

Tests can always be run manually or via CTest regardless of build type:

```bash
build/test_parser                 # direct
ctest --test-dir build            # via CTest
```

## Usage

```bash
./build/dicTerm
```

Opens a window running `/bin/bash` inside a pseudo-terminal.  Type normally;
the shell output is rendered in the raylib window.  Close the window or type
`exit` to quit.

## Project structure

```
├── CMakeLists.txt          # Build: dicTerm + test_parser
├── include/
│   └── parser.h            # ECMA-48 escape sequence parser API
├── src/
│   ├── main.c              # Terminal emulator: PTY, screen buffer, callbacks
│   ├── parser.c            # ANSI escape sequence state machine
│   └── test_parser.c       # 45 unit tests
└── .opencode/
    ├── agents/             # Agent definitions
    └── package.json        # opencode configuration
```

## Architecture

```
┌───────────────────────────────────────────────────────┐
│                    raylib window                      │
│  ┌─────────────────────────────────────────────────┐  │
│  │              DrawText(screen[r])                │  │
│  └─────────────────────────────────────────────────┘  │
└───────────────────────┬───────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────┐
│                   main.c                              │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  PTY master  │  │  screen[][] │  │  parser_t    │  │
│  │  (forkpty)   │──│  + cx,cy    │──│  (callbacks) │  │
│  └──────┬──────┘  └──────────────┘  └──────┬───────┘  │
│         │  keyboard input                  │          │
└─────────┼──────────────────────────────────┼──────────┘
          │  PTY output                      │ parser_feed
          ▼                                  ▼
     ┌──────────┐                    ┌──────────────┐
     │  bash    │                    │  parser.c    │
     │  (child) │                    │  state machine│
     └──────────┘                    └──────────────┘
```

## What's implemented

### ANSI escape sequence parser (`parser.c`)
- Full ECMA-48 state machine: GROUND, ESC, CSI, OSC, DCS, SOS/PM/APC
- Callback-based API with zero-allocation design
- 7-bit and 8-bit control code support
- Multi-digit parameters, private markers, intermediate bytes
- String termination via BEL, 7-bit ST (ESC `\`), and 8-bit ST (0x9C)
- 45 unit tests with 100% pass rate

### Terminal emulation (`main.c`)
- ForkPTY child process (bash)
- Screen buffer (36 × 100 chars) with scrolling
- **C0 controls**: LF, CR, BS, HT
- **CSI cursor movement**: CUU, CUD, CUF, CUB, CUP, HVP
- **CSI erase**: ED (clear display), EL (clear line)
- **CSI SGR** (select graphic rendition) – parsed, attributes not yet rendered
- **CSI save/restore cursor**: `s` / `u`
- **ESC sequences**: IND, RI, NEL, DECSC, DECRC, RIS

## Roadmap

- [ ] Font rendering (glyph atlas, TrueType via raylib)
- [ ] SGR attributes (bold, italic, colors)
- [ ] Scrollback buffer
- [ ] Mouse support
- [ ] Clipboard integration
- [ ] Configuration file
