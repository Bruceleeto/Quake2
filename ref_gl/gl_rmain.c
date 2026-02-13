
#include "gl_local.h"

void R_Clear (void);

viddef_t	vid;

refimport_t	ri;

model_t		*r_worldmodel;

float		gldepthmin, gldepthmax;

glconfig_t gl_config;
glstate_t  gl_state;

image_t		*r_notexture;		// use for bad textures
image_t		*r_particletexture;	// little dot for particles

entity_t	*currententity;
model_t		*currentmodel;

cplane_t	frustum[4];

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

int			c_brush_polys, c_alias_polys;

float		v_blend[4];			// final blending color


//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float r_world_matrix[16] __attribute__((aligned(32)));
float	r_base_world_matrix[16] __attribute__((aligned(32)));


//
// screen size info
//
refdef_t	r_newrefdef;

int		r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_lerpmodels;
cvar_t	*r_lefthand;
cvar_t	*r_lightlevel;	// FIXME: This is a HACK to get the client's light level
cvar_t	*gl_vertex_arrays;
cvar_t	*gl_particle_min_size;
cvar_t	*gl_particle_max_size;
cvar_t	*gl_particle_size;
cvar_t	*gl_particle_att_a;
cvar_t	*gl_particle_att_b;
cvar_t	*gl_particle_att_c;
cvar_t	*gl_log;
cvar_t	*gl_bitdepth;
cvar_t	*gl_drawbuffer;
cvar_t  *gl_driver;
cvar_t	*gl_lightmap;
cvar_t	*gl_shadows;
cvar_t	*gl_mode;
cvar_t	*gl_dynamic;
cvar_t  *gl_monolightmap;
cvar_t	*gl_modulate;
cvar_t	*gl_nobind;
cvar_t	*gl_round_down;
cvar_t	*gl_picmip;
cvar_t	*gl_skymip;
cvar_t	*gl_finish;
cvar_t	*gl_clear;
cvar_t	*gl_cull;
cvar_t	*gl_polyblend;
cvar_t	*gl_flashblend;
cvar_t	*gl_playermip;
cvar_t  *gl_saturatelighting;
cvar_t	*gl_swapinterval;
cvar_t	*gl_texturemode;
cvar_t	*gl_texturealphamode;
cvar_t	*gl_texturesolidmode;
cvar_t	*gl_lockpvs;



extern //because vid_fullscreen also exists in client
cvar_t	*vid_fullscreen;

extern //because vid_gamma also exists in client
cvar_t	*vid_gamma;

extern //because vid_ref also exists in client
cvar_t	*vid_ref;
#define PI_OVER_360 (M_PI / 360.0f)  // 0.00872665f
#define RAD_NEG90 (-1.570796f)        // -90 degrees in radians
#define RAD_90 (1.570796f)            // 90 degrees in radians
static int cached_width = 0, cached_height = 0;


 
/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox(vec3_t mins, vec3_t maxs)
{
    
    int i;
    
    if (r_nocull->value)
        return false;
    
    for (i = 0; i < 4; i++)
        if (BOX_ON_PLANE_SIDE(mins, maxs, &frustum[i]) == 2)
            return true;
    return false;
}


void R_RotateForEntity(entity_t *e)
{
    float yaw   =  e->angles[1] * SHZ_DEG_TO_RAD(1.0f);
    float pitch = -e->angles[0] * SHZ_DEG_TO_RAD(1.0f);
    float roll  = -e->angles[2] * SHZ_DEG_TO_RAD(1.0f);
    
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
    shz_xmtrx_translate(e->origin[0], e->origin[1], e->origin[2]);
    shz_xmtrx_rotate_z(yaw);
    shz_xmtrx_rotate_y(pitch);
    shz_xmtrx_rotate_x(roll);
    shz_xmtrx_store_4x4((shz_mat4x4_t*)r_world_matrix);
}


