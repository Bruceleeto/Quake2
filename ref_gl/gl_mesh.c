#include "gl_local.h"

#define NUMVERTEXNORMALS 162
float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

typedef float vec4_t[4];
static vec4_t s_lerped[MAX_VERTS];

extern float r_world_matrix[16];
vec3_t shadevector;
float shadelight[3];

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anormtab.h"
;

float *shadedots = r_avertexnormal_dots[0];

void GL_LerpVerts(int nverts, dtrivertx_t *v, dtrivertx_t *ov, dtrivertx_t *verts, float *lerp, float move[3], float frontv[3], float backv[3])
{
    // Hoist constants outside loop
    shz_vec3_t mv = shz_vec3_init(move[0], move[1], move[2]);
    shz_vec3_t fv = shz_vec3_init(frontv[0], frontv[1], frontv[2]);
    shz_vec3_t bv = shz_vec3_init(backv[0], backv[1], backv[2]);
    
    int shell_flags = currententity->flags & (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM);
    
    if (shell_flags)
    {
        for (int i = 0; i < nverts; i++, v++, ov++, lerp += 4)
        {
            // Prefetch next iteration's data
            SHZ_PREFETCH(v + 1);
            SHZ_PREFETCH(ov + 1);
            
            float *normal = r_avertexnormals[verts[i].lightnormalindex];
            
            shz_vec3_t old_v = shz_vec3_init((float)ov->v[0], (float)ov->v[1], (float)ov->v[2]);
            shz_vec3_t new_v = shz_vec3_init((float)v->v[0], (float)v->v[1], (float)v->v[2]);
            shz_vec3_t norm = shz_vec3_init(normal[0], normal[1], normal[2]);
            
            shz_vec3_t result = shz_vec3_add(mv, shz_vec3_mul(old_v, bv));
            result = shz_vec3_add(result, shz_vec3_mul(new_v, fv));
            result = shz_vec3_add(result, shz_vec3_scale(norm, POWERSUIT_SCALE));
            
            lerp[0] = result.x;
            lerp[1] = result.y;
            lerp[2] = result.z;
        }
    }
    else
    {
        for (int i = 0; i < nverts; i++, v++, ov++, lerp += 4)
        {
            SHZ_PREFETCH(v + 1);
            SHZ_PREFETCH(ov + 1);
            
            shz_vec3_t old_v = shz_vec3_init((float)ov->v[0], (float)ov->v[1], (float)ov->v[2]);
            shz_vec3_t new_v = shz_vec3_init((float)v->v[0], (float)v->v[1], (float)v->v[2]);
            
            shz_vec3_t result = shz_vec3_add(mv, shz_vec3_mul(old_v, bv));
            result = shz_vec3_add(result, shz_vec3_mul(new_v, fv));
            
            lerp[0] = result.x;
            lerp[1] = result.y;
            lerp[2] = result.z;
        }
    }
}

