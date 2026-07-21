# dicTerm

A GPU-accelerated terminal emulator built with GLFW 3 + OpenGL 3.3 Core Profile
and C23 (C2y).  Text rendering uses FreeType + HarfBuzz for font shaping, glyph
caching, and ligature support.

## Status

Early development – a minimal terminal that spawns a child shell, renders its
output to a GLFW window, and forwards keyboard input.  ANSI SGR colour
attributes (foreground, background, bold, underline) with full 24-bit RGB and
256-colour palette support are implemented.  Multi-font fallback rendering
with automatic discovery of Maple Mono, Nerd Fonts, and symbol fallback fonts
is functional.  Scrollback history viewing with viewport navigation, text
selection, clipboard integration (CLIPBOARD + PRIMARY), mouse tracking, and
HarfBuzz-based ligature shaping are implemented.

## Dependencies

- **clang** 16+ (C23 / `-std=c2y`)
- **cmake** 3.20+
- **glfw3** (system-installed; tested with 3.4)
- **freetype2** (TrueType/OpenType font loading)
- **harfbuzz** (glyph shaping with ligature support)
- **fontconfig** (system font discovery)
- **X11** (optional; enables `bg_blur` compositor blur via `_KDE_NET_WM_BLUR_BEHIND_REGION`)
- **xsel** (optional; enables X11 PRIMARY selection / middle-click paste)
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
build/test_parser              # 59 unit tests (parser state machine)
build/test_config              # 7 unit tests (config INI parser)
build/test_scrollback          # 18 unit tests (scrollback ring buffer)
build/test_screen              # 20 unit tests (screen buffer, SGR ops)
build/test_ansi_colors         # 32 unit tests (ANSI 256-colour parsing)
build/test_integration         # 22 integration tests (end-to-end sequences)
build/test_terminal            # 41 terminal doc tests (cursor, erase, scroll)
build/test_input               # 32 unit tests (key_to_seq, keyboard handling)
build/test_font_codepoints     # 10 unit tests (codepoint list builder)
build/test_clipboard           # unit tests (base64 decode, clipboard ops)

ctest --test-dir build         # via CTest (all 10 suites)
```

## Usage

```bash
./build/dicTerm
```

Opens a window running `/bin/bash` inside a pseudo-terminal.  Type normally;
the shell output is rendered in the GLFW window using the GPU-accelerated
OpenGL renderer with HarfBuzz-shaped glyphs.  Close the window or type `exit`
to quit.

### Keyboard shortcuts

| Key | Action |
|-----|--------|
| `Ctrl` + `Shift` + `C` | Copy selection to clipboard |
| `Ctrl` + `Shift` + `V` | Paste from clipboard (with bracketed-paste wrapping) |
| `Shift` + `Insert` | Paste from X11 PRIMARY selection |
| `Ctrl` + `Shift` + `U` | Cancel / reset (SGR 0) |

### Scrollback (history) navigation

The terminal stores lines that scroll off the top of the screen in a ring
buffer (default 1000 lines).  You can navigate the history with:

| Key | Action |
|-----|--------|
| `Page Up`   | Scroll up one page (screen height) |
| `Page Down` | Scroll down one page |
| `Shift` + `↑` | Scroll up one line |
| `Shift` + `↓` | Scroll down one line |

While browsing history, a "`-- Scrollback (offset/count) --`" indicator is
shown at the bottom of the window.  Press any regular key (letter, Enter,
Ctrl+C, etc.) to return to the normal terminal view.  The viewport
automatically stays stable when new output arrives from the shell.

### Mouse

- **Text selection**: click and drag with the left mouse button while mouse
  tracking is disabled (default).  Selected text is highlighted in reverse
  colours.  The selection is automatically copied to the X11 PRIMARY
  selection on release.
- **Middle-click**: pastes from the X11 PRIMARY selection.
- **Mouse tracking**: enabled by terminal applications that request it
  (X10, VT200, SGR, urxvt, SGR-pixels).  When mouse tracking is active,
  click-and-drag is forwarded to the application instead of performing
  local text selection.

### Window decorations

By default the window has no OS title bar / decorations.  You can re-enable
them in the config file:

```conf
window_decorations = true
```

### Transparent background

dicTerm supports a transparent framebuffer (Porter/Duff `src-over` blending)
so the compositor can show the desktop behind the terminal.  The effective
opacity is controlled by `bg_opacity` (0.0–1.0).  On KWin and compatible
compositors, `bg_blur` enables a blur effect behind the window.

```conf
clear_bg   = true     # enable transparent framebuffer
bg_opacity = 0.5      # background opacity 0–1
bg_blur    = true     # request compositor blur (X11, KDE atom)
```

### Configuration

dicTerm reads a Unix-style `.conf` file at startup.  The search order (first existing wins) is:

1. `$XDG_CONFIG_HOME/dicTerm/dicTerm.conf`
2. `~/.config/dicTerm/dicTerm.conf`
3. `/etc/dicTerm.conf`
4. Built-in defaults (applied when no file is found)

All settings are **optional** — any key you omit falls back to its default.
A commented example is shipped in `.config/dicTerm.conf.example`.

#### Install or edit the configuration file

**1. Copy the example to your config directory**
```bash
mkdir -p ~/.config/dicTerm
cp .config/dicTerm.conf.example ~/.config/dicTerm/dicTerm.conf
$EDITOR ~/.config/dicTerm/dicTerm.conf
```

**2. Use the installation script**
```bash
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