/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel(entity_t *e)
{
    float alpha;
    vec3_t point;
    dsprframe_t *frame;
    float *up, *right;
    dsprite_t *psprite;

    if (!currentmodel || !currentmodel->extradata)
        return;

    psprite = (dsprite_t *)currentmodel->extradata;
    e->frame %= psprite->numframes;
    frame = &psprite->frames[e->frame];

    // Billboard: face camera
    up = vup;
    right = vright;

    alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;

    // Get texture
    image_t *skin = currentmodel->skins[e->frame];
    if (!skin) skin = r_notexture;

    int texnum = skin->texnum;
    if (texnum <= 0 || texnum >= MAX_GLTEXTURES || !pvr_textures[texnum].loaded)
        return;

    // Choose list based on translucency
    int pvr_list = (alpha < 1.0f) ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;

    pvr_dr_state_t dr_state;
    pvr_dr_init(&dr_state);

    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, pvr_list,
                    pvr_textures[texnum].format,
                    pvr_textures[texnum].width,
                    pvr_textures[texnum].height,
                    pvr_textures[texnum].ptr,
                    PVR_FILTER_BILINEAR);

    cxt.gen.culling = PVR_CULLING_NONE;

    if (alpha < 1.0f) {
        cxt.gen.alpha = PVR_ALPHA_ENABLE;
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }

    pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);

    uint32_t argb = ((uint32_t)(alpha * 255.0f) << 24) | 0x00FFFFFF;

    // Build 4 corner vertices (same as original GL_QUADS order)
    vec3_t corners[4];
    float uvs[4][2] = { {0,1}, {0,0}, {1,0}, {1,1} };

    // Corner 0: bottom-left
    VectorMA(e->origin, -frame->origin_y, up, point);
    VectorMA(point, -frame->origin_x, right, corners[0]);

    // Corner 1: top-left
    VectorMA(e->origin, frame->height - frame->origin_y, up, point);
    VectorMA(point, -frame->origin_x, right, corners[1]);

    // Corner 2: top-right
    VectorMA(e->origin, frame->height - frame->origin_y, up, point);
    VectorMA(point, frame->width - frame->origin_x, right, corners[2]);

    // Corner 3: bottom-right
    VectorMA(e->origin, -frame->origin_y, up, point);
    VectorMA(point, frame->width - frame->origin_x, right, corners[3]);

    // Transform all 4 corners
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);

    shz_vec4_t transformed[4];
    for (int i = 0; i < 4; i++) {
        shz_vec3_t pos = shz_vec3_init(corners[i][0], corners[i][1], corners[i][2]);
        transformed[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(pos, 1.0f));
    }

    // Draw as 2 triangles: (0,1,2) and (0,2,3)
    // Using ClipAndSubmitTriangle for safety near the camera
    float uv0[2] = {uvs[0][0], uvs[0][1]};
    float uv1[2] = {uvs[1][0], uvs[1][1]};
    float uv2[2] = {uvs[2][0], uvs[2][1]};
    float uv3[2] = {uvs[3][0], uvs[3][1]};

    ClipAndSubmitTriangle(transformed[0], transformed[1], transformed[2],
                          uv0, uv1, uv2, &dr_state, argb);
    ClipAndSubmitTriangle(transformed[0], transformed[2], transformed[3],
                          uv0, uv2, uv3, &dr_state, argb);

    pvr_dr_finish();
}



/*
=============
R_DrawNullModel
=============
*/
void R_DrawNullModel (void)
{
    
}



/*
=============
R_DrawOpaqueEntities
Draws all non-translucent entities
=============
*/
void R_DrawOpaqueEntities(void)
{
    int i;
    vec3_t mins, maxs;
    
    if (!r_drawentities->value)
        return;
    
    for (i = 0; i < r_newrefdef.num_entities; i++)
    {
        currententity = &r_newrefdef.entities[i];
        
        // Skip translucent entities
        if (currententity->flags & RF_TRANSLUCENT)
            continue;
        
        if (currententity->flags & RF_BEAM)
        {
            R_DrawBeam(currententity);
        }
        else
        {
            currentmodel = currententity->model;
            if (!currentmodel)
                continue;
            
            // Frustum culling
            if (currentmodel->type == mod_alias || currentmodel->type == mod_brush)
            {
                VectorAdd(currententity->origin, currentmodel->mins, mins);
                VectorAdd(currententity->origin, currentmodel->maxs, maxs);
                
                if (R_CullBox(mins, maxs))
                    continue;
            }
            
            switch (currentmodel->type)
            {
            case mod_alias:
                R_DrawAliasModel(currententity);
                break;
            case mod_brush:
                R_DrawBrushModel(currententity);
                break;
            case mod_sprite:
                R_DrawSpriteModel(currententity);
                break;
            default:
                ri.Sys_Error(ERR_DROP, "Bad modeltype");
                break;
            }
        }
    }
}

