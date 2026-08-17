# med

`med` is a small Vulkan-backed modal text editor prototype.

## Features

- Vulkan renderer with FreeType text atlas
- Default font: `CaskaydiaCoveNerdFontMono-Regular.ttf`, size `14`
- Gruvbox dark color theme
- Vim-like modes: normal, insert, visual
- Relative line numbers in the left gutter
- Normal/visual block cursor and insert bar cursor
- Fading cursor trail while moving, similar to terminal/editor cursor effects
- Tree-sitter powered C syntax highlighting
- LSP client architecture with bundled cJSON; first server support is `clangd` for C/C++
- GUI hover and completion popups rendered over the editor surface

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Test

```sh
cmake --build build --target med_editor_tests
ctest --test-dir build --output-on-failure
```

## Run

```sh
./build/med
./build/med path/to/file.c
./build/med a.c b.c
```

## Keys

- Counts: `3j`, `10dd`, `5w`
- Motions: `h j k l`, arrow keys, `w b e`, `W B E`, `ge gE`, `0 $ ^ _ g_ | + -`, `{ }`, `gg G`, `H M L`, `%`
- Find on line: `f F t T`, repeat with `;` and `,`
- Scrolling: `Ctrl-d`, `Ctrl-u`, `Ctrl-f`, `Ctrl-b`, `zz`, `zt`, `zb`
- Insert: `i`, `a`, `I`, `A`, `o`, `O`
- `Esc`: return to normal mode; insert-mode edits are grouped into a single undo step
- Visual: `v` for character selection, `V` for line selection, `Ctrl-v` for block selection, `o` to swap selection endpoints, `gv` to restore the previous selection
- Visual block: `r{char}` replaces the selected block
- Visual block insert: `I` inserts text on each selected line, `A` appends text on each selected line
- Operators: `d`, `c`, `y` with motions, including `dd`, `cc`, `yy`, `dw`, `cw`, `d$`, `c0`
- Text objects: `iw`, `aw`, `iW`, `aW`, `ip`, `ap`, plus delimiter objects such as `i(`, `a(`, `i"`, `a"`
- Edit: `x`, `s`, `S`, `D`, `r`, `~`, `.`, `u`, `Ctrl-r`; `.` replays the last recorded change event stream
- Paste: `p`, `P`
- Registers: named register prefixes such as `"ayy`, `"ap`
- Macros: `q{reg}` records, `@{reg}` replays, `@@` repeats the last macro
- Marks: `ma`, `'a`, and `` `a`` for lowercase file-local marks
- Search: `/pattern`, `?pattern`, `n`, `N`
- Command line: defaults to one line, wraps/expands up to eight lines for long input or output; `Shift-Enter` or `Ctrl-Enter` inserts a command-line newline
- `:w`, `:w filename`, `:q`, `:q!`, `:wq`: write and quit commands
- Ex ranges: `:1,3d`, `:%d`, `:1,3y`, `:%y`
- Substitute/global: `:s/pat/repl/`, `:%s/pat/repl/g`, `:g/pat/d`, `:v/pat/d` use POSIX extended regular expressions; `&` in replacements expands to the whole match
- `:e path`: open a file in a new buffer
- `:bnew`, `:bn`, `:bp`, `:bfirst`, `:blast`, `:buffer N`, `:ls`, `:bd`, `:bd!`: buffer commands; `:ls` uses multiline output
- Windows/tabs: `Ctrl-w s`, `Ctrl-w v`, `Ctrl-w w`, `Ctrl-w q`, `Ctrl-w c`, `Ctrl-w o`, `Ctrl-w h/j/k/l`, `gt`, `gT`
- LSP: `K` requests hover, `gd` jumps to definition, `gD` jumps to declaration
- Completion: automatically requested in insert mode for C/C++ buffers; `Tab`/`Ctrl-n` selects next, `Shift-Tab`/`Ctrl-p` selects previous, `Enter`/`Ctrl-y` accepts
- `:set number`, `:set nonumber`, `:set relativenumber`, `:set norelativenumber`

## LSP

`med` starts `clangd` automatically for C/C++ files and communicates through LSP JSON-RPC over stdio. File changes are synchronized with full-document `didChange` messages. Diagnostics currently surface ERROR-level messages, matching the local Neovim configuration.
