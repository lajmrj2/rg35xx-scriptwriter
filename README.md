# Scriptwriter (libretro core for RG35XX / GarlicOS)

A screenplay editor that launches like a game. Plug in a USB keyboard, type,
and it auto-formats scene headings, action, character, parentheticals,
dialogue and transitions. Saves plain **Fountain** (.fountain) files to
RetroArch's save folder; open them in WriterDuet, Highland, Final Draft, etc.

## Keys
- TAB (on an empty line): cycle scene > character > transition > action
- ENTER: next element, adds blank lines for you
  (scene > action, character > dialogue, dialogue > next character, transition > scene)
- SHIFT+ENTER: new line inside the same element
- BACKSPACE: delete (no cursor movement; you always write at the end)
- F1 help, F2 text size, F3 hide/show status bar, F4 save now (gamepad START also saves, R changes size)
- Autosaves every ~3 seconds and on exit

## Build
- Desktop test: `make test` (types a sample script and prints the saved file)
- RG35XX: `make arm` (needs arm-linux-gnueabihf-gcc), produces freewrite_libretro.so

## Install (UNVERIFIED on a real device)
1. Copy freewrite_libretro.so where your GarlicOS cores live, alongside the other cores.
2. Put Script.fw (any small placeholder file) in a ROMS system folder that GarlicOS
   maps to this core, the same way the existing cores are mapped.
3. Launch Script.fw. Saves appear in RetroArch's save folder on the SD card.
Folder names and core mapping differ by GarlicOS version; copy how an existing core is set up.
If the core is built on a newer glibc than the device has, it may fail to load; build with an older toolchain.

## Limitations
- Original 5x7 pixel font has no descenders (g, y, p look slightly cramped).
- No title page, notes, dual dialogue or cursor navigation yet.
- Page count is a rough estimate.

## Standalone app (no RetroArch core needed)
`make app` builds `scriptwriter`, a static ARM program that draws to /dev/fb0 and reads
the USB keyboard directly. Put it in an Apps folder with `Scriptwriter.sh` (see dist/).
It saves to a `scripts` folder next to the binary. F10 or Ctrl+Q saves and quits.
Untested on real hardware; the launcher format must match how your GarlicOS Apps folder works.
