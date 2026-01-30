#include "lgx.h"
#include "kheap.h"
#include "string.h"

/* --- Estruturas Internas --- */

struct lg_physical_device_s {
  lg_physical_device_props_t props;
};

struct lg_instance_s {
  char app_name[64];
  uint32_t app_version;
  struct lg_physical_device_s software_renderer;
};

struct lg_device_s {
  lg_physical_device_t physical_device;
};

struct lg_device_memory_s {
  size_t size;
  void *ptr;
  bool mapped;
};

struct lg_buffer_s {
  size_t size;
  lg_buffer_usage_flags_t usage;
  lg_device_memory_t memory;
  size_t memory_offset;
};

struct lg_image_s {
  uint32_t width;
  uint32_t height;
  lg_format_t format;
  lg_image_usage_flags_t usage;
  lg_device_memory_t memory;
  size_t memory_offset;
  void *raw_ptr;
};

struct lg_swapchain_s {
  uint32_t width;
  uint32_t height;
  struct lg_image_s backbuffer_image;
};

struct lg_render_pass_s {
  uint32_t attachment_count;
};

struct lg_framebuffer_s {
  uint32_t width;
  uint32_t height;
  lg_image_t color_attachment;
  lg_image_t depth_attachment;
};

struct lg_pipeline_s {
  lg_pipeline_vertex_input_state_create_info_t vertex_input;
  lg_pipeline_rasterization_state_create_info_t rasterization;
};

struct lg_queue_s {
  lg_device_t device;
};

struct lg_command_pool_s {
  uint32_t queue_family_index;
};

typedef enum {
  CMD_COPY_BUFFER,
  CMD_FILL_BUFFER,
  CMD_COPY_BUFFER_TO_IMAGE,
  CMD_COPY_IMAGE,
  CMD_BIND_PIPELINE,
  CMD_BIND_VERTEX_BUFFER,
  CMD_DRAW,
  CMD_BEGIN_RENDER_PASS,
  CMD_END_RENDER_PASS
} cmd_type_t;

typedef struct {
  cmd_type_t type;
  union {
    struct {
      lg_buffer_t src;
      lg_buffer_t dst;
      size_t size;
    } copy;
    struct {
      lg_buffer_t dst;
      uint32_t data;
    } fill;
    struct {
      lg_buffer_t src;
      lg_image_t dst;
      int32_t x;
      int32_t y;
      uint32_t sw;
      uint32_t sh;
    } copy_to_img;
    struct {
      lg_image_t src;
      lg_image_t dst;
      int32_t x;
      int32_t y;
    } copy_img;
    struct {
      lg_pipeline_t pipeline;
    } bind_pipeline;
    struct {
      lg_buffer_t buffer;
      size_t offset;
    } bind_vb;
    struct {
      uint32_t vertex_count;
      uint32_t first_vertex;
    } draw;
    struct {
      lg_framebuffer_t framebuffer;
      uint32_t clear_color;
      float clear_depth;
    } render_pass;
  } data;
} cmd_t;

struct lg_command_buffer_s {
  cmd_t *commands;
  uint32_t capacity;
  uint32_t count;
  bool recording;

  lg_pipeline_t current_pipeline;
  lg_buffer_t current_vb;
  size_t current_vb_offset;
  lg_framebuffer_t current_fb;
};

/* --- Externs do LiwusOS Video --- */
extern uint32_t *backbuffer;
extern uint32_t screen_width;
extern uint32_t screen_height;
extern void refresh_screen();

lg_swapchain_t global_sw;
lg_device_t global_lg_device;
lg_queue_t global_lg_queue;
lg_command_pool_t global_lg_pool;

/* --- Rasterizador Interno --- */

typedef struct {
  float x, y, z;
} vec3_t;
typedef struct {
  float r, g, b, a;
} color_t;