/*
=============
R_DrawTranslucentEntities
Draws all translucent entities
=============
*/
void R_DrawTranslucentEntities(void)
{
    int i;
    vec3_t mins, maxs;
    
    if (!r_drawentities->value)
        return;
    
    for (i = 0; i < r_newrefdef.num_entities; i++)
    {
        currententity = &r_newrefdef.entities[i];
        
        // Skip non-translucent entities
        if (!(currententity->flags & RF_TRANSLUCENT))
            continue;
        
        if (currententity->flags & RF_BEAM)
        {
             R_DrawBeam(currententity);  //not tested. 
        }
        else
        {
            currentmodel = currententity->model;
            if (!currentmodel)
                continue;
            
            // Frustum culling
            if (currentmodel->type == mod_alias || currentmodel->type == mod_brush)
            {
                VectorAdd(currententity->origin, currentmodel->mins, mins);
                VectorAdd(currententity->origin, currentmodel->maxs, maxs);
                
                if (R_CullBox(mins, maxs))
                    continue;
            }
            
            switch (currentmodel->type)
            {
            case mod_alias:
                R_DrawAliasModel(currententity);
                break;
            case mod_brush:
                R_DrawBrushModel(currententity);
                break;
            case mod_sprite:
                R_DrawSpriteModel(currententity);
                break;
            default:
                ri.Sys_Error(ERR_DROP, "Bad modeltype");
                break;
            }
        }
    }
}




/*
** GL_DrawParticles
**
*/
void GL_DrawParticles(int num_particles, const particle_t particles[], const unsigned colortable[256])
{
    const particle_t *p;
    int i;
    float scale;
    pvr_dr_state_t dr_state;
    
    if (SHZ_UNLIKELY(num_particles <= 0))
        return;
    
    // Get particle texture info
    int texnum = r_particletexture->texnum;
    if (SHZ_UNLIKELY(texnum <= 0 || texnum >= MAX_GLTEXTURES || !pvr_textures[texnum].loaded))
        return;
    
    pvr_dr_init(&dr_state);
    
    // Setup translucent polygon context with alpha blending
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, 
                    pvr_textures[texnum].format,
                    pvr_textures[texnum].width, 
                    pvr_textures[texnum].height,
                    pvr_textures[texnum].ptr, 
                    PVR_FILTER_BILINEAR);
    
    cxt.gen.alpha = PVR_ALPHA_ENABLE;
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    
    pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);
    
    // Pre-scale view vectors
    shz_vec3_t up = shz_vec3_scale(shz_vec3_init(vup[0], vup[1], vup[2]), 1.5f);
    shz_vec3_t right = shz_vec3_scale(shz_vec3_init(vright[0], vright[1], vright[2]), 1.5f);
    shz_vec3_t origin = shz_vec3_init(r_origin[0], r_origin[1], r_origin[2]);
    shz_vec3_t vpn_v = shz_vec3_init(vpn[0], vpn[1], vpn[2]);
    
    // Load matrix ONCE for all particles
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
    
    // Prefetch first particle
    SHZ_PREFETCH(particles);
    
    // Draw each particle as a single triangle
    for (p = particles, i = 0; i < num_particles; i++, p++) {
        // Prefetch next particle
        SHZ_PREFETCH(p + 1);
        
        shz_vec3_t p_origin = shz_vec3_init(p->origin[0], p->origin[1], p->origin[2]);
        
        // Calculate distance scale using dot product
        shz_vec3_t delta = shz_vec3_sub(p_origin, origin);
        scale = shz_vec3_dot(delta, vpn_v);
        
        if (scale < 20.0f)
            scale = 1.0f;
        else
            scale = 1.0f + scale * 0.004f;
        
        // Get color from palette
        unsigned int color_entry = colortable[p->color];
        byte r = (color_entry >> 0) & 0xFF;
        byte g = (color_entry >> 8) & 0xFF;
        byte b = (color_entry >> 16) & 0xFF;
        byte a = (byte)(p->alpha * 255);
        uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
        
        // Calculate triangle vertices using sh4zam vectors
        // v0: particle origin
        // v1: origin + up*scale
        // v2: origin + right*scale
        shz_vec3_t v0 = p_origin;
        shz_vec3_t v1 = shz_vec3_add(p_origin, shz_vec3_scale(up, scale));
        shz_vec3_t v2 = shz_vec3_add(p_origin, shz_vec3_scale(right, scale));
        
        // Transform vertices via XMTRX
        shz_vec4_t t0 = shz_xmtrx_transform_vec4(shz_vec3_vec4(v0, 1.0f));
        shz_vec4_t t1 = shz_xmtrx_transform_vec4(shz_vec3_vec4(v1, 1.0f));
        shz_vec4_t t2 = shz_xmtrx_transform_vec4(shz_vec3_vec4(v2, 1.0f));
        
        // Near plane cull - skip if any vertex behind
        if (SHZ_UNLIKELY(t0.w < 0.1f || t1.w < 0.1f || t2.w < 0.1f))
            continue;
        
        // Fast 1/W using FSRRA
        float inv_w0 = shz_invf_fsrra(t0.w);
        float inv_w1 = shz_invf_fsrra(t1.w);
        float inv_w2 = shz_invf_fsrra(t2.w);
        
        // Submit triangle
        pvr_vertex_t *vert;
        
        vert = pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = t0.x * inv_w0;
        vert->y = t0.y * inv_w0;
        vert->z = inv_w0;
        vert->u = 0.0625f;
        vert->v = 0.0625f;
        vert->argb = argb;
        vert->oargb = 0;
        pvr_dr_commit(vert);
        
        vert = pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = t1.x * inv_w1;
        vert->y = t1.y * inv_w1;
        vert->z = inv_w1;
        vert->u = 1.0625f;
        vert->v = 0.0625f;
        vert->argb = argb;
        vert->oargb = 0;
        pvr_dr_commit(vert);
        
        vert = pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX_EOL;
        vert->x = t2.x * inv_w2;
        vert->y = t2.y * inv_w2;
        vert->z = inv_w2;
        vert->u = 0.0625f;
        vert->v = 1.0625f;
        vert->argb = argb;
        vert->oargb = 0;
        pvr_dr_commit(vert);
    }
    
    pvr_dr_finish();
}

