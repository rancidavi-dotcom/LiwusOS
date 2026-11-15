#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "GL/glew.h"
#include "platform.h"
#include "softgl.h"

#define MAX_BUFFERS 8192
#define MAX_TEXTURES 8
#define MAX_SHADERS 16
#define MAX_PROGRAMS 8
#define PI_F 3.14159265358979323846f

typedef struct { unsigned char *rgba; int w, h, linear, clamp; } SoftTexture;
typedef struct { void *data; size_t bytes; } SoftBuffer;
typedef struct { GLenum type; const char *source; } SoftShader;
typedef struct {
    GLuint vs, fs;
    float matrix[16], camera[3], timer, daylight, fog_distance;
    int ortho, sampler, sky_sampler, is_sign;
} SoftProgram;
typedef struct { GLint size, stride; size_t offset; int enabled; GLuint buffer; } SoftAttrib;
typedef struct {
    float x, y, z, iw;
    float u, v, ao, light, diffuse, fog, fog_height;
} SoftVertex;

static uint32_t *color_buffer;
static float *depth_buffer;
static int screen_w, screen_h;
static GLuint next_buffer = 1, next_texture = 1, next_shader = 1, next_program = 1;
static GLuint bound_buffer, current_program, active_texture;
static GLuint bound_textures[4];
static SoftBuffer buffers[MAX_BUFFERS];
static SoftTexture textures[MAX_TEXTURES];
static SoftShader shaders[MAX_SHADERS];
static SoftProgram programs[MAX_PROGRAMS];
static SoftAttrib attributes[8];
static GLint viewport_x, viewport_y, viewport_w, viewport_h;
static GLint scissor_x, scissor_y, scissor_w, scissor_h;
static uint32_t clear_color;
static int depth_test = 1, cull_face, blend, logic_invert, scissor_test;
static float line_width = 1.0f, polygon_offset;

static SoftProgram *program(void) {
    return current_program < MAX_PROGRAMS ? &programs[current_program] : &programs[0];
}
static SoftTexture *sample_texture(int unit) {
    GLuint id = (unit >= 0 && unit < 4) ? bound_textures[unit] : 0;
    return id < MAX_TEXTURES ? &textures[id] : &textures[0];
}
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static uint32_t rgb(float r, float g, float b) {
    unsigned ri = (unsigned)(clamp01(r) * 255.0f + 0.5f);
    unsigned gi = (unsigned)(clamp01(g) * 255.0f + 0.5f);
    unsigned bi = (unsigned)(clamp01(b) * 255.0f + 0.5f);
    return 0xFF000000U | (ri << 16) | (gi << 8) | bi;
}

int craft_gl_init(int width, int height) {
    if (width <= 0 || height <= 0 || width > 1920 || height > 1080) return -1;
    color_buffer = calloc((size_t)width * height, sizeof(uint32_t));
    depth_buffer = malloc((size_t)width * height * sizeof(float));
    if (!color_buffer || !depth_buffer) { craft_gl_shutdown(); return -1; }
    screen_w = width; screen_h = height;
    viewport_w = width; viewport_h = height;
    scissor_w = width; scissor_h = height;
    clear_color = 0;
    for (size_t i = 0; i < (size_t)width * height; ++i) depth_buffer[i] = 1.0f;
    return 0;
}
int craft_gl_resize(int width, int height) {
    if (width == screen_w && height == screen_h) return 0;
    if (width <= 0 || height <= 0 || width > 1920 || height > 1080) return -1;
    uint32_t *new_color = calloc((size_t)width * height, sizeof(uint32_t));
    float *new_depth = malloc((size_t)width * height * sizeof(float));
    if (!new_color || !new_depth) { free(new_color); free(new_depth); return -1; }
    for (size_t i = 0; i < (size_t)width * height; ++i) new_depth[i] = 1.0f;
    free(color_buffer); free(depth_buffer);
    color_buffer = new_color; depth_buffer = new_depth;
    screen_w = width; screen_h = height;
    viewport_x = viewport_y = 0;
    viewport_w = scissor_w = width;
    viewport_h = scissor_h = height;
    return 0;
}
void craft_gl_shutdown(void) {
    free(color_buffer); free(depth_buffer);
    color_buffer = NULL; depth_buffer = NULL; screen_w = screen_h = 0;
}
void craft_gl_present(void) {
    if (color_buffer) craft_platform_present(color_buffer, screen_w, screen_h);
}

