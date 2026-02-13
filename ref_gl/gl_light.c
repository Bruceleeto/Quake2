/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_light.c

#include "gl_local.h"

int	r_dlightframecount;

#define	DLIGHT_CUTOFF	64

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/


/*
 * WARNING: DO NOT EVER USE DOUBLES WITH FTRV IN THE SAME FUNCTION.
 * FTRV with double vars bad - causes undefined behavior on SH4.
 */
void R_RenderDlight(dlight_t *light)
{
    int i;
    float rad;
    pvr_dr_state_t dr_state;
    
    rad = light->intensity * 0.85f;
    
    // Distance check using sh4zam
    shz_vec3_t light_origin = shz_vec3_init(light->origin[0], light->origin[1], light->origin[2]);
    shz_vec3_t view_origin = shz_vec3_init(r_origin[0], r_origin[1], r_origin[2]);
    shz_vec3_t delta = shz_vec3_sub(light_origin, view_origin);
    
    if (shz_vec3_magnitude(delta) < rad)
        return;
    
    pvr_dr_init(&dr_state);
    
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    cxt.gen.alpha = PVR_ALPHA_ENABLE;
    cxt.blend.src = PVR_BLEND_ONE;
    cxt.blend.dst = PVR_BLEND_ONE;
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    
    pvr_poly_hdr_t *hdr = (pvr_poly_hdr_t *)pvr_dr_target(dr_state);
    pvr_poly_compile(hdr, &cxt);
    pvr_dr_commit(hdr);
    
    // Load matrix once
    shz_xmtrx_load_4x4((shz_mat4x4_t*)r_world_matrix);
    
    // Calculate center position
    shz_vec3_t vpn_v = shz_vec3_init(vpn[0], vpn[1], vpn[2]);
    shz_vec3_t center_pos = shz_vec3_sub(light_origin, shz_vec3_scale(vpn_v, rad));
    
    uint32_t center_color = 0x04000000 | 
                           ((uint32_t)(light->color[0] * 4) << 16) |
                           ((uint32_t)(light->color[1] * 4) << 8) |
                           ((uint32_t)(light->color[2] * 4));
    
    // Transform center
    shz_vec4_t tc = shz_xmtrx_transform_vec4(shz_vec3_vec4(center_pos, 1.0f));
    
    if (SHZ_UNLIKELY(tc.w < 0.1f)) {
        pvr_dr_finish();
        return;
    }
    
    float inv_wc = shz_invf_fsrra(tc.w);
    float cx = tc.x * inv_wc;
    float cy = tc.y * inv_wc;
    float cz = inv_wc;
    
    // Pre-calculate view vectors
    shz_vec3_t vright_v = shz_vec3_init(vright[0], vright[1], vright[2]);
    shz_vec3_t vup_v = shz_vec3_init(vup[0], vup[1], vup[2]);
    
    // Transform all 17 edge vertices
    shz_vec4_t transformed[17];
    const float angle_step = (2.0f * SHZ_F_PI) / 16.0f;
    
    for (i = 0; i < 17; i++) {
        float angle = (float)(i % 16) * angle_step;
        shz_sincos_t sc = shz_sincosf(angle);
        
        // edge = origin + vright * cos * rad + vup * sin * rad
        shz_vec3_t edge = shz_vec3_add(light_origin,
                            shz_vec3_add(
                                shz_vec3_scale(vright_v, sc.cos * rad),
                                shz_vec3_scale(vup_v, sc.sin * rad)
                            ));
        
        transformed[i] = shz_xmtrx_transform_vec4(shz_vec3_vec4(edge, 1.0f));
    }
    
    // Submit triangles: center, edge[i], edge[i+1]
    for (i = 0; i < 16; i++) {
        // Near plane cull
        if (SHZ_UNLIKELY(transformed[i].w < 0.1f || transformed[i+1].w < 0.1f))
            continue;
        
        // Center vertex
        pvr_vertex_t *vert = pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = cx;
        vert->y = cy;
        vert->z = cz;
        vert->u = 0;
        vert->v = 0;
        vert->argb = center_color;
        vert->oargb = 0;
        pvr_dr_commit(vert);
        
        // Edge i
        float inv_w0 = shz_invf_fsrra(transformed[i].w);
        vert = pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX;
        vert->x = transformed[i].x * inv_w0;
        vert->y = transformed[i].y * inv_w0;
        vert->z = inv_w0;
        vert->u = 0;
        vert->v = 0;
        vert->argb = 0x00000000;
        vert->oargb = 0;
        pvr_dr_commit(vert);
        
        // Edge i+1 (EOL)
        float inv_w1 = shz_invf_fsrra(transformed[i+1].w);
        vert = pvr_dr_target(dr_state);
        vert->flags = PVR_CMD_VERTEX_EOL;
        vert->x = transformed[i+1].x * inv_w1;
        vert->y = transformed[i+1].y * inv_w1;
        vert->z = inv_w1;
        vert->u = 0;
        vert->v = 0;
        vert->argb = 0x00000000;
        vert->oargb = 0;
        pvr_dr_commit(vert);
    }
    
    pvr_dr_finish();
}
/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights(void)
{
    int i;
    dlight_t *l;
    
    if (!gl_flashblend->value)
        return;
    
    r_dlightframecount = r_framecount + 1;
    
    l = r_newrefdef.dlights;
    for (i = 0; i < r_newrefdef.num_dlights; i++, l++) {
 
        R_RenderDlight(l);
    }
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	cplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents != -1)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
	if (dist > light->intensity-DLIGHT_CUTOFF)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -light->intensity+DLIGHT_CUTOFF)
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}
		surf->dlightbits |= bit;
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

 

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	l = r_newrefdef.dlights;
	for (i=0 ; i<r_newrefdef.num_dlights ; i++, l++)
		R_MarkLights ( l, 1<<i, r_worldmodel->nodes );
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t			pointcolor;
cplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	cplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	int			maps;
	int			r;

	if (node->contents != -1)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	
	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags&(SURF_DRAWTURB|SURF_DRAWSKY)) 
			continue;	// no lightmaps

		tex = surf->texinfo;
		
		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;
		
		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];
		
		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		VectorCopy (vec3_origin, pointcolor);
		if (lightmap)
		{
			vec3_t scale;

			lightmap += 3*(dt * ((surf->extents[0]>>4)+1) + ds);

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				for (i=0 ; i<3 ; i++)
					scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

				pointcolor[0] += lightmap[0] * scale[0] * (1.0/255);
				pointcolor[1] += lightmap[1] * scale[1] * (1.0/255);
				pointcolor[2] += lightmap[2] * scale[2] * (1.0/255);
				lightmap += 3*((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
		}
		
		return 1;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

/*
===============
R_LightPoint
===============
*/
void R_LightPoint (vec3_t p, vec3_t color)
{
	vec3_t		end;
	float		r;
	int			lnum;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;
	
	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;
	
	r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
	
	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}

	//
	// add dynamic lights
	//
	light = 0;
	dl = r_newrefdef.dlights;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++, dl++)
	{
		VectorSubtract (currententity->origin,
						dl->origin,
						dist);
		add = dl->intensity - VectorLength(dist);
		add *= (1.0f / 256.0f);  // or just 0.00390625f

		if (add > 0)
		{
			VectorMA (color, add, dl->color, color);
		}
	}

	VectorScale (color, gl_modulate->value, color);
}


