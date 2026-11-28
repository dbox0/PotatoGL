# UI

Screen-space UI made of ordinary game objects, so prefabs, undo, uuids and scene files
work as for everything else. Code lives in `src/engine/ui/` and `src/engine/render/UIRenderer.*`.

## Canvas and layout

A `Canvas` is the root. Its descendants with a `RectTransform` are laid out in canvas
units: target pixels divided by the canvas scale, origin at the top-left, y down.

| Canvas field | |
| --- | --- |
| `referenceResolution` | The resolution the UI is designed at |
| `scaleMode` | `ConstantPixelSize` (scale 1) or `ScaleWithScreenSize` |
| `matchWidthOrHeight` | 0 scales with the width, 1 with the height, in between blends (log space) |
| `sortOrder` | Higher canvases draw on top |

`RectTransform`, like Unity's:

- `anchorMin` / `anchorMax` are 0..1 of the parent rect; (0, 0) is its top-left corner.
  Equal anchors pin the element to a point, different ones stretch it.
- `pivot` is 0..1 of the element itself; rotation and scale happen around it.
- `anchoredPosition` moves the pivot from its point between the anchors.
- `sizeDelta` is the size minus the anchors' span (with point anchors: the size).
- `rotationDeg` turns counter-clockwise.

Children draw after their parent and later siblings on top. `Disabled` hides a subtree.
An object with a `RectTransform` ignores its `Transform`, which the inspector hides.

## Graphics

| Component | |
| --- | --- |
| `UIShape` | Rect, rounded rect, circle or line; fill, border; antialiased SDF |
| `UIImage` | A sprite (or plain color): Simple, Sliced (9 quads from the `.meta` border), Filled (horizontal, vertical, radial) |
| `UIText` | SDF text: font, size, alignment, wrapping, truncation, line spacing |

Colors are sRGB. Sprite settings live in `<image>.meta` (border, pixels per unit, filter),
edited in the inspector when the image is selected in the Project view. Images and their
`.meta` hot reload. Fonts are `.ttf` / `.otf`; an empty font is
`assets/Fonts/NotoSans-Regular.ttf` (SIL OFL, see `NotoSans-OFL.txt`).

## Interaction (Play mode)

`UIButton` tints its `target` graphic (itself if none) for hover, press and disabled. A
click (press and release over it) emits `UIClicked{button}` and, if `onClick` is set, a
`NamedEvent` of that name with `sender` = the button.

`UISlider` takes its value from the pointer's position along `direction`; a drag holds on
until release. Changes emit `UISliderChanged{slider, value}` and, if `onChanged` is set, a
`NamedEvent` with `value` and `max`. Its `fill` (a Filled `UIImage`, or a rect whose anchors
follow) and `handle` follow the value in Edit mode too.

UI input runs before the game update. `Input::pointerOverUi` is set while the pointer is
over any UI graphic; check it before acting on gameplay clicks.

## Bars

`UIBar` drives a Filled `UIImage` (`fill`) and an optional `UIText` (`label`). With `listen`
set, named events of that name set its `value` and `max`, so gameplay only needs to emit:

```cpp
ctx.emit(NamedEvent::make("PlayerHealth", current, max, ctx.self));
```

`smoothing` eases the fill towards the value (0 jumps), `gradient` tints it from
`lowColor` to `highColor`, and `labelFormat` takes `{0}` (value), `{1}` (max) and `{2}`
(percent). Ready-made prefabs are in `assets/UI/`: `HealthBar`, `StaminaBar` and
`ProgressBar`. GameObject > UI > Health Bar instantiates the first. An instance keeps its
own `Name`, `Transform` and `RectTransform` when the prefab is rebuilt.

The main scene's HUD is the sample: `Player` has the `PlayerHealth` script, H deals 10
damage, and the Heal button's `onClick = "Heal"` restores it. Nothing links the two but
the event names. See [Events.md](Events.md).

## Canvas editor

Window > Canvas, or Edit in Canvas on a Canvas or Rect Transform in the inspector. It
shows the selected object's canvas, or the first one, at a preview resolution.

| Input | |
| --- | --- |
| Left click | Select the topmost element; Ctrl / Shift toggles it in the selection |
| Drag the body | Move the selection |
| Drag a handle | Resize; Shift keeps the aspect ratio, Alt resizes around the centre |
| Drag the pivot dot | Move the pivot, the rect stays put |
| Drag an anchor triangle | Move the anchors, the rect stays put |
| Arrows | Nudge 1 unit; Shift: 10 |
| Wheel, middle drag, Space + drag, F | Zoom, pan, pan, frame |

Moves and resizes snap to the parent's and siblings' edges and centres (pink guides) or,
with Grid on, to the grid; hold Ctrl to drag freely. Every drag, nudge and anchor preset
(Anchor Presets in the Rect Transform header) is one undo step. GameObject > UI creates
Canvas, Panel, Shape, Image, Text, Button, Slider and Health Bar, making a Canvas first
when the selection isn't under one.

## World sprites

`SpriteRenderer` draws a textured quad in the object's XY plane, after the lit pass,
sorted by `sortOrder` then back to front. Unlit by default: its color goes through
exposure and tonemapping like everything else. With `lit` it gets diffuse light from the
sun (and its shadow with `receiveShadows`), point and spot lights and the ambient. Sprites
are pickable in the Scene view and never cast shadows.
