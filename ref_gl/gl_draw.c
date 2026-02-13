#include "gl_local.h"
#include <dc/pvr.h>

image_t *draw_chars;
extern qboolean scrap_dirty;
void Scrap_Upload(void);

// External references to PVR texture array
extern pvr_texture_t pvr_textures[MAX_GLTEXTURES];

// 2D Batching System
#define MAX_2D_VERTICES 512  // Max vertices per batch
#define MAX_2D_INDICES  (MAX_2D_VERTICES * 3 / 2)  // 6 indices per quad, 4 verts per quad

typedef struct {
    float x, y, z;
    float u, v;
    uint32_t color;
} vertex_2d_t;

typedef struct {
    vertex_2d_t vertices[MAX_2D_VERTICES];
    int vertex_count;
    int current_texture;
    pvr_ptr_t current_tex_addr;
    uint32_t current_tex_format;
    int current_tex_width;
    int current_tex_height;
    int list_open;
} batch_2d_t;

static batch_2d_t batch_2d;

/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal(void) {
    // Load console characters (will auto-convert to .dt)
    draw_chars = GL_FindImage("pics/conchars.pcx", it_pic);
    if (!draw_chars) {
        draw_chars = r_notexture;
    }
    Draw_InitBatch();
}

/*
===============
Draw_InitBatch
===============
*/
void Draw_InitBatch(void) {
    memset(&batch_2d, 0, sizeof(batch_2d));
    batch_2d.current_texture = -1;
}

/*
===============
Batch_Flush
Internal function to flush current batch
===============
*/
static void Batch_Flush(void) {
    if (batch_2d.vertex_count == 0)
        return;
    
    // Make sure we have a list open
    if (!batch_2d.list_open) {
        pvr_list_begin(PVR_LIST_PT_POLY);
        batch_2d.list_open = 1;
    }
    
    pvr_dr_state_t dr_state;
    pvr_dr_init(&dr_state);
    
    // Setup polygon context based on whether we have a texture
    pvr_poly_cxt_t cxt;
    
    if (batch_2d.current_texture > 0 && batch_2d.current_tex_addr) {
        // Textured polygon
        pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY, 
                        batch_2d.current_tex_format,
                        batch_2d.current_tex_width, 
                        batch_2d.current_tex_height,
                        batch_2d.current_tex_addr, 
                        PVR_FILTER_NONE);
    } else {
        // Colored polygon (no texture)
        pvr_poly_cxt_col(&cxt, PVR_LIST_PT_POLY);
    }
    
    // 2D settings
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    
    // Submit header
    pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);
    
    // Submit all vertices as triangle strips (quads)
    // Each quad is submitted as a separate strip
    for (int i = 0; i < batch_2d.vertex_count; i += 4) {
        pvr_vertex_t *vert;
        
        // Vertex 0
        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = batch_2d.vertices[i].x;
        vert->y = batch_2d.vertices[i].y;
        vert->z = batch_2d.vertices[i].z;
        vert->u = batch_2d.vertices[i].u;
        vert->v = batch_2d.vertices[i].v;
        vert->argb = batch_2d.vertices[i].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);
        
        // Vertex 1
        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = batch_2d.vertices[i+1].x;
        vert->y = batch_2d.vertices[i+1].y;
        vert->z = batch_2d.vertices[i+1].z;
        vert->u = batch_2d.vertices[i+1].u;
        vert->v = batch_2d.vertices[i+1].v;
        vert->argb = batch_2d.vertices[i+1].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);
        
        // Vertex 2
        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = batch_2d.vertices[i+2].x;
        vert->y = batch_2d.vertices[i+2].y;
        vert->z = batch_2d.vertices[i+2].z;
        vert->u = batch_2d.vertices[i+2].u;
        vert->v = batch_2d.vertices[i+2].v;
        vert->argb = batch_2d.vertices[i+2].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);
        
        // Vertex 3 with EOL
        vert = (pvr_vertex_t *)pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX_EOL;
        vert->x = batch_2d.vertices[i+3].x;
        vert->y = batch_2d.vertices[i+3].y;
        vert->z = batch_2d.vertices[i+3].z;
        vert->u = batch_2d.vertices[i+3].u;
        vert->v = batch_2d.vertices[i+3].v;
        vert->argb = batch_2d.vertices[i+3].color;
        vert->oargb = 0;
        pvr_dr_commit(vert);
    }
    
    // Reset batch
    batch_2d.vertex_count = 0;
}