static float edge_func(vec3_t a, vec3_t b, vec3_t c) {
  return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

static void software_rasterize_triangle(lg_framebuffer_t fb, vec3_t v0,
                                        vec3_t v1, vec3_t v2, color_t c0,
                                        color_t c1, color_t c2,
                                        bool depth_test) {
  struct lg_framebuffer_s *f = (struct lg_framebuffer_s *)fb;
  struct lg_image_s *color_img = (struct lg_image_s *)f->color_attachment;
  struct lg_image_s *depth_img = (struct lg_image_s *)f->depth_attachment;

  uint32_t *pixels =
      color_img->raw_ptr
          ? (uint32_t *)color_img->raw_ptr
          : (uint32_t *)((struct lg_device_memory_s *)color_img->memory)->ptr;
  float *depth_buffer =
      depth_img ? (float *)((struct lg_device_memory_s *)depth_img->memory)->ptr
                : NULL;

  int min_x = (int)(v0.x < v1.x ? (v0.x < v2.x ? v0.x : v2.x)
                                : (v1.x < v2.x ? v1.x : v2.x));
  int max_x = (int)(v0.x > v1.x ? (v0.x > v2.x ? v0.x : v2.x)
                                : (v1.x > v2.x ? v1.x : v2.x));
  int min_y = (int)(v0.y < v1.y ? (v0.y < v2.y ? v0.y : v2.y)
                                : (v1.y < v2.y ? v1.y : v2.y));
  int max_y = (int)(v0.y > v1.y ? (v0.y > v2.y ? v0.y : v2.y)
                                : (v1.y > v2.y ? v1.y : v2.y));

  if (min_x < 0)
    min_x = 0;
  if (max_x >= (int)color_img->width)
    max_x = color_img->width - 1;
  if (min_y < 0)
    min_y = 0;
  if (max_y >= (int)color_img->height)
    max_y = color_img->height - 1;

  float area = edge_func(v0, v1, v2);
  if (area == 0)
    return;
  float inv_area = 1.0f / area;

  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      vec3_t p = {(float)x + 0.5f, (float)y + 0.5f, 0};
      float w0 = edge_func(v1, v2, p) * inv_area;
      float w1 = edge_func(v2, v0, p) * inv_area;
      float w2 = edge_func(v0, v1, p) * inv_area;

      if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
        float z = w0 * v0.z + w1 * v1.z + w2 * v2.z;
        if (depth_test && depth_buffer) {
          if (z > depth_buffer[y * color_img->width + x])
            continue;
          depth_buffer[y * color_img->width + x] = z;
        }
        float r = w0 * c0.r + w1 * c1.r + w2 * c2.r;
        float g = w0 * c0.g + w1 * c1.g + w2 * c2.g;
        float b = w0 * c0.b + w1 * c1.b + w2 * c2.b;
        pixels[y * color_img->width + x] = ((uint8_t)(r * 255) << 16) |
                                           ((uint8_t)(g * 255) << 8) |
                                           ((uint8_t)(b * 255));
      }
    }
  }
}

/* --- Implementação da API --- */

lg_result_t lg_create_instance(const lg_instance_create_info_t *create_info,
                               lg_instance_t *instance) {
  struct lg_instance_s *new_instance =
      (struct lg_instance_s *)kmalloc(sizeof(struct lg_instance_s));
  if (create_info && create_info->app_name)
    strncpy(new_instance->app_name, create_info->app_name, 63);
  new_instance->app_version = create_info ? create_info->app_version : 0;
  strcpy(new_instance->software_renderer.props.name, "Liwus Software Renderer");
  new_instance->software_renderer.props.vendor_id = 0x1111;
  new_instance->software_renderer.props.device_id = 0x0001;
  new_instance->software_renderer.props.is_software_renderer = true;
  *instance = (lg_instance_t)new_instance;
  return LGX_SUCCESS;
}

void lg_destroy_instance(lg_instance_t instance) { (void)instance; }

lg_result_t lg_enumerate_physical_devices(lg_instance_t instance,
                                          uint32_t *count,
                                          lg_physical_device_t *devices) {
  if (!instance || !count)
    return LGX_ERROR_INITIALIZATION_FAILED;
  struct lg_instance_s *inst = (struct lg_instance_s *)instance;
  if (!devices) {
    *count = 1;
    return LGX_SUCCESS;
  }
  if (*count >= 1) {
    devices[0] = (lg_physical_device_t)&inst->software_renderer;
    *count = 1;
    return LGX_SUCCESS;
  }
  return LGX_ERROR_INITIALIZATION_FAILED;
}

