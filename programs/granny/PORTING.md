# Granny PSVita source port

## Source selected

- Upstream: `https://github.com/BasHeemskerk/Granny-Decompiled`
- Revision: `56b1111e445318138614a722334cfd7c81276060` (`v1.2.0`, branch `vita`)
- Unity project version: `2018.2.19f1`
- Local checkout: `/home/davivbr/Documentos/Granny-Decompiled`

Keep the original C# gameplay scripts and Unity scene/assets as the behavior
reference. Do not use the separate Windows `Granny Legacy 1.9.2` executable as
the source for this port.

## Port boundary

The selected project contains Unity game code and serialized assets, but not
the Unity player/runtime. Its `Library/il2cpp_cache` is a PS Vita ARM32 build
cache (`Il2CppAssemblies.suprx` plus 587 ARM32 EABI `.obj` files); it contains
no generated C++ source. Those objects cannot be linked into the LiwusOS
x86-64 `.lxe`.

The LiwusOS SDK currently builds static native ELF applications. It has no CIL
runtime or Unity API implementation. A native port therefore needs a
compatible runtime layer before the original game scripts can execute.

## Linux player compatibility

The Linux reference player is a dynamically linked x86-64 `ET_EXEC`. Its
`PT_INTERP` requests `/lib64/ld-linux-x86-64.so.2`, and eight `DT_NEEDED`
entries identify the Linux libraries it uses. The shell command
`linux /path/to/Granny` enters the Linux ABI. The kernel loads the interpreter
from the VFS, sets `AT_BASE` to its load bias, and starts at its entry point.
`AT_ENTRY` and the program headers describe the original game executable.

LiwusOS implements private file mappings and `MAP_FIXED` replacement under
the Linux ABI. Mappings copy the requested file data into private frames.
`fstat` identifies files by their VFS inode, so glibc can distinguish separate
libraries opened through the same descriptor number.

In QEMU with `-cpu max`, a dynamically linked glibc program exited with status
0. With the original game executable, its Linux libraries and the original
Unity startup data staged in the guest, the loader initialized the shared
libraries and entered Unity. Unity found its data directory and loaded
`Granny_Data/MonoBleedingEdge/x86_64/libmonobdwgc-2.0.so`.