#### Format

```conf
rows         = 36          # visible terminal rows
cols         = 100         # visible terminal columns
scrollback   = 1000        # scrollback ring-buffer line count
font_size    = 20.0        # font point size in pixels

window_width      = 1280   # initial window width (pixels)
window_height     = 800    # initial window height (pixels)
window_padding    = 10     # padding around the terminal text area
window_decorations = false # show OS window decorations / title bar

clear_bg   = true          # transparent framebuffer for compositor blending
bg_opacity = 0.5           # background opacity 0–1 when clear_bg is on
bg_blur    = false         # request compositor blur behind window (X11 KDE)

font_regular  =             # path to regular font (blank = auto-discover)
font_nerd      =             # path to Nerd Font (blank = auto-discover)
font_symbols   =             # path to symbol fallback font (blank = auto-discover)

foreground = 220,220,220    # default text colour as "R,G,B" (0–255)
background = 0,0,0          # default background colour
```

Lines beginning with `#` or `;` are comments.  Unknown keys are silently ignored.
Out-of-range or non-positive numeric values are rejected and the default is kept.
Malformed `R,G,B` colour tuples fall back to the default.

### Font rendering

dicTerm uses a **three-font fallback system** to maximise glyph coverage,
rendered via FreeType + HarfBuzz and cached in an OpenGL glyph atlas:

1. **Regular font** (e.g. Maple Mono) — primary rendering font for everyday
   text.  Auto-discovered from common system font directories.
2. **Nerd Font** (e.g. AtkynsonMonoNerdFontMono) — fallback for PUA icons,
   Powerline symbols, Devicons, and other Nerd Font glyphs.
3. **Symbol fallback** (e.g. Noto Sans Symbols 2) — fallback for Unicode
   symbols not covered by the above two (geometric shapes, dingbats, etc.).

When rendering a line of text, HarfBuzz shapes the codepoints into glyphs with
ligature and contextual-alternate features enabled (`liga`, `clig`).  Each
glyph is rendered from the OpenGL atlas; if a glyph is missing from the
primary font, the fallback fonts are checked in order.  If no font has the
glyph, a `?` is rendered from the regular font.

The font atlas is built with `FONT_MAX_CODEPOINTS` (8192) entries covering
ASCII printable (0x20–0x7E) plus Nerd Font PUA ranges (box drawing, powerline,
devicons, Font Awesome, Material Design Icons).

## Project structure

