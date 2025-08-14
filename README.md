# wee - A Small Terminal Text Editor

A lightweight, terminal-based text editor written in C.

## Features

- **File Browser**: A built-in file browser to visually navigate and open files (`Ctrl-O`).
- **Core Editing**: Basic text manipulation (insert, delete characters, newlines).
- **Find**: Incremental search within the file (`Ctrl-F`).
- **Standard Navigation**: Arrow keys, Home, End, PageUp, PageDown.
- **Save & Quit**: Save functionality (`Ctrl-S`) and a safe quit (`Ctrl-Q`) with a warning for unsaved changes.

## Building

To compile the editor, use a C compiler like `gcc`:

```bash
gcc wee.c -o wee -Wall
```

## Usage

To run the editor, you can either start it without a file or specify one to open:

```bash
# Start the editor
./wee

# Open a specific file
./wee filename.txt
```

## Keybindings

- `Ctrl-S`: Save the current file.
- `Ctrl-Q`: Quit the editor.
- `Ctrl-O`: Open the file browser to select a file.
- `Ctrl-F`: Search for text within the file.
- **Arrow Keys**: Move the cursor.
- **Home** / **End**: Move cursor to the beginning/end of the line.
- **PageUp** / **PageDown**: Scroll the view up or down.
- **Backspace** / **Delete**: Delete characters.