GLenum glewInit(void) { return GLEW_OK; }
void glGenBuffers(GLsizei n, GLuint *out) {
    for (int i = 0; i < n; ++i) {
        if (next_buffer >= MAX_BUFFERS) { out[i] = 0; continue; }
        out[i] = next_buffer++;
    }
}
void glBindBuffer(GLenum target, GLuint id) { (void)target; bound_buffer = id; }
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage) {
    (void)target; (void)usage;
    if (!bound_buffer || bound_buffer >= MAX_BUFFERS || size < 0) return;
    SoftBuffer *b = &buffers[bound_buffer];
    free(b->data); b->data = NULL; b->bytes = 0;
    if (!size) return;
    b->data = malloc((size_t)size);
    if (!b->data) return;
    b->bytes = (size_t)size;
    if (data) memcpy(b->data, data, b->bytes);
}
void glDeleteBuffers(GLsizei n, const GLuint *ids) {
    for (int i = 0; i < n; ++i) if (ids[i] && ids[i] < MAX_BUFFERS) {
        free(buffers[ids[i]].data); buffers[ids[i]].data = NULL; buffers[ids[i]].bytes = 0;
    }
}
void glEnableVertexAttribArray(GLuint i) { if (i < 8) attributes[i].enabled = 1; }
void glDisableVertexAttribArray(GLuint i) { if (i < 8) attributes[i].enabled = 0; }
void glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const GLvoid *offset) {
    (void)type; (void)normalized;
    if (i >= 8) return;
    attributes[i].size = size; attributes[i].stride = stride;
    attributes[i].offset = (size_t)offset; attributes[i].buffer = bound_buffer;
}

void glEnable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) depth_test = 1;
    else if (cap == GL_CULL_FACE) cull_face = 1;
    else if (cap == GL_BLEND) blend = 1;
    else if (cap == GL_COLOR_LOGIC_OP) logic_invert = 1;
    else if (cap == GL_SCISSOR_TEST) scissor_test = 1;
}
void glDisable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) depth_test = 0;
    else if (cap == GL_CULL_FACE) cull_face = 0;
    else if (cap == GL_BLEND) blend = 0;
    else if (cap == GL_COLOR_LOGIC_OP) logic_invert = 0;
    else if (cap == GL_SCISSOR_TEST) scissor_test = 0;
}
void glBlendFunc(GLenum src, GLenum dst) { (void)src; (void)dst; }
void glPolygonOffset(GLfloat factor, GLfloat units) { polygon_offset = factor * 0.000001f + units * 0.000000001f; }
void glLineWidth(GLfloat width) { line_width = width < 1 ? 1 : width > 8 ? 8 : width; }
void glLogicOp(GLenum op) { (void)op; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    viewport_x = x; viewport_y = y; viewport_w = w; viewport_h = h;
}
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    scissor_x = x; scissor_y = screen_h - (y + h); scissor_w = w; scissor_h = h;
}
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    (void)a; clear_color = rgb(r, g, b);
}
void glClear(GLbitfield mask) {
    if (!color_buffer) return;
    size_t n = (size_t)screen_w * screen_h;
    if (mask & GL_COLOR_BUFFER_BIT) for (size_t i = 0; i < n; ++i) color_buffer[i] = clear_color;
    if (mask & GL_DEPTH_BUFFER_BIT) for (size_t i = 0; i < n; ++i) depth_buffer[i] = 1.0f;
}

void glGenTextures(GLsizei n, GLuint *out) {
    for (int i = 0; i < n; ++i) out[i] = next_texture < MAX_TEXTURES ? next_texture++ : 0;
}
void glActiveTexture(GLenum unit) {
    active_texture = unit >= GL_TEXTURE0 && unit <= GL_TEXTURE3 ? unit - GL_TEXTURE0 : 0;
}
void glBindTexture(GLenum target, GLuint id) {
    (void)target; if (active_texture < 4) bound_textures[active_texture] = id;
}
void glTexParameteri(GLenum target, GLenum pname, GLint value) {
    (void)target;
    SoftTexture *t = sample_texture((int)active_texture);
    if (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER) t->linear = value == GL_LINEAR;
    if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) t->clamp = value == GL_CLAMP_TO_EDGE;
}
void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)internal; (void)border; (void)format; (void)type;
    SoftTexture *t = sample_texture((int)active_texture);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || !pixels) return;
    size_t bytes = (size_t)w * h * 4;
    unsigned char *copy = malloc(bytes);
    if (!copy) return;
    memcpy(copy, pixels, bytes); free(t->rgba); t->rgba = copy; t->w = w; t->h = h;
}