void lg_get_physical_device_props(lg_physical_device_t physical_device,
                                  lg_physical_device_props_t *props) {
  if (physical_device && props)
    *props = ((struct lg_physical_device_s *)physical_device)->props;
}

lg_result_t lg_create_device(lg_physical_device_t physical_device,
                             lg_device_t *device) {
  struct lg_device_s *new_device =
      (struct lg_device_s *)kmalloc(sizeof(struct lg_device_s));
  new_device->physical_device = physical_device;
  *device = (lg_device_t)new_device;
  return LGX_SUCCESS;
}

void lg_destroy_device(lg_device_t device) { (void)device; }

void lg_get_device_queue(lg_device_t device, uint32_t queue_family_index,
                         uint32_t queue_index, lg_queue_t *queue) {
  (void)queue_family_index;
  (void)queue_index;
  struct lg_queue_s *new_queue =
      (struct lg_queue_s *)kmalloc(sizeof(struct lg_queue_s));
  new_queue->device = device;
  *queue = (lg_queue_t)new_queue;
}

lg_result_t lg_allocate_memory(lg_device_t device,
                               const lg_memory_allocate_info_t *alloc_info,
                               lg_device_memory_t *memory) {
  (void)device;
  struct lg_device_memory_s *new_mem =
      (struct lg_device_memory_s *)kmalloc(sizeof(struct lg_device_memory_s));
  new_mem->size = alloc_info->size;
  new_mem->ptr = kmalloc_a(alloc_info->size);
  new_mem->mapped = false;
  *memory = (lg_device_memory_t)new_mem;
  return LGX_SUCCESS;
}

void lg_free_memory(lg_device_t device, lg_device_memory_t memory) {
  (void)device;
  (void)memory;
}

lg_result_t lg_map_memory(lg_device_t device, lg_device_memory_t memory,
                          size_t offset, size_t size, void **data) {
  (void)device;
  (void)size;
  *data =
      (void *)((uintptr_t)((struct lg_device_memory_s *)memory)->ptr + offset);
  ((struct lg_device_memory_s *)memory)->mapped = true;
  return LGX_SUCCESS;
}

void lg_unmap_memory(lg_device_t device, lg_device_memory_t memory) {
  (void)device;
  if (memory)
    ((struct lg_device_memory_s *)memory)->mapped = false;
}

lg_result_t lg_create_buffer(lg_device_t device,
                             const lg_buffer_create_info_t *create_info,
                             lg_buffer_t *buffer) {
  (void)device;
  struct lg_buffer_s *buf =
      (struct lg_buffer_s *)kmalloc(sizeof(struct lg_buffer_s));
  buf->size = create_info->size;
  buf->usage = create_info->usage;
  buf->memory = NULL;
  *buffer = (lg_buffer_t)buf;
  return LGX_SUCCESS;
}

void lg_destroy_buffer(lg_device_t device, lg_buffer_t buffer) {
  (void)device;
  (void)buffer;
}

void lg_get_buffer_memory_requirements(lg_device_t device, lg_buffer_t buffer,
                                       lg_memory_requirements_t *requirements) {
  (void)device;
  requirements->size = ((struct lg_buffer_s *)buffer)->size;
  requirements->alignment = 16;
  requirements->memory_type_bits = 0xFFFFFFFF;
}

lg_result_t lg_bind_buffer_memory(lg_device_t device, lg_buffer_t buffer,
                                  lg_device_memory_t memory, size_t offset) {
  (void)device;
  struct lg_buffer_s *buf = (struct lg_buffer_s *)buffer;
  buf->memory = memory;
  buf->memory_offset = offset;
  return LGX_SUCCESS;
}

