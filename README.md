# Hand Poser

A GML plugin for making your own hand poses in Geronimo: where your hand sits on a magazine and how every finger
joint is bent, set live while you hold the magazine, saved, and put on every magazine of that kind from then on.

## Using it

1. Install: copy `HandPoser.dll` to `...\Binaries\Win64\GML\plugins\HandPoser\` (or `build.bat install`).
2. Start the game and take a magazine in either hand.
3. Open **http://127.0.0.1:8765/** in a browser on the PC. In the headset, open Virtual Desktop's desktop view and
   use the browser there. To use a phone or tablet instead, set `[Server] AllowLan = true` and open
   `http://<your PC's address>:8765/` on it.
4. The page shows each hand's magazine:
   - **Where the hand sits on it:** front/back, side to side and up/down in cm along the magazine, then tilt, turn
     and roll.
   - **Fingers:** each finger's base knuckle, middle joint and tip joint, each with **bend**, **side to side** and
     **twist**. "Open all 10%" and "Close all 10%" scale every joint at once.

   Drag a slider, type a number, or use the − / + buttons. Your hand takes the change about a third of a second
   later: the plugin lets go of the magazine and takes it again, since the game only places the hand when it takes
   something.
5. **Save for every …** keeps the pose for every magazine of that kind. **Undo changes** goes back to your saved pose
   (or the default). **Back to the default** forgets your pose.

Opening Virtual Desktop's desktop view lets go of the magazine in the game. The page keeps editing it: any change puts
it back in your hand, and so does **Put it back in my hand**.

## What "a kind" is

The magazine's class, plus `@mod` when another plugin has re-dressed it. The Saiga-12K's Sb.5 is
`Magazine_AK_6L23_C@mod`, the Barrett's is `Magazine_SR25_Steel_20_C@mod`, and a plain AK-74M magazine is
`Magazine_AK_6L23_C`. The editor starts from whatever grip the magazine has, including the fits those plugins make,
and a saved pose replaces it.

One pose serves both hands. The game mirrors a magazine's grip for the other hand, so a pose made with your left hand
fits your right the mirror way.

## Where poses are kept

`GML\config\com.remarrow.handposer.cfg`, `[Poses]`: one line per kind,
`loc x y z; rot pitch yaw roll; bone <name> qx qy qz qw; ...` (the hand socket's position and rotation on the
magazine, and each finger joint's turn from the open hand). Copy that file to take your poses to another PC.

## Install

Needs [GML 2.2.0 or newer](https://github.com/RemArrow/geronimo-mod-loader/releases). The easiest way is
[GML Manager](https://github.com/RemArrow/geronimo-mod-loader#gml-manager): Hand Poser is in its Browse tab. By hand: close
the game, download `HandPoser-<version>.zip` from [Releases](https://github.com/RemArrow/geronimo-hand-poser/releases) and
extract it into `...GERONIMOGeronimoBinariesWin64`. That adds `GMLplugins$d`. To remove the
plugin, delete that folder.

## Settings

| Key | Default | |
|---|---|---|
| `[Server] Port` | 8765 | the page's port |
| `[Server] AllowLan` | false | also serve the page to other devices on your network |

## How it works

Every magazine has a hand socket (VRExpansion `UHandSocketComponent`): its position and rotation on the magazine is
where the hand goes, and its `CustomPoseDeltas` (bone name, rotation from the open hand) shape the fingers. The plugin
reads what each controller holds (`GetGrippedObjects`), edits that socket, and takes the magazine again with the
game's own slot grip so the body's hand (IK target and finger pose, set when it takes something) follows. Saved poses
go onto every magazine of their kind twice a second, also after another plugin re-dresses one. The page is served by
a small HTTP server on its own thread (localhost only unless `AllowLan`); the game's objects are only touched on the
game thread.

## Testing

`tests\HandPoserDev` (built, never installed) loads the team room, takes a spare magazine from the player's pouch with
the left hand through the game's slot grip, holds it where the arm reaches, logs the socket and the hand once a second,
and renders the hand when asked. A test script drives the page's API alongside.

Verified in game (2026-10-12):
- The page loads from the game (no script errors), lists the left hand's Sb.5 and its 15 finger joints.
- Moving the hand 3 cm forward on the page moved the body's hand 3.00 cm along the magazine.
- Bending the index middle joint from the page (+10°) moved the fingertip; Undo, Save, Close/Open all, Back to the
  default all applied.
- Letting go, then a change (or Put it back): the magazine went back into the hand with the change.
- A saved pose in the config was put on the pouch magazine at the next start, and the hand took it.

Not done yet: gun grips (pistol grip, support hand) and other items; a separate pose per hand.