Unity logged failures to install signal handlers for SIGSEGV, SIGBUS, SIGFPE
and SIGILL. Mono then exited with `Mono requires /proc to be mounted.` Its
startup code checks for `/proc/self/maps`, and its garbage collector reads the
actual mappings there. The QEMU image staged the original startup files
and Mono library. The full game data remains in the reference build.
[Unity Mono startup source](https://github.com/Unity-Technologies/mono/blob/684649f590e758b168626173ef4c64dac05da4fc/mono/mini/mini-runtime.c#L3815).

After runtime initialization, the Unity player also requires Linux desktop services
for graphics, input and audio. The Linux ABI lacks the `ioctl`, `socket`,
`poll`/`epoll` and signal interfaces used by desktop libraries. LiwusOS exposes LVGL and
its own window/input/audio paths; it does not provide the Linux X11/GL stack
expected by this Unity player. The installed Unity Editor provides player
binaries and IL2CPP runtime source, but not the UnityEngine native engine source
or a LiwusOS player backend needed to retarget that runtime. Its Mono binaries
are host Linux binaries too.
The generated player contains SDL/X11/GLX and ALSA/PulseAudio backend imports,
confirmed by its embedded library and symbol names.

The Vita cache has a different target mismatch: it contains ARM32 Vita
IL2CPP/SUPRX output and uses Vita platform APIs. It cannot be linked into the
x86-64 LiwusOS executable format either.

## Required runtime services

Source inventory shows gameplay code uses Unity object/component lifecycle,
transforms, input, 3D rendering, collision and rigid bodies, NavMesh, animation,
audio, UI, scene changes, and PlayerPrefs. The current LiwusOS graphics path
provides LVGL windows and the smaller software renderer used by Craft; it does
not implement those Unity services.

A workspace inventory found no Mono, CoreCLR, or other CIL execution runtime
in the LiwusOS source or SDK. The reference Unity build contains a 952,320-byte
`Assembly-CSharp.dll` and Unity's managed facade assemblies, but those
assemblies call into the Unity native player; copying them into an `.lxe`
package alone would not execute the game.

The selected `Assembly-CSharp` tree contains 344 C# files; 337 contain
`MonoBehaviour`. A source-token inventory finds `GameObject` in 205 files,
collider types in 115, `AudioSource` in 86, animation APIs in 60, input APIs
in 50, Unity UI in 39, physics APIs in 37, and `PlayerPrefs` in 32. These
counts are a rough source inventory, not a claim that each file needs a
separate engine subsystem.

The main `Scene.unity` alone serializes 1,766 GameObjects, 588 MonoBehaviours,
614 mesh filters, 595 mesh renderers, 117 rigid bodies, and 67 lights. The
original scene data is present; rendering and lifecycle behavior for those
serialized objects is the missing part.

Representative script-to-runtime links from the selected source:

| Original script | Game behavior | Unity services called |
| --- | --- | --- |
| `EnemyAIGranny.cs` | Granny's search, chase, and attack state machine | `NavMeshAgent`, physics raycasts, animation clips, coroutines, audio, object/component lifecycle |
| `FPSControllerNEW.cs` | Player movement and camera control | `CharacterController`, transforms, `Physics`, `Time`, legacy `Animation`, touch input |
| `PickUp.cs` | Item pickup, placement, and puzzle interactions | raycasts/colliders, `GameObject`, transforms, animation, audio |
| `SensitivitySlider.cs` | Persist control sensitivity | `PlayerPrefs`, Unity UI slider |

## Current state

- Source and target versions are identified and pinned above.
- Unity `2018.2.19f1` and Linux Standalone support are installed locally; the
  Editor is now activated with Unity Personal.
- An isolated export of the pinned revision builds under Unity 2018.2 as an
  x86-64 Linux Mono player. The current build is in
  `/home/davivbr/Documentos/.granny-linux-reference.kkSLGg/player/`.
  The build log is in the parent folder, and `Assembly-CSharp.dll` is in
  `Granny_Data/Managed`. This copy preserves the gameplay scripts and assets.
- Unity 2018.2 does not support IL2CPP for Linux Standalone. Unity added that
  target in 2019.3; the installed 2018.2 Linux module is Mono-only. Therefore
  its `Currently selected scripting backend (IL2CPP) is not installed.` error
  is an expected target/version limitation, not a Granny source-code failure.
  See [Unity 2019.3.0b4 release notes](https://unity.com/releases/editor/beta/2019.3.0b4).
- A direct invocation of the bundled `il2cpp.exe` against the Linux player's
  `Assembly-CSharp.dll` was also tried outside Unity's supported build
  pipeline. It aborted in `DelegateMethodsWriter` while processing
  `Mono.Math.Prime.PrimalityTest::.ctor(Object, IntPtr)` with
  `Sequence contains no matching element`; passing the managed directory as
  both an assembly directory and an explicit assembly also reports a duplicate
  assembly. This experiment did not produce C++ and does not establish whether
  the project converts successfully with a supported IL2CPP target.
- The current isolated build embeds the locally cached Post Processing 2.3.0
  package. The checkout at the pinned revision remains intact.
- The existing ignored `build/liwus-sdk/bin/granny.lxe` is not a port of this
  source project: its embedded strings identify it as `PROTOTIPO 01`, and it
  has a standalone `move_granny` implementation rather than the game's Unity
  assemblies and assets.

## Port gate

The source-side compile gate is passed. The LiwusOS runtime gate is not:

| Gate | State | Evidence |
| --- | --- | --- |
| Compile selected C# scripts and scenes | Passed on Unity 2018.2 Linux player | Isolated build completed; `Assembly-CSharp.dll` generated |
| Translate the Linux gameplay assembly with Unity 2018.2 IL2CPP | Unsupported target | Linux Standalone IL2CPP was introduced in Unity 2019.3 |
| Load the Vita build in LiwusOS | Not compatible | Vita ARM32 IL2CPP/SUPRX output and Vita APIs |
| Start the Linux player's ELF interpreter | Passed in QEMU | The original Granny executable reaches glibc's shared library loader |
| Load shared libraries | Passed for the staged Unity build in QEMU | glibc transfers control to Unity, which loads Mono with `dlopen` |
| Initialize Mono | In progress | `/proc` (`maps`, `stat`, `readlink`), signal masks, `getrlimit(RLIMIT_STACK)`, JIT `MAP_32BIT`/RWX, futexes, TLS, POSIX scheduling, `poll`/`epoll` and socket-probe errors now let Mono create its worker pool; the player then reaches SDL bootstrap and reports `Display 0 '0': 1024x768` |
| Run Unity graphics/input/audio on LiwusOS | Pending | The Linux player still needs an X11-compatible window/display path (or an SDL framebuffer backend), plus GL/input/audio services |
| Package a functional Granny `.lxe` | Pending | The original player has not reached Unity's menu on LiwusOS; no screenshot or package is claimed |

The current route reuses the Linux Unity player and its Mono runtime. The Linux
compatibility layer now includes inert `poll`/`epoll` queues and display-lock
file publication so an Xvfb probe advances to its Unix-socket listener. A probe
with `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` reaches Unity's display
enumeration but produces no frame, so it is not a menu screenshot. The next
checkpoint is still the windowing backend: no Unity menu is visible until Liwus
supplies an OpenGL-capable X11/SDL display path and the associated input/audio
services. The QEMU fixture used during this work is temporary and is not a
distributable `.lxe`.