lg_result_t lg_create_image(lg_device_t device,
                            const lg_image_create_info_t *create_info,
                            lg_image_t *image) {
  (void)device;
  struct lg_image_s *img =
      (struct lg_image_s *)kmalloc(sizeof(struct lg_image_s));
  img->width = create_info->width;
  img->height = create_info->height;
  img->format = create_info->format;
  img->usage = create_info->usage;
  img->memory = NULL;
  img->raw_ptr = NULL;
  *image = (lg_image_t)img;
  return LGX_SUCCESS;
}

void lg_destroy_image(lg_device_t device, lg_image_t image) {
  (void)device;
  (void)image;
}

void lg_get_image_memory_requirements(lg_device_t device, lg_image_t image,
                                      lg_memory_requirements_t *requirements) {
  (void)device;
  struct lg_image_s *img = (struct lg_image_s *)image;
  requirements->size = img->width * img->height * 4;
  requirements->alignment = 64;
  requirements->memory_type_bits = 0xFFFFFFFF;
}

lg_result_t lg_bind_image_memory(lg_device_t device, lg_image_t image,
                                 lg_device_memory_t memory, size_t offset) {
  (void)device;
  struct lg_image_s *img = (struct lg_image_s *)image;
  img->memory = memory;
  img->memory_offset = offset;
  return LGX_SUCCESS;
}

void lg_set_image_raw_ptr(lg_image_t image, void *ptr) {
  if (image)
    ((struct lg_image_s *)image)->raw_ptr = ptr;
}

lg_result_t
lg_create_render_pass(lg_device_t device,
                      const lg_render_pass_create_info_t *create_info,
                      lg_render_pass_t *render_pass) {
  (void)device;
  struct lg_render_pass_s *rp =
      (struct lg_render_pass_s *)kmalloc(sizeof(struct lg_render_pass_s));
  rp->attachment_count = create_info->attachment_count;
  *render_pass = (lg_render_pass_t)rp;
  return LGX_SUCCESS;
}

lg_result_t
lg_create_framebuffer(lg_device_t device,
                      const lg_framebuffer_create_info_t *create_info,
                      lg_framebuffer_t *framebuffer) {
  (void)device;
  struct lg_framebuffer_s *fb =
      (struct lg_framebuffer_s *)kmalloc(sizeof(struct lg_framebuffer_s));
  fb->width = create_info->width;
  fb->height = create_info->height;
  fb->color_attachment = create_info->attachments[0];
  fb->depth_attachment =
      (create_info->attachment_count > 1) ? create_info->attachments[1] : NULL;
  *framebuffer = (lg_framebuffer_t)fb;
  return LGX_SUCCESS;
}

lg_result_t lg_create_graphics_pipelines(
    lg_device_t device, uint32_t create_info_count,
    const lg_graphics_pipeline_create_info_t *create_infos,
    lg_pipeline_t *pipelines) {
  (void)device;
  for (uint32_t i = 0; i < create_info_count; i++) {
    struct lg_pipeline_s *p =
        (struct lg_pipeline_s *)kmalloc(sizeof(struct lg_pipeline_s));
    p->vertex_input = create_infos[i].vertex_input_state;
    p->rasterization = create_infos[i].rasterization_state;
    pipelines[i] = (lg_pipeline_t)p;
  }
  return LGX_SUCCESS;
}

void lg_destroy_pipeline(lg_device_t device, lg_pipeline_t pipeline) {
  (void)device;
  (void)pipeline;
}

lg_result_t
lg_create_command_pool(lg_device_t device,
                       const lg_command_pool_create_info_t *create_info,
                       lg_command_pool_t *command_pool) {
  (void)device;
  struct lg_command_pool_s *pool =
      (struct lg_command_pool_s *)kmalloc(sizeof(struct lg_command_pool_s));
  pool->queue_family_index = create_info->queue_family_index;
  *command_pool = (lg_command_pool_t)pool;
  return LGX_SUCCESS;
}

void lg_destroy_command_pool(lg_device_t device,
                             lg_command_pool_t command_pool) {
  (void)device;
  (void)command_pool;
}

