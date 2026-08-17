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
```

## Keys

- `h j k l` / arrow keys: move
- `w b e`: word motions
- `0` / `$`: line start / line end
- `gg` / `G`: file start / file end
- `i`: enter insert mode
- `a`: append and enter insert mode
- `I` / `A`: insert at line start / append at line end
- `Esc`: return to normal mode
- `v`: enter visual mode
- `y` / `yy`: yank selection or current line
- `p`: paste after cursor or below current line
- `x`: delete character or visual selection
- `dd`: delete current line
- `D`: delete to end of line
- `o` / `O`: open a line below / above and enter insert mode
- `Ctrl-d` / `Ctrl-u`: scroll down / up
- `:w`, `:q`, `:wq`: write, quit, write and quit
- `:e path`: open a file in a new buffer
- `:bnew`: create an empty buffer
- `:bn` / `:bp`: next / previous buffer
