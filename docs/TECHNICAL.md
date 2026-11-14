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
| `pad_post`   | Fills the pointer fields from the C-stick, and the Nunchuk accelerometer (still, or a flick). |
| `pad_probe`  | Makes `WPADProbe` report a Nunchuk so the game accepts the device. |

Synthetic samples use extension type 1 (Nunchuk), format 5. State (previous Wii button bits and so on) lives in zeroed padding at 0x80005D40.

The pointer is absolute and recentres, because the camera follows the pointer position rather than its velocity.

## Flick targeting

The only motion control in the game is flicking the Nunchuk in Targeting Mode to switch to the next target in that direction.
`RVLControllerInputDriver`'s per-controller update (USA `0x803074A0`) copies the Nunchuk acceleration (KPADStatus `+0x68/+0x6C/+0x70`)
into the controller at `+0x9C`, and the control mapper returns that vector as axis 2. The targeting code (USA `0x8005F9D0`) switches
target when `|acc.x|` or `|acc.y|` exceeds 1.25 g, at most every 0.25 s. For a sideways flick it asks the selector (`0x80389960`)
for the nearest target on one side, chosen by the sign of `-acc.x`; in game a positive `acc.x` moves the lock to the target on
Wilbur's right. The vertical case goes through a separate ray query and its sign has not been checked in game (`-acc.y` is taken as up).

With the lock (L / ZL) held, a C-stick push past 80/127 starts a 3-frame pulse of 2.0 g on the dominant axis; the stick must return
inside 30/127 before the next one, and the pointer is frozen while the lock is held. Otherwise `pad_post` writes a still accelerometer
(all zero) for any channel the patch drives, so a synthetic or Classic-Controller-backed Nunchuk never reports stray motion.

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

The flick was first checked in the opening Egypt level: with the lock held, C-stick pushes set one pulse per push,
and the game's controller object for channel 0 read the pulse vector and (0, 0, 0) at rest. Egypt has no lockable targets.

The target switch itself was then tested in the Robinson house music room (USA), where the frog band sits on the stage.
To get there quickly the lab redirects New Game to world index 2 (`a1_robinson`), moves Wilbur onto the stage, and gives him
the scanner (`scannerSweep`) through the game's own `WeaponInventory` slot call, since Targeting Mode does nothing without a gadget.
Holding the lock with no flick keeps the same target; each C-stick push moves the lock one target in that direction, and pushing the
other way returns to the previous one. Wilbur's heading was read back after each flick and matched the frogs' positions from the level data.
This also showed that the first version had left and right swapped, which is now fixed. Up and down pushes were sent but found
no target above or below in that spot, so the vertical direction is still unconfirmed.