lg_result_t lg_allocate_command_buffers(
    lg_device_t device, const lg_command_buffer_allocate_info_t *allocate_info,
    lg_command_buffer_t *command_buffers) {
  (void)device;
  for (uint32_t i = 0; i < allocate_info->count; i++) {
    struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)kmalloc(
        sizeof(struct lg_command_buffer_s));
    cb->capacity = 128;
    cb->commands = (cmd_t *)kmalloc(sizeof(cmd_t) * cb->capacity);
    cb->count = 0;
    cb->recording = false;
    command_buffers[i] = (lg_command_buffer_t)cb;
  }
  return LGX_SUCCESS;
}

lg_result_t
lg_begin_command_buffer(lg_command_buffer_t command_buffer,
                        const lg_command_buffer_begin_info_t *begin_info) {
  (void)begin_info;
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  cb->count = 0;
  cb->recording = true;
  return LGX_SUCCESS;
}

lg_result_t lg_end_command_buffer(lg_command_buffer_t command_buffer) {
  ((struct lg_command_buffer_s *)command_buffer)->recording = false;
  return LGX_SUCCESS;
}

void lg_cmd_copy_buffer(lg_command_buffer_t command_buffer, lg_buffer_t src,
                        lg_buffer_t dst, size_t size) {
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_COPY_BUFFER;
    cb->commands[cb->count].data.copy.src = src;
    cb->commands[cb->count].data.copy.dst = dst;
    cb->commands[cb->count].data.copy.size = size;
    cb->count++;
  }
}

void lg_cmd_fill_buffer(lg_command_buffer_t command_buffer, lg_buffer_t dst,
                        uint32_t data) {
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_FILL_BUFFER;
    cb->commands[cb->count].data.fill.dst = dst;
    cb->commands[cb->count].data.fill.data = data;
    cb->count++;
  }
}

void lg_cmd_copy_buffer_to_image(lg_command_buffer_t command_buffer,
                                 lg_buffer_t src, lg_image_t dst, int32_t dst_x,
                                 int32_t dst_y, uint32_t src_w,
                                 uint32_t src_h) {
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_COPY_BUFFER_TO_IMAGE;
    cb->commands[cb->count].data.copy_to_img.src = src;
    cb->commands[cb->count].data.copy_to_img.dst = dst;
    cb->commands[cb->count].data.copy_to_img.x = dst_x;
    cb->commands[cb->count].data.copy_to_img.y = dst_y;
    cb->commands[cb->count].data.copy_to_img.sw = src_w;
    cb->commands[cb->count].data.copy_to_img.sh = src_h;
    cb->count++;
  }
}

void lg_cmd_copy_image(lg_command_buffer_t command_buffer, lg_image_t src,
                       lg_image_t dst, int32_t dst_x, int32_t dst_y) {
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_COPY_IMAGE;
    cb->commands[cb->count].data.copy_img.src = src;
    cb->commands[cb->count].data.copy_img.dst = dst;
    cb->commands[cb->count].data.copy_img.x = dst_x;
    cb->commands[cb->count].data.copy_img.y = dst_y;
    cb->count++;
  }
}

void lg_cmd_begin_render_pass(lg_command_buffer_t command_buffer,
                              const lg_render_pass_begin_info_t *begin_info) {
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_BEGIN_RENDER_PASS;
    cb->commands[cb->count].data.render_pass.framebuffer =
        begin_info->framebuffer;
    cb->commands[cb->count].data.render_pass.clear_color =
        begin_info->clear_color;
    cb->commands[cb->count].data.render_pass.clear_depth =
        begin_info->clear_depth;
    cb->count++;
  }
}

void lg_cmd_end_render_pass(lg_command_buffer_t command_buffer) {
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_END_RENDER_PASS;
    cb->count++;
  }
}

void lg_cmd_bind_pipeline(lg_command_buffer_t command_buffer,
                          lg_pipeline_t pipeline) {
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_BIND_PIPELINE;
    cb->commands[cb->count].data.bind_pipeline.pipeline = pipeline;
    cb->count++;
  }
}

