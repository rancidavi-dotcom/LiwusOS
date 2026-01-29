# GPU Backend — Future Hardware Acceleration Architecture

## Objective

Define how the abstract `renderer_ops_t` interface enables a future GPU-accelerated backend (Vulkan, Metal, DirectX 12, or a minimal native GPU driver). The architecture is prepared today: the same `gui_renderer_t` struct and vtable dispatch that feeds the software `fb_renderer` can feed a GPU command buffer with zero changes to compositor or widget code.

## Problems Solved

- **Hardware-agnostic design**: The renderer abstraction was designed specifically to allow a GPU backend to be dropped in without touching the scene graph, event system, or compositor loop.
- **Minimal driver surface**: Only 9 function pointers need to be implemented for a new backend.
- **Graceful fallback**: If GPU init fails, fall back to `fb_renderer` by simply changing the `renderer_ops_t` pointer.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    renderer_ops_t                        │
│                                                         │
│  fill_rect  ──→  [SOFTWARE]  fb_fill_rect              │
│              └──→  [GPU]      vk_fill_rect              │
│                                                         │
│  blit       ──→  [SOFTWARE]  fb_blit                    │
│              └──→  [GPU]      vk_blit (textured quad)   │
│                                                         │
│  blit_scaled ──→  [SOFTWARE]  fb_blit_scaled            │
│               └──→  [GPU]      vk_blit_scaled (sampler) │
│                                                         │
│  draw_glyph ──→  [SOFTWARE]  fb_draw_glyph (1bpp)      │
│              └──→  [GPU]      vk_draw_glyph (SDF glyph) │
│                                                         │
│  present    ──→  [SOFTWARE]  memcpy to VRAM             │
│              └──→  [GPU]      vkQueuePresentKHR         │
└─────────────────────────────────────────────────────────┘
```

**Backend selection at init:**

```
gui_init()
    │
    ├── gpu_backend_init()          // try Vulkan/GPU
    │   ├── success → use gpu_ops
    │   └── fail    → fall through
    │
    └── fb_renderer_create()       // always falls back to software
        └── use fb_ops (current)
```

## Vtable Mapping: Software → GPU

### `fill_rect`

| Software | GPU Equivalent |
|---|---|
| `fb_fill_rect`: loop over pixels, alpha-blend each | Command buffer: draw a full-screen quad with push-constants for color. Fragment shader writes `color * alpha` with blend mode `SRC_OVER`. |

### `draw_rect`

| Software | GPU Equivalent |
|---|---|
| 4x `fill_rect` calls for top/bottom/left/right strips | Single indexed draw with 4 triangles (outline geometry) or using `VK_FILL_MODE_LINE`. |

### `blit`

| Software | GPU Equivalent |
|---|---|
| Nested loop, src-over alpha per pixel | `vkCmdBlitImage` or textured full-screen quad with trilinear sampler. Fragment shader samples source texture, applies blend mode. |

### `blit_scaled`

| Software | GPU Equivalent |
|---|---|
| Nearest-neighbor per-pixel in nested loop | Textured quad with `VK_FILTER_NEAREST` sampler. Scale is a push-constant in the vertex shader. |

### `draw_glyph`

| Software | GPU Equivalent |
|---|---|
| 1bpp bitmap unpacking with per-pixel fg/bg | SDF (Signed Distance Field) texture atlas. Fragment shader does smoothstep on the SDF value. |

### `set_clip`

| Software | GPU Equivalent |
|---|---|
| Store in `r->clip`, intersect in `fb_clip` | `vkCmdSetScissor` dynamic state. |

### `set_opacity`

| Software | GPU Equivalent |
|---|---|
| Store in `r->opacity` (unused by fb currently) | Push-constant in fragment shader, multiplied with output alpha. |

### `present`

| Software | GPU Equivalent |
|---|---|
| `memcpy(backbuf → vram)` | `vkQueuePresentKHR(swapchain)` with proper semaphore synchronization. |

## Backend State (Hypothetical)

```c
typedef struct {
    // Vulkan instance/device/queue
    VkInstance       instance;
    VkDevice         device;
    VkQueue          queue;
    VkSwapchainKHR   swapchain;
    VkRenderPass     render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline       fill_pipe;
    VkPipeline       blit_pipe;
    VkPipeline       glyph_pipe;

    // Command buffers (double/triple buffered)
    VkCommandBuffer  cmd_buf[3];
    uint32_t         frame_index;

    // Texture atlas for glyphs and small images
    VkImage          atlas;
    VkDeviceMemory   atlas_mem;
    VkDescriptorSet  atlas_descriptor;

    // Sync primitives
    VkSemaphore      acquire_sem[3];
    VkSemaphore      submit_sem[3];
    VkFence          in_flight[3];
} gpu_state_t;
```

## Texture Atlas Management

The GPU backend would maintain a single texture atlas containing:
- **Glyph atlas**: All ASCII glyphs rendered from PSF1 at startup, stored as 8-bit alpha maps.
- **Cursor sprites**: 16×16 cursor bitmaps expanded to RGBA.
- **Window textures**: When a window is static, its content can be cached as an atlas sub-region.

Allocation strategy: simple row-by-row allocator with a compaction pass when fragmentation exceeds a threshold.

## Command Buffer Translation

Each draw call produces a command buffer entry. These are batched and submitted once per `renderer_present()`.

```
Frame N:
  1. vkAcquireNextImageKHR(swapchain)
  2. vkBeginCommandBuffer(cmd)
  3. vkCmdSetScissor(r->clip)       ← set_clip
  4. for each draw call:
       if fill_rect:
         vkCmdPushConstants(color)
         vkCmdDraw(quad)
       elif blit:
         vkCmdBindDescriptorSet(atlas)
         vkCmdPushConstants(src_rect, dest_rect)
         vkCmdDraw(quad)
       elif draw_glyph:
         vkCmdBindDescriptorSet(atlas)
         vkCmdPushConstants(pos, fg, bg)
         vkCmdDraw(glyph_quad)
  5. vkEndCommandBuffer(cmd)
  6. vkQueueSubmit(queue, cmd, semaphores)
  7. vkQueuePresentKHR(queue, swapchain)