/*
===============
R_DrawParticles
===============
*/
void R_DrawParticles(void)
{
     if (r_newrefdef.num_particles > 0) {
        GL_DrawParticles(r_newrefdef.num_particles, r_newrefdef.particles, d_8to24table);
    }
 }

/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
 
}

//=======================================================================

int SignbitsForPlane (cplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}


void R_SetFrustum (void)
{
	int		i;


	// rotate VPN right by FOV_X/2 degrees
	RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_newrefdef.fov_x / 2 ) );
	// rotate VPN left by FOV_X/2 degrees
	RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_newrefdef.fov_x / 2 );
	// rotate VPN up by FOV_X/2 degrees
	RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_newrefdef.fov_y / 2 );
	// rotate VPN down by FOV_X/2 degrees
	RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_newrefdef.fov_y / 2 ) );

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

//=======================================================================

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	int i;
	mleaf_t	*leaf;

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_newrefdef.vieworg, r_origin);

	AngleVectors (r_newrefdef.viewangles, vpn, vright, vup);

// current viewcluster
	if ( !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		r_oldviewcluster = r_viewcluster;
		r_oldviewcluster2 = r_viewcluster2;
		leaf = Mod_PointInLeaf (r_origin, r_worldmodel);
		r_viewcluster = r_viewcluster2 = leaf->cluster;

		// check above and below so crossing solid water doesn't draw wrong
		if (!leaf->contents)
		{	// look down a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] -= 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
		else
		{	// look up a bit
			vec3_t	temp;

			VectorCopy (r_origin, temp);
			temp[2] += 16;
			leaf = Mod_PointInLeaf (temp, r_worldmodel);
			if ( !(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != r_viewcluster2) )
				r_viewcluster2 = leaf->cluster;
		}
	}

	for (i=0 ; i<4 ; i++)
		v_blend[i] = r_newrefdef.blend[i];

	c_brush_polys = 0;
	c_alias_polys = 0;

  
}


 
// Keep the constant
const float quake_coord_matrix[16] __attribute__((aligned(8))) = {
     0,  0, -1,  0,
    -1,  0,  0,  0,
     0,  1,  0,  0,
     0,  0,  0,  1
};

