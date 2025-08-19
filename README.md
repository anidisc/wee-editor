# wee - A Small Terminal Text Editor

**Version 0.89.1 Beta**

A lightweight, terminal-based text editor written in C.

## Features

- **Text Selection**: Select a block of text by marking a start point (`Ctrl-B`) and an end point (`Ctrl-E`). The selected text will be highlighted.
- **Operations on Selection**: Perform copy (`Ctrl-W`), cut (`Ctrl-K`), paste (`Ctrl-U`), or delete (`Delete`/`Backspace`) operations on the selected text.
- **Move Selection**: Move selected text up/down/left/right using arrow keys in selection mode.
- **Deselection**: Clear the current selection by pressing `Esc` or `Ctrl-L`.
- **Syntax Highlighting**: Extensible syntax highlighting for different programming languages (C and Python included by default).
- **File Browser**: A built-in file browser to visually navigate and open files (`Ctrl-O`).
- **Core Editing**: Basic text manipulation (insert, delete characters, newlines).
- **Find**: Incremental search within the file (`Ctrl-F`).
- **Jump to Line**: Quickly navigate to a specific line number (`Ctrl-J`).
- **Standard Navigation**: Arrow Keys, Home, End, PageUp, PageDown.
- **Save & Quit**: Save functionality (`Ctrl-S`) and a safe quit (`Ctrl-Q`) with a warning for unsaved changes.
- **Save As**: Save the current file with a new name (`Ctrl-Y`).
- **Line-based Clipboard**: Copy (`Ctrl-W`), cut (`Ctrl-K`), and paste (`Ctrl-U`) entire lines.
- **Line Numbers**: Toggle the display of line numbers (`Ctrl-N`).
- **New File**: Create a new, empty file buffer (`Ctrl-T`).
- **Help Screen**: An in-editor help screen with a list of keybindings (`Ctrl-G`).
* **Auto-Indentation**: Automatically carries over the indentation from the previous line when creating a new one.

## What's New in 0.89.1 Beta

- Improved selection & deselection logic with a full anchor-cursor model for Shift+arrow selection (character and line).
- Pressing `Esc` now consistently clears any active selection in both NORMAL and SELECTION modes.
- New snapshot-based Undo/Redo system: use `Ctrl-Z` and `Ctrl-R` to undo and redo edits reliably.
- Enhanced selection operations: cut/copy/paste on selected text with `Ctrl-K`/`Ctrl-W`/`Ctrl-U`, ensuring proper syntax-highlighting updates on deselection.
- Expanded in-editor help screen (`Ctrl-G`) now includes all keybindings, selection commands, and undo/redo tips.

## Building

The project uses a `Makefile` for compilation.

To compile the editor, simply run `make`:

```bash
make
```

## Usage

To run the editor, you can either start it without a file or specify one to open:

```bash
# Start the editor
./wee

# Open a specific file
./wee filename.txt
```

## Syntax Highlighting

Syntax highlighting rules are defined in `.json` files located in the `syntax/` directory. You can add support for new languages by creating a new JSON file in this directory. See `syntax/c.json` and `syntax/python.json` for examples.

## Keybindings

- `Ctrl-S`: Save the current file.
- `Ctrl-Y`: Save As (prompt for new filename).
- `Ctrl-Q`: Quit editor (with prompt if unsaved changes).
- `Ctrl-O`: Open built-in file browser.
- `Ctrl-N`: Toggle line numbers.
- `Ctrl-T`: New empty file buffer.
- `Ctrl-G`: Show the in-editor help screen (interactive list of all commands).

- `Ctrl-J`: Jump to line number.
- `Ctrl-Z`: Undo (snapshot-based).
- `Ctrl-R`: Redo.

- `Ctrl-A`: Select all text.
- `Ctrl-B`: Mark start of selection.
- `Ctrl-E`: Mark end of selection and enter SELECTION mode.
- `Esc` (in SELECTION mode): Cancel selection.
- `Ctrl-W` (in SELECTION mode): Copy selection.
- `Ctrl-K` (in SELECTION mode): Cut selection.
- `Del` / `Backspace` (in SELECTION mode): Delete selection.
- Arrow keys (in SELECTION mode): Move selected text up/down/left/right.

- `Shift+Arrow`: Quick-select character (left/right) or full lines (up/down).

- `Ctrl-W`: Copy current line (in NORMAL mode).
- `Ctrl-K`: Cut current line (in NORMAL mode).
- `Ctrl-U`: Paste last yanked or cut text.

*For interactive help and additional tips, press `Ctrl-G` within the editor.*

- `Ctrl-S`: Save the current file.
- `Ctrl-Y`: Save As...
- `Ctrl-Q`: Quit the editor.
- `Ctrl-O`: Open the file browser to select a file.
- `Ctrl-F`: Search for text within the file.
- `Ctrl-J`: Jump to a specific line number.
- `Ctrl-T`: New empty file.
- `Ctrl-G`: Show the help screen.
- `Ctrl-N`: Toggle line numbers.
- `Ctrl-W`: Copy the current line or selected text.
- `Ctrl-K`: Cut the current line or selected text.
- `Ctrl-U`: Paste the copied/cut line or selected text.
- `Ctrl-B`: Mark the start of a text selection.
- `Ctrl-E`: Mark the end of a text selection.
- `Esc` / `Ctrl-L`: Clear the current text selection.
- **Arrow Keys**: Move the cursor.
- **Arrow Keys (in Sel. Mode)**: Move selected text.
- **Home** / **End**: Move cursor to the beginning/end of the line.
- `ALT+b`: Move cursor to the beginning of the line.
- `ALT+e`: Move cursor to the end of the line.
- **PageUp** / **PageDown**: Scroll the view up or down.
- **Backspace** / **Delete**: Delete characters or selected text.