/*
===============
Batch_SetTexture
Internal function to set current texture
===============
*/
static void Batch_SetTexture(int texnum) {
    // If texture changed, flush current batch
    if (texnum != batch_2d.current_texture) {
        Batch_Flush();
        
        batch_2d.current_texture = texnum;
        
        if (texnum > 0 && texnum < MAX_GLTEXTURES && pvr_textures[texnum].loaded) {
            batch_2d.current_tex_addr = pvr_textures[texnum].ptr;
            batch_2d.current_tex_format = pvr_textures[texnum].format;
            batch_2d.current_tex_width = pvr_textures[texnum].width;
            batch_2d.current_tex_height = pvr_textures[texnum].height;
        } else {
            batch_2d.current_tex_addr = NULL;
        }
    }
}

/*
===============
Draw_FlushBatch
Public function to flush all pending 2D draws
===============
*/
void Draw_FlushBatch(void) {
    Batch_Flush();
    
    // Close the list if it was opened
    if (batch_2d.list_open) {
        pvr_list_finish();
        batch_2d.list_open = 0;
    }
}

/*
===============
Draw_AddQuad
Add a textured quad to the batch
===============
*/
void Draw_AddQuad(float x1, float y1, float x2, float y2,
                  float s1, float t1, float s2, float t2, int texnum) {
    
    // Skip if no texture
    if (texnum <= 0 || texnum >= MAX_GLTEXTURES)
        return;
    
    // Skip if texture not loaded
    if (!pvr_textures[texnum].loaded)
        return;
    
    // Check if we need to flush (texture change or buffer full)
    if (batch_2d.vertex_count + 4 > MAX_2D_VERTICES) {
        Batch_Flush();
    }
    
    // Set the texture (will flush if different)
    Batch_SetTexture(texnum);
    
    // Add quad vertices (as triangle strip order)
    int idx = batch_2d.vertex_count;
    
    // Top-left
    batch_2d.vertices[idx].x = x1;
    batch_2d.vertices[idx].y = y1;
    batch_2d.vertices[idx].z = 1.0f;
    batch_2d.vertices[idx].u = s1;
    batch_2d.vertices[idx].v = t1;
    batch_2d.vertices[idx].color = 0xFFFFFFFF;
    
    // Top-right
    batch_2d.vertices[idx+1].x = x2;
    batch_2d.vertices[idx+1].y = y1;
    batch_2d.vertices[idx+1].z = 1.0f;
    batch_2d.vertices[idx+1].u = s2;
    batch_2d.vertices[idx+1].v = t1;
    batch_2d.vertices[idx+1].color = 0xFFFFFFFF;
    
    // Bottom-left
    batch_2d.vertices[idx+2].x = x1;
    batch_2d.vertices[idx+2].y = y2;
    batch_2d.vertices[idx+2].z = 1.0f;
    batch_2d.vertices[idx+2].u = s1;
    batch_2d.vertices[idx+2].v = t2;
    batch_2d.vertices[idx+2].color = 0xFFFFFFFF;
    
    // Bottom-right
    batch_2d.vertices[idx+3].x = x2;
    batch_2d.vertices[idx+3].y = y2;
    batch_2d.vertices[idx+3].z = 1.0f;
    batch_2d.vertices[idx+3].u = s2;
    batch_2d.vertices[idx+3].v = t2;
    batch_2d.vertices[idx+3].color = 0xFFFFFFFF;
    
    batch_2d.vertex_count += 4;
}

/*
===============
Draw_AddColoredQuad
Add a colored quad (no texture) to the batch
===============
*/
static void Draw_AddColoredQuad(float x1, float y1, float x2, float y2, uint32_t color) {
    // Check if we need to flush
    if (batch_2d.vertex_count + 4 > MAX_2D_VERTICES) {
        Batch_Flush();
    }
    
    // Set no texture (will flush if we had a texture)
    Batch_SetTexture(-1);
    
    // Add quad vertices
    int idx = batch_2d.vertex_count;
    
    // Top-left
    batch_2d.vertices[idx].x = x1;
    batch_2d.vertices[idx].y = y1;
    batch_2d.vertices[idx].z = 1.0f;
    batch_2d.vertices[idx].u = 0;
    batch_2d.vertices[idx].v = 0;
    batch_2d.vertices[idx].color = color;
    
    // Top-right
    batch_2d.vertices[idx+1].x = x2;
    batch_2d.vertices[idx+1].y = y1;
    batch_2d.vertices[idx+1].z = 1.0f;
    batch_2d.vertices[idx+1].u = 1;
    batch_2d.vertices[idx+1].v = 0;
    batch_2d.vertices[idx+1].color = color;
    
    // Bottom-left
    batch_2d.vertices[idx+2].x = x1;
    batch_2d.vertices[idx+2].y = y2;
    batch_2d.vertices[idx+2].z = 1.0f;
    batch_2d.vertices[idx+2].u = 0;
    batch_2d.vertices[idx+2].v = 1;
    batch_2d.vertices[idx+2].color = color;
    
    // Bottom-right
    batch_2d.vertices[idx+3].x = x2;
    batch_2d.vertices[idx+3].y = y2;
    batch_2d.vertices[idx+3].z = 1.0f;
    batch_2d.vertices[idx+3].u = 1;
    batch_2d.vertices[idx+3].v = 1;
    batch_2d.vertices[idx+3].color = color;
    
    batch_2d.vertex_count += 4;
}