// 640x480 screen matrix - column major
const float screen_matrix_640x480[16] __attribute__((aligned(8))) = {
    320,    0,  0,  0,   // column 0
      0, -240,  0,  0,   // column 1
      0,    0,  1,  0,   // column 2
    320,  240,  0,  1    // column 3
};

void R_SetupGL(void)
{
    float screenaspect = (float)r_newrefdef.width / r_newrefdef.height;
    float fov_rad = r_newrefdef.fov_y * SHZ_DEG_TO_RAD(1.0f);
    
    float roll  = -r_newrefdef.viewangles[2] * SHZ_DEG_TO_RAD(1.0f);
    float pitch = -r_newrefdef.viewangles[0] * SHZ_DEG_TO_RAD(1.0f);
    float yaw   = -r_newrefdef.viewangles[1] * SHZ_DEG_TO_RAD(1.0f);
    
    shz_xmtrx_load_4x4((shz_mat4x4_t*)screen_matrix_640x480);
    shz_xmtrx_apply_perspective(fov_rad, screenaspect, 4.0f);
    shz_xmtrx_apply_4x4((shz_mat4x4_t*)quake_coord_matrix);
    shz_xmtrx_rotate_x(roll);
    shz_xmtrx_rotate_y(pitch);
    shz_xmtrx_rotate_z(yaw);
    shz_xmtrx_translate(-r_newrefdef.vieworg[0], 
                        -r_newrefdef.vieworg[1], 
                        -r_newrefdef.vieworg[2]);
    
    shz_xmtrx_store_4x4((shz_mat4x4_t*)r_world_matrix);
}
/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	 
 	 

}

void R_Flash( void )
{
	//R_PolyBlend ();
}

/*
================
R_RenderView

r_newrefdef must be set before the first call
================
*/
void R_RenderView (refdef_t *fd)
{
 

	if (r_norefresh->value)
		return;

	r_newrefdef = *fd;

	if (!r_worldmodel && !( r_newrefdef.rdflags & RDF_NOWORLDMODEL ) )
		ri.Sys_Error (ERR_DROP, "R_RenderView: NULL worldmodel");


	R_PushDlights ();


	R_SetupFrame ();

	R_SetFrustum ();
 
	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

    pvr_list_begin(PVR_LIST_OP_POLY);
    R_DrawWorld ();
    R_DrawOpaqueEntities(); 
    pvr_list_finish();


 

    pvr_list_begin(PVR_LIST_TR_POLY);
	R_DrawParticles ();
	R_DrawTranslucentEntities();
    R_DrawAlphaSurfaces ();
    R_RenderDlights ();
    pvr_list_finish();

	R_Flash();

    
	
}


 
 

/*
====================
R_SetLightLevel

====================
*/
void R_SetLightLevel (void)
{
	vec3_t		shadelight;

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		return;

	// save off light value for server to look at (BIG HACK!)

	R_LightPoint (r_newrefdef.vieworg, shadelight);

	// pick the greatest component, which should be the same
	// as the mono value returned by software
	if (shadelight[0] > shadelight[1])
	{
		if (shadelight[0] > shadelight[2])
			r_lightlevel->value = 150*shadelight[0];
		else
			r_lightlevel->value = 150*shadelight[2];
	}
	else
	{
		if (shadelight[1] > shadelight[2])
			r_lightlevel->value = 150*shadelight[1];
		else
			r_lightlevel->value = 150*shadelight[2];
	}

}

/*
@@@@@@@@@@@@@@@@@@@@@
R_RenderFrame

@@@@@@@@@@@@@@@@@@@@@
*/
void R_RenderFrame (refdef_t *fd)
{
    // Make sure any pending 2D is flushed before starting 3D
    
    // Render 3D scene
    R_RenderView(fd);
    R_SetLightLevel();
    
    // 2D drawing will happen after this function returns
    // and will be automatically flushed in GLimp_EndFrame
}

