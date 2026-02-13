

#include "gl_local.h"

extern	model_t	*loadmodel;

char	skyname[MAX_QPATH];
float	skyrotate;
vec3_t	skyaxis;
image_t	*sky_images[6];

msurface_t	*warpface;

#define	SUBDIVIDE_SIZE	256
//#define	SUBDIVIDE_SIZE	1024

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;
	vec3_t	total;
	float	total_s, total_t;

	if (numverts > 60)
		ri.Sys_Error (ERR_DROP, "numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = SUBDIVIDE_SIZE * floor (m/SUBDIVIDE_SIZE + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	// add a point in the center to help keep warp valid
	poly = Hunk_Alloc (sizeof(glpoly_t) + ((numverts-4)+2) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts+2;
	VectorClear (total);
	total_s = 0;
	total_t = 0;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i+1]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);

		total_s += s;
		total_t += t;
		VectorAdd (total, verts, total);

		poly->verts[i+1][3] = s;
		poly->verts[i+1][4] = t;
	}

	VectorScale (total, (1.0/numverts), poly->verts[0]);
	poly->verts[0][3] = total_s/numverts;
	poly->verts[0][4] = total_t/numverts;

	// copy first vertex to last
	memcpy_fast (poly->verts[i+1], poly->verts[1], sizeof(poly->verts[0]));
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	vec3_t		pos; // Changed from float *vec

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			pos[0] = (float)loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position[0];
			pos[1] = (float)loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position[1];
			pos[2] = (float)loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position[2];
		}
		else
		{
			pos[0] = (float)loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position[0];
			pos[1] = (float)loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position[1];
			pos[2] = (float)loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position[2];
		}
		VectorCopy (pos, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}
//=========================================================


 

//===================================================================


vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1} 
};
int	c_sky;

// 1 = s, 2 = t, 3 = 2048
int	st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},

	{1,3,2},
	{-1,-3,2},

	{-2,-1,3},		// 0 degrees yaw, look straight up
	{2,-1,-3}		// look straight down

//	{-1,2,3},
//	{1,2,-3}
};

// s = [0]/[2], t = [1]/[2]
int	vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},

	{1,3,2},
	{-1,3,-2},

	{-2,-1,3},
	{-2,1,-3}

//	{-1,2,3},
//	{1,2,-3}
};

float	skymins[2][6], skymaxs[2][6];
float	sky_min, sky_max;

void DrawSkyPolygon (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	c_sky++;
#if 0
glBegin (GL_POLYGON);
for (i=0 ; i<nump ; i++, vecs+=3)
{
	VectorAdd(vecs, r_origin, v);
	qglVertex3fv (v);
}
glEnd();
return;
#endif
	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];
		if (dv < 0.001)
			continue;	// don't divide by zero
		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

#define	ON_EPSILON		0.1			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64
void ClipSkyPolygon (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		ri.Sys_Error (ERR_DROP, "ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		DrawSkyPolygon (nump, vecs);
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon (newc[0], newv[0][0], stage+1);
	ClipSkyPolygon (newc[1], newv[1][0], stage+1);
}

/*
=================
R_AddSkySurface
=================
*/
void R_AddSkySurface (msurface_t *fa)
{
	// Skip clipping - just mark that we have sky
	return;
}


/*
==============
R_ClearSkyBox
==============
*/
void R_ClearSkyBox (void)
{
	int		i;

	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = 9999;
		skymaxs[0][i] = skymaxs[1][i] = -9999;
	}
}


void MakeSkyVec (float s, float t, int axis)
{
	vec3_t		v, b;
	int			j, k;

	b[0] = s*2300;
	b[1] = t*2300;
	b[2] = 2300;

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
	}

	// avoid bilerp seam
	s = (s+1)*0.5;
	t = (t+1)*0.5;

	if (s < sky_min)
		s = sky_min;
	else if (s > sky_max)
		s = sky_max;
	if (t < sky_min)
		t = sky_min;
	else if (t > sky_max)
		t = sky_max;

	t = 1.0 - t;
	//qglTexCoord2f (s, t);
	//qglVertex3fv (v);
}