GLuint glCreateShader(GLenum type) {
    if (next_shader >= MAX_SHADERS) return 0;
    GLuint id = next_shader++; shaders[id].type = type; return id;
}
void glShaderSource(GLuint id, GLsizei n, const GLchar *const *src, const GLint *lengths) {
    if (!id || id >= MAX_SHADERS || !src || n < 1) return;
    (void)lengths; shaders[id].source = src[0];
}
void glCompileShader(GLuint id) { (void)id; }
void glGetShaderiv(GLuint id, GLenum pname, GLint *value) { (void)id; if (value) *value = pname == GL_INFO_LOG_LENGTH ? 1 : GL_TRUE; }
void glGetShaderInfoLog(GLuint id, GLsizei max, GLsizei *length, GLchar *info) {
    (void)id; if (length) *length = 0; if (max > 0 && info) *info = 0;
}
GLuint glCreateProgram(void) {
    if (next_program >= MAX_PROGRAMS) return 0;
    return next_program++;
}
void glAttachShader(GLuint id, GLuint shader) {
    if (!id || id >= MAX_PROGRAMS || !shader || shader >= MAX_SHADERS) return;
    if (shaders[shader].type == GL_VERTEX_SHADER) programs[id].vs = shader;
    else if (shaders[shader].type == GL_FRAGMENT_SHADER) programs[id].fs = shader;
}
void glLinkProgram(GLuint id) { (void)id; }
void glGetProgramiv(GLuint id, GLenum pname, GLint *value) { (void)id; if (value) *value = pname == GL_INFO_LOG_LENGTH ? 1 : GL_TRUE; }
void glGetProgramInfoLog(GLuint id, GLsizei max, GLsizei *length, GLchar *info) {
    (void)id; if (length) *length = 0; if (max > 0 && info) *info = 0;
}
void glDetachShader(GLuint id, GLuint shader) { (void)id; (void)shader; }
void glDeleteShader(GLuint id) { (void)id; }
void glUseProgram(GLuint id) { current_program = id < MAX_PROGRAMS ? id : 0; }
GLint glGetAttribLocation(GLuint id, const GLchar *name) {
    (void)id;
    if (!strcmp(name, "position")) return 0;
    if (!strcmp(name, "normal")) return 1;
    if (!strcmp(name, "uv")) return 2;
    return -1;
}
GLint glGetUniformLocation(GLuint id, const GLchar *name) {
    (void)id;
    if (!strcmp(name, "matrix")) return 3;
    if (!strcmp(name, "sampler")) return 4;
    if (!strcmp(name, "camera")) return 5;
    if (!strcmp(name, "timer")) return 6;
    if (!strcmp(name, "sky_sampler") || !strcmp(name, "is_sign")) return 7;
    if (!strcmp(name, "daylight")) return 8;
    if (!strcmp(name, "fog_distance")) return 9;
    if (!strcmp(name, "ortho")) return 10;
    return -1;
}
void glUniformMatrix4fv(GLint loc, GLsizei n, GLboolean transpose, const GLfloat *v) {
    (void)n; (void)transpose; if (loc == 3 && v) memcpy(program()->matrix, v, 16 * sizeof(float));
}
void glUniform1i(GLint loc, GLint v) {
    if (loc == 4) program()->sampler = v;
    else if (loc == 7) {
        if (current_program == 3) program()->is_sign = v;
        else program()->sky_sampler = v;
    }
    else if (loc == 10) program()->ortho = v;
}
void glUniform1f(GLint loc, GLfloat v) {
    if (loc == 6) program()->timer = v;
    else if (loc == 8) program()->daylight = v;
    else if (loc == 9) program()->fog_distance = v;
}
void glUniform3f(GLint loc, GLfloat x, GLfloat y, GLfloat z) {
    if (loc == 5) { program()->camera[0] = x; program()->camera[1] = y; program()->camera[2] = z; }
}

