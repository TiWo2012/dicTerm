# dicTerm

A GPU-accelerated terminal emulator built with [raylib](https://www.raylib.com/) and C23 (C2y).

## Status

Early development – a minimal terminal that spawns a child shell, renders its
output to a raylib window, and forwards keyboard input.  ANSI SGR colour
attributes (foreground, background, bold, underline) with full 24-bit RGB and
256-colour palette support are implemented.  Multi-font fallback rendering
with automatic discovery of Maple Mono, Nerd Fonts, and symbol fallback fonts
is functional.  Scrollback history viewing with viewport navigation is
implemented.  Mouse reporting (xterm tracking modes with legacy and SGR
encodings) is implemented.  Richer terminal features are on the roadmap.

## Dependencies

- **clang** 16+ (C23 / `-std=c2y`)
- **cmake** 3.20+
- **raylib** (system-installed; tested with 5.x / 6.x)
- **TrueType/OpenType font** (monospace recommended, e.g. Maple Mono,
  JetBrains Mono, Fira Code, DejaVu Sans Mono)
- **Nerd Fonts** (optional, automatically detected for icon/powerline glyphs)
- **Noto Sans Symbols 2** (optional, automatically detected for symbol glyphs
  not covered by the primary or Nerd font)

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
build/test_mouse                  # mouse encoding tests
ctest --test-dir build            # via CTest (all 5 suites)
build/test_parser              # 59 unit tests (parser state machine)
build/test_config              # 7 unit tests (config INI parser)
build/test_scrollback          # 18 unit tests (scrollback ring buffer)
build/test_screen              # 20 unit tests (screen buffer, SGR ops)
build/test_ansi_colors         # 32 unit tests (ANSI 256-colour parsing)
build/test_integration         # 22 integration tests (end-to-end sequences)
build/test_terminal            # 41 terminal doc tests (cursor, erase, scroll)
build/test_input               # 32 unit tests (key_to_seq, keyboard handling)
build/test_font_codepoints     # 10 unit tests (codepoint list builder)

ctest --test-dir build         # via CTest (all 9 suites, 241 tests total)
```

## Usage

```bash
./build/dicTerm
```

Opens a window running `/bin/bash` inside a pseudo-terminal.  Type normally;
the shell output is rendered in the raylib window using the GPU-accelerated
font atlas.  Close the window or type `exit` to quit.

The window title shows which fonts were loaded, e.g.:
`dicTerm [MapleMono-Regular.ttf] nerd=AtkynsonMonoNerdFontMono-Regular.otf sym=NotoSansSymbols2-Regular.ttf`

### Scrollback (history) navigation

The terminal stores lines that scroll off the top of the screen in a ring
buffer (default 1000 lines).  You can navigate the history with:

| Key | Action |
|-----|--------|
| `Page Up`   | Scroll up one page (screen height) |
| `Page Down` | Scroll down one page |
| `Shift` + `↑` | Scroll up one line |
| `Shift` + `↓` | Scroll down one line |

You can also scroll the history with the **mouse wheel** whenever the
running application has not enabled mouse reporting.

While browsing history, a "`-- Scrollback (offset/count) --`" indicator is
shown at the bottom of the window.  Press any regular key (letter, Enter,
Ctrl+C, etc.) to return to the normal terminal view.  The viewport
automatically stays stable when new output arrives from the shell.

### Configuration

dicTerm reads a Unix-style `.conf` file at startup.  The search order (first existing wins) is:

1. `$XDG_CONFIG_HOME/dicTerm/dicTerm.conf`
2. `~/.config/dicTerm/dicTerm.conf`
3. `/etc/dicTerm.conf`
4. Built-in defaults (applied when no file is found)

All settings are **optional** — any key you omit falls back to its default.
A commented example is shipped in `.config/dicTerm.conf.example`.

#### Install or edit the configuration file

You have several options:

**1. Copy the example to your config directory**
```bash
mkdir -p ~/.config/dicTerm
cp .config/dicTerm.conf.example ~/.config/dicTerm/dicTerm.conf
$EDITOR ~/.config/dicTerm/dicTerm.conf
```

**2. Use the installation script**
```bash
# Execute this from the repository root
./.config/install.sh
```

**3. Place a custom config system-wide**
```bash
cp .config/dicTerm.conf.example /etc/dicTerm.conf
```

**4. Edit locally within the repo** (for development)
```bash
cp .config/dicTerm.conf.example .config/dicTerm.conf
$EDITOR .config/dicTerm.conf
```

Edit the configuration file with your desired values.  Refer to the format section below and `config.h` for the available options.

**Format**

```conf
rows         = 36          # visible terminal rows
cols         = 100         # visible terminal columns
scrollback   = 1000       # scrollback ring-buffer line count
font_size    = 20.0        # font point size in pixels (pixels)

window_width  = 1280       # initial window width (pixels)
window_height = 800        # initial window height (pixels)
window_padding = 10        # padding around the terminal text area

font_regular  =            # path to regular font (blank = auto-discover)
font_nerd      =            # path to Nerd Font (blank = auto-discover)
font_symbols   =            # path to symbol fallback font (blank = auto-discover)

foreground = 220,220,220   # default text colour as "R,G,B" (0–255)
background = 0,0,0         # default background colour
```

Lines beginning with `#` or `;` are comments.  Unknown keys are silently ignored.
Out-of-range or non-positive numeric values are rejected and the default is kept.
Malformed `R,G,B` colour tuples fall back to the default.

#### Install or edit the configuration file

You have several options:

**1. Copy the example to your config directory**
```bash
mkdir -p ~/.config/dicTerm
cp dicTerm.conf.example ~/.config/dicTerm/dicTerm.conf
$EDITOR ~/.config/dicTerm/dicTerm.conf
```

**2. Use the installation script**
```bash
# Execute this from the repository root
./.config/install.sh
```

**3. Place a custom config system-wide**
```bash
cp dicTerm.conf.example /etc/dicTerm.conf
```

**4. Edit locally within the repo** (for development)
```bash
cp .config/dicTerm.conf.example .config/dicTerm.conf
$EDITOR .config/dicTerm.conf
```

Edit the configuration file with your desired values.  Refer to the format section below and `config.h` for the available options.

### Mouse support

When an application enables mouse tracking, pointer events are forwarded to
it so you can click, drag and scroll inside programs such as `vim`, `tmux`,
`htop` and `less`.  The following xterm DEC private modes are recognised:

| Mode | Meaning |
|------|---------|
| `?9`    | X10 compatibility – button press only |
| `?1000` | Normal tracking – button press and release |
| `?1002` | Button-event tracking – adds motion while a button is held |
| `?1003` | Any-event tracking – reports all pointer motion |
| `?1006` | SGR extended coordinate encoding |

Both the legacy X10 encoding (`CSI M Cb Cx Cy`) and the SGR encoding
(`CSI < Cb ; Cx ; Cy M`/`m`) are supported, including Shift/Alt/Ctrl
modifier bits and mouse-wheel reporting.  When no application has enabled
tracking, the wheel scrolls the local scrollback viewport instead.

### Nerd Fonts
**Format**

```conf
rows         = 36          # visible terminal rows
cols         = 100         # visible terminal columns
scrollback   = 1000       # scrollback ring-buffer line count
font_size    = 20.0        # font point size in pixels (pixels)

window_width  = 1280       # initial window width (pixels)
window_height = 800        # initial window height (pixels)
window_padding = 10        # padding around the terminal text area

font_regular  =            # path to regular font (blank = auto-discover)
font_nerd      =            # path to Nerd Font (blank = auto-discover)
font_symbols   =            # path to symbol fallback font (blank = auto-discover)

foreground = 220,220,220   # default text colour as "R,G,B" (0–255)
background = 0,0,0         # default background colour
```

Lines beginning with `#` or `;` are comments.  Unknown keys are silently ignored.
Out-of-range or non-positive numeric values are rejected and the default is kept.
Malformed `R,G,B` colour tuples fall back to the default.

window_width  = 1280   # initial window width (pixels)
window_height = 800    # initial window height (pixels)
window_padding = 10    # padding around the terminal text area

font_regular  =       # path to regular font (blank = auto-discover)
font_nerd      =       # path to Nerd Font (blank = auto-discover)
font_symbols   =       # path to symbol fallback font (blank = auto-discover)

foreground = 220,220,220   # default text colour as "R,G,B" (0–255)
background = 0,0,0         # default background colour
```

Lines beginning with `#` or `;` are comments.  Unknown keys are silently ignored.
Out-of-range or non-positive numeric values are rejected and the default is kept.
Malformed `R,G,B` colour tuples fall back to the default.

### Font rendering

dicTerm uses a **three-font fallback system** to maximise glyph coverage:

1. **Regular font** (e.g. Maple Mono) — primary rendering font for everyday
   text.  Auto-discovered from common system font directories.
2. **Nerd Font** (e.g. AtkynsonMonoNerdFontMono) — fallback for PUA icons,
   Powerline symbols, Devicons, and other Nerd Font glyphs.
3. **Symbol fallback** (e.g. Noto Sans Symbols 2) — fallback for Unicode
   symbols not covered by the above two (geometric shapes, dingbats, etc.).

When rendering a character, `pick_glyph_font()` checks each font in turn
and uses the first one that has a non-zero atlas rectangle for the glyph.
If none have it, a `?` is rendered from the regular font.

The font atlas is built with `FONT_MAX_CODEPOINTS` (8192) entries covering
ASCII printable (0x20–0x7E) plus Nerd Font PUA ranges (box drawing, powerline,
devicons, Font Awesome, Material Design Icons).

## Project structure

```
├── CMakeLists.txt          # Build: dicTerm + 5 test executables
├── CMakeLists.txt          # Build: dicTerm + 9 test executables
├── Doxyfile                # Doxygen documentation configuration
├── .config/
│   ├── dicTerm.conf.example   # Sample configuration file
│   └── install.sh            # Helper script to install the config
├── README.md               # This file
├── include/
│   ├── config.h            # Terminal configuration (default + INI parser)
│   ├── parser.h            # ECMA-48 escape sequence parser API
│   ├── font.h              # Font rendering subsystem (glyph atlas + Nerd Fonts)
│   ├── input.h             # Keyboard input → PTY sequence converter
│   ├── mouse.h             # Mouse tracking → PTY report encoder
│   ├── scrollback.h        # Scrollback ring buffer
│   └── screen.h            # Screen cell with SGR colour attributes
├── src/
│   ├── main.c              # Terminal emulator: PTY, screen, SGR callbacks,
│   │                       #   scrollback viewport, UTF-8 decoder, rendering
│   ├── config.c            # INI-style configuration file parser
│   ├── parser.c            # ANSI escape sequence state machine
│   ├── font.c              # TrueType font loader, glyph atlas, Nerd Fonts PUA
│   ├── input.c             # Keyboard handling
│   ├── mouse.c             # Mouse report encoder (X10 + SGR)
│   ├── scrollback.c        # Line-based ring buffer
│   ├── screen.c            # Screen grid buffer (per-cell fg/bg colours)
│   ├── test_parser.c       # 46 unit tests (parser, incl. colon sub-params)
│   ├── test_scrollback.c   # 18 unit tests (scrollback ring buffer + viewport indexing)
│   ├── test_screen.c       # 6 unit tests (screen buffer ops)
│   ├── test_ansi_colors.c  # 32 unit tests (SGR colour parsing)
│   └── test_mouse.c        # 11 unit tests (mouse encoding: X10 + SGR)
│   ├── font.c              # TrueType font loader, glyph atlas, multi-font fallback
│   ├── input.c             # Keyboard handling (GetCharPressed + GetKeyPressed)
│   ├── scrollback.c        # Line-based ring buffer
│   ├── screen.c            # Screen grid buffer (per-cell fg/bg colours)
│   │
│   ├── test_config.c       # 7 unit tests (config parser, defaults, RGB)
│   ├── test_parser.c       # 59 unit tests (parser, incl. colon sub-params)
│   ├── test_scrollback.c   # 18 unit tests (scrollback ring buffer + viewport)
│   ├── test_screen.c       # 20 unit tests (screen buffer ops, NULL safety)
│   ├── test_ansi_colors.c  # 32 unit tests (SGR colour parsing)
│   ├── test_integration.c  # 22 integration tests (real-world sequences)
│   ├── test_terminal.c     # 41 terminal doc tests (cursor/erase/scroll)
│   ├── test_input.c        # 32 unit tests (key_to_seq, keyboard mappings)
│   └── test_font_codepoints.c  # 10 unit tests (codepoint list builder)
└── .opencode/
    ├── agents/             # Development agent definitions
    └── package.json        # opencode configuration
```

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                     raylib window                          │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  render_cell() per cell: bg rect, fg glyph,           │  │
│  │  pseudo-bold offset, underline line                   │  │
│  │  pick_glyph_font() for multi-font fallback            │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────┬──────────────────────────────────┘
                         │
┌────────────────────────▼──────────────────────────────────┐
│                       main.c                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │  PTY master   │  │  screen[][] │  │  parser_t    │     │
│  │  (forkpty)    │──│  + cx,cy    │──│  (callbacks) │     │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘     │
│         │                 │                 │             │
│         │         ┌───────▼────────┐        │             │
│         │         │  font_handle_t │        │             │
│         │         │  - regular     │        │             │
│         │         │  - nerd        │        │ parser_feed │
│         │         │  - symbols     │        │             │
│         │         │  - glyph_cache │        │             │
│         │         └───────▲────────┘        │             │
│         │  keyboard       │                 │             │
│         │  input          │                 │             │
│         │  (input.c)      │                 │             │
│         │                 │                 │             │
│         │  UTF-8 decoder  │  pick_glyph_    │             │
│         │  (byte-at-a-time) │ font()        │             │
└─────────┼─────────────────┼─────────────────┼─────────────┘
          │                 │                 │
          ▼                 │                 ▼
     ┌──────────┐           │          ┌──────────────┐
     │  bash    │           │          │  parser.c    │
     │  (child) │           │          │  state machine│
     └──────────┘           │          └──────────────┘
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
- Three-font fallback: regular → nerd → symbols → `?`
- Glyph atlas caching with raylib's built-in texture management
- Nerd Fonts auto-detection from common system paths (TTF/OTF subdirectories)
- PUA codepoint ranges for Nerd Font icons (powerline, devicons,
  Font Awesome, Material Design, box drawing, geometric shapes)
- Hot-swappable font selection: `font_select(handle, "nerd")` / `"regular"`
- Glyph index cache for O(1) codepoint → glyph lookups
- Sub-pixel positioning with proper ascent/descent metrics
- Font discovery: automatic search for monospace + Nerd Fonts + symbols
- `font_has_glyph()` — direct `f->recs[]` access (avoids raylib's built-in
  `?` fallback in `GetGlyphAtlasRec()`)

### Configuration (`config.c` / `config.h`)

- INI-style configuration file parser (sections + `key = value`)
- Default values applied before file parse; file overlays defaults only
- Searches `$XDG_CONFIG_HOME/dicTerm/config`, falls back to built-in defaults
- Configurable: terminal rows/cols, scrollback capacity, font size, window
  dimensions, padding, font paths, and default fg/bg colours
- Robust parsing: comments (`#`/`;`), whitespace trimming, unknown-section/key
  tolerance, bounds-checked numeric and RGB values
- 7 unit tests with 100% pass rate

### ANSI escape sequence parser (`parser.c`)
- Full ECMA-48 state machine: GROUND, ESC, CSI, OSC, DCS, SOS/PM/APC
- Callback-based API with zero-allocation design
- 7-bit and 8-bit control code support
- Multi-digit parameters, private markers, intermediate bytes
- String termination via BEL, 7-bit ST (ESC `\`), and 8-bit ST (0x9C)
- 59 unit tests with 100% pass rate

### Terminal emulation (`main.c`)
- ForkPTY child process (login shell via SHELL env var)
- Screen buffer (36 × 100 cells) with per-cell SGR colour attributes
- **UTF-8 decoder**: byte-at-a-time stateful decoder for multi-byte sequences
  (up to 4-byte codepoints, NERD-compatible)
- **Scrollback viewport**: browse historical output with Page Up/Down and
  Shift+Up/Down; viewport stays stable when new output arrives; cursor
  hidden in history mode; scrollback indicator bar
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
- **ESC sequences**: DECSC/DECRC (save/restore cursor), IND, RI, NEL, RIS, HTS
- Keyboard input via `input.c` with modifier support (Ctrl, Alt, Shift)
- **Mouse reporting** via `mouse.c`: DECSET modes `?9`/`?1000`/`?1002`/`?1003`
  and SGR encoding `?1006`; button, motion and wheel events with modifier
  bits; wheel drives local scrollback when tracking is off
- Cursor drawn as inverted block
- Window resizing with PTY resize notification
- Non-blocking PTY I/O
- Per-cell rendering: each character drawn individually with its own fg/bg
  colour, background fills, pseudo-bold (1 px offset), and underline
- Parser stuck detection: reset after 60 frames in non-GROUND state

### Keyboard input (`input.c` / `input.h`)
- `key_to_seq()` — raylib key → terminal escape sequence converter
- Printable characters via `GetCharPressed()` with proper Shift/Caps
- Ctrl+A–Z → 0x01–0x1A
- Alt+key → ESC prefix + key sequence
- Arrow keys (plain, Ctrl+arrows for word jumps)
- Home/End, Page Up/Down, Insert/Delete
- F1–F12 (xterm-style)
- Keypad digits and operators
- 32 unit tests with 100% pass rate

### Scrollback buffer (`scrollback.c` / `scrollback.h`)
- Fixed-capacity ring buffer (default 1000 lines)
- Push, indexed lookup (0 = most recent), clear, reset operations
- Fully integrated with `main.c`: lines are pushed automatically as they
  scroll off the top of the screen; the viewport can navigate the history
  and stays stable when new output arrives
- 18 unit tests with 100% pass rate

### Screen buffer (`screen.c` / `screen.h`)
- Row-major cell grid with full SGR attributes (fg, bg, bold, italic,
  underline, blink)
- Packed colour accessors (`screen_cell_fg_u32`, `screen_cell_bg_u32`)
- ED (erase display) with modes 0, 1, 2
- EL (erase line) with modes 0, 1, 2
- NULL-safe and bounds-checked operations
- 20 unit tests with 100% pass rate

## Doxygen

Generate the HTML documentation with:

```bash
doxygen Doxyfile
# → open docs/doxygen/html/index.html
```

The README is used as the main page.  All source and header files are
documented with Doxygen comments.

## Roadmap

- [x] Font rendering (glyph atlas, TrueType via raylib, Nerd Fonts support)
- [x] SGR colour attributes (standard/bright/extended foreground, background,
      bold, underline)
- [x] 256-colour palette and 24-bit truecolor RGB support
- [x] Scrollback buffer integration with viewport scrolling
- [x] Mouse support
- [x] Multi-font fallback (regular → nerd → symbols → `?`)
- [x] UTF-8 decoder for multi-byte Unicode codepoints
- [x] Configuration file
- [ ] Mouse support
- [ ] Clipboard integration
- [ ] Font ligature support