//===================================================================

static float s_blocklights[34*34*3];
/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		fdist, frad, fminlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	float		*pfBL;
	float		fsacc, ftacc;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		dl = &r_newrefdef.dlights[lnum];
		frad = dl->intensity;
		fdist = DotProduct (dl->origin, surf->plane->normal) -
				surf->plane->dist;
		frad -= fabs(fdist);
		// rad is now the highest intensity on the plane

		fminlight = DLIGHT_CUTOFF;	// FIXME: make configurable?
		if (frad < fminlight)
			continue;
		fminlight = frad - fminlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = dl->origin[i] -
					surf->plane->normal[i]*fdist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1];

		pfBL = s_blocklights;
		for (t = 0, ftacc = 0 ; t<tmax ; t++, ftacc += 16)
		{
			td = local[1] - ftacc;
			if ( td < 0 )
				td = -td;

			for ( s=0, fsacc = 0 ; s<smax ; s++, fsacc += 16, pfBL += 3)
			{
                    SHZ_PREFETCH(pfBL + 3);  // prefetch next iteration

				sd = Q_ftol( local[0] - fsacc );

				if ( sd < 0 )
					sd = -sd;

				if (sd > td)
					fdist = sd + (td>>1);
				else
					fdist = td + (sd>>1);

				if ( fdist < fminlight )
				{
					pfBL[0] += ( frad - fdist ) * dl->color[0];
					pfBL[1] += ( frad - fdist ) * dl->color[1];
					pfBL[2] += ( frad - fdist ) * dl->color[2];
				}
			}
		}
	}
}


