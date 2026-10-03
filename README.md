<p align="center"><img src="assets/logo.png" width="420" alt="Meet the Robinsons"></p>

# Robinsons-Patch

GameCube controller and Classic Controller support for **Meet the Robinsons** on Wii.
Plug a GameCube pad into any controller port, or use a Classic Controller, and play without a Wii Remote and Nunchuk.

Supported releases (all disc version 0):

| ID       | Release                  |
|----------|--------------------------|
| `RRSE4Q` | USA                      |
| `RRSP4Q` | Europe (English/German/Spanish) |
| `RRSX4Q` | Europe (French/Italian)  |

## Controls

Follows the game's own Wii controls, remapped to the pad:

| GameCube / Classic      | Does                               |
|-------------------------|------------------------------------|
| Left stick              | Move                               |
| C-stick / right stick   | Pointer (camera and aiming)        |
| A                       | A (context action)                 |
| B                       | B (fire)                           |
| X                       | C (alternate fire)                 |
| Y, Minus                | −                                  |
| L / ZL                  | Z (target lock)                    |
| R / ZR                  | 1 (centre camera)                  |
| Z                       | 2 (help)                           |
| Start                   | +                                  |
| Home (Classic)          | Home                               |
| D-pad                   | D-pad (switch gadget, down grabs)  |

The C-stick is an absolute pointer that springs back to the centre, because the game's camera follows the pointer.

## Installing

**Patcher (recommended):** download it from the releases page, drop your `.wbfs` or `.iso` on the window.
The patched image replaces the original in place and the untouched original is kept as `<name>.bak`.

Command line:

```
python3 tools/patch_disc.py "Meet the Robinsons (USA).wbfs"
```

This needs [wit](https://wit.wiimm.de/) on `PATH` (the GUI builds bundle it).

**Gecko codes / Riivolution:** `codes/<ID>.ini`, `codes/<ID>.txt` and `riivolution/<ID>.xml` are generated from the same data.
The codes keep their helper routines in low memory (0x80001820–0x80003000), so do not combine them with a loader's own Gecko handler on a real Wii.

## Testing status

Tested in **Dolphin only**, with scripted inputs against all three releases (menus, navigation and gameplay on USA; boot and menu navigation on both European releases).
**Nothing has been tested on a real Wii.** The Classic Controller path and a GameCube pad used alongside a real Wii Remote are written but not yet exercised.
Reports from real hardware are welcome.

## Building from source

Needs devkitPPC and your own retail `main.dol` files.

```
ROBINSONS_DOLS=/dir/with/RRSE4Q.dol,... python3 tools/gen_prebuilt.py   # regenerate tools/prebuilt/*.json
python3 tools/build.py                                                    # codes/ and riivolution/
python3 tools/check.py                                                    # no game files needed
ROBINSONS_DOLS=/dir python3 tools/verify.py                               # against real DOLs
```

See [docs/TECHNICAL.md](docs/TECHNICAL.md) for how it works.

## Credits

Patches by quatric.