void lg_cmd_bind_vertex_buffers(lg_command_buffer_t command_buffer,
                                uint32_t first_binding, uint32_t binding_count,
                                const lg_buffer_t *buffers,
                                const size_t *offsets) {
  (void)first_binding;
  (void)binding_count;
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_BIND_VERTEX_BUFFER;
    cb->commands[cb->count].data.bind_vb.buffer = buffers[0];
    cb->commands[cb->count].data.bind_vb.offset = offsets[0];
    cb->count++;
  }
}

void lg_cmd_draw(lg_command_buffer_t command_buffer, uint32_t vertex_count,
                 uint32_t instance_count, uint32_t first_vertex,
                 uint32_t first_instance) {
  (void)instance_count;
  (void)first_instance;
  struct lg_command_buffer_s *cb = (struct lg_command_buffer_s *)command_buffer;
  if (cb->count < cb->capacity) {
    cb->commands[cb->count].type = CMD_DRAW;
    cb->commands[cb->count].data.draw.vertex_count = vertex_count;
    cb->commands[cb->count].data.draw.first_vertex = first_vertex;
    cb->count++;
  }
}

lg_result_t lg_queue_submit(lg_queue_t queue, uint32_t submit_count,
                            const lg_submit_info_t *submits) {
  (void)queue;
  if (!global_sw)
    return LGX_ERROR_INITIALIZATION_FAILED;
  for (uint32_t s = 0; s < submit_count; s++) {
    for (uint32_t b = 0; b < submits[s].command_buffer_count; b++) {
      struct lg_command_buffer_s *cb =
          (struct lg_command_buffer_s *)submits[s].command_buffers[b];
      for (uint32_t c = 0; c < cb->count; c++) {
        cmd_t *cmd = &cb->commands[c];
        if (cmd->type == CMD_COPY_BUFFER) {
          struct lg_buffer_s *src = (struct lg_buffer_s *)cmd->data.copy.src;
          struct lg_buffer_s *dst = (struct lg_buffer_s *)cmd->data.copy.dst;
          memcpy((void *)((uintptr_t)((struct lg_device_memory_s *)dst->memory)
                              ->ptr +
                          dst->memory_offset),
                 (void *)((uintptr_t)((struct lg_device_memory_s *)src->memory)
                              ->ptr +
                          src->memory_offset),
                 cmd->data.copy.size);
        } else if (cmd->type == CMD_FILL_BUFFER) {
          struct lg_buffer_s *dst = (struct lg_buffer_s *)cmd->data.fill.dst;
          uint32_t *ptr =
              (uint32_t *)((uintptr_t)((struct lg_device_memory_s *)dst->memory)
                               ->ptr +
                           dst->memory_offset);
          for (size_t i = 0; i < dst->size / 4; i++)
            ptr[i] = cmd->data.fill.data;
        } else if (cmd->type == CMD_COPY_BUFFER_TO_IMAGE) {
          struct lg_buffer_s *src =
              (struct lg_buffer_s *)cmd->data.copy_to_img.src;
          struct lg_image_s *dst =
              (struct lg_image_s *)cmd->data.copy_to_img.dst;
          int32_t dx = cmd->data.copy_to_img.x;
          int32_t dy = cmd->data.copy_to_img.y;
          uint32_t sw = cmd->data.copy_to_img.sw;
          uint32_t sh = cmd->data.copy_to_img.sh;
          uint32_t *src_ptr =
              (uint32_t *)((uintptr_t)((struct lg_device_memory_s *)src->memory)
                               ->ptr +
                           src->memory_offset);
          uint32_t *dst_ptr =
              dst->raw_ptr
                  ? (uint32_t *)dst->raw_ptr
                  : (uint32_t *)((uintptr_t)((struct lg_device_memory_s *)
                                                 dst->memory)
                                     ->ptr +
                                 dst->memory_offset);
          uint32_t dw = dst->width;
          uint32_t dh = dst->height;
          uint32_t start_y = (dy < 0) ? -dy : 0;
          uint32_t end_y =
              (dy + (int32_t)sh > (int32_t)dh) ? (uint32_t)(dh - dy) : sh;
          int32_t start_x = (dx < 0) ? -dx : 0;
          int32_t end_x = (dx + (int32_t)sw > (int32_t)dw) ? (int32_t)(dw - dx)
                                                           : (int32_t)sw;
          if (end_x > start_x && end_y > start_y) {
            size_t row_size = (end_x - start_x) * 4;
            for (uint32_t y = start_y; y < end_y; y++)
              memcpy(&dst_ptr[(dy + y) * dw + (dx + start_x)],
                     &src_ptr[y * sw + start_x], row_size);
          }
        } else if (cmd->type == CMD_COPY_IMAGE) {
          struct lg_image_s *src = (struct lg_image_s *)cmd->data.copy_img.src;
          struct lg_image_s *dst = (struct lg_image_s *)cmd->data.copy_img.dst;
          int32_t dx = cmd->data.copy_img.x;
          int32_t dy = cmd->data.copy_img.y;
          uint32_t *src_ptr =
              src->raw_ptr
                  ? (uint32_t *)src->raw_ptr
                  : (uint32_t *)((struct lg_device_memory_s *)src->memory)->ptr;
          uint32_t *dst_ptr =
              dst->raw_ptr
                  ? (uint32_t *)dst->raw_ptr
                  : (uint32_t *)((uintptr_t)((struct lg_device_memory_s *)
                                                 dst->memory)
                                     ->ptr +
                                 dst->memory_offset);
          uint32_t sw = src->width;
          uint32_t sh = src->height;
          uint32_t dw = dst->width;
          uint32_t dh = dst->height;
          uint32_t start_y = (dy < 0) ? -dy : 0;
          uint32_t end_y =
              (dy + (int32_t)sh > (int32_t)dh) ? (uint32_t)(dh - dy) : sh;
          int32_t start_x = (dx < 0) ? -dx : 0;
          int32_t end_x = (dx + (int32_t)sw > (int32_t)dw) ? (int32_t)(dw - dx)
                                                           : (int32_t)sw;
          if (end_x > start_x && end_y > start_y) {
            uint32_t line_len = end_x - start_x;
            for (uint32_t y = start_y; y < end_y; y++) {
              uint32_t *d_line = &dst_ptr[(dy + y) * dw + (dx + start_x)];
              uint32_t *s_line = &src_ptr[y * sw + start_x];

              // Optimization: Check for transparency?
              // For now, assume most copies are opaque or we want speed.
              // If we use memcpy, we lose chroma key (0 is transparency).
              // But usually surfaces are opaque rectangles.
              // Let's use memcpy for SPEED as requested ("Throw to GPU").
              // If user needs transparency, we might break it, but performance
              // first. Or: Check the first pixel? No.

              // HYBRID APPROACH:
              // Try memcpy. If it breaks "transparent terminal background" (if
              // any), revert. Terminal is black background? Chroma key 0 makes
              // black transparent. If terminal back is 0, it becomes
              // transparent. If we use memcpy, black becomes black (opaque).
              // This actually fixes "Black is transparent" artifacts often
              // found in VESA.

              // ENABLE FAST PATH (Memcpy)
              memcpy(d_line, s_line, line_len * 4);

              /* Slow Path (Chroma Key) - CPU Heavy
              for (uint32_t x = 0; x < line_len; x++) {
                  uint32_t p = s_line[x];
                  if (p != 0) d_line[x] = p;
              }
              */
            }
          }
        } else if (cmd->type == CMD_BEGIN_RENDER_PASS) {
          cb->current_fb = cmd->data.render_pass.framebuffer;
          struct lg_framebuffer_s *fb =
              (struct lg_framebuffer_s *)cb->current_fb;
          struct lg_image_s *color = (struct lg_image_s *)fb->color_attachment;
          uint32_t *cptr =
              color->raw_ptr
                  ? (uint32_t *)color->raw_ptr
                  : (uint32_t *)((struct lg_device_memory_s *)color->memory)
                        ->ptr;
          uint32_t count = fb->width * fb->height;
          for (uint32_t i = 0; i < count; i++)
            cptr[i] = cmd->data.render_pass.clear_color;
          if (fb->depth_attachment) {
            struct lg_image_s *depth =
                (struct lg_image_s *)fb->depth_attachment;
            float *dptr =
                (float *)((struct lg_device_memory_s *)depth->memory)->ptr;
            float val = cmd->data.render_pass.clear_depth;
            for (uint32_t i = 0; i < count; i++)
              dptr[i] = val;
          }
        } else if (cmd->type == CMD_END_RENDER_PASS) {
          cb->current_fb = NULL;
        } else if (cmd->type == CMD_BIND_PIPELINE) {
          cb->current_pipeline = cmd->data.bind_pipeline.pipeline;
        } else if (cmd->type == CMD_BIND_VERTEX_BUFFER) {
          cb->current_vb = cmd->data.bind_vb.buffer;
          cb->current_vb_offset = cmd->data.bind_vb.offset;
        } else if (cmd->type == CMD_DRAW) {
          if (cb->current_vb && cb->current_pipeline && cb->current_fb) {
            struct lg_buffer_s *vb = (struct lg_buffer_s *)cb->current_vb;
            float *vertices =
                (float *)((uintptr_t)((struct lg_device_memory_s *)vb->memory)
                              ->ptr +
                          vb->memory_offset);
            bool dtest = ((struct lg_pipeline_s *)cb->current_pipeline)
                             ->rasterization.depth_test_enable;
            for (uint32_t i = 0; i < cmd->data.draw.vertex_count; i += 3) {
              float *v0p = &vertices[(i + 0) * 7];
              float *v1p = &vertices[(i + 1) * 7];
              float *v2p = &vertices[(i + 2) * 7];
              vec3_t v0 = {v0p[0], v0p[1], v0p[2]};
              vec3_t v1 = {v1p[0], v1p[1], v1p[2]};
              vec3_t v2 = {v2p[0], v2p[1], v2p[2]};
              color_t c0 = {v0p[3], v0p[4], v0p[5], v0p[6]};
              color_t c1 = {v1p[3], v1p[4], v1p[5], v1p[6]};
              color_t c2 = {v2p[3], v2p[4], v2p[5], v2p[6]};
              software_rasterize_triangle(cb->current_fb, v0, v1, v2, c0, c1,
                                          c2, dtest);
            }
          }
        }
      }
    }
  }
  return LGX_SUCCESS;
}

