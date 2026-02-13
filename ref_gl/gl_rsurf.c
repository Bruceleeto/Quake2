#include <assert.h>
#include "gl_local.h"

static vec3_t	modelorg;		// relative to viewpoint
msurface_t	*r_alpha_surfaces;


// TLDR DMA  not working right. need to fix. 
#define DYNAMIC_LIGHT_WIDTH  128
#define DYNAMIC_LIGHT_HEIGHT 128

#define LIGHTMAP_BYTES 2

#define	BLOCK_WIDTH		128   // BRUCE Look into this.  Large amount of memory.
#define	BLOCK_HEIGHT	128

#define	MAX_LIGHTMAPS	128

int		c_visible_lightmaps;
int		c_visible_textures;



 




int		c_visible_lightmaps;
int		c_visible_textures;

extern void R_SetCacheState( msurface_t *surf );
extern void R_BuildLightMap (msurface_t *surf, byte *dest, int stride);

typedef struct {
    shz_vec4_t pos;   // x, y, z, w (w is the clip-space W)
    float u, v;
    uint32_t argb;
} ClipVert_t;

// Build visibility mask for triangle
// Returns: bit 0 = v0 visible, bit 1 = v1 visible, bit 2 = v2 visible
static inline unsigned nearz_vismask_tri(ClipVert_t *verts)
{
    unsigned mask = 0;
    // Vertex is visible if z >= -w (i.e. w + z >= 0)
    if (verts[0].pos.z >= -verts[0].pos.w) mask |= 1;
    if (verts[1].pos.z >= -verts[1].pos.w) mask |= 2;
    if (verts[2].pos.z >= -verts[2].pos.w) mask |= 4;
    return mask;
}