void GL_DrawAliasFrameLerp(dmdl_t *paliashdr, float backlerp)
{
    daliasframe_t *frame, *oldframe;
    dtrivertx_t *v, *ov, *verts;
    int *order;
    int count;
    float frontlerp;
    vec3_t move, delta, vectors[3];
    vec3_t frontv, backv;
    int i;
    float *lerp;
    pvr_dr_state_t dr_state;
    float alpha;
    
    frame = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
        + currententity->frame * paliashdr->framesize);
    verts = v = frame->verts;

    oldframe = (daliasframe_t *)((byte *)paliashdr + paliashdr->ofs_frames 
        + currententity->oldframe * paliashdr->framesize);
    ov = oldframe->verts;

    order = (int *)((byte *)paliashdr + paliashdr->ofs_glcmds);
    
    alpha = (currententity->flags & RF_TRANSLUCENT) ? currententity->alpha : 1.0f;
    
    frontlerp = 1.0f - backlerp;

    // Precalculate movement vectors
    VectorSubtract(currententity->oldorigin, currententity->origin, delta);
    AngleVectors(currententity->angles, vectors[0], vectors[1], vectors[2]);

    move[0] = DotProduct(delta, vectors[0]);
    move[1] = -DotProduct(delta, vectors[1]);
    move[2] = DotProduct(delta, vectors[2]);

    VectorAdd(move, oldframe->translate, move);

    for (i = 0; i < 3; i++)
    {
        move[i] = backlerp * move[i] + frontlerp * frame->translate[i];
        frontv[i] = frontlerp * frame->scale[i];
        backv[i] = backlerp * oldframe->scale[i];
    }

    lerp = s_lerped[0];
    GL_LerpVerts(paliashdr->num_xyz, v, ov, verts, lerp, move, frontv, backv);

    // Get current skin texture
    image_t *skin = currententity->skin;
    if (!skin)
    {
        int skinnum = (currententity->skinnum >= MAX_MD2SKINS) ? 0 : currententity->skinnum;
        skin = currentmodel->skins[skinnum];
        if (!skin) skin = currentmodel->skins[0];
    }
    if (!skin) skin = r_notexture;

    pvr_dr_init(&dr_state);
    
    // Determine which list to use
    int pvr_list = (currententity->flags & RF_TRANSLUCENT) ? 
                   PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
    
    // Check for shell effects
    uint32_t shell_flags = currententity->flags & 
        (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM);
    
    // Precalculate shell color if needed
    uint32_t shell_color = 0xFF000000;
    if (shell_flags)
    {
        if (currententity->flags & RF_SHELL_RED) shell_color |= 0x00FF0000;
        if (currententity->flags & RF_SHELL_GREEN) shell_color |= 0x0000FF00;
        if (currententity->flags & RF_SHELL_BLUE) shell_color |= 0x000000FF;
        
        if (currententity->flags & RF_TRANSLUCENT) {
            shell_color = ((uint32_t)(alpha * 255.0f) << 24) | (shell_color & 0x00FFFFFF);
        }
        
        pvr_poly_cxt_t cxt;
        pvr_poly_cxt_col(&cxt, pvr_list);
        
        if (currententity->flags & RF_TRANSLUCENT) {
            cxt.gen.alpha = PVR_ALPHA_ENABLE;
            cxt.blend.src = PVR_BLEND_SRCALPHA;
            cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
            cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
        }
        
        cxt.gen.culling = PVR_CULLING_CCW;
        
        pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
        pvr_poly_compile(hdr, &cxt);
        pvr_dr_commit(hdr);
    }
    else
    {
        // Normal textured rendering
        int texnum = skin->texnum;
        pvr_ptr_t tex_addr = NULL;
        uint32_t tex_format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED;
        int tex_width = 256, tex_height = 256;
        
        if (texnum > 0 && texnum < MAX_GLTEXTURES && pvr_textures[texnum].loaded) {
            tex_addr = pvr_textures[texnum].ptr;
            tex_format = pvr_textures[texnum].format;
            tex_width = pvr_textures[texnum].width;
            tex_height = pvr_textures[texnum].height;
        }
        
        pvr_poly_cxt_t cxt;
        if (tex_addr) {
            pvr_poly_cxt_txr(&cxt, pvr_list, tex_format, 
                            tex_width, tex_height, tex_addr, PVR_FILTER_BILINEAR);
        } else {
            pvr_poly_cxt_col(&cxt, pvr_list);
        }
        
        if (currententity->flags & RF_TRANSLUCENT) {
            cxt.gen.alpha = PVR_ALPHA_ENABLE;
            cxt.blend.src = PVR_BLEND_SRCALPHA;
            cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
            cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
        }
        
        cxt.gen.culling = PVR_CULLING_CCW;
        
        pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
        pvr_poly_compile(hdr, &cxt);
        pvr_dr_commit(hdr);
    }

    // Load matrix ONCE for all vertices
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
    
    shz_vec4_t transformed[64];
    float inv_w[64];
    int indices[64];
    float u_coords[64], v_coords[64];
    uint32_t vertex_colors[64];
    
    // Precalculate UV scaling
    float inv_skin_width = shz_invf((float)skin->width);
    float inv_skin_height = shz_invf((float)skin->height);
    float u_scale = (float)paliashdr->skinwidth * inv_skin_width;
    float v_scale = (float)paliashdr->skinheight * inv_skin_height;
    
    // Precalculate alpha component
    uint32_t alpha_byte = (uint32_t)(alpha * 255.0f) << 24;
    
    while (1)
    {
        count = *order++;
        if (SHZ_UNLIKELY(!count)) break;
        
        int is_fan = (count < 0);
        if (is_fan) count = -count;
        
        // Prefetch next batch
        SHZ_PREFETCH(order + count * 3);
        
        // Load vertex data
        for (i = 0; i < count; i++)
        {
            u_coords[i] = ((float *)order)[0] * u_scale;
            v_coords[i] = ((float *)order)[1] * v_scale;
            order += 2;
            indices[i] = *order++;
        }
        
        // Transform all vertices using FTRV
        for (i = 0; i < count; i++)
        {
            int idx = indices[i];
            SHZ_PREFETCH(&s_lerped[indices[i + 1]]);
            
            shz_vec3_t pos = shz_vec3_init(s_lerped[idx][0], s_lerped[idx][1], s_lerped[idx][2]);
            transformed[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(pos, 1.0f));
        }
        
        // Precalculate all inv_w values using FSRRA
        for (i = 0; i < count; i++)
        {
            inv_w[i] = shz_invf_fsrra(transformed[i].w);
        }
        
        // Precalculate vertex colors if not using shell
        if (!shell_flags)
        {
            for (i = 0; i < count; i++)
            {
                float l = shadedots[verts[indices[i]].lightnormalindex];
                
                float r = l * shadelight[0];
                float g = l * shadelight[1];
                float b = l * shadelight[2];
                
                // Branchless clamp
                r = (r > 1.0f) ? 1.0f : r;
                g = (g > 1.0f) ? 1.0f : g;
                b = (b > 1.0f) ? 1.0f : b;
                
                vertex_colors[i] = alpha_byte |
                                  ((uint32_t)(r * 255.0f) << 16) |
                                  ((uint32_t)(g * 255.0f) << 8) |
                                  ((uint32_t)(b * 255.0f));
            }
        }
        
        // Render triangles
        if (is_fan)
        {
            // Triangle fan - cache vertex 0 values
            float x0 = transformed[0].x * inv_w[0];
            float y0 = transformed[0].y * inv_w[0];
            float z0 = inv_w[0];
            uint32_t color0 = shell_flags ? shell_color : vertex_colors[0];
            
            for (i = 1; i < count - 1; i++)
            {
                // Near plane cull
                if (SHZ_UNLIKELY(transformed[0].w < 0.1f || 
                                 transformed[i].w < 0.1f || 
                                 transformed[i+1].w < 0.1f))
                    continue;
                
                pvr_vertex_t *vert;
                
                // Vertex 0
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX;
                vert->x = x0;
                vert->y = y0;
                vert->z = z0;
                vert->u = u_coords[0];
                vert->v = v_coords[0];
                vert->argb = color0;
                vert->oargb = 0;
                pvr_dr_commit(vert);
                
                // Vertex i
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX;
                vert->x = transformed[i].x * inv_w[i];
                vert->y = transformed[i].y * inv_w[i];
                vert->z = inv_w[i];
                vert->u = u_coords[i];
                vert->v = v_coords[i];
                vert->argb = shell_flags ? shell_color : vertex_colors[i];
                vert->oargb = 0;
                pvr_dr_commit(vert);
                
                // Vertex i+1
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX_EOL;
                vert->x = transformed[i+1].x * inv_w[i+1];
                vert->y = transformed[i+1].y * inv_w[i+1];
                vert->z = inv_w[i+1];
                vert->u = u_coords[i+1];
                vert->v = v_coords[i+1];
                vert->argb = shell_flags ? shell_color : vertex_colors[i+1];
                vert->oargb = 0;
                pvr_dr_commit(vert);
            }
        }
        else
        {
            // Triangle strip
            for (i = 2; i < count; i++)
            {
                int idx0, idx1, idx2;
                
                if (i & 1)
                {
                    idx0 = i - 1;
                    idx1 = i - 2;
                    idx2 = i;
                }
                else
                {
                    idx0 = i - 2;
                    idx1 = i - 1;
                    idx2 = i;
                }
                
                // Near plane cull
                if (SHZ_UNLIKELY(transformed[idx0].w < 0.1f || 
                                 transformed[idx1].w < 0.1f || 
                                 transformed[idx2].w < 0.1f))
                    continue;
                
                pvr_vertex_t *vert;
                
                // First vertex
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX;
                vert->x = transformed[idx0].x * inv_w[idx0];
                vert->y = transformed[idx0].y * inv_w[idx0];
                vert->z = inv_w[idx0];
                vert->u = u_coords[idx0];
                vert->v = v_coords[idx0];
                vert->argb = shell_flags ? shell_color : vertex_colors[idx0];
                vert->oargb = 0;
                pvr_dr_commit(vert);
                
                // Second vertex
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX;
                vert->x = transformed[idx1].x * inv_w[idx1];
                vert->y = transformed[idx1].y * inv_w[idx1];
                vert->z = inv_w[idx1];
                vert->u = u_coords[idx1];
                vert->v = v_coords[idx1];
                vert->argb = shell_flags ? shell_color : vertex_colors[idx1];
                vert->oargb = 0;
                pvr_dr_commit(vert);
                
                // Third vertex
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX_EOL;
                vert->x = transformed[idx2].x * inv_w[idx2];
                vert->y = transformed[idx2].y * inv_w[idx2];
                vert->z = inv_w[idx2];
                vert->u = u_coords[idx2];
                vert->v = v_coords[idx2];
                vert->argb = shell_flags ? shell_color : vertex_colors[idx2];
                vert->oargb = 0;
                pvr_dr_commit(vert);
            }
        }
    }
    
    pvr_dr_finish();   
}

