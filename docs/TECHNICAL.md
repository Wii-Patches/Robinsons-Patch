# How it works

The game reads input through the Wii SDK KPAD/WPAD libraries and only knows Wii Remotes with a Nunchuk.
The patch makes a GameCube pad or Classic Controller look like one.

## Hooks

Four small position-independent routines (`src/pad.c`, built with devkitPPC) are injected at 0x80001820 in the DOL's init section,
and four call sites in the input library branch into them:

| Hook         | Purpose |
|--------------|---------|
| `pad_poll`   | Polls the SI registers at 0xCD006400 for GameCube pads. |
| `pad_sample` | Turns a GC or Classic Controller state into a synthetic Nunchuk sample in the `KPADiRead` sample ring. |
| `pad_post`   | Fills the pointer fields from the C-stick. |
| `pad_probe`  | Makes `WPADProbe` report a Nunchuk so the game accepts the device. |

Synthetic samples use extension type 1 (Nunchuk), format 5. State (previous Wii button bits and so on) lives in zeroed padding at 0x80005D40.

The pointer is absolute and recentres, because the camera follows the pointer position rather than its velocity.

## Releases

The three releases are the same build with the input library at different addresses.
`src/gen_pad.py` takes USA addresses as constants and finds the others with masked-signature search (`tools/sig.py`).
USA and Europe En/De/Es happen to share every patched site; Fr/It differs.

## Patched sites and layout

`tools/prebuilt/pad_<ID>.json` holds the operations (hooks, blobs, in-place patches). `tools/ops.py` applies them to a DOL
or renders them as Gecko `C2` codes or Riivolution memory patches, so the three install methods cannot disagree.
`tools/check.py` enforces windows and branch-back slots; `tools/verify.py` checks real DOLs.

## Testing

Dolphin lab: private user dir, Pipe controller device, GDB stub for reading memory and applying the patch live,
per-window screenshots, and scripted inputs. Only Dolphin has been used; no real hardware.