// Clip edge from v1 to v2, store result in out
static inline void nearz_clip_edge(const ClipVert_t *v1, const ClipVert_t *v2, ClipVert_t *out)
{
    const float d0 = v1->pos.w + v1->pos.z;
    const float d1 = v2->pos.w + v2->pos.z;
    
    float t = d0 / (d0 - d1);
    float inv_t = 1.0f - t;

    out->pos.x = inv_t * v1->pos.x + t * v2->pos.x;
    out->pos.y = inv_t * v1->pos.y + t * v2->pos.y;
    out->pos.z = inv_t * v1->pos.z + t * v2->pos.z;
    out->pos.w = inv_t * v1->pos.w + t * v2->pos.w;
    
    out->u = inv_t * v1->u + t * v2->u;
    out->v = inv_t * v1->v + t * v2->v;
    
    // Color lerp
    uint8_t ti = (uint8_t)(t * 255);
    uint32_t c1 = v1->argb, c2 = v2->argb;
    uint32_t rb = ((((c2 & 0x00FF00FF) - (c1 & 0x00FF00FF)) * ti) >> 8) + (c1 & 0x00FF00FF);
    uint32_t g  = ((((c2 & 0x0000FF00) - (c1 & 0x0000FF00)) * ti) >> 8) + (c1 & 0x0000FF00);
    uint32_t a  = ((((c2 >> 24) - (c1 >> 24)) * ti) >> 8) + (c1 >> 24);
    out->argb = (a << 24) | (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
image_t *R_TextureAnimation (mtexinfo_t *tex)
{
	int		c;

	if (!tex->next)
		return tex->image;

	c = currententity->frame % tex->numframes;
	while (c)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}


static inline void submit_vert(pvr_dr_state_t *dr_state, ClipVert_t *cv, uint32_t flags)
{
    float invw = shz_invf_fsrra(cv->pos.w);
    
    pvr_vertex_t *vert = pvr_dr_target(*dr_state);
    vert->flags = flags;
    vert->x = cv->pos.x * invw;
    vert->y = cv->pos.y * invw;
    vert->z = invw;
    vert->u = cv->u;
    vert->v = cv->v;
    vert->argb = cv->argb;
    vert->oargb = 0;
    pvr_dr_commit(vert);
}


void ClipAndSubmitTriangle(
    shz_vec4_t p0, shz_vec4_t p1, shz_vec4_t p2,
    float uv0[2], float uv1[2], float uv2[2],
    pvr_dr_state_t *dr_state,
    uint32_t argb_color)
{
    ClipVert_t verts[5];  // Up to 5 for clipped quad as strip
    
    // Initialize vertices
    verts[0].pos = p0; verts[0].u = uv0[0]; verts[0].v = uv0[1]; verts[0].argb = argb_color;
    verts[1].pos = p1; verts[1].u = uv1[0]; verts[1].v = uv1[1]; verts[1].argb = argb_color;
    verts[2].pos = p2; verts[2].u = uv2[0]; verts[2].v = uv2[1]; verts[2].argb = argb_color;
    
    unsigned vismask = nearz_vismask_tri(verts);
    
    // All behind - cull
    if (vismask == 0) return;
    
    // All visible - fast path
    if (vismask == 7) {
        submit_vert(dr_state, &verts[0], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[1], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[2], PVR_CMD_VERTEX_EOL);
        return;
    }
    
    unsigned n_verts = 3;
    
    switch (vismask) {
    // Only v0 visible -> clip v0-v1 and v0-v2
    case 1:
        nearz_clip_edge(&verts[0], &verts[1], &verts[1]);
        nearz_clip_edge(&verts[0], &verts[2], &verts[2]);
        break;
        
    // Only v1 visible -> clip v1-v0 and v1-v2
    case 2:
        nearz_clip_edge(&verts[1], &verts[0], &verts[0]);
        nearz_clip_edge(&verts[1], &verts[2], &verts[2]);
        break;
        
    // v0 + v1 visible -> clip v1-v2 to new v3, clip v0-v2 to v2, output 4 verts as strip
    case 3:
        n_verts = 4;
        nearz_clip_edge(&verts[1], &verts[2], &verts[3]);
        nearz_clip_edge(&verts[0], &verts[2], &verts[2]);
        break;
        
    // Only v2 visible -> clip v2-v0 and v2-v1
    case 4:
        nearz_clip_edge(&verts[2], &verts[0], &verts[0]);
        nearz_clip_edge(&verts[2], &verts[1], &verts[1]);
        break;
        
    // v0 + v2 visible -> clip v2-v1 to new v3, clip v0-v1 to v1, output 4 verts
    case 5:
        n_verts = 4;
        nearz_clip_edge(&verts[1], &verts[2], &verts[3]);
        nearz_clip_edge(&verts[0], &verts[1], &verts[1]);
        break;
        
    // v1 + v2 visible -> copy v2 to v3, clip v0-v2 to v2, clip v0-v1 to v0
    case 6:
        n_verts = 4;
        verts[3] = verts[2];
        nearz_clip_edge(&verts[0], &verts[2], &verts[2]);
        nearz_clip_edge(&verts[0], &verts[1], &verts[0]);
        break;
    }
    
    // Submit as triangle strip
    if (n_verts == 3) {
        submit_vert(dr_state, &verts[0], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[1], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[2], PVR_CMD_VERTEX_EOL);
    } else {
        // 4 verts = triangle strip (2 triangles)
        submit_vert(dr_state, &verts[0], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[1], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[2], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[3], PVR_CMD_VERTEX_EOL);
    }
}

SHZ_HOT
void DrawGLPolyVertices(glpoly_t *p, pvr_dr_state_t *dr_state, uint32_t argb_color)
{
    if (SHZ_UNLIKELY(p->numverts < 3)) return;
    
    float *v = p->verts[0];
    const int numverts = p->numverts;
    
    // Load matrix once
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
    
    // Transform all vertices
    shz_vec4_t transformed[64];
    float uv[64][2];
    unsigned vismask_all = 0;
    
    SHZ_PREFETCH(v);
    
    for (int i = 0; i < numverts; i++, v += VERTEXSIZE) {
        SHZ_PREFETCH(v + VERTEXSIZE);
        
        shz_vec3_t pos = shz_vec3_init(v[0], v[1], v[2]);
        transformed[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(pos, 1.0f));
        
        uv[i][0] = v[3];
        uv[i][1] = v[4];
        
        // Track if any vertex is visible
        if (transformed[i].z >= -transformed[i].w)
            vismask_all |= (1 << i);
    }
    
    // Early out if entire poly is behind near plane
    if (SHZ_UNLIKELY(vismask_all == 0)) return;
    
    // Check if all visible (common case)
    unsigned all_visible_mask = (1 << numverts) - 1;
    
    if (SHZ_LIKELY(vismask_all == all_visible_mask)) {
        // Fast path - no clipping
        for (int i = 1; i < numverts - 1; i++) {
            float inv_w0 = shz_invf_fsrra(transformed[0].w);
            float inv_wi = shz_invf_fsrra(transformed[i].w);
            float inv_wi1 = shz_invf_fsrra(transformed[i+1].w);
            
            pvr_vertex_t *vert = pvr_dr_target(*dr_state);
            vert->flags = PVR_CMD_VERTEX;
            vert->x = transformed[0].x * inv_w0;
            vert->y = transformed[0].y * inv_w0;
            vert->z = inv_w0;
            vert->u = uv[0][0];
            vert->v = uv[0][1];
            vert->argb = argb_color;
            vert->oargb = 0;
            pvr_dr_commit(vert);
            
            vert = pvr_dr_target(*dr_state);
            vert->flags = PVR_CMD_VERTEX;
            vert->x = transformed[i].x * inv_wi;
            vert->y = transformed[i].y * inv_wi;
            vert->z = inv_wi;
            vert->u = uv[i][0];
            vert->v = uv[i][1];
            vert->argb = argb_color;
            vert->oargb = 0;
            pvr_dr_commit(vert);
            
            vert = pvr_dr_target(*dr_state);
            vert->flags = PVR_CMD_VERTEX_EOL;
            vert->x = transformed[i+1].x * inv_wi1;
            vert->y = transformed[i+1].y * inv_wi1;
            vert->z = inv_wi1;
            vert->u = uv[i+1][0];
            vert->v = uv[i+1][1];
            vert->argb = argb_color;
            vert->oargb = 0;
            pvr_dr_commit(vert);
        }
    } else {
        // Slow path - need clipping
        for (int i = 1; i < numverts - 1; i++) {
            ClipAndSubmitTriangle(
                transformed[0], transformed[i], transformed[i+1],
                uv[0], uv[i], uv[i+1],
                dr_state, argb_color
            );
        }
    }
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly(glpoly_t *p)
{
    if (p->numverts < 3) return;
    
    pvr_dr_state_t dr_state;
    pvr_dr_init(&dr_state);
    
    // Get current texture
    int texnum = gl_state.currenttextures[0];
    pvr_ptr_t tex_addr = NULL;
    uint32_t tex_format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED;
    int tex_width = 64, tex_height = 64;
    
    if (texnum > 0 && texnum < MAX_GLTEXTURES && pvr_textures[texnum].loaded) {
        tex_addr = pvr_textures[texnum].ptr;
        tex_format = pvr_textures[texnum].format;
        tex_width = pvr_textures[texnum].width;
        tex_height = pvr_textures[texnum].height;
    }
    
    // Setup polygon header with texture
    pvr_poly_cxt_t cxt;
    if (tex_addr) {
        pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, tex_format, 
                        tex_width, tex_height, tex_addr, PVR_FILTER_BILINEAR);
    } else {
        pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    }
    cxt.gen.culling = PVR_CULLING_NONE;
    
    pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);
    
    DrawGLPolyVertices(p, &dr_state, 0xFFFFFFFF);  // Opaque white
}

/*
================
DrawGLFlowingPoly -- version of DrawGLPoly that handles scrolling texture
================
*/
void DrawGLFlowingPoly (msurface_t *fa)
{
 
}

/*
================
R_RenderBrushPoly - SIMPLIFIED VERSION
Just draw the texture, no lightmaps
================
*/
void R_RenderBrushPoly (msurface_t *fa)
{
   if (fa->flags & SURF_DRAWTURB)
       return;
   
   // Just add to texture chain for batch drawing
   image_t *image = R_TextureAnimation(fa->texinfo);
   fa->texturechain = image->texturechain;
   image->texturechain = fa;
}




/*
================
DrawTextureChains - MODIFIED VERSION
No white colors used
================
*/

// Sample light at a vertex position from the surface's lightmap data
static void AddDynamicLightToVertex(msurface_t *surf, float *vert, float *r, float *g, float *b)
{
    if (surf->dlightframe != r_framecount)
        return;
    
    for (int lnum = 0; lnum < r_newrefdef.num_dlights; lnum++)
    {
        if (!(surf->dlightbits & (1 << lnum)))
            continue;
        
        dlight_t *dl = &r_newrefdef.dlights[lnum];
        
        // Distance from light to vertex
        vec3_t impact;
        VectorSubtract(dl->origin, vert, impact);
        float dist = VectorLength(impact);
        
        float add = dl->intensity - dist;
        if (add > 0)
        {
            *r += add * dl->color[0];
            *g += add * dl->color[1];
            *b += add * dl->color[2];
        }
    }
}

// Sample light at a vertex position from the surface's lightmap data
static inline uint32_t SampleVertexLight(msurface_t *surf, float *vert)
{
    float r = 0, g = 0, b = 0;
    float mod = gl_modulate->value;
    
    // Sample static lightmap if available
    if (surf->samples && r_worldmodel->lightdata)
    {
        int smax = (surf->extents[0] >> 4) + 1;
        int tmax = (surf->extents[1] >> 4) + 1;
        
        float s = DotProduct(vert, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
        float t = DotProduct(vert, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
        
        s -= surf->texturemins[0];
        t -= surf->texturemins[1];
        
        int ls = (int)(s * 0.0625f);
        int lt = (int)(t * 0.0625f);
        
        if (ls < 0) ls = 0;
        if (ls >= smax) ls = smax - 1;
        if (lt < 0) lt = 0;
        if (lt >= tmax) lt = tmax - 1;
        
        byte *lightmap = surf->samples + (lt * smax + ls) * 3;
        
        for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
        {
            // Use per-channel RGB from lightstyle, not just white
            float sr = r_newrefdef.lightstyles[surf->styles[maps]].rgb[0] * mod;
            float sg = r_newrefdef.lightstyles[surf->styles[maps]].rgb[1] * mod;
            float sb = r_newrefdef.lightstyles[surf->styles[maps]].rgb[2] * mod;
            
            r += lightmap[0] * sr;
            g += lightmap[1] * sg;
            b += lightmap[2] * sb;
            lightmap += smax * tmax * 3;
        }
    }
    else
    {
        // No lightmap - fullbright base
        r = g = b = 255.0f;
    }
    
    // Add dynamic lights (gun flash, explosions, etc)
    AddDynamicLightToVertex(surf, vert, &r, &g, &b);
    
    // Clamp
    int ir = (int)r; if (ir > 255) ir = 255; if (ir < 0) ir = 0;
    int ig = (int)g; if (ig > 255) ig = 255; if (ig < 0) ig = 0;
    int ib = (int)b; if (ib > 255) ib = 255; if (ib < 0) ib = 0;
    
    return 0xFF000000 | (ir << 16) | (ig << 8) | ib;
}

// Clipping version that takes per-vertex colors
static void ClipAndSubmitTriangleGouraud(
    shz_vec4_t p0, shz_vec4_t p1, shz_vec4_t p2,
    float uv0[2], float uv1[2], float uv2[2],
    uint32_t c0, uint32_t c1, uint32_t c2,
    pvr_dr_state_t *dr_state)
{
    ClipVert_t verts[5];
    
    verts[0].pos = p0; verts[0].u = uv0[0]; verts[0].v = uv0[1]; verts[0].argb = c0;
    verts[1].pos = p1; verts[1].u = uv1[0]; verts[1].v = uv1[1]; verts[1].argb = c1;
    verts[2].pos = p2; verts[2].u = uv2[0]; verts[2].v = uv2[1]; verts[2].argb = c2;
    
    unsigned vismask = nearz_vismask_tri(verts);
    
    if (vismask == 0) return;
    
    if (vismask == 7) {
        submit_vert(dr_state, &verts[0], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[1], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[2], PVR_CMD_VERTEX_EOL);
        return;
    }
    
    unsigned n_verts = 3;
    
    switch (vismask) {
    case 1:
        nearz_clip_edge(&verts[0], &verts[1], &verts[1]);
        nearz_clip_edge(&verts[0], &verts[2], &verts[2]);
        break;
    case 2:
        nearz_clip_edge(&verts[1], &verts[0], &verts[0]);
        nearz_clip_edge(&verts[1], &verts[2], &verts[2]);
        break;
    case 3:
        n_verts = 4;
        nearz_clip_edge(&verts[1], &verts[2], &verts[3]);
        nearz_clip_edge(&verts[0], &verts[2], &verts[2]);
        break;
    case 4:
        nearz_clip_edge(&verts[2], &verts[0], &verts[0]);
        nearz_clip_edge(&verts[2], &verts[1], &verts[1]);
        break;
    case 5:
        n_verts = 4;
        nearz_clip_edge(&verts[1], &verts[2], &verts[3]);
        nearz_clip_edge(&verts[0], &verts[1], &verts[1]);
        break;
    case 6:
        n_verts = 4;
        verts[3] = verts[2];
        nearz_clip_edge(&verts[0], &verts[2], &verts[2]);
        nearz_clip_edge(&verts[0], &verts[1], &verts[0]);
        break;
    }
    
    if (n_verts == 3) {
        submit_vert(dr_state, &verts[0], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[1], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[2], PVR_CMD_VERTEX_EOL);
    } else {
        submit_vert(dr_state, &verts[0], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[1], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[2], PVR_CMD_VERTEX);
        submit_vert(dr_state, &verts[3], PVR_CMD_VERTEX_EOL);
    }
}

SHZ_HOT
static void DrawGLPolySurfaceGouraud(glpoly_t *p, pvr_dr_state_t *dr_state, msurface_t *surf)
{
    if (SHZ_UNLIKELY(p->numverts < 3)) return;
    
    float *v = p->verts[0];
    const int numverts = p->numverts;
    
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
    
    shz_vec4_t transformed[64];
    float uv[64][2];
    uint32_t colors[64];
    unsigned vismask_all = 0;
    
    SHZ_PREFETCH(v);
    
    for (int i = 0; i < numverts; i++, v += VERTEXSIZE) {
        SHZ_PREFETCH(v + VERTEXSIZE);
        
        shz_vec3_t pos = shz_vec3_init(v[0], v[1], v[2]);
        transformed[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(pos, 1.0f));
        
        uv[i][0] = v[3];
        uv[i][1] = v[4];
        colors[i] = SampleVertexLight(surf, v);
        
        if (transformed[i].z >= -transformed[i].w)
            vismask_all |= (1 << i);
    }
    
    if (SHZ_UNLIKELY(vismask_all == 0)) return;
    
    unsigned all_visible_mask = (1 << numverts) - 1;
    
    if (SHZ_LIKELY(vismask_all == all_visible_mask)) {
        for (int i = 1; i < numverts - 1; i++) {
            float inv_w0 = shz_invf_fsrra(transformed[0].w);
            float inv_wi = shz_invf_fsrra(transformed[i].w);
            float inv_wi1 = shz_invf_fsrra(transformed[i+1].w);
            
            pvr_vertex_t *vert = pvr_dr_target(*dr_state);
            vert->flags = PVR_CMD_VERTEX;
            vert->x = transformed[0].x * inv_w0;
            vert->y = transformed[0].y * inv_w0;
            vert->z = inv_w0;
            vert->u = uv[0][0];
            vert->v = uv[0][1];
            vert->argb = colors[0];
            vert->oargb = 0;
            pvr_dr_commit(vert);
            
            vert = pvr_dr_target(*dr_state);
            vert->flags = PVR_CMD_VERTEX;
            vert->x = transformed[i].x * inv_wi;
            vert->y = transformed[i].y * inv_wi;
            vert->z = inv_wi;
            vert->u = uv[i][0];
            vert->v = uv[i][1];
            vert->argb = colors[i];
            vert->oargb = 0;
            pvr_dr_commit(vert);
            
            vert = pvr_dr_target(*dr_state);
            vert->flags = PVR_CMD_VERTEX_EOL;
            vert->x = transformed[i+1].x * inv_wi1;
            vert->y = transformed[i+1].y * inv_wi1;
            vert->z = inv_wi1;
            vert->u = uv[i+1][0];
            vert->v = uv[i+1][1];
            vert->argb = colors[i+1];
            vert->oargb = 0;
            pvr_dr_commit(vert);
        }
    } else {
        for (int i = 1; i < numverts - 1; i++) {
            ClipAndSubmitTriangleGouraud(
                transformed[0], transformed[i], transformed[i+1],
                uv[0], uv[i], uv[i+1],
                colors[0], colors[i], colors[i+1],
                dr_state
            );
        }
    }
}

void DrawTextureChains(void)
{
    int i;
    msurface_t *s;
    image_t *image;
    
    c_visible_textures = 0;
    
    for (i = 0, image = gltextures; i < numgltextures; i++, image++) {
        if (!image->registration_sequence)
            continue;
        s = image->texturechain;
        if (!s)
            continue;
        c_visible_textures++;
        
        int texnum = image->texnum;
        pvr_ptr_t tex_addr = NULL;
        uint32_t tex_format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED;
        int tex_width = 64, tex_height = 64;
        
        if (texnum > 0 && texnum < MAX_GLTEXTURES && pvr_textures[texnum].loaded) {
            tex_addr = pvr_textures[texnum].ptr;
            tex_format = pvr_textures[texnum].format;
            tex_width = pvr_textures[texnum].width;
            tex_height = pvr_textures[texnum].height;
        }
        
        pvr_dr_state_t dr_state;
        pvr_dr_init(&dr_state);
        
        pvr_poly_cxt_t cxt;
        if (tex_addr) {
            pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, tex_format, 
                            tex_width, tex_height, tex_addr, PVR_FILTER_BILINEAR);
        } else {
            pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
        }
        cxt.gen.culling = PVR_CULLING_NONE;
 
        pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
        pvr_poly_compile(hdr, &cxt);
        pvr_dr_commit(hdr);
        
        for (; s; s = s->texturechain) {
            c_brush_polys++;
            
            if (s->flags & SURF_DRAWTURB)
                continue;
                
            if (s->polys && s->polys->numverts >= 3)
                DrawGLPolySurfaceGouraud(s->polys, &dr_state, s);
        }
        
        pvr_dr_finish();
        image->texturechain = NULL;
    }
}

// Stubbed
void DrawLightmaps(void)
{
    // Empty - Gouraud shading handles lighting
}



float	r_turbsin[] =
{
	#include "warpsin.h"
};
#define TURBSCALE (256.0 / (2 * M_PI))


/*
================
R_DrawAlphaSurfaces  
================
*/
void R_DrawAlphaSurfaces(void)
{
    msurface_t *s;
    
    if (!r_alpha_surfaces)
        return;
    
    // Initialize DR state once
    pvr_dr_state_t dr_state;
    pvr_dr_init(&dr_state);
    
    // Process each alpha surface
    for (s = r_alpha_surfaces; s; s = s->texturechain) {
        c_brush_polys++;
        
        if (!s->polys || s->polys->numverts < 3)
            continue;
            
        // Get texture info
        image_t *image = R_TextureAnimation(s->texinfo);
        int texnum = image->texnum;
        pvr_ptr_t tex_addr = NULL;
        uint32_t tex_format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED;
        int tex_width = 64, tex_height = 64;
        
        if (texnum > 0 && texnum < MAX_GLTEXTURES && pvr_textures[texnum].loaded) {
            tex_addr = pvr_textures[texnum].ptr;
            tex_format = pvr_textures[texnum].format;
            tex_width = pvr_textures[texnum].width;
            tex_height = pvr_textures[texnum].height;
        }
        
        // Setup translucent polygon context
        pvr_poly_cxt_t cxt;
        if (tex_addr) {
            pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, tex_format, 
                            tex_width, tex_height, tex_addr, PVR_FILTER_BILINEAR);
        } else {
            pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
        }
        
        // Enable alpha blending
        cxt.gen.alpha = PVR_ALPHA_ENABLE;
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
        cxt.gen.culling = PVR_CULLING_NONE;
        
        // Submit polygon header
        pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
        pvr_poly_compile(hdr, &cxt);
        pvr_dr_commit(hdr);
        
        // Handle water surfaces with animated texture coordinates
        if (s->flags & SURF_DRAWTURB) {
            glpoly_t *p;
            float rdt = r_newrefdef.time;
            float scroll = (s->texinfo->flags & SURF_FLOWING) ? 
                          -64 * (rdt * 0.5f - (int)(rdt * 0.5f)) : 0;
            
            // Water surfaces have multiple subdivided polygons - render them all
            for (p = s->polys; p; p = p->next) {
                if (SHZ_UNLIKELY(p->numverts < 3)) continue;
                
                shz_vec4_t transformed[64];
                float warped_uv[64][2];
                float *v = p->verts[0];
                
                // Load matrix once per polygon
                shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
                
                SHZ_PREFETCH(v);
                
                for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
                    SHZ_PREFETCH(v + VERTEXSIZE);
                    
                    // Transform via XMTRX using FTRV instruction
                    shz_vec3_t pos = shz_vec3_init(v[0], v[1], v[2]);
                    transformed[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(pos, 1.0f));
                    
                    // Warp UVs using r_turbsin table
                    float os = v[3];
                    float ot = v[4];
                    
                    float s_coord = os + r_turbsin[Q_ftol(((ot * 0.125f + rdt) * TURBSCALE)) & 255];
                    s_coord += scroll;
                    s_coord *= (1.0f / 64.0f);
                    
                    float t_coord = ot + r_turbsin[Q_ftol(((os * 0.125f + rdt) * TURBSCALE)) & 255];
                    t_coord *= (1.0f / 64.0f);
                    
                    warped_uv[i][0] = s_coord;
                    warped_uv[i][1] = t_coord;
                }
                
                // Submit warped water triangles - fan triangulation
                for (int i = 1; i < p->numverts - 1; i++) {
                    // Near plane cull check
                    if (SHZ_UNLIKELY(transformed[0].w < 0.1f || 
                                     transformed[i].w < 0.1f || 
                                     transformed[i+1].w < 0.1f))
                        continue;
                    
                    // Fast 1/W using FSRRA
                    float inv_w0 = shz_invf_fsrra(transformed[0].w);
                    float inv_wi = shz_invf_fsrra(transformed[i].w);
                    float inv_wi1 = shz_invf_fsrra(transformed[i+1].w);
                    
                    pvr_vertex_t *vert = pvr_dr_target(dr_state);
                    vert->flags = PVR_CMD_VERTEX;
                    vert->x = transformed[0].x * inv_w0;
                    vert->y = transformed[0].y * inv_w0;
                    vert->z = inv_w0;
                    vert->u = warped_uv[0][0];
                    vert->v = warped_uv[0][1];
                    vert->argb = 0xCCFFFFFF;
                    vert->oargb = 0;
                    pvr_dr_commit(vert);
                    
                    vert = pvr_dr_target(dr_state);
                    vert->flags = PVR_CMD_VERTEX;
                    vert->x = transformed[i].x * inv_wi;
                    vert->y = transformed[i].y * inv_wi;
                    vert->z = inv_wi;
                    vert->u = warped_uv[i][0];
                    vert->v = warped_uv[i][1];
                    vert->argb = 0xCCFFFFFF;
                    vert->oargb = 0;
                    pvr_dr_commit(vert);
                    
                    vert = pvr_dr_target(dr_state);
                    vert->flags = PVR_CMD_VERTEX_EOL;
                    vert->x = transformed[i+1].x * inv_wi1;
                    vert->y = transformed[i+1].y * inv_wi1;
                    vert->z = inv_wi1;
                    vert->u = warped_uv[i+1][0];
                    vert->v = warped_uv[i+1][1];
                    vert->argb = 0xCCFFFFFF;
                    vert->oargb = 0;
                    pvr_dr_commit(vert);
                }
            }
        } else {
            // Regular translucent surfaces
            uint32_t argb_color;
            if (s->texinfo->flags & SURF_TRANS33)
                argb_color = 0x55FFFFFF;
            else if (s->texinfo->flags & SURF_TRANS66)
                argb_color = 0xAAFFFFFF;
            else
                argb_color = 0x80FFFFFF;
            
            DrawGLPolyVertices(s->polys, &dr_state, argb_color);
        }
        
        pvr_dr_finish();
    }
    
    r_alpha_surfaces = NULL;
}
/*
=================
R_DrawInlineBModel 
=================
*/
void R_DrawInlineBModel(void)
{
    int i, k;
    cplane_t *pplane;
    float dot;
    msurface_t *psurf;
    dlight_t *lt;

    // Calculate dynamic lighting for bmodel
    if (!gl_flashblend->value)
    {
        lt = r_newrefdef.dlights;
        for (k = 0; k < r_newrefdef.num_dlights; k++, lt++)
        {
            R_MarkLights(lt, 1 << k, currentmodel->nodes + currentmodel->firstnode);
        }
    }

    psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

    // Draw surfaces
    for (i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
    {
        // Find which side of the node we are on
        pplane = psurf->plane;
        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

        // Draw the polygon
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
            (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
        {
            if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
            {
                // Add to the translucent chain
                psurf->texturechain = r_alpha_surfaces;
                r_alpha_surfaces = psurf;
            }
            else
            {
                // Draw immediately instead of adding to texture chain
                if (psurf->polys && psurf->polys->numverts >= 3) {
                    image_t *image = R_TextureAnimation(psurf->texinfo);
                    GL_Bind(image->texnum);
                    DrawGLPoly(psurf->polys);
                }
            }
        }
    }
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel(entity_t *e)
{
    vec3_t mins, maxs;
    int i;
    qboolean rotated;

    if (currentmodel->nummodelsurfaces == 0)
        return;

    currententity = e;
    gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

    if (e->angles[0] || e->angles[1] || e->angles[2])
    {
        rotated = true;
        for (i = 0; i < 3; i++)
        {
            mins[i] = e->origin[i] - currentmodel->radius;
            maxs[i] = e->origin[i] + currentmodel->radius;
        }
    }
    else
    {
        rotated = false;
        VectorAdd(e->origin, currentmodel->mins, mins);
        VectorAdd(e->origin, currentmodel->maxs, maxs);
    }

    if (R_CullBox(mins, maxs))
        return;

    VectorSubtract(r_newrefdef.vieworg, e->origin, modelorg);
    if (rotated)
    {
        vec3_t temp;
        vec3_t forward, right, up;

        VectorCopy(modelorg, temp);
        AngleVectors(e->angles, forward, right, up);
        modelorg[0] = DotProduct(temp, forward);
        modelorg[1] = -DotProduct(temp, right);
        modelorg[2] = DotProduct(temp, up);
    }

    // CRITICAL: Save the world matrix before rotation
    float saved_world_matrix[16];
    memcpy_fast(saved_world_matrix, r_world_matrix, sizeof(float) * 16);

    e->angles[0] = -e->angles[0];  // stupid quake bug
    e->angles[2] = -e->angles[2];  // stupid quake bug
    R_RotateForEntity(e);
    e->angles[0] = -e->angles[0];  // stupid quake bug
    e->angles[2] = -e->angles[2];  // stupid quake bug

    R_DrawInlineBModel();

    // CRITICAL: Restore the world matrix after drawing
    memcpy_fast(r_world_matrix, saved_world_matrix, sizeof(float) * 16);
}
/*
================
R_RecursiveWorldNode  
================
*/
void R_RecursiveWorldNode (mnode_t *node)
{
    int         c, side, sidebit;
    cplane_t    *plane;
    msurface_t  *surf, **mark;
    mleaf_t     *pleaf;
    float       dot;
    image_t     *image;

    if (SHZ_UNLIKELY(node->contents == CONTENTS_SOLID))
        return;

    if (SHZ_UNLIKELY(node->visframe != r_visframecount))
        return;
    
    if (SHZ_UNLIKELY(R_CullBox(node->minmaxs, node->minmaxs+3)))
        return;
    
    // If a leaf node, draw stuff
    if (node->contents != -1)
    {
        pleaf = (mleaf_t *)node;

        if (r_newrefdef.areabits)
        {
            if (SHZ_UNLIKELY(!(r_newrefdef.areabits[pleaf->area>>3] & (1<<(pleaf->area&7)))))
                return;
        }

        mark = pleaf->firstmarksurface;
        c = pleaf->nummarksurfaces;

        if (c)
        {
            SHZ_PREFETCH(mark);
            do
            {
                (*mark)->visframe = r_framecount;
                mark++;
            } while (--c);
        }

        return;
    }

    plane = node->plane;

    switch (plane->type)
    {
    case PLANE_X:
        dot = modelorg[0] - plane->dist;
        break;
    case PLANE_Y:
        dot = modelorg[1] - plane->dist;
        break;
    case PLANE_Z:
        dot = modelorg[2] - plane->dist;
        break;
    default:
        dot = DotProduct(modelorg, plane->normal) - plane->dist;
        break;
    }

    if (dot >= 0)
    {
        side = 0;
        sidebit = 0;
    }
    else
    {
        side = 1;
        sidebit = SURF_PLANEBACK;
    }

    // Prefetch back side while we process front
    SHZ_PREFETCH(node->children[!side]);
    R_RecursiveWorldNode(node->children[side]);

    // Prefetch first surface
    surf = r_worldmodel->surfaces + node->firstsurface;
    if (node->numsurfaces)
        SHZ_PREFETCH(surf);

    for (c = node->numsurfaces; c; c--, surf++)
    {
        SHZ_PREFETCH(surf + 1);
        
        if (surf->visframe != r_framecount)
            continue;

        if ((surf->flags & SURF_PLANEBACK) != sidebit)
            continue;

        if (SHZ_UNLIKELY(surf->texinfo->flags & SURF_SKY))
        {
            R_AddSkySurface(surf);
        }
        else if (SHZ_UNLIKELY(surf->texinfo->flags & (SURF_TRANS33|SURF_TRANS66)))
        {
            surf->texturechain = r_alpha_surfaces;
            r_alpha_surfaces = surf;
        }
        else
        {
            image = R_TextureAnimation(surf->texinfo);
            surf->texturechain = image->texturechain;
            image->texturechain = surf;
        }
    }

    R_RecursiveWorldNode(node->children[!side]);
}
/*
=============
R_DrawWorld - SIMPLIFIED VERSION
=============
*/
void R_DrawWorld (void)
{
	entity_t	ent;

	if (!r_drawworld->value)
		return;

	if ( r_newrefdef.rdflags & RDF_NOWORLDMODEL )
		return;

	currentmodel = r_worldmodel;

	VectorCopy (r_newrefdef.vieworg, modelorg);

	// Auto cycle the world frame for texture animation
	memset (&ent, 0, sizeof(ent));
	ent.frame = (int)(r_newrefdef.time*2);
	currententity = &ent;

	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	//qglColor3f (0.8f, 0.8f, 0.8f); // Light gray instead of white
	R_ClearSkyBox ();

	// Traverse the BSP tree
	R_RecursiveWorldNode (r_worldmodel->nodes);
	R_DrawSkyBox ();

	// Draw all the texture chains
	DrawTextureChains ();
	
	// Draw the skybox
}

/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	byte	fatvis[MAX_MAP_LEAFS/8];
	mnode_t	*node;
	int		i, c;
	mleaf_t	*leaf;
	int		cluster;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !r_novis->value && r_viewcluster != -1)
		return;

	if (gl_lockpvs->value)
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || r_viewcluster == -1 || !r_worldmodel->vis)
	{
		// Mark everything
		for (i=0 ; i<r_worldmodel->numleafs ; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;
		for (i=0 ; i<r_worldmodel->numnodes ; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;
		return;
	}

	vis = Mod_ClusterPVS (r_viewcluster, r_worldmodel);
	// May have to combine two clusters because of solid water boundaries
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy_fast (fatvis, vis, (r_worldmodel->numleafs+7)/8);
		vis = Mod_ClusterPVS (r_viewcluster2, r_worldmodel);
		c = (r_worldmodel->numleafs+31)/32;
		for (i=0 ; i<c ; i++)
			((int *)fatvis)[i] |= ((int *)vis)[i];
		vis = fatvis;
	}
	
	for (i=0,leaf=r_worldmodel->leafs ; i<r_worldmodel->numleafs ; i++, leaf++)
	{
		cluster = leaf->cluster;
		if (cluster == -1)
			continue;
		if (vis[cluster>>3] & (1<<(cluster&7)))
		{
			node = (mnode_t *)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}



/*
================
GL_BuildPolygonFromSurface
================
*/
void GL_BuildPolygonFromSurface(msurface_t *fa)
{
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	float		s, t;
	glpoly_t	*poly;
	vec3_t		total;
	vec3_t		pos;

	// Reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	VectorClear (total);
	
	// Allocate polygon
	poly = Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			pos[0] = (float)currentmodel->vertexes[r_pedge->v[0]].position[0];
			pos[1] = (float)currentmodel->vertexes[r_pedge->v[0]].position[1];
			pos[2] = (float)currentmodel->vertexes[r_pedge->v[0]].position[2];
		}
		else
		{
			r_pedge = &pedges[-lindex];
			pos[0] = (float)currentmodel->vertexes[r_pedge->v[1]].position[0];
			pos[1] = (float)currentmodel->vertexes[r_pedge->v[1]].position[1];
			pos[2] = (float)currentmodel->vertexes[r_pedge->v[1]].position[2];
		}
		
		// Texture coordinates
		s = DotProduct (pos, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct (pos, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;

		VectorAdd (total, pos, total);
		VectorCopy (pos, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// Lightmap texture coordinates (not used but kept for compatibility)
		s = DotProduct (pos, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= 128*16;

		t = DotProduct (pos, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= 128*16;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	poly->numverts = lnumverts;
}