```
├── CMakeLists.txt          # Build: dicTerm + 10 test executables
├── Doxyfile                # Doxygen documentation configuration
├── .config/
│   ├── dicTerm.conf.example   # Sample configuration file
│   └── install.sh            # Helper script to install the config
├── README.md               # This file
├── include/
│   ├── config.h            # Terminal configuration (default + INI parser)
│   ├── parser.h            # ECMA-48 escape sequence parser API
│   ├── font.h              # Font loading (FreeType + HarfBuzz)
│   ├── gl_renderer.h       # OpenGL 3.3 renderer (glyph atlas, shaping)
│   ├── input.h             # Keyboard input + mouse tracking
│   ├── clipboard.h         # Clipboard integration (GLFW + xsel)
│   ├── scrollback.h        # Scrollback ring buffer
│   └── screen.h            # Screen cell with SGR colour attributes
├── src/
│   ├── main.c              # Terminal emulator: PTY, screen, SGR callbacks,
│   │                       #   scrollback viewport, UTF-8 decoder, selection,
│   │                       #   clipboard shortcuts, blur behind window
│   ├── config.c            # INI-style configuration file parser
│   ├── parser.c            # ANSI escape sequence state machine
│   ├── font.c              # TrueType/OpenType font loader, glyph cache
│   ├── gl_renderer.c       # OpenGL 3.3 renderer: glyph atlas, HarfBuzz
│   │                       #   shaping, ligature support, cell drawing
│   ├── input.c             # Keyboard handling + mouse tracking (X10, VT200,
│   │                       #   SGR, urxvt, SGR pixels)
│   ├── clipboard.c         # GLFW clipboard + X11 PRIMARY selection (xsel)
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
│   ├── test_font_codepoints.c # 10 unit tests (codepoint list builder)
│   └── test_clipboard.c    # Unit tests (base64 decode, clipboard ops)
└── .opencode/
    ├── agents/             # Development agent definitions
    └── package.json        # opencode configuration
```

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                        GLFW window                               │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  gl_renderer_draw_cells():                                  │  │
│  │   • HarfBuzz shape (liga/clig features enabled)             │  │
│  │   • OpenGL glyph atlas lookup (multi-font fallback)         │  │
│  │   • Per-cell rendering: bg rect, fg glyph, underline        │  │
│  │   • Selection highlight (swap fg/bg)                        │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────────┐
│                         main.c                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │  PTY master   │  │  screen[][] │  │  parser_t    │           │
│  │  (forkpty)    │──│  + cx,cy    │──│  (callbacks) │           │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘           │
│         │                 │                  │                  │
│         │         ┌───────▼────────┐         │                  │
│         │         │  font_handle_t  │         │                  │
│         │         │  + gl_renderer  │         │ parser_feed      │
│         │         └───────▲────────┘         │                  │
│         │  clipboard      │                  │                  │
│         │  (clipboard.c)  │                  │                  │
│         │  mouse          │                  │                  │
│         │  (input.c)      │                  │                  │
│         │                 │                  │                  │
│         │  UTF-8 decoder  │  gl_renderer_    │                  │
│         │  (byte-at-a-time) │ draw_cells()   │                  │
└─────────┼─────────────────┼──────────────────┼──────────────────┘
          │                 │                  │
          ▼                 │                  ▼
     ┌──────────┐           │           ┌──────────────┐
     │  bash    │           │           │  parser.c    │
     │  (child) │           │           │  state machine│
     └──────────┘           │           └──────────────┘
                           ▼
                    ┌──────────────┐
                    │  font.c +    │
                    │  gl_renderer │
                    │  FreeType    │
                    │  HarfBuzz    │
                    │  OpenGL 3.3  │
                    └──────────────┘