lg_result_t lg_queue_wait_idle(lg_queue_t queue) {
  (void)queue;
  return LGX_SUCCESS;
}

lg_result_t lg_create_swapchain(lg_device_t device,
                                const lg_swapchain_create_info_t *create_info,
                                lg_swapchain_t *swapchain) {
  (void)device;
  (void)create_info;
  struct lg_swapchain_s *sw =
      (struct lg_swapchain_s *)kmalloc(sizeof(struct lg_swapchain_s));
  sw->width = screen_width;
  sw->height = screen_height;
  sw->backbuffer_image.width = sw->width;
  sw->backbuffer_image.height = sw->height;
  sw->backbuffer_image.format = LGX_FORMAT_B8G8R8A8_UNORM;
  sw->backbuffer_image.usage =
      LGX_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | LGX_IMAGE_USAGE_TRANSFER_DST_BIT;
  sw->backbuffer_image.memory = NULL;
  sw->backbuffer_image.raw_ptr = backbuffer;
  *swapchain = (lg_swapchain_t)sw;
  return LGX_SUCCESS;
}

void lg_destroy_swapchain(lg_device_t device, lg_swapchain_t swapchain) {
  (void)device;
  (void)swapchain;
}

lg_image_t lg_get_swapchain_image(lg_swapchain_t swapchain, uint32_t index) {
  (void)index;
  return (lg_image_t) & ((struct lg_swapchain_s *)swapchain)->backbuffer_image;
}

lg_result_t lg_queue_present(lg_queue_t queue, lg_swapchain_t swapchain,
                             uint32_t image_index) {
  (void)queue;
  (void)swapchain;
  (void)image_index;
  refresh_screen();
  return LGX_SUCCESS;
}