/*
==============
R_DrawSkyBox
==============
*/

int skytexorder[6] = {0,2,1,3,4,5};

extern const float screen_matrix_640x480[16];
extern const float quake_coord_matrix[16];

void R_DrawSkyBox(void)
{
    int i, j, k;
    float s, t;
    
    if (SHZ_UNLIKELY(!sky_images[0]))
        return;
    
    // Build sky matrix: screen * projection * coord_swap * view_rotation (NO translation)
    float fov_rad = r_newrefdef.fov_y * SHZ_DEG_TO_RAD(1.0f);
    float screenaspect = 640.0f / 480.0f;
    
    float roll  = -r_newrefdef.viewangles[2] * SHZ_DEG_TO_RAD(1.0f);
    float pitch = -r_newrefdef.viewangles[0] * SHZ_DEG_TO_RAD(1.0f);
    float yaw   = -r_newrefdef.viewangles[1] * SHZ_DEG_TO_RAD(1.0f);
    
    shz_xmtrx_load_4x4((shz_mat4x4_t*)screen_matrix_640x480);
    shz_xmtrx_apply_perspective(fov_rad, screenaspect, 4.0f);
    shz_xmtrx_apply_4x4((shz_mat4x4_t*)quake_coord_matrix);
    shz_xmtrx_rotate_x(roll);
    shz_xmtrx_rotate_y(pitch);
    shz_xmtrx_rotate_z(yaw);
    // NO translation - sky follows camera
    
    // Optional sky rotation
    if (skyrotate != 0.0f) {
        float sky_angle = r_newrefdef.time * skyrotate * SHZ_DEG_TO_RAD(1.0f);
        shz_xmtrx_rotate(sky_angle, skyaxis[0], skyaxis[1], skyaxis[2]);
    }
    
    // Static UV corner coordinates
    static const float st_coords[4][2] = {{-1,-1}, {-1,1}, {1,1}, {1,-1}};
    
    // Draw 6 skybox faces
    for (i = 0; i < 6; i++)
    {
        image_t *sky_tex = sky_images[skytexorder[i]];
        if (SHZ_UNLIKELY(!sky_tex || sky_tex == r_notexture))
            continue;
            
        int texnum = sky_tex->texnum;
        if (SHZ_UNLIKELY(texnum <= 0 || texnum >= MAX_GLTEXTURES || !pvr_textures[texnum].loaded))
            continue;
        
        pvr_dr_state_t dr_state;
        pvr_dr_init(&dr_state);
        
        pvr_poly_cxt_t cxt;
        pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, 
                        pvr_textures[texnum].format,
                        pvr_textures[texnum].width, 
                        pvr_textures[texnum].height,
                        pvr_textures[texnum].ptr, 
                        PVR_FILTER_BILINEAR);
        cxt.gen.culling = PVR_CULLING_NONE;
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
        
        pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
        pvr_poly_compile(hdr, &cxt);
        pvr_dr_commit(hdr);
        
        // Build 4 corner vertices
        shz_vec3_t verts[4];
        float uvs[4][2];
        int face_idx = skytexorder[i];
        
        for (j = 0; j < 4; j++) {
            s = st_coords[j][0];
            t = st_coords[j][1];
            
            shz_vec3_t b = shz_vec3_init(s * 2300.0f, t * 2300.0f, 2300.0f);
            
            // Map to skybox face coordinates
            float v[3];
            for (k = 0; k < 3; k++) {
                int idx = st_to_vec[face_idx][k];
                if (idx < 0)
                    v[k] = -b.e[-idx - 1];
                else
                    v[k] = b.e[idx - 1];
            }
            verts[j] = shz_vec3_init(v[0], v[1], v[2]);
            
            // UV calculation
            float u = (s + 1.0f) * 0.5f;
            float v_coord = (t + 1.0f) * 0.5f;
            
            // Clamp UVs
            if (u < sky_min) u = sky_min;
            else if (u > sky_max) u = sky_max;
            if (v_coord < sky_min) v_coord = sky_min;
            else if (v_coord > sky_max) v_coord = sky_max;
            
            uvs[j][0] = u;
            uvs[j][1] = 1.0f - v_coord;
        }
        
        // Transform vertices using FTRV (XMTRX already loaded)
        shz_vec4_t transformed[4];
        for (j = 0; j < 4; j++) {
            transformed[j] = shz_xmtrx_transform_vec4(shz_vec3_vec4(verts[j], 1.0f));
        }
        
        // Draw as 2 triangles (0,1,2) and (0,2,3)
        for (j = 0; j < 2; j++) {
            int idx0 = 0;
            int idx1 = j + 1;
            int idx2 = j + 2;
            
            // Skip if all behind near plane
            if (SHZ_UNLIKELY(transformed[idx0].w < 0.1f && 
                             transformed[idx1].w < 0.1f && 
                             transformed[idx2].w < 0.1f))
                continue;
            
            // All visible - fast path
            if (SHZ_LIKELY(transformed[idx0].w >= 0.1f && 
                           transformed[idx1].w >= 0.1f && 
                           transformed[idx2].w >= 0.1f)) {
                
                float inv_w0 = shz_invf_fsrra(transformed[idx0].w);
                float inv_w1 = shz_invf_fsrra(transformed[idx1].w);
                float inv_w2 = shz_invf_fsrra(transformed[idx2].w);
                
                pvr_vertex_t *vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX;
                vert->x = transformed[idx0].x * inv_w0;
                vert->y = transformed[idx0].y * inv_w0;
                vert->z = inv_w0;
                vert->u = uvs[idx0][0];
                vert->v = uvs[idx0][1];
                vert->argb = 0xFFFFFFFF;
                vert->oargb = 0;
                pvr_dr_commit(vert);
                
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX;
                vert->x = transformed[idx1].x * inv_w1;
                vert->y = transformed[idx1].y * inv_w1;
                vert->z = inv_w1;
                vert->u = uvs[idx1][0];
                vert->v = uvs[idx1][1];
                vert->argb = 0xFFFFFFFF;
                vert->oargb = 0;
                pvr_dr_commit(vert);
                
                vert = pvr_dr_target(dr_state);
                vert->flags = PVR_CMD_VERTEX_EOL;
                vert->x = transformed[idx2].x * inv_w2;
                vert->y = transformed[idx2].y * inv_w2;
                vert->z = inv_w2;
                vert->u = uvs[idx2][0];
                vert->v = uvs[idx2][1];
                vert->argb = 0xFFFFFFFF;
                vert->oargb = 0;
                pvr_dr_commit(vert);
            }
            else {
                // Partial visibility - clip against near plane
                ClipAndSubmitTriangle(
                    transformed[idx0], transformed[idx1], transformed[idx2],
                    uvs[idx0], uvs[idx1], uvs[idx2],
                    &dr_state, 0xFFFFFFFF);
            }
        }
        
        pvr_dr_finish();
    }
    
    // Restore world matrix for subsequent rendering
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
}
/*
============
R_SetSky
============
*/
// 3dstudio environment map names
char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
void R_SetSky (char *name, float rotate, vec3_t axis)
{
	int		i;
	char	pathname[MAX_QPATH];

	strncpy (skyname, name, sizeof(skyname)-1);
	skyrotate = rotate;
	VectorCopy (axis, skyaxis);

	for (i=0 ; i<6 ; i++)
	{
		// chop down rotating skies for less memory
		if (gl_skymip->value || skyrotate)
			gl_picmip->value++;

		
			Com_sprintf (pathname, sizeof(pathname), "env/%s%s.tga", skyname, suf[i]);

		sky_images[i] = GL_FindImage (pathname, it_sky);
		if (!sky_images[i])
			sky_images[i] = r_notexture;

		if (gl_skymip->value || skyrotate)
		{	// take less memory
			gl_picmip->value--;
			sky_min = 1.0/256;
			sky_max = 255.0/256;
		}
		else	
		{
			sky_min = 1.0/512;
			sky_max = 511.0/512;
		}
	}
}


