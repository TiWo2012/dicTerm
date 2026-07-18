# dicTerm

A GPU-accelerated terminal emulator built with [raylib](https://www.raylib.com/) and C23 (C2y).

## Status

Early development – a minimal terminal that spawns a child shell, renders its
output to a raylib window, and forwards keyboard input.  ANSI SGR colour
attributes (foreground, background, bold, underline) with full 24-bit RGB and
256-colour palette support are implemented.  Font rendering with Nerd Fonts
is functional; richer terminal features are on the roadmap.

## Dependencies

- **clang** 16+ (C23 / `-std=c2y`)
- **cmake** 3.20+
- **raylib** (system-installed; tested with 5.x / 6.x)
- **TrueType/OpenType font** (monospace recommended, e.g. JetBrains Mono, Fira Code, DejaVu Sans Mono)
- **Nerd Fonts** (optional, automatically detected for icon/powerline glyphs)

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
build/test_scrollback             # direct
build/test_screen                 # screen buffer tests
build/test_ansi_colors            # ANSI colour parsing tests
ctest --test-dir build            # via CTest (all 4 suites)
```

## Usage

```bash
./build/dicTerm
```

Opens a window running `/bin/bash` inside a pseudo-terminal.  Type normally;
the shell output is rendered in the raylib window using the GPU-accelerated
font atlas.  Close the window or type `exit` to quit.

### Nerd Fonts

If a Nerd Fonts variant is installed on the system it will be detected
automatically. The terminal then has access to Nerd Font icons, powerline
symbols, and other glyphs in the Private Use Area (PUA) ranges.  You can
switch between standard and Nerd Fonts at runtime via the API.

## Project structure

```
├── CMakeLists.txt          # Build: dicTerm + 4 test executables
├── Doxyfile                # Doxygen documentation configuration
├── include/
│   ├── parser.h            # ECMA-48 escape sequence parser API
│   ├── font.h              # Font rendering subsystem (glyph atlas + Nerd Fonts)
│   ├── input.h             # Keyboard input → PTY sequence converter
│   ├── scrollback.h        # Scrollback ring buffer
│   └── screen.h            # Screen cell with SGR colour attributes
├── src/
│   ├── main.c              # Terminal emulator: PTY, screen buffer, SGR callbacks
│   ├── parser.c            # ANSI escape sequence state machine
│   ├── font.c              # TrueType font loader, glyph atlas, Nerd Fonts PUA
│   ├── input.c             # Keyboard handling
│   ├── scrollback.c        # Line-based ring buffer
│   ├── screen.c            # Screen grid buffer (per-cell fg/bg colours)
│   ├── test_parser.c       # 46 unit tests (parser, incl. colon sub-params)
│   ├── test_scrollback.c   # 9 unit tests (scrollback)
│   ├── test_screen.c       # 6 unit tests (screen buffer ops)
│   └── test_ansi_colors.c  # 32 unit tests (SGR colour parsing)
└── .opencode/
    ├── agents/             # Agent definitions
    └── package.json        # opencode configuration
```

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                     raylib window                          │
│  ┌──────────────────────────────────────────────────────┐  │
│  │      DrawTextCodepoints(font, codepoints, ...)       │  │
│  │      with glyph atlas from font subsystem            │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────┬──────────────────────────────────┘
                         │
┌────────────────────────▼──────────────────────────────────┐
│                       main.c                              │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │  PTY master  │  │  screen[][] │  │  parser_t    │     │
│  │  (forkpty)   │──│  + cx,cy    │──│  (callbacks) │     │
│  └──────┬──────┘  └──────┬───────┘  └──────┬───────┘     │
│         │                │                 │             │
│         │        ┌───────▼────────┐        │             │
│         │        │  font_handle_t │        │             │
│         │        │  (glyph cache, │        │             │
│         │        │   Font atlas,  │        │             │
│         │        │   Nerd switch) │        │             │
│         │        └───────▲────────┘        │             │
│         │  keyboard      │                 │ parser_feed │
│         │  input         │                 │             │
└─────────┼────────────────┼─────────────────┼─────────────┘
          │                │                 │
          ▼                │                 ▼
     ┌──────────┐          │          ┌──────────────┐
     │  bash    │          │          │  parser.c    │
     │  (child) │          │          │  state machine│
     └──────────┘          │          └──────────────┘
                           ▼
                    ┌──────────────┐
                    │  font.c      │
                    │  LoadFontEx  │
                    │  DrawText-   │
                    │  Codepoints  │
                    │  Nerd PUA    │
                    └──────────────┘
```

## What's implemented

### Font rendering (`font.c` / `font.h`)
- TrueType/OpenType font loading via `LoadFontEx` with codepoint range
- Glyph atlas caching with raylib's built-in texture management
- Nerd Fonts auto-detection from common system paths (TTF/OTF subdirectories)
- PUA codepoint ranges for Nerd Font icons (powerline, devicons, Font Awesome, Material Design, misc symbols)
- Hot-swappable font selection: `font_select(handle, "nerd")` / `"regular"`
- `DrawTextCodepoints` for fixed-width terminal rendering
- Glyph index cache for O(1) codepoint → glyph lookups
- Sub-pixel positioning with proper ascent/descent metrics
- Font discovery: automatic search for monospace + Nerd Fonts on the system

### ANSI escape sequence parser (`parser.c`)
- Full ECMA-48 state machine: GROUND, ESC, CSI, OSC, DCS, SOS/PM/APC
- Callback-based API with zero-allocation design
- 7-bit and 8-bit control code support
- Multi-digit parameters, private markers, intermediate bytes
- String termination via BEL, 7-bit ST (ESC `\`), and 8-bit ST (0x9C)
- 45 unit tests with 100% pass rate

### Terminal emulation (`main.c`)
- ForkPTY child process (bash)
- Screen buffer (36 × 100 cells) with per-cell SGR colour attributes
- **C0 controls**: LF, CR, BS, HT, VT, FF
- **CSI cursor movement**: CUU, CUD, CUF, CUB, CUP, HVP
- **CSI erase**: ED (clear display), EL (clear line)
- **CSI save/restore cursor**: `s` / `u`
- **CSI SGR colours**: `m` with full parameter parsing
  - Standard colours: 30–37 (fg), 40–47 (bg)
  - Bright colours: 90–97 (fg), 100–107 (bg)
  - 256-colour palette: 38;5;N (fg), 48;5;N (bg), 58;5;N (underline)
  - 24-bit truecolor: 38;2;R;G;B (fg), 48;2;R;G;B (bg), 58;2;R;G;B (underline)
  - Colon-separated sub-parameters: 38:5:N / 38:2:R:G:B
  - Default resets: 39 (fg), 49 (bg)
  - Attributes: 0 (reset), 1 (bold), 4 (underline), 22/24 (off)
- **ESC sequences**: IND, RI, NEL, DECSC, DECRC, RIS
- Keyboard input via `input.c` with modifier support (Ctrl, Alt, Shift)
- Cursor drawn as inverted block
- Window resizing with PTY resize notification
- Non-blocking PTY I/O
- Per-cell rendering: each character drawn individually with its own fg/bg colour, background fills, pseudo-bold, and underline

### Scrollback buffer (`scrollback.c` / `scrollback.h`)
- Fixed-capacity ring buffer (default 1000 lines)
- Push, indexed lookup, clear, reset operations
- 9 unit tests with 100% pass rate

## Roadmap

- [x] Font rendering (glyph atlas, TrueType via raylib, Nerd Fonts support)
- [x] SGR colour attributes (standard/bright/extended foreground, background, bold, underline)
- [x] 256-colour palette and 24-bit truecolor RGB support
- [ ] Scrollback buffer integration with viewport scrolling
- [ ] Mouse support
- [ ] Clipboard integration
- [ ] Configuration file