/*
================
Draw_Char
================
*/
void Draw_Char(int x, int y, int num) {
    int row, col;
    float frow, fcol, size;
    
    num &= 255;
    
    if ((num & 127) == 32)
        return;  // space
    
    if (y <= -8)
        return;  // off screen
    
    row = num >> 4;
    col = num & 15;
    
    frow = row * 0.0625f;
    fcol = col * 0.0625f;
    size = 0.0625f;
    
    Draw_AddQuad(x, y, x + 8, y + 8,
                 fcol, frow, fcol + size, frow + size,
                 draw_chars->texnum);
}

/*
=============
Draw_FindPic
=============
*/
image_t *Draw_FindPic(char *name) {
    image_t *gl;
    char fullname[MAX_QPATH];
    
    if (name[0] != '/' && name[0] != '\\') {
        Com_sprintf(fullname, sizeof(fullname), "pics/%s.pcx", name);
        gl = GL_FindImage(fullname, it_pic);
    } else {
        gl = GL_FindImage(name + 1, it_pic);
    }
    
    return gl;
}

/*
=============
Draw_GetPicSize
=============
*/
void Draw_GetPicSize(int *w, int *h, char *pic) {
    image_t *gl;
    
    gl = Draw_FindPic(pic);
    if (!gl) {
        *w = *h = -1;
        return;
    }
    *w = gl->width;
    *h = gl->height;
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic(int x, int y, int w, int h, char *pic) {
    image_t *gl;
    
    gl = Draw_FindPic(pic);
    if (!gl) {
        ri.Con_Printf(PRINT_ALL, "Can't find pic: %s\n", pic);
        return;
    }
    
    if (scrap_dirty)
        Scrap_Upload();
    
    Draw_AddQuad(x, y, x + w, y + h,
                 gl->sl, gl->tl, gl->sh, gl->th, gl->texnum);
}

/*
=============
Draw_Pic
=============
*/
void Draw_Pic(int x, int y, char *pic) {
    image_t *gl;
    
    gl = Draw_FindPic(pic);
    if (!gl) {
        ri.Con_Printf(PRINT_ALL, "Can't find pic: %s\n", pic);
        return;
    }
    
    if (scrap_dirty)
        Scrap_Upload();
    
    Draw_AddQuad(x, y, x + gl->width, y + gl->height,
                 gl->sl, gl->tl, gl->sh, gl->th, gl->texnum);
}

/*
=============
Draw_TileClear
=============
*/
void Draw_TileClear(int x, int y, int w, int h, char *pic) {
    image_t *image;
    
    image = Draw_FindPic(pic);
    if (!image) {
        ri.Con_Printf(PRINT_ALL, "Can't find pic: %s\n", pic);
        return;
    }
    
    // Use wrapped texture coordinates for tiling
    Draw_AddQuad(x, y, x + w, y + h,
                 x / 64.0f, y / 64.0f, (x + w) / 64.0f, (y + h) / 64.0f,
                 image->texnum);
}

/*
=============
Draw_Fill
Colored rectangle without texture
=============
*/
void Draw_Fill(int x, int y, int w, int h, int c) {
    union {
        unsigned c;
        byte v[4];
    } color;
    
    if ((unsigned)c > 255)
        ri.Sys_Error(ERR_FATAL, "Draw_Fill: bad color");
    
    // Get color from palette
    color.c = d_8to24table[c];
    
    // Build ARGB color
    uint32_t argb = 0xFF000000 | 
                    (color.v[2] << 16) |  // R
                    (color.v[1] << 8) |   // G
                    color.v[0];           // B
    
    Draw_AddColoredQuad(x, y, x + w, y + h, argb);
}

/*
================
Draw_FadeScreen
================
*/
void Draw_FadeScreen(void) {
    // 80% black overlay
    uint32_t argb = 0xCC000000;
    Draw_AddColoredQuad(0, 0, vid.width, vid.height, argb);
}

/*
=============
Draw_StretchRaw
=============
*/
void Draw_StretchRaw(int x, int y, int w, int h, int cols, int rows, byte *data) {
    // Not implemented for now - would need to create temporary texture
}