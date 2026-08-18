# Blockcraft DS

An original block-building homebrew game for the Nintendo DS Lite, written in C
with libnds. Fly around a 3D voxel world, break blocks, and place blocks of
different types — rendered on the DS's real 3D hardware.

## Controls

| Input          | Action                          |
| -------------- | ------------------------------- |
| D-Pad          | Move forward/back and strafe    |
| X / B          | Fly up / fly down               |
| Stylus drag    | Look around                     |
| L / R          | Turn left / right               |
| A              | Place the selected block        |
| Y              | Break the highlighted block     |
| SELECT         | Cycle block type (stone, dirt, grass, wood, leaves, sand, brick) |
| START          | Regenerate the world            |

The top screen shows the 3D world; the bottom screen shows controls and a HUD
(selected block, position, what you're aiming at). The block you're looking at
is highlighted.

## Building the .nds file

You need the free devkitPro toolchain (Windows, macOS, or Linux):

1. Install devkitPro: https://devkitpro.org/wiki/Getting_Started
2. Install the NDS development package:
   ```
   sudo dkp-pacman -S nds-dev
   ```
   (On Windows, use the devkitPro installer and tick "NDS Development".)
3. Open a terminal in this folder and run:
   ```
   make
   ```
4. This produces **`blockcraft.nds`** — that's your game file.

## Running it

- **On a real DS Lite:** copy `blockcraft.nds` to a flashcart (e.g. R4-style
  card) and launch it from the card's menu. The DS Lite has no SD slot of its
  own, so a flashcart is required for homebrew.
- **In an emulator:** open `blockcraft.nds` in melonDS or DeSmuME — great for
  testing before putting it on hardware.

## Notes

- This is an original homebrew game inspired by block-building games; it is
  not affiliated with Mojang or Nintendo.
- World size is 24×10×24 blocks with a draw radius tuned to the DS's polygon
  budget. You can tweak `WX/WY/WZ` and `DRAW_RADIUS` in `source/main.c`.
