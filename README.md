# Hidden Pictures

Turn any picture into two sheets of random-looking black-and-white noise. Each sheet on its own is gibberish. Print both on transparent film (or thin paper), cut them apart, stack them, hold them against the light, and the hidden picture appears.

This is the classic *visual cryptography* trick (Naor–Shamir), wrapped in a small, modern Windows app.

- Native Win32 application written in C17, no runtime or third-party dependencies
- Windows 10 / 11, per-monitor DPI aware, light and dark theme
- English and German user interface (follows the Windows display language)
- A single `.exe` of about 250 KB
- Free software under the [GPL-3.0](LICENSE)

## How it works

1. The selected area of your picture is divided into a grid of square cells (the **detail** setting).
2. The cell brightness is auto-levelled and converted to black and white with Floyd–Steinberg dithering.
3. **Sheet A** is pure random noise.
4. **Sheet B** is derived from A and the picture: where the picture is white, B equals A; where the picture is black, B is the inverse of A.
5. When the sheets are stacked, black wins. White picture areas show about 50 % noise, black picture areas are solid black, so the picture appears dark on a speckled background.

Neither sheet alone contains any information about the picture.

## Using the app

1. **Open image**: click the button, click the left panel, or drop a file onto the window.
   Supported formats are everything Windows Imaging Component can decode, such as JPEG, PNG, GIF, BMP, TIFF, JPEG XR, ICO, and HEIC/WebP/AVIF if the matching codec is installed. EXIF orientation is honored.
2. **Choose the area**: drag a rectangle over the picture. Drag the edges, corners or the inside to adjust or move it.
3. **Do some magic**: generates the two sheets. They fly together and show the revealed picture.
4. **Explore the result**: see below.
5. **Save** a PNG or **Print**.

### Choosing the area

| Action | Result |
| --- | --- |
| Drag outside the selection | Draws a new selection |
| Drag the corners / edges | Resizes the selection |
| Drag inside the selection | Moves the selection |
| Click (without dragging) outside the selection, or on the empty panel | Opens the file dialog |

### The result panel

The right panel shows only the two generated sheets.

- **Slider** (*Together ↔ Apart*): moves the sheets onto each other or apart. After generating, the sheets start overlaid so you immediately see the result.
- **Drag a sheet with the mouse**: sheets move in whole-cell steps. A sheet within one cell of perfect alignment snaps into place, and the picture appears. Off by one cell and it is noise again, which shows how precise the stacking has to be on paper.
- **Keyboard**: arrow keys move sheet B by one cell, `Home` overlays the sheets, `End` separates them.

### Settings

| Setting | Description |
| --- | --- |
| **Sheet size** | Edge length of one sheet in centimeters (the longer side for non-square areas). Limited by the paper size of the selected printer, see below. |
| **Detail** | Number of cells along the longer side, 48 to 384 in steps of 16 (default 160). More detail means a finer picture but harder alignment. 128 to 160 is a good starting point for the first print. |
| **Printer** | Shows the current printer and paper size. Click to open the Windows printer setup (printer, properties, paper size, orientation). |
| **Light / Dark mode** | Toggle in the top right. Follows Windows until you choose; your choice is then remembered. |

Changing sheet size or detail after generating marks the result as outdated. Press **Do some magic** again to apply it.

### Printer and paper size

The maximum sheet size is derived from the paper of the selected printer. Both sheets are printed one below the other with 10 mm margins and a 12 mm gap, with a dashed cut line between them.

- Without an explicit choice the app follows the **Windows default printer**. It re-reads it every time the window is activated, so changes in Windows are picked up without restarting.
- A printer chosen in the app (via the printer button or the print dialog) is **remembered across restarts**, including paper size and orientation. If that printer is no longer installed, the app falls back to the default printer.
- If no printer is available, A4 is assumed.

### Saving

**Save** asks for a target and writes one image containing both sheets stacked, separated by a dashed cut line. The file type is chosen in the save dialog (or by typing the extension); changing the type there also updates the extension in the file name.

| Format | Notes |
| --- | --- |
| **PNG** (default) | 1-bit black and white, lossless, smallest files |
| **TIFF** | 1-bit black and white, lossless. The app encodes the image with both **LZW** and **PackBits** and keeps whichever is smaller for the picture at hand |
| **BMP** | 1-bit with a black/white palette, uncompressed |
| **GIF** | Palette image (black/white), LZW-compressed by the format itself |

- Resolution is in the 600 dpi class. PNG, TIFF and BMP store the DPI so that the file prints at exactly the chosen sheet size (GIF has no DPI field).
- All formats contain exactly the same pixels.
- Default file name: `<source>_hidden.png`

### Printing

**Print** opens the standard Windows print dialog and prints both sheets at the chosen physical size, scaled down only if they would not fit on the page.

Tips for good results:

- Print on **transparent film** (inkjet or laser, matching your printer) for the best effect. Thin paper held against a bright window also works.
- Print at the highest quality, with **scaling set to 100 %** and no "fit to page" or ink-saving mode in the printer driver.
- Cut along the dashed line, align the cell grids exactly, then hold the stack against the light.

## Window, theme and settings storage

- The window opens centered on the first start. Size, position and maximized state are restored on the next start.
- Settings are stored per user in `HKCU\Software\Rekow IT\Hidden Pictures`: window placement, theme choice, printer name and printer settings. Delete that key to reset everything.
- The about dialog (click the version in the status bar) always opens centered over the app window.

## Building

Requirements: Visual Studio 2022 Build Tools (MSVC) and the Windows SDK.

```bat
build.bat
```

The script compiles `app.rc` (string tables for English and German, icon, manifest, `VERSIONINFO`) and `main.c` with `/std:c17`, `/W4` and control-flow-guard enabled, and produces `HiddenPictures.exe`.

| File | Purpose |
| --- | --- |
| `main.c` | The whole application |
| `app.rc`, `resource.h` | Strings (EN/DE), version info, icon and manifest references |
| `app.manifest` | Common controls v6, Windows 10/11, per-monitor DPI v2 |
| `build.bat` | Build script |

## Command line

```
HiddenPictures.exe [image-file]
```

Opens the given image on start.

## License and copyright

Copyright © 2026 Rekow IT

Hidden Pictures is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [GNU General Public License](LICENSE) for more details.

SPDX-License-Identifier: GPL-3.0-or-later