void R_Register( void )
{
	r_lefthand = ri.Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", 0);
	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", 0);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", 0);
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", 0);
	r_novis = ri.Cvar_Get ("r_novis", "0", 0);
	r_nocull = ri.Cvar_Get ("r_nocull", "0", 0);
	r_lerpmodels = ri.Cvar_Get ("r_lerpmodels", "1", 0);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", 0);

	r_lightlevel = ri.Cvar_Get ("r_lightlevel", "0", 0);
	gl_particle_min_size = ri.Cvar_Get( "gl_particle_min_size", "2", CVAR_ARCHIVE );
	gl_particle_max_size = ri.Cvar_Get( "gl_particle_max_size", "40", CVAR_ARCHIVE );
	gl_particle_size = ri.Cvar_Get( "gl_particle_size", "40", CVAR_ARCHIVE );
	gl_particle_att_a = ri.Cvar_Get( "gl_particle_att_a", "0.01", CVAR_ARCHIVE );
	gl_particle_att_b = ri.Cvar_Get( "gl_particle_att_b", "0.0", CVAR_ARCHIVE );
	gl_particle_att_c = ri.Cvar_Get( "gl_particle_att_c", "0.01", CVAR_ARCHIVE );

	gl_modulate = ri.Cvar_Get ("gl_modulate", "1.5", CVAR_ARCHIVE );
	gl_log = ri.Cvar_Get( "gl_log", "0", 0 );
	gl_bitdepth = ri.Cvar_Get( "gl_bitdepth", "0", 0 );
	gl_mode = ri.Cvar_Get( "gl_mode", "3", CVAR_ARCHIVE );
	gl_lightmap = ri.Cvar_Get ("gl_lightmap", "0", 0);
	gl_shadows = ri.Cvar_Get ("gl_shadows", "0", CVAR_ARCHIVE );
	gl_dynamic = ri.Cvar_Get ("gl_dynamic", "1", 0);
	gl_nobind = ri.Cvar_Get ("gl_nobind", "0", 0);
	gl_round_down = ri.Cvar_Get ("gl_round_down", "1", 0);
	gl_picmip = ri.Cvar_Get ("gl_picmip", "0", 0);
	gl_skymip = ri.Cvar_Get ("gl_skymip", "0", 0);
	gl_finish = ri.Cvar_Get ("gl_finish", "0", CVAR_ARCHIVE);
	gl_clear = ri.Cvar_Get ("gl_clear", "0", 0);
	gl_cull = ri.Cvar_Get ("gl_cull", "1", 0);
	gl_polyblend = ri.Cvar_Get ("gl_polyblend", "1", 0);
	gl_flashblend = ri.Cvar_Get ("gl_flashblend", "1", 0);
	gl_playermip = ri.Cvar_Get ("gl_playermip", "0", 0);
	gl_monolightmap = ri.Cvar_Get( "gl_monolightmap", "0", 0 );
	gl_driver = ri.Cvar_Get( "gl_driver", "opengl32", CVAR_ARCHIVE );
	gl_texturemode = ri.Cvar_Get( "gl_texturemode", "gl_linear", CVAR_ARCHIVE );
	gl_texturealphamode = ri.Cvar_Get( "gl_texturealphamode", "default", CVAR_ARCHIVE );
	gl_texturesolidmode = ri.Cvar_Get( "gl_texturesolidmode", "default", CVAR_ARCHIVE );
	gl_lockpvs = ri.Cvar_Get( "gl_lockpvs", "0", 0 );
	gl_drawbuffer = ri.Cvar_Get( "gl_drawbuffer", "GL_BACK", 0 );
	gl_swapinterval = ri.Cvar_Get( "gl_swapinterval", "1", CVAR_ARCHIVE );
	gl_saturatelighting = ri.Cvar_Get( "gl_saturatelighting", "0", 0 );
	vid_fullscreen = ri.Cvar_Get( "vid_fullscreen", "0", CVAR_ARCHIVE );
	vid_gamma = ri.Cvar_Get( "vid_gamma", "1.0", CVAR_ARCHIVE );
	ri.Cmd_AddCommand( "imagelist", GL_ImageList_f );
	ri.Cmd_AddCommand( "modellist", Mod_Modellist_f );
}

