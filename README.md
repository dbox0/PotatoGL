# OpenGL Renderer

A small ECS-based engine on OpenGL 4.3 core, built to run well on old hardware.
Minimum target is Intel HD Graphics 4600 (Haswell), which tops out at GL 4.3 on Windows.

- SDL3 window, input and GL context; glad loader; glm math
- Entities and components via EnTT, with a parent/child scene graph
- One shared vertex/index buffer, one texture array and a per-draw SSBO: the whole
  scene is a single `glMultiDrawElementsIndirect` call
- glTF (`.gltf`/`.glb`) via cgltf, FBX/OBJ via ufbx, images via stb_image
- Gameplay as C++ systems, plus Unity-style scripts stored as components
  (see [docs/Scripting.md](docs/Scripting.md))
- Optional Dear ImGui editor: debug stats, scene hierarchy, inspector
- Shader hot reload in Debug builds; GPU buffers grow on demand

## Building

Needs CMake 3.24+ and a C++20 compiler.

```sh
cmake -S . -B build
cmake --build build
./build/renderer
```

SDL3 and glm are used from the system if installed, otherwise downloaded and built
(SDL3 then links statically). cgltf, ufbx, stb, EnTT and Dear ImGui are always
downloaded. glad is pre-generated in `src/third_party/glad`, so Python is not needed.

| CMake option | Default | Effect |
| --- | --- | --- |
| `BUILD_EDITOR` | `ON` | Builds the ImGui editor. `OFF` drops ImGui entirely (not even downloaded). |

`shaders/` and `assets/` are copied next to the executable on every build and loaded
relative to it. Debug builds read them from the source tree instead, so saving a shader
hot reloads it. A shader that fails to compile keeps the previous program running.

## Controls

| Input | Action |
| --- | --- |
| W A S D | Move |
| Q / E | Up / down |
| Left Shift | Sprint |
| Hold left or right mouse | Look |
| F1 | Toggle editor |
| Esc | Quit |

Editor:

- **Scene Hierarchy** (top right): click to select, drag onto another object to
  reparent, drag into empty space to move to the top level, right-click for
  Create / Delete, `Delete` key removes the selection.
- **Inspector** (bottom right): edit the selected object's components and scripts,
  remove them with the × on their header, add new ones with *Add Component* /
  *Add Script*.

## Layout

The source tree matches the three CMake libraries one to one.

```
src/
  main.cpp       wires App + Gameplay (+ Editor when BUILD_EDITOR)
  engine/        lib "engine" - never links ImGui
    app/         App (window, loop), CameraController, Game and Overlay interfaces
    assets/      file loaders producing ModelData / ImageData, no GL
    core/        Paths: resourcePath()
    gfx/         thin GL wrappers: buffers, texture array, shaders
    reflect/     ComponentRegistry: runtime component/field descriptions for tools
    render/      Renderer: owns GPU buffers, uploads models, draws the scene
    scene/       Scene (registry + hierarchy), Transform, Camera, engine components
    script/      ScriptContext, ScriptRunner
  editor/        lib "editor" - the only code that touches ImGui; optional
  game/          lib "game" - gameplay components, systems and scripts
    scripts/     script types and their registration
  third_party/   glad, single-header library implementation files
shaders/         GLSL 430
assets/          models and textures
```

Dependencies only point one way: `game` and `editor` depend on `engine`, never the
reverse, and `game` never sees ImGui. App talks to gameplay through the `Game`
interface and to the editor through the `Overlay` interface, so either can be swapped
out or left away (e.g. a shipping build without the editor).

## Frame

```
events -> editor UI -> camera -> game update -> world transforms -> render sync -> draw -> editor draw
```

Game update runs scripts' `onUpdate`, then a fixed 60 Hz step (systems, `onFixedUpdate`,
cleanup of `Dead` entities), then `onLateUpdate`.

## Scene

`Scene` owns the `entt::registry` and the hierarchy. Every object made with
`createGameObject` has `Name`, `Transform` (local), `WorldTransform` and `Relationship`
(an intrusive child list). `updateWorldTransforms()` walks the tree each frame; the
renderer only reads `WorldTransform`.

```cpp
entt::entity duck = scene.createGameObject("Duck", parent);
scene.registry().get<Transform>(duck).setPos({0, 1, 0});
scene.registry().emplace<MeshRenderer>(duck, duckModel);
scene.setParent(duck, otherParent);
scene.destroyRecursive(duck);
```

Models are added once with `renderer.addModel(*loadModel(resourcePath("assets/x.glb")))`
and referenced by `ModelId` from `MeshRenderer`.

## Gameplay

Content and systems live in `src/game`. `Gameplay::init` builds the scene,
`Gameplay::fixedUpdate` runs systems in a fixed order. Prefer a component + system for
behaviour shared by many entities and a script for one-off logic; both are covered in
[docs/Scripting.md](docs/Scripting.md).

## GL 4.3 notes

No DSA (4.5), no `glBufferStorage` (4.4) and no `gl_DrawID` (4.6). The per-draw index
comes from `baseInstance` through an instanced vertex attribute. Buffers and texture
layers double when full (`glCopyBufferSubData` / `glCopyImageSubData`).

To regenerate glad:

```sh
pip install glad2
python -m glad --api "gl:core=4.3" --extensions "" --reproducible --out-path src/third_party/glad c
```

## License

MIT, see `LICENSE`. Third-party libraries and assets are covered by their own licenses.
