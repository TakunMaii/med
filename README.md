# med

`med` is a small Vulkan-backed modal text editor prototype.

## Features

- Vulkan renderer with FreeType text atlas
- Default font: `CaskaydiaCoveNerdFontMono-Regular.ttf`, size `14`
- Gruvbox dark color theme
- Vim-like modes: normal, insert, visual
- Relative line numbers in the left gutter
- Normal/visual block cursor and insert bar cursor
- Tree-sitter powered C syntax highlighting

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/med
./build/med path/to/file.c
./build/med a.c b.c
```

## Keys

- Counts: `3j`, `10dd`, `5w`
- Motions: `h j k l`, arrow keys, `w b e`, `0 $ ^ _ + -`, `gg G`, `H M L`, `%`
- Find on line: `f F t T`, repeat with `;` and `,`
- Scrolling: `Ctrl-d`, `Ctrl-u`, `Ctrl-f`, `Ctrl-b`, `zz`, `zt`, `zb`
- Insert: `i`, `a`, `I`, `A`, `o`, `O`
- `Esc`: return to normal mode
- Visual: `v` for character selection, `V` for line selection
- Operators: `d`, `c`, `y` with motions, including `dd`, `cc`, `yy`, `dw`, `cw`, `d$`, `c0`
- Text objects: `iw`, `aw` for `diw`, `ciw`, `yiw`, `daw`, `caw`, `yaw`
- Edit: `x`, `s`, `S`, `D`, `r`, `.`, `u`, `Ctrl-r`
- Paste: `p`, `P`
- Search: `/pattern`, `?pattern`, `n`, `N`
- `:w`, `:w filename`, `:q`, `:q!`, `:wq`: write and quit commands
- `:e path`: open a file in a new buffer
- `:bnew`, `:bn`, `:bp`, `:bfirst`, `:blast`, `:buffer N`, `:ls`, `:bd`, `:bd!`: buffer commands
- `:set number`, `:set nonumber`, `:set relativenumber`, `:set norelativenumber`