/*
==================
R_SetMode
==================
*/
qboolean R_SetMode (void)
{
	rserr_t err;

	vid_fullscreen->modified = false;
	gl_mode->modified = false;

	if ( ( err = GLimp_SetMode( &vid.width, &vid.height, gl_mode->value, true ) ) == rserr_ok )
	{
		gl_state.prev_mode = gl_mode->value;
		return true;
	}

	return false;
}

/*
===============
R_Init
===============
*/
int R_Init( void *hinstance, void *hWnd )
{	
	int		j;
	extern float r_turbsin[256];

	for ( j = 0; j < 256; j++ )
	{
		r_turbsin[j] *= 0.5;
	}

	Draw_GetPalette ();
	R_Register();

	
	gl_state.prev_mode = 3;

	if ( !R_SetMode () )
	{
		
		return -1;
	}

	ri.Vid_MenuInit();

	gl_config.renderer = GL_RENDERER_OTHER;
	gl_config.allow_cds = true;

	GL_SetDefaultState();
	GL_InitImages ();
	Mod_Init ();
	R_InitParticleTexture ();
	Draw_InitLocal ();

	return 0;
}
/*
===============
R_Shutdown
===============
*/
void R_Shutdown (void)
{	
	ri.Cmd_RemoveCommand ("modellist");
	ri.Cmd_RemoveCommand ("imagelist");

	Mod_FreeAll ();

	GL_ShutdownImages ();

	/*
	** shut down OS specific OpenGL stuff like contexts, etc.
	*/
	GLimp_Shutdown();

	/*
	** shutdown our QGL subsystem
	*/
}



/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginFrame
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginFrame(float camera_separation)
{
    pvr_wait_ready();
    pvr_scene_begin();  // Add this line
}
/*
=============
R_SetPalette
=============
*/
unsigned r_rawpalette[256];

void R_SetPalette ( const unsigned char *palette)
{
	int		i;

	byte *rp = ( byte * ) r_rawpalette;

	if ( palette )
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = palette[i*3+0];
			rp[i*4+1] = palette[i*3+1];
			rp[i*4+2] = palette[i*3+2];
			rp[i*4+3] = 0xff;
		}
	}
	else
	{
		for ( i = 0; i < 256; i++ )
		{
			rp[i*4+0] = d_8to24table[i] & 0xff;
			rp[i*4+1] = ( d_8to24table[i] >> 8 ) & 0xff;
			rp[i*4+2] = ( d_8to24table[i] >> 16 ) & 0xff;
			rp[i*4+3] = 0xff;
		}
	}
	//GL_SetTexturePalette( r_rawpalette );

	//qglClearColor (0,0,0,0);
	//qglClear (GL_COLOR_BUFFER_BIT);
	//qglClearColor (1,0, 0.5 , 0.5);
}

/*
** R_DrawBeam
*/
#define NUM_BEAM_SEGS 6