```

## What's implemented

### Font rendering (`font.c` / `font.h`)
- TrueType/OpenType font loading via FreeType
- Three-font fallback: regular → nerd → symbols → `?`
- Nerd Fonts auto-detection from common system paths (TTF/OTF subdirectories)
- PUA codepoint ranges for Nerd Font icons (powerline, devicons,
  Font Awesome, Material Design, box drawing, geometric shapes)
- Hot-swappable font selection: `font_select(handle, "nerd")` / `"regular"`
- Font discovery: automatic search for monospace + Nerd Fonts + symbols
- `font_has_glyph()` — direct glyph cache access

### OpenGL renderer (`gl_renderer.c` / `gl_renderer.h`)
- OpenGL 3.3 Core Profile renderer with glyph atlas texture
- HarfBuzz shaping with ligature (`liga`) and contextual alternate (`clig`)
- Multi-font fallback during shaping
- Sub-pixel positioning with proper ascent/descent metrics
- Per-cell rendering: background fills, foreground glyphs,
  pseudo-bold (1 px offset), underline
- Transparent framebuffer support (`clear_bg` + `bg_opacity`)

### Clipboard (`clipboard.c` / `clipboard.h`)
- GLFW clipboard API for CLIPBOARD selection (Ctrl+Shift+C / Ctrl+Shift+V)
- X11 PRIMARY selection via `xsel` (middle-click paste)
- OSC 52 escape sequence support (programmatic clipboard access)
- Base64 decoder for OSC 52 payloads
- Bracketed paste mode wrapping

### Configuration (`config.c` / `config.h`)
- INI-style configuration file parser (key = value)
- Default values applied before file parse; file overlays defaults only
- Searches `$XDG_CONFIG_HOME/dicTerm/dicTerm.conf`, falls back to built-in defaults
- Configurable: terminal rows/cols, scrollback capacity, font size, window
  dimensions, padding, font paths, window decorations, transparency/opacity/blur,
  and default fg/bg colours
- Robust parsing: comments (`#`/`;`), whitespace trimming, unknown-key
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
- **Text selection**: click-drag mouse selection with reverse-colour highlight;
  auto-copy to PRIMARY on release
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
- **OSC sequences**: OSC 0/2 (icon/title set), OSC 52 (clipboard), OSC 104 (reset palette)
- **DEC private modes**: cursor visibility, mouse tracking (X10/VT200/SGR/urxvt/SGR-pixels),
  alternate screen buffer, bracketed paste
- Keyboard input via `input.c` with modifier support (Ctrl, Alt, Shift)
- Clipboard shortcuts (Ctrl+Shift+C/V, Shift+Insert, Ctrl+Shift+U)
- Cursor drawn as inverted block
- Window resizing with PTY resize notification
- Non-blocking PTY I/O
- Parser stuck detection: reset after 60 frames in non-GROUND state
- Background blur behind window via `_KDE_NET_WM_BLUR_BEHIND_REGION` X11 atom
- Window decorations toggle (default off)

### Keyboard & mouse input (`input.c` / `input.h`)
- `key_to_seq()` — GLFW key → terminal escape sequence converter
- Printable characters via GLFW character callback with proper Shift/Caps
- Ctrl+A–Z → 0x01–0x1A
- Alt+key → ESC prefix + key sequence
- Arrow keys (plain, Ctrl+arrows for word jumps)
- Home/End, Page Up/Down, Insert/Delete
- F1–F12 (xterm-style)
- Keypad digits and operators
- Mouse tracking: X10, VT200 (normal + button-event + any-event), SGR,
  urxvt, SGR-pixels
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

- [x] Font rendering (glyph atlas, TrueType via FreeType, Nerd Fonts support)
- [x] SGR colour attributes (standard/bright/extended foreground, background,
      bold, underline)
- [x] 256-colour palette and 24-bit truecolor RGB support
- [x] Scrollback buffer integration with viewport scrolling
- [x] Multi-font fallback (regular → nerd → symbols → `?`)
- [x] UTF-8 decoder for multi-byte Unicode codepoints
- [x] Configuration file
- [x] Mouse support (X10, VT200, SGR, urxvt, SGR-pixels)
- [x] Clipboard integration (copy/paste, selection, OSC 52, bracketed paste)
- [x] Font ligature support (HarfBuzz shaping with liga/clig)
- [x] Transparent framebuffer / background opacity
- [x] Background blur behind window
- [x] Text selection (click-drag, PRIMARY auto-copy)
- [x] Alternate screen buffer
- [ ] GPU-accelerated rendering via OpenGL
- [ ] Tabbed / multi-window support
- [ ] Sixel / Kitty graphics protocol