/*
** R_SetCacheState
*/
void R_SetCacheState( msurface_t *surf )
{
	int maps;

	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
		 maps++)
	{
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the floating format in blocklights
===============
*/
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
    int			smax, tmax;
    int			r, g, b, a, max;
    int			i, j, size;
    byte		*lightmap;
    float		scale[4];
    int			nummaps;
    float		*bl;
    lightstyle_t	*style;
    int monolightmap;
    uint16_t	*dest16 = (uint16_t *)dest;  // Cast to 16-bit

    if ( surf->texinfo->flags & (SURF_SKY|SURF_TRANS33|SURF_TRANS66|SURF_WARP) )
        ri.Sys_Error (ERR_DROP, "R_BuildLightMap called for non-lit surface");

    smax = (surf->extents[0]>>4)+1;
    tmax = (surf->extents[1]>>4)+1;
    size = smax*tmax;
    if (size > (sizeof(s_blocklights)>>4) )
        ri.Sys_Error (ERR_DROP, "Bad s_blocklights size");

    stride >>= 1;  // Adjust stride for 16-bit values

// set to full bright if no light data
    if (!surf->samples)
    {
        int maps;

        for (i=0 ; i<size*3 ; i++)
            s_blocklights[i] = 255;
        for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
             maps++)
        {
            style = &r_newrefdef.lightstyles[surf->styles[maps]];
        }
        goto store;
    }

    // count the # of maps
    for ( nummaps = 0 ; nummaps < MAXLIGHTMAPS && surf->styles[nummaps] != 255 ;
         nummaps++)
        ;

    lightmap = surf->samples;

    // add all the lightmaps
    if ( nummaps == 1 )
    {
        int maps;

        for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
             maps++)
        {
            bl = s_blocklights;

            for (i=0 ; i<3 ; i++)
                scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

            if ( scale[0] == 1.0F &&
                 scale[1] == 1.0F &&
                 scale[2] == 1.0F )
            {
                for (i=0 ; i<size ; i++, bl+=3)
                {
                    bl[0] = lightmap[i*3+0];
                    bl[1] = lightmap[i*3+1];
                    bl[2] = lightmap[i*3+2];
                }
            }
            else
            {
                for (i=0 ; i<size ; i++, bl+=3)
                {
                    bl[0] = lightmap[i*3+0] * scale[0];
                    bl[1] = lightmap[i*3+1] * scale[1];
                    bl[2] = lightmap[i*3+2] * scale[2];
                }
            }
            lightmap += size*3;		// skip to next lightmap
        }
    }
    else
    {
        int maps;

        memset( s_blocklights, 0, sizeof( s_blocklights[0] ) * size * 3 );

        for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
             maps++)
        {
            bl = s_blocklights;

            for (i=0 ; i<3 ; i++)
                scale[i] = gl_modulate->value*r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];

            if ( scale[0] == 1.0F &&
                 scale[1] == 1.0F &&
                 scale[2] == 1.0F )
            {
                for (i=0 ; i<size ; i++, bl+=3 )
                {
                    bl[0] += lightmap[i*3+0];
                    bl[1] += lightmap[i*3+1];
                    bl[2] += lightmap[i*3+2];
                }
            }
            else
            {
                for (i=0 ; i<size ; i++, bl+=3)
                {
                    bl[0] += lightmap[i*3+0] * scale[0];
                    bl[1] += lightmap[i*3+1] * scale[1];
                    bl[2] += lightmap[i*3+2] * scale[2];
                }
            }
            lightmap += size*3;		// skip to next lightmap
        }
    }

// add all the dynamic lights
    if (surf->dlightframe == r_framecount)
        R_AddDynamicLights (surf);

store:
    stride -= smax;  // Adjust for 16-bit pixels
    bl = s_blocklights;

    monolightmap = gl_monolightmap->string[0];

    if ( monolightmap == '0' )
    {
        for (i=0 ; i<tmax ; i++, dest16 += stride)
        {
            for (j=0 ; j<smax ; j++)
            {
                r = Q_ftol( bl[0] );
                g = Q_ftol( bl[1] );
                b = Q_ftol( bl[2] );

                // catch negative lights
                if (r < 0) r = 0;
                if (g < 0) g = 0;
                if (b < 0) b = 0;

                // clamp to avoid overflow
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;

                // Pack as RGB565
                *dest16++ = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

                bl += 3;
            }
        }
    }
    else
    {
        for (i=0 ; i<tmax ; i++, dest16 += stride)
        {
            for (j=0 ; j<smax ; j++)
            {
                r = Q_ftol( bl[0] );
                g = Q_ftol( bl[1] );
                b = Q_ftol( bl[2] );

                // catch negative lights
                if (r < 0) r = 0;
                if (g < 0) g = 0;
                if (b < 0) b = 0;

                /*
                ** determine the brightest of the three color components
                */
                if (r > g)
                    max = r;
                else
                    max = g;
                if (b > max)
                    max = b;

                /*
                ** rescale all the color components if the intensity of the greatest
                ** channel exceeds 1.0
                */
                if (max > 255)
                {
float t = 255.0f * shz_invf((float)max);

                    r = r*t;
                    g = g*t;
                    b = b*t;
                }

                /*
                ** So if we are doing alpha lightmaps we need to set the R, G, and B
                ** components to 0 and we need to set alpha to 1-alpha.
                */
                switch ( monolightmap )
                {
                case 'L':
                case 'I':
                    // For RGB565, just use grayscale
                    r = g = b = max;
                    break;
                case 'C':
                    // Keep colored
                    break;
                case 'A':
                default:
                    // Grayscale for alpha mode
                    r = g = b = 255 - max;
                    break;
                }

                // Pack as RGB565
                *dest16++ = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

                bl += 3;
            }
        }
    }
}