void R_DrawBeam(entity_t *e)
{
    int i;
    vec3_t perpvec;
    vec3_t direction, normalized_direction;
    vec3_t start_points[NUM_BEAM_SEGS], end_points[NUM_BEAM_SEGS];
    vec3_t oldorigin, origin;

    VectorCopy(e->oldorigin, oldorigin);
    VectorCopy(e->origin, origin);

    VectorSubtract(oldorigin, origin, direction);
    VectorCopy(direction, normalized_direction);

    if (VectorNormalize(normalized_direction) == 0)
        return;

    PerpendicularVector(perpvec, normalized_direction);
    VectorScale(perpvec, e->frame / 2.0f, perpvec);

    for (i = 0; i < NUM_BEAM_SEGS; i++)
    {
        RotatePointAroundVector(start_points[i], normalized_direction, perpvec,
                                (360.0f / NUM_BEAM_SEGS) * i);
        VectorAdd(start_points[i], origin, start_points[i]);
        VectorAdd(start_points[i], direction, end_points[i]);
    }

    // Color from palette
    float r = (d_8to24table[e->skinnum & 0xFF] & 0xFF) / 255.0f;
    float g = ((d_8to24table[e->skinnum & 0xFF] >> 8) & 0xFF) / 255.0f;
    float b = ((d_8to24table[e->skinnum & 0xFF] >> 16) & 0xFF) / 255.0f;

    uint32_t argb = ((uint32_t)(e->alpha * 255.0f) << 24) |
                    ((uint32_t)(r * 255.0f) << 16) |
                    ((uint32_t)(g * 255.0f) << 8) |
                    ((uint32_t)(b * 255.0f));

    pvr_dr_state_t dr_state;
    pvr_dr_init(&dr_state);

    // Untextured, blended
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.gen.alpha = PVR_ALPHA_ENABLE;
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;

    pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);

    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);

    // Transform all points
    shz_vec4_t t_start[NUM_BEAM_SEGS], t_end[NUM_BEAM_SEGS];
    for (i = 0; i < NUM_BEAM_SEGS; i++) {
        shz_vec3_t sp = shz_vec3_init(start_points[i][0], start_points[i][1], start_points[i][2]);
        shz_vec3_t ep = shz_vec3_init(end_points[i][0], end_points[i][1], end_points[i][2]);
        t_start[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(sp, 1.0f));
        t_end[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(ep, 1.0f));
    }

    // Draw as triangle strip segments (same winding as original)
    float dummy_uv[2] = {0, 0};

    for (i = 0; i < NUM_BEAM_SEGS; i++) {
        int next = (i + 1) % NUM_BEAM_SEGS;

        // Quad as 2 triangles: start[i], end[i], start[next] + end[i], end[next], start[next]
        ClipAndSubmitTriangle(
            t_start[i], t_end[i], t_start[next],
            dummy_uv, dummy_uv, dummy_uv,
            &dr_state, argb);

        ClipAndSubmitTriangle(
            t_end[i], t_end[next], t_start[next],
            dummy_uv, dummy_uv, dummy_uv,
            &dr_state, argb);
    }

    pvr_dr_finish();
}
byte	dottexture[8][8] =
{
	{0,0,0,0,0,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};

void R_InitParticleTexture (void)
{
	int		x,y;
	byte	data[8][8][4];

	//
	// particle texture
	//
	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;
			data[y][x][3] = dottexture[x][y]*255;
		}
	}
	r_particletexture = GL_LoadPic ("***particle***", (byte *)data, 8, 8, it_sprite, 32);

	//
	// also use this for bad textures, but without alpha
	//
	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = dottexture[x&3][y&3]*255;
			data[y][x][1] = 0; // dottexture[x&3][y&3]*255;
			data[y][x][2] = 0; //dottexture[x&3][y&3]*255;
			data[y][x][3] = 255;
		}
	}
	r_notexture = GL_LoadPic ("***r_notexture***", (byte *)data, 8, 8, it_wall, 32);
}


void GL_SetDefaultState( void )
{
 
}

void GL_UpdateSwapInterval( void )
{

}


//===================================================================


void	R_BeginRegistration (char *map);
struct model_s	*R_RegisterModel (char *name);
struct image_s	*R_RegisterSkin (char *name);
void R_SetSky (char *name, float rotate, vec3_t axis);
void	R_EndRegistration (void);

void	R_RenderFrame (refdef_t *fd);

struct image_s	*Draw_FindPic (char *name);

void	Draw_Pic (int x, int y, char *name);
void	Draw_Char (int x, int y, int c);
void	Draw_TileClear (int x, int y, int w, int h, char *name);
void	Draw_Fill (int x, int y, int w, int h, int c);
void	Draw_FadeScreen (void);

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
refexport_t GetRefAPI (refimport_t rimp )
{
	refexport_t	re;

	ri = rimp;

	re.api_version = API_VERSION;

	re.BeginRegistration = R_BeginRegistration;
	re.RegisterModel = R_RegisterModel;
	re.RegisterSkin = R_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = R_SetSky;
	re.EndRegistration = R_EndRegistration;

	re.RenderFrame = R_RenderFrame;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeScreen= Draw_FadeScreen;

	re.DrawStretchRaw = Draw_StretchRaw;

	re.Init = R_Init;
	re.Shutdown = R_Shutdown;

	re.CinematicSetPalette = R_SetPalette;
	re.BeginFrame = R_BeginFrame;
	re.EndFrame = GLimp_EndFrame;

	re.AppActivate = GLimp_AppActivate;

	Swap_Init ();

	return re;
}