```

## Current Status

**This is purely hypothetical.** The codebase today has:
- Zero GPU compute or graphics code in the render path.
- `gpu.c` / `gpu.h` only handles BGA detection, PCI discovery, and MTRR write-combining setup.
- `init_gpu()` is called during boot but is unrelated to the GUI pipeline.
- All rendering goes through `fb_renderer.c` → `fb_ops`.

## Dependencies

- `renderer_ops_t` vtable already defined in `renderer.h`
- `gui_renderer_t` struct already contains `void *backend` for backend state
- No changes needed to compositor, widgets, or scene graph

## Limitations & Trade-offs

| Aspect | Consideration |
|---|---|
| Driver complexity | A Vulkan driver in a custom OS is a massive undertaking (10k+ lines). |
| Memory pressure | GPU needs dedicated VRAM or stolen memory for framebuffer. |
| Scheduling | GPU command submission must not block the compositor task. Need proper async submission. |
| Portability | Vulkan, Metal, and DX12 backends would each be thousands of lines. |

## Performance & Memory Optimizations

- **Command batching**: Multiple fill_rect calls can be merged into a single draw call using instancing.
- **Atlas packing**: Reduces texture binds per frame. All glyphs in a single descriptor set.
- **Dedicated transfer queue**: Upload window buffers on a separate queue to avoid stalling the graphics queue.
- **Shader compilation caching**: Store SPIR-V blobs in a filesystem cache.

## Future Extensions

- **Full GPU pipeline**: Transform nodes via vertex shaders, composite layers via fragment shaders.
- **Compute-based post-processing**: Bloom, blur, color grading.
- **Hardware video decode**: YUV→RGB conversion in shader for video playback.
- **Multi-GPU**: Split-screen or alternate-frame rendering.
- **Tessellation**: Deform widgets for 3D UI effects.

## Usage Example (Hypothetical)

```c
// In gui_init():
gui_renderer_t *rend = NULL;

if (gpu_backend_available()) {
    gpu_state_t *gs = gpu_state_create(1024, 768);
    renderer_ops_t gpu_ops = {
        .fill_rect   = vk_fill_rect,
        .blit        = vk_blit,
        .blit_scaled = vk_blit_scaled,
        .draw_glyph  = vk_draw_glyph,
        .set_clip    = vk_set_clip,
        .set_opacity = vk_set_opacity,
        .present     = vk_present,
        .destroy     = vk_destroy,
    };
    rend = renderer_create(&gpu_ops, gs, 1024, 768);
    serial_print("GUI: Using GPU backend\n");
}

if (!rend) {
    rend = fb_renderer_create();
    serial_print("GUI: Falling back to software backend\n");
}
```