void R_DrawAliasModel(entity_t *e)
{
    dmdl_t *paliashdr;
    int i;
    float an;
    image_t *skin;

    if (!currentmodel || !currentmodel->extradata)
        return;

    paliashdr = (dmdl_t *)currentmodel->extradata;

    // Clamp frame numbers
    if ((e->frame >= paliashdr->num_frames) || (e->frame < 0))
        e->frame = 0;
    if ((e->oldframe >= paliashdr->num_frames) || (e->oldframe < 0))
        e->oldframe = 0;

    // Calculate lighting
    if (currententity->flags & (RF_SHELL_HALF_DAM | RF_SHELL_GREEN | RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE))
    {
        if ((currententity->flags & RF_SHELL_RED) &&
            (currententity->flags & RF_SHELL_BLUE) &&
            (currententity->flags & RF_SHELL_GREEN))
        {
            for (i = 0; i < 3; i++)
                shadelight[i] = 1.0;
        }
        else if (currententity->flags & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE))
        {
            VectorClear(shadelight);
            if (currententity->flags & RF_SHELL_RED)
            {
                shadelight[0] = 1.0;
                if (currententity->flags & (RF_SHELL_BLUE | RF_SHELL_DOUBLE))
                    shadelight[2] = 1.0;
            }
            else if (currententity->flags & RF_SHELL_BLUE)
            {
                if (currententity->flags & RF_SHELL_DOUBLE)
                {
                    shadelight[1] = 1.0;
                    shadelight[2] = 1.0;
                }
                else
                {
                    shadelight[2] = 1.0;
                }
            }
            else if (currententity->flags & RF_SHELL_DOUBLE)
            {
                shadelight[0] = 0.9;
                shadelight[1] = 0.7;
            }
        }
        else if (currententity->flags & (RF_SHELL_HALF_DAM | RF_SHELL_GREEN))
        {
            VectorClear(shadelight);
            if (currententity->flags & RF_SHELL_HALF_DAM)
            {
                shadelight[0] = 0.56;
                shadelight[1] = 0.59;
                shadelight[2] = 0.45;
            }
            if (currententity->flags & RF_SHELL_GREEN)
            {
                shadelight[1] = 1.0;
            }
        }
    }
    else if (currententity->flags & RF_FULLBRIGHT)
    {
        for (i = 0; i < 3; i++)
            shadelight[i] = 1.0;
    }
    else
    {
        R_LightPoint(currententity->origin, shadelight);
        
        if (currententity->flags & RF_WEAPONMODEL)
        {
            if (shadelight[0] > shadelight[1])
            {
                if (shadelight[0] > shadelight[2])
                    r_lightlevel->value = 150 * shadelight[0];
                else
                    r_lightlevel->value = 150 * shadelight[2];
            }
            else
            {
                if (shadelight[1] > shadelight[2])
                    r_lightlevel->value = 150 * shadelight[1];
                else
                    r_lightlevel->value = 150 * shadelight[2];
            }
        }
        
        if (gl_monolightmap->string[0] != '0')
        {
            float s = shadelight[0];
            if (s < shadelight[1])
                s = shadelight[1];
            if (s < shadelight[2])
                s = shadelight[2];
            shadelight[0] = s;
            shadelight[1] = s;
            shadelight[2] = s;
        }
    }

    if (currententity->flags & RF_MINLIGHT)
    {
        for (i = 0; i < 3; i++)
            if (shadelight[i] > 0.1)
                break;
        if (i == 3)
        {
            shadelight[0] = 0.1;
            shadelight[1] = 0.1;
            shadelight[2] = 0.1;
        }
    }

    if (currententity->flags & RF_GLOW)
    {
        float scale = 0.1 * sin(r_newrefdef.time * 7);
        for (i = 0; i < 3; i++)
        {
            float min = shadelight[i] * 0.8;
            shadelight[i] += scale;
            if (shadelight[i] < min)
                shadelight[i] = min;
        }
    }

    if (r_newrefdef.rdflags & RDF_IRGOGGLES && currententity->flags & RF_IR_VISIBLE)
    {
        shadelight[0] = 1.0;
        shadelight[1] = 0.0;
        shadelight[2] = 0.0;
    }

    shadedots = r_avertexnormal_dots[((int)(currententity->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
    
    shz_sincos_t sc = shz_sincosf(-currententity->angles[1] * SHZ_DEG_TO_RAD(1.0f));
    shadevector[0] = sc.cos;
    shadevector[1] = sc.sin;
    shadevector[2] = 1;
    VectorNormalize(shadevector);

    c_alias_polys += paliashdr->num_tris;

    // Save base world matrix
    float saved_world_matrix[16];
    memcpy_fast(saved_world_matrix, r_world_matrix, sizeof(float) * 16);
    
    // Apply entity transform (this modifies r_world_matrix)
    e->angles[PITCH] = -e->angles[PITCH];
    R_RotateForEntity(e);
    e->angles[PITCH] = -e->angles[PITCH];

    if (!r_lerpmodels->value)
        currententity->backlerp = 0;

    GL_DrawAliasFrameLerp(paliashdr, currententity->backlerp);
    
    // Restore base world matrix
    memcpy_fast(r_world_matrix, saved_world_matrix, sizeof(float) * 16);
}