static int attr_read(GLuint index, GLint vertex, float *out, int expected) {
    if (index >= 8) return 0;
    SoftAttrib *a = &attributes[index];
    if (!a->enabled || !a->buffer || a->buffer >= MAX_BUFFERS || !buffers[a->buffer].data) return 0;
    SoftBuffer *b = &buffers[a->buffer];
    size_t stride = a->stride ? (size_t)a->stride : (size_t)a->size * sizeof(float);
    size_t offset = a->offset + (size_t)vertex * stride;
    if (offset + (size_t)a->size * sizeof(float) > b->bytes) return 0;
    const float *src = (const float *)((const unsigned char *)b->data + offset);
    for (int i = 0; i < expected; ++i) out[i] = i < a->size ? src[i] : (i == 3 ? 1.0f : 0.0f);
    return 1;
}

static int transform_vertex(int vertex, SoftVertex *out, int kind) {
    float pos[4] = {0,0,0,1}, normal[4] = {0,1,0,0}, uv[4] = {0,0,0,1};
    if (!attr_read(0, vertex, pos, 4)) return 0;
    (void)attr_read(1, vertex, normal, 3);
    (void)attr_read(2, vertex, uv, 4);
    SoftProgram *p = program();
    float clip[4];
    for (int i=0;i<4;i++) clip[i]=p->matrix[i]*pos[0]+p->matrix[4+i]*pos[1]+p->matrix[8+i]*pos[2]+p->matrix[12+i]*pos[3];
    if (clip[3] <= 0.0001f) return 0;
    float iw = 1.0f/clip[3], nx=clip[0]*iw, ny=clip[1]*iw, nz=clip[2]*iw;
    out->x = viewport_x+(nx*0.5f+0.5f)*viewport_w;
    out->y = screen_h-(viewport_y+(ny*0.5f+0.5f)*viewport_h);
    out->z = clamp01(nz*0.5f+0.5f)+polygon_offset;
    out->iw = iw;
    out->u=uv[0]; out->v=uv[1]; out->ao=0; out->light=0; out->diffuse=0; out->fog=0; out->fog_height=0;
    if (kind == 1) {
        out->ao=0.3f+(1.0f-uv[2])*0.7f; out->light=uv[3];
        float nd=sqrtf(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        float diffuse=(normal[0]*-0.57735027f+normal[1]*0.57735027f-normal[2]*0.57735027f)/(nd>0?nd:1);
        out->diffuse=diffuse>0?diffuse:0;
        if (!p->ortho && p->fog_distance>0) {
            float dx=pos[0]-p->camera[0], dy=pos[1]-p->camera[1], dz=pos[2]-p->camera[2];
            float d=sqrtf(dx*dx+dy*dy+dz*dz)/p->fog_distance;
            d=clamp01(d); out->fog=d*d*d*d;
            float h=sqrtf(dx*dx+dz*dz);
            out->fog_height=(atan2f(dy,h)+PI_F*0.5f)/PI_F;
        }
    }
    return 1;
}

static void sample_rgba(int unit, float u, float v, float *rgba) {
    SoftTexture *t=sample_texture(unit);
    if (!t->rgba || !t->w || !t->h) { rgba[0]=rgba[1]=rgba[2]=rgba[3]=1; return; }
    /* Craft's four atlases use UVs strictly inside [0, 1]; clamp directly
     * instead of calling floor/modulo for every shaded fragment. */
    u=clamp01(u); v=clamp01(v);
    /* The 320x200 first-pass target does not benefit visibly from bilinear
     * sky filtering, and the sky shader samples it for many fragments. */
    if (!t->linear || unit == 2) {
        int x=(int)(u*t->w), y=(int)(v*t->h);
        if (x>=t->w)x=t->w-1; if(y>=t->h)y=t->h-1;
        const unsigned char *p=t->rgba+((size_t)y*t->w+x)*4;
        for(int i=0;i<4;i++)rgba[i]=p[i]/255.0f;
        return;
    }
    float fx=u*t->w-0.5f, fy=v*t->h-0.5f;
    int x0=(int)floorf(fx), y0=(int)floorf(fy);
    float ax=fx-x0, ay=fy-y0;
    int xs[2]={x0,x0+1}, ys[2]={y0,y0+1};
    for(int k=0;k<2;k++) {
        if(xs[k]<0)xs[k]=0; if(xs[k]>=t->w)xs[k]=t->w-1;
        if(ys[k]<0)ys[k]=0; if(ys[k]>=t->h)ys[k]=t->h-1;
    }
    const unsigned char *p00=t->rgba+((size_t)ys[0]*t->w+xs[0])*4;
    const unsigned char *p10=t->rgba+((size_t)ys[0]*t->w+xs[1])*4;
    const unsigned char *p01=t->rgba+((size_t)ys[1]*t->w+xs[0])*4;
    const unsigned char *p11=t->rgba+((size_t)ys[1]*t->w+xs[1])*4;
    for(int i=0;i<4;i++) {
        float top=mixf(p00[i],p10[i],ax), bottom=mixf(p01[i],p11[i],ax);
        rgba[i]=mixf(top,bottom,ay)/255.0f;
    }
}

static int pixel_rejected(int x, int y, float z) {
    if (x<0||y<0||x>=screen_w||y>=screen_h) return 1;
    if (scissor_test && (x<scissor_x||y<scissor_y||x>=scissor_x+scissor_w||y>=scissor_y+scissor_h)) return 1;
    size_t at=(size_t)y*screen_w+x;
    return depth_test && z>=depth_buffer[at];
}

static void put_pixel(int x, int y, float z, const float *color) {
    if (pixel_rejected(x,y,z)) return;
    size_t at=(size_t)y*screen_w+x;
    if (logic_invert) {
        uint32_t old=color_buffer[at]; color_buffer[at]=((~old)&0x00ffffffU); return;
    }
    uint32_t c=rgb(color[0],color[1],color[2]);
    if (blend) {
        float a=clamp01(color[3]); uint32_t old=color_buffer[at];
        float r=((old>>16)&255)/255.0f, g=((old>>8)&255)/255.0f, b=(old&255)/255.0f;
        c=rgb(mixf(r,color[0],a),mixf(g,color[1],a),mixf(b,color[2],a));
    }
    color_buffer[at]=c;
    if (depth_test) depth_buffer[at]=clamp01(z);
}

static void shade_fragment(int kind, float u, float v, float ao, float light,
                           float diffuse, float fog, float fog_height, float *out) {
    SoftProgram *p=program(); float c[4];
    out[0]=out[1]=out[2]=0; out[3]=0;
    if (kind==1) {
        sample_rgba(p->sampler,u,v,c);
        if(c[0]>.98f&&c[1]<.02f&&c[2]>.98f)return;
        int cloud=c[0]>.98f&&c[1]>.98f&&c[2]>.98f;
        if(cloud&&p->ortho)return;
        float df=cloud?1.0f-diffuse*.2f:diffuse;
        float av=cloud?1.0f-(1.0f-ao)*.2f:ao;
        if(av+light>1)av=1;else av+=light;
        if(df+light>1)df=1;else df+=light;
        float value=p->daylight+light;if(value>1)value=1;
        float ambient=value*.3f+.2f;
        float shade=ambient+ambient*df;
        float sr=c[0]*shade*av,sg=c[1]*shade*av,sb=c[2]*shade*av;
        float sky[4]; sample_rgba(p->sky_sampler,p->timer,fog_height,sky);
        out[0]=mixf(sr,sky[0],fog);out[1]=mixf(sg,sky[1],fog);out[2]=mixf(sb,sky[2],fog);out[3]=1;
        return;
    }
    if(kind==2){sample_rgba(p->sampler,p->timer,v,c);memcpy(out,c,4*sizeof(float));return;}
    if(kind==3){
        sample_rgba(p->sampler,u,v,c);
        if(p->is_sign&&c[0]>.98f&&c[1]>.98f&&c[2]>.98f&&c[3]>.98f)return;
        if(!p->is_sign&&c[3]<.4f)c[3]=.4f;
        memcpy(out,c,4*sizeof(float));return;
    }
    out[0]=out[1]=out[2]=0;out[3]=1;
}

static float edge(float ax,float ay,float bx,float by,float x,float y){return (x-ax)*(by-ay)-(y-ay)*(bx-ax);}
static void raster_triangle(SoftVertex a,SoftVertex b,SoftVertex c,int kind){
    float area=edge(a.x,a.y,b.x,b.y,c.x,c.y); if(fabsf(area)<.001f)return;
    if(cull_face&&area<0)return;
    int minx=(int)floorf(fminf(a.x,fminf(b.x,c.x))),maxx=(int)ceilf(fmaxf(a.x,fmaxf(b.x,c.x)));
    int miny=(int)floorf(fminf(a.y,fminf(b.y,c.y))),maxy=(int)ceilf(fmaxf(a.y,fmaxf(b.y,c.y)));
    if(minx<viewport_x)minx=viewport_x;if(miny<screen_h-viewport_y-viewport_h)miny=screen_h-viewport_y-viewport_h;
    if(maxx>viewport_x+viewport_w)maxx=viewport_x+viewport_w;if(maxy>screen_h-viewport_y)maxy=screen_h-viewport_y;
    if(minx<0)minx=0;if(miny<0)miny=0;if(maxx>screen_w)maxx=screen_w;if(maxy>screen_h)maxy=screen_h;
    float invarea=1.0f/area;
    float start_x=minx+.5f, start_y=miny+.5f;
    float wa_row=edge(b.x,b.y,c.x,c.y,start_x,start_y)*invarea;
    float wb_row=edge(c.x,c.y,a.x,a.y,start_x,start_y)*invarea;
    float dwa_dx=(c.y-b.y)*invarea, dwa_dy=-(c.x-b.x)*invarea;
    float dwb_dx=(a.y-c.y)*invarea, dwb_dy=-(a.x-c.x)*invarea;
    for(int y=miny;y<maxy;y++) {
        float wa=wa_row, wb=wb_row;
        for(int x=minx;x<maxx;x++) {
            float wc=1.0f-wa-wb;
            if(wa>=0&&wb>=0&&wc>=0) {
                float z=wa*a.z+wb*b.z+wc*c.z;
                if(!pixel_rejected(x,y,z)) {
                    float u=wa*a.u+wb*b.u+wc*c.u;
                    float v=wa*a.v+wb*b.v+wc*c.v;
                    float ao=wa*a.ao+wb*b.ao+wc*c.ao;
                    float li=wa*a.light+wb*b.light+wc*c.light;
                    float df=wa*a.diffuse+wb*b.diffuse+wc*c.diffuse;
                    float fog=wa*a.fog+wb*b.fog+wc*c.fog;
                    float fh=wa*a.fog_height+wb*b.fog_height+wc*c.fog_height;
                    float color[4];shade_fragment(kind,u,v,ao,li,df,fog,fh,color);
                    if(color[3]!=0)put_pixel(x,y,z,color);
                }
            }
            wa+=dwa_dx; wb+=dwb_dx;
        }
        wa_row+=dwa_dy; wb_row+=dwb_dy;
    }
}

static void raster_line(SoftVertex a,SoftVertex b){
    float dx=b.x-a.x,dy=b.y-a.y;int steps=(int)fmaxf(fabsf(dx),fabsf(dy));if(steps<1)steps=1;
    int radius=(int)(line_width*.5f);
    for(int i=0;i<=steps;i++){float t=(float)i/steps;int x=(int)(a.x+dx*t),y=(int)(a.y+dy*t);float c[4]={0,0,0,1};for(int oy=-radius;oy<=radius;oy++)for(int ox=-radius;ox<=radius;ox++)put_pixel(x+ox,y+oy,mixf(a.z,b.z,t),c);}
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    int kind=current_program==1?1:current_program==2?4:current_program==3?3:current_program==4?2:0;
    if(mode==GL_LINES){for(int i=0;i+1<count;i+=2){SoftVertex a,b;if(transform_vertex(first+i,&a,kind)&&transform_vertex(first+i+1,&b,kind))raster_line(a,b);}return;}
    if(mode!=GL_TRIANGLES)return;
    for(int i=0;i+2<count;i+=3){SoftVertex a,b,c;if(transform_vertex(first+i,&a,kind)&&transform_vertex(first+i+1,&b,kind)&&transform_vertex(first+i+2,&c,kind))raster_triangle(a,b,c,kind);}
}
