# wee - Minimal Terminal Text Editor

A lightweight, terminal-based text editor written in C, inspired by vim and micro.

## Features
- Open and edit text files
- Basic text manipulation (insert, delete characters, newlines)
- Save changes to file (`Ctrl-S`)
- Save As (`Ctrl-Y`)
- Toggle line numbers (`Ctrl-N`)
- Copy current line (`Ctrl-W`)
- Cut current line (`Ctrl-K`)
- Paste (`Ctrl-U`)
- Enhanced Search (`Ctrl-F`):
  - Highlights found text
  - Navigate through results using arrow keys
  - Places cursor at the end of the found text upon pressing Enter
- Simple navigation (arrow keys, Home, End, PageUp, PageDown)

## Building
```bash
gcc wee.c -o wee
```

## Usage
```bash
./wee [filename]
```

## Keybindings
- `Ctrl-S`: Save file
- `Ctrl-Y`: Save file as (Save As)
- `Ctrl-Q`: Quit editor
- `Ctrl-F`: Search for text
- `Ctrl-N`: Toggle line numbers visibility
- `Ctrl-W`: Copy current line
- `Ctrl-K`: Cut current line
- `Ctrl-U`: Paste content
- Arrow Keys: Move cursor
- `Home`/`End`: Move cursor to beginning/end of line
- `PageUp`/`PageDown`: Scroll page up/down