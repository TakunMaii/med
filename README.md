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
- Operators: `d`, `c`, `y` with motions, including `dd`, `cc`, `yy`, `dw`, `cw`, `d$`, `c0`
- Text objects: `iw`, `aw`, `iW`, `aW`, `ip`, `ap`, plus delimiter objects such as `i(`, `a(`, `i"`, `a"`
- Edit: `x`, `s`, `S`, `D`, `r`, `~`, `.`, `u`, `Ctrl-r`; `.` repeats simple edits and the last inserted text
- Paste: `p`, `P`
- Registers: named register prefixes such as `"ayy`, `"ap`
- Marks: `ma`, `'a`, and `` `a`` for lowercase file-local marks
- Search: `/pattern`, `?pattern`, `n`, `N`
- Command line: defaults to one line, wraps/expands up to eight lines for long input or output; `Shift-Enter` or `Ctrl-Enter` inserts a command-line newline
- `:w`, `:w filename`, `:q`, `:q!`, `:wq`: write and quit commands
- `:e path`: open a file in a new buffer
- `:bnew`, `:bn`, `:bp`, `:bfirst`, `:blast`, `:buffer N`, `:ls`, `:bd`, `:bd!`: buffer commands; `:ls` uses multiline output
- `:set number`, `:set nonumber`, `:set relativenumber`, `:set norelativenumber`
