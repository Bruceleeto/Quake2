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
// models.c -- model loading and caching

#include "gl_local.h"
#include <malloc.h>
model_t	*loadmodel;
int		modfilelen;

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	256
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

// the inline * models from the current map are kept seperate
model_t	mod_inline[MAX_MOD_KNOWN];

int		registration_sequence;




typedef struct {
    char *name;
    int size;
} bsp_size_t;


// MD2 model size lookup table - exact sizes
typedef struct {
    char *name;
    int size;
} md2_size_t;



static bsp_size_t bsp_sizes[] = {
    {"maps/base2.bsp", 4582296},
    {"maps/base1.bsp", 4191500},
    {"maps/base3.bsp", 4929248},
    {"maps/biggun.bsp", 3567584},
    {"maps/boss1.bsp", 3866592},
    {"maps/boss2.bsp", 3914440},
    {"maps/bunk1.bsp", 5751948},
    {"maps/city1.bsp", 5377516},
    {"maps/city2.bsp", 4971500},
    {"maps/city3.bsp", 6168512},
    {"maps/command.bsp", 6103368},
    {"maps/cool1.bsp", 6147488},
    {"maps/fact1.bsp", 5394436},
    {"maps/fact2.bsp", 5465000},
    {"maps/fact3.bsp", 1779476},
    {"maps/hangar1.bsp", 4506640},
    {"maps/hangar2.bsp", 6204640},
    {"maps/jail1.bsp", 5249580},
    {"maps/jail2.bsp", 4483680},
    {"maps/jail3.bsp", 4947948},
    {"maps/jail4.bsp", 5531476},
    {"maps/jail5.bsp", 5133752},
    {"maps/lab.bsp", 6453308},
    {"maps/mine1.bsp", 5932628},
    {"maps/mine2.bsp", 6057640},
    {"maps/mine3.bsp", 4928660},
    {"maps/mine4.bsp", 5261140},
    {"maps/mintro.bsp", 4965516},
    {"maps/power1.bsp", 3857620},
    {"maps/power2.bsp", 7409616},
    {"maps/q2dm1.bsp", 1912984},
    {"maps/q2dm2.bsp", 2033600},
    {"maps/q2dm3.bsp", 2477624},
    {"maps/q2dm4.bsp", 5243008},
    {"maps/q2dm5.bsp", 2338168},
    {"maps/q2dm6.bsp", 3430836},
    {"maps/q2dm7.bsp", 1844808},
    {"maps/q2dm8.bsp", 2618784},
    {"maps/security.bsp", 3259796},
    {"maps/space.bsp", 6163504},
    {"maps/strike.bsp", 4730796},
    {"maps/train.bsp", 4716508},
    {"maps/ware1.bsp", 5266916},
    {"maps/ware2.bsp", 4045100},
    {"maps/waste1.bsp", 6273092},
    {"maps/waste2.bsp", 4135436},
    {"maps/waste3.bsp", 5056488},
    {NULL, 0}  // Sentinel
};


static md2_size_t md2_sizes[] = {
    {"models/deadbods/dude/tris.md2", 14380},
    {"models/items/adrenal/tris.md2", 1820},
    {"models/items/ammo/bullets/medium/tris.md2", 548},
    {"models/items/ammo/cells/medium/tris.md2", 740},
    {"models/items/ammo/grenades/medium/tris.md2", 1544},
    {"models/items/ammo/mines/tris.md2", 904},
    {"models/items/ammo/nuke/tris.md2", 1024},
    {"models/items/ammo/rockets/medium/tris.md2", 768},
    {"models/items/ammo/shells/medium/tris.md2", 520},
    {"models/items/ammo/slugs/medium/tris.md2", 1424},
    {"models/items/armor/body/tris.md2", 5616},
    {"models/items/armor/combat/tris.md2", 4840},
    {"models/items/armor/effect/tris.md2", 484},
    {"models/items/armor/jacket/tris.md2", 4868},
    {"models/items/armor/screen/tris.md2", 5688},
    {"models/items/armor/shard/tris.md2", 696},
    {"models/items/armor/shield/tris.md2", 7448},
    {"models/items/band/tris.md2", 8584},
    {"models/items/breather/tris.md2", 9796},
    {"models/items/c_head/tris.md2", 2688},
    {"models/items/enviro/tris.md2", 12564},
    {"models/items/healing/large/tris.md2", 484},
    {"models/items/healing/medium/tris.md2", 484},
    {"models/items/healing/stimpack/tris.md2", 1820},
    {"models/items/invulner/tris.md2", 6416},
    {"models/items/keys/data_cd/tris.md2", 1892},
    {"models/items/keys/key/tris.md2", 1500},
    {"models/items/keys/pass/tris.md2", 1936},
    {"models/items/keys/power/tris.md2", 6140},
    {"models/items/keys/pyramid/tris.md2", 3416},
    {"models/items/keys/red_key/tris.md2", 1500},
    {"models/items/keys/spinner/tris.md2", 2996},
    {"models/items/keys/target/tris.md2", 5044},
    {"models/items/mega_h/tris.md2", 1544},
    {"models/items/pack/tris.md2", 5124},
    {"models/items/quaddama/tris.md2", 3572},
    {"models/items/silencer/tris.md2", 2096},
    {"models/monsters/berserk/tris.md2", 338452},
    {"models/monsters/bitch/tris.md2", 422608},
    {"models/monsters/boss1/tris.md2", 999496},
    {"models/monsters/boss2/tris.md2", 704880},
    {"models/monsters/boss3/jorg/tris.md2", 552116},
    {"models/monsters/boss3/rider/tris.md2", 1008412},
    {"models/monsters/brain/tris.md2", 322448},
    {"models/monsters/commandr/head/tris.md2", 2632},
    {"models/monsters/commandr/tris.md2", 55704},
    {"models/monsters/flipper/tris.md2", 217200},
    {"models/monsters/float/tris.md2", 247864},
    {"models/monsters/flyer/tris.md2", 145004},
    {"models/monsters/gladiatr/tris.md2", 141220},
    {"models/monsters/gunner/tris.md2", 298012},
    {"models/monsters/hover/tris.md2", 263860},
    {"models/monsters/infantry/tris.md2", 218012},
    {"models/monsters/insane/tris.md2", 380904},
    {"models/monsters/medic/tris.md2", 360664},
    {"models/monsters/mutant/tris.md2", 209532},
    {"models/monsters/parasite/segment/tris.md2", 900},
    {"models/monsters/parasite/tip/tris.md2", 452},
    {"models/monsters/parasite/tris.md2", 152668},
    {"models/monsters/soldier/tris.md2", 461016},
    {"models/monsters/tank/tris.md2", 505764},
    {"models/objects/banner/tris.md2", 11124},
    {"models/objects/barrels/tris.md2", 5596},
    {"models/objects/black/tris.md2", 35528},
    {"models/objects/bomb/tris.md2", 1988},
    {"models/objects/debris1/tris.md2", 740},
    {"models/objects/debris2/tris.md2", 388},
    {"models/objects/debris3/tris.md2", 712},
    {"models/objects/dmspot/tris.md2", 836},
    {"models/objects/explode/tris.md2", 2020},
    {"models/objects/flash/tris.md2", 1444},
    {"models/objects/gibs/arm/tris.md2", 2560},
    {"models/objects/gibs/bone/tris.md2", 1792},
    {"models/objects/gibs/bone2/tris.md2", 2076},
    {"models/objects/gibs/chest/tris.md2", 5604},
    {"models/objects/gibs/gear/tris.md2", 956},
    {"models/objects/gibs/head/tris.md2", 2580},
    {"models/objects/gibs/head2/tris.md2", 2644},
    {"models/objects/gibs/leg/tris.md2", 2224},
    {"models/objects/gibs/skull/tris.md2", 1668},
    {"models/objects/gibs/sm_meat/tris.md2", 2544},
    {"models/objects/gibs/sm_metal/tris.md2", 388},
    {"models/objects/grenade/tris.md2", 708},
    {"models/objects/grenade2/tris.md2", 2032},
    {"models/objects/laser/tris.md2", 484},
    {"models/objects/minelite/light1/tris.md2", 1816},
    {"models/objects/minelite/light2/tris.md2", 2108},
    {"models/objects/r_explode/tris.md2", 34296},
    {"models/objects/rocket/tris.md2", 1988},
    {"models/objects/satellite/tris.md2", 31064},
    {"models/objects/smoke/tris.md2", 1732},
    {"models/ships/bigviper/tris.md2", 7140},
    {"models/ships/strogg1/tris.md2", 12812},
    {"models/ships/viper/tris.md2", 7140},
    {"models/weapons/g_bfg/tris.md2", 5876},
    {"models/weapons/g_blast/tris.md2", 4880},
    {"models/weapons/g_chain/tris.md2", 3792},
    {"models/weapons/g_disint/tris.md2", 5876},
    {"models/weapons/g_flareg/tris.md2", 3720},
    {"models/weapons/g_hyperb/tris.md2", 4168},
    {"models/weapons/g_launch/tris.md2", 5432},
    {"models/weapons/g_machn/tris.md2", 3640},
    {"models/weapons/g_rail/tris.md2", 5444},
    {"models/weapons/g_rocket/tris.md2", 5528},
    {"models/weapons/g_shotg/tris.md2", 4072},
    {"models/weapons/g_shotg2/tris.md2", 5632},
    {"models/weapons/v_bfg/tris.md2", 56156},
    {"models/weapons/v_blast/tris.md2", 44284},
    {"models/weapons/v_chain/tris.md2", 43696},
    {"models/weapons/v_disint/tris.md2", 49660},
    {"models/weapons/v_flareg/tris.md2", 51108},
    {"models/weapons/v_handgr/tris.md2", 53352},
    {"models/weapons/v_hyperb/tris.md2", 47272},
    {"models/weapons/v_launch/tris.md2", 66532},
    {"models/weapons/v_machn/tris.md2", 51264},
    {"models/weapons/v_rail/tris.md2", 56420},
    {"models/weapons/v_rocket/tris.md2", 54052},
    {"models/weapons/v_shotg/tris.md2", 30112},
    {"models/weapons/v_shotg2/tris.md2", 66780},
    {"players/male/a_grenades.md2", 30609},
    {"players/male/tris.md2", 271512},
    {"players/male/w_bfg.md2", 94453},
    {"players/male/w_blaster.md2", 70792},
    {"players/male/w_chainfist.md2", 148261},
    {"players/male/w_chaingun.md2", 61921},
    {"players/male/w_disrupt.md2", 151949},
    {"players/male/w_etfrifle.md2", 105277},
    {"players/male/w_glauncher.md2", 78984},
    {"players/male/w_grapple.md2", 61157},
    {"players/male/w_hyperblaster.md2", 69909},
    {"players/male/w_machinegun.md2", 63845},
    {"players/male/w_phalanx.md2", 125081},
    {"players/male/w_plasma.md2", 98725},
    {"players/male/w_plauncher.md2", 89189},
    {"players/male/w_railgun.md2", 89869},
    {"players/male/w_ripper.md2", 89589},
    {"players/male/w_rlauncher.md2", 94797},
    {"players/male/w_shotgun.md2", 38048},
    {"players/male/w_sshotgun.md2", 83372},
    {"players/male/weapon.md2", 38048},
    {NULL, 0}  // Sentinel
};


// Helper function to lookup BSP size
static int GetBSPSize(const char *name)
{
    int i;
    for (i = 0; bsp_sizes[i].name != NULL; i++) {
        if (!strcmp(bsp_sizes[i].name, name)) {
            return bsp_sizes[i].size;
        }
    }
    ri.Sys_Error(ERR_DROP, "GetBSPSize: Unknown BSP file '%s' - size not in lookup table", name);
    return 0;  // Never reached
}


static int GetMD2Size(const char *name)
{
    int i;
    for (i = 0; md2_sizes[i].name != NULL; i++) {
        if (!strcmp(md2_sizes[i].name, name)) {
            return md2_sizes[i].size;
        }
    }
    ri.Sys_Error(ERR_DROP, "GetMD2Size: Unknown MD2 file '%s' - size not in lookup table", name);
    return 0;  // Never reached
}








/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	cplane_t	*plane;
	
	if (!model || !model->nodes)
		ri.Sys_Error (ERR_DROP, "Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents != -1)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->vis->numclusters+7)>>3;	
	out = decompressed;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
	
	return decompressed;
}

/*
==============
Mod_ClusterPVS
==============
*/
byte *Mod_ClusterPVS (int cluster, model_t *model)
{
	if (cluster == -1 || !model->vis)
		return mod_novis;
	return Mod_DecompressVis ( (byte *)model->vis + model->vis->bitofs[cluster][DVIS_PVS],
		model);
}


//===============================================================================

/*
================
Mod_Modellist_f
================
*/
void Mod_Modellist_f (void)
{
	int		i;
	model_t	*mod;
	int		total;

	total = 0;
	ri.Con_Printf (PRINT_ALL,"Loaded models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		ri.Con_Printf (PRINT_ALL, "%8i : %s\n",mod->extradatasize, mod->name);
		total += mod->extradatasize;
	}
	ri.Con_Printf (PRINT_ALL, "Total resident: %i\n", total);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	memset (mod_novis, 0xff, sizeof(mod_novis));
}



/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
   model_t	*mod;
   unsigned *buf;
   int		i;
   
   if (!name[0])
   	ri.Sys_Error (ERR_DROP, "Mod_ForName: NULL name");
   	
   //
   // inline models are grabbed only from worldmodel
   //
   if (name[0] == '*')
   {
   	i = atoi(name+1);
   	if (i < 1 || !r_worldmodel || i >= r_worldmodel->numsubmodels)
   		ri.Sys_Error (ERR_DROP, "bad inline model number");
   	return &mod_inline[i];
   }

   //
   // search the currently loaded models
   //
   for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
   {
   	if (!mod->name[0])
   		continue;
   	if (!strcmp (mod->name, name))
   	{
   		return mod;
   	}
   }
   
   //
   // find a free model slot spot
   //
   for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
   {
   	if (!mod->name[0])
   		break;	// free spot
   }
   if (i == mod_numknown)
   {
   	if (mod_numknown == MAX_MOD_KNOWN)
   		ri.Sys_Error (ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
   	mod_numknown++;
   }
   
   strcpy (mod->name, name);
   
   //
   // load the file
   //
   modfilelen = ri.FS_LoadFile (mod->name, (void **)&buf);
   if (!buf)
   {
   	if (crash)
   		ri.Sys_Error (ERR_DROP, "Mod_NumForName: %s not found", mod->name);
   	memset (mod->name, 0, sizeof(mod->name));
   	return NULL;
   }
   
   loadmodel = mod;

   //
   // fill it in
   //
   // call the apropriate loader
   
   unsigned header_id = LittleLong(*(unsigned *)buf);
   
   switch (header_id)
   {
	case IDALIASHEADER:
    loadmodel->extradata = Hunk_Begin(modfilelen);
    if (!loadmodel->extradata) {
        ri.Sys_Error(ERR_DROP, "Hunk_Begin failed for MD2!");
    }
    Mod_LoadAliasModel(mod, buf);
    break;
   	
   case IDSPRITEHEADER:
    loadmodel->extradata = Hunk_Begin (2048);  // Was 65536   not tested.
	Mod_LoadSpriteModel (mod, buf);
   	break;
   
   case IDBSPHEADER:
   	{
   		int bsp_size = GetBSPSize(mod->name);
   		loadmodel->extradata = Hunk_Begin (bsp_size);
   		if (!loadmodel->extradata) {
   			ri.Sys_Error (ERR_DROP, "Hunk_Begin failed for BSP!");
   		}
   		Mod_LoadBrushModel (mod, buf);
   	}
   	break;

   default:
   	ri.Sys_Error (ERR_DROP,"Mod_NumForName: unknown fileid for %s", mod->name);
   	break;
   }

   loadmodel->extradatasize = Hunk_End ();

   ri.FS_FreeFile (buf);
   
   malloc_stats();
   return mod;
}
/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;


/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	    printf("LIGHTMAP DATA: %d bytes (%.2f MB)\n", l->filelen, l->filelen / (1024.0f * 1024.0f));

	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}
	loadmodel->lightdata = Hunk_Alloc ( l->filelen);	
	memcpy_fast (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	int		i;

	if (!l->filelen)
	{
		loadmodel->vis = NULL;
		return;
	}
	loadmodel->vis = Hunk_Alloc ( l->filelen);	
	memcpy_fast (loadmodel->vis, mod_base + l->fileofs, l->filelen);

	loadmodel->vis->numclusters = LittleLong (loadmodel->vis->numclusters);
	for (i=0 ; i<loadmodel->vis->numclusters ; i++)
	{
		loadmodel->vis->bitofs[i][0] = LittleLong (loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong (loadmodel->vis->bitofs[i][1]);
	}
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
    dvertex_t	*in;
    mvertex_t	*out;
    int			i, count;
    float       size_mb;

    in = (void *)(mod_base + l->fileofs);
    if (l->filelen % sizeof(*in))
        ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
    count = l->filelen / sizeof(*in);
    out = Hunk_Alloc ( count*sizeof(*out));	

    // Calculate size in MB
    size_mb = (float)(count * sizeof(*out)) / (1024.0 * 1024.0);
    printf("Vertex data size: %.2f MB (Count: %d vertices)\n", size_mb, count);

    loadmodel->vertexes = out;
    loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = (int16_t)LittleFloat (in->point[0]);
		out->position[1] = (int16_t)LittleFloat (in->point[1]);
		out->position[2] = (int16_t)LittleFloat (in->point[2]);
	}

}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}


/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	mmodel_t	*out;
	int			i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		out->radius = RadiusFromBounds (out->mins, out->maxs);
		out->headnode = LittleLong (in->headnode);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( (count + 1) * sizeof(*out));	

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out, *step;
	int 	i, j, count;
	char	name[MAX_QPATH];
	int		next;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<4 ; j++) {
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat (in->vecs[1][j]);
		}

		out->flags = LittleLong (in->flags);
		next = LittleLong (in->nexttexinfo);
		if (next > 0)
			out->next = loadmodel->texinfo + next;
		else
		    out->next = NULL;
		Com_sprintf (name, sizeof(name), "textures/%s.wal", in->texture);

		out->image = GL_FindImage (name, it_wall);
		if (!out->image)
		{
			ri.Con_Printf (PRINT_ALL, "Couldn't load %s\n", name);
			out->image = r_notexture;
		}
	}

	// count animation frames
	for (i=0 ; i<count ; i++)
	{
		out = &loadmodel->texinfo[i];
		out->numframes = 1;
		for (step = out->next ; step && step != out ; step=step->next)
			out->numframes++;
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
		val = (float)v->position[0] * tex->vecs[j][0] + 
			(float)v->position[1] * tex->vecs[j][1] +
			(float)v->position[2] * tex->vecs[j][2] +
			tex->vecs[j][3];

			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

//		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
//			ri.Sys_Error (ERR_DROP, "Bad surface extents");
	}
}


void GL_BuildPolygonFromSurface(msurface_t *fa);
void GL_EndBuildingLightmaps (void);


/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;
	int			ti;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	currentmodel = loadmodel;

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);		
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort (in->texinfo);
		if (ti < 0 || ti >= loadmodel->numtexinfo)
			ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: bad texinfo number");
		out->texinfo = loadmodel->texinfo + ti;

		CalcSurfaceExtents (out);  // Still needed for Gouraud sampling
				
		// lighting info - still needed for Gouraud vertex sampling
		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);
		if (i == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + i;
		
		// set the drawing flags
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			GL_SubdivideSurface (out);	// cut up polygon for warps
		}

		if (! (out->texinfo->flags & SURF_WARP) ) 
			GL_BuildPolygonFromSurface(out);
	}
}

/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents != -1)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);
		out->contents = -1;	// differentiate from leafs

		for (j=0 ; j<2 ; j++)
		{
			p = LittleLong (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;
//	glpoly_t	*poly;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		out->firstmarksurface = loadmodel->marksurfaces +
			LittleShort(in->firstleafface);
		out->nummarksurfaces = LittleShort(in->numleaffaces);
		
		// gl underwater warp
#if 0
		if (out->contents & (CONTENTS_WATER|CONTENTS_SLIME|CONTENTS_LAVA|CONTENTS_THINWATER) )
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
			{
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
				for (poly = out->firstmarksurface[j]->polys ; poly ; poly=poly->next)
					poly->flags |= SURF_UNDERWATER;
			}
		}
#endif
	}	
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	msurface_t **out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = LittleShort(in[i]);
		if (j < 0 ||  j >= loadmodel->numsurfaces)
			ri.Sys_Error (ERR_DROP, "Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	if (count < 1 || count >= MAX_MAP_SURFEDGES)
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: bad surfedges count in %s: %i",
		loadmodel->name, count);

	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	cplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*2*sizeof(*out));	
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i;
	dheader_t	*header;
	mmodel_t 	*bm;
	
	loadmodel->type = mod_brush;
	if (loadmodel != mod_known)
		ri.Sys_Error (ERR_DROP, "Loaded a brush model after the world");

	header = (dheader_t *)buffer;

	i = LittleLong (header->version);
	if (i != BSPVERSION)
		ri.Sys_Error (ERR_DROP, "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap
	
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);
	mod->numframes = 2;		// regular and alternate animation
	
//
// set up the submodels
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		model_t	*starmod;

		bm = &mod->submodels[i];
		starmod = &mod_inline[i];

		*starmod = *loadmodel;
		
		starmod->firstmodelsurface = bm->firstface;
		starmod->nummodelsurfaces = bm->numfaces;
		starmod->firstnode = bm->headnode;
		if (starmod->firstnode >= loadmodel->numnodes)
			ri.Sys_Error (ERR_DROP, "Inline model %i has bad firstnode", i);

		VectorCopy (bm->maxs, starmod->maxs);
		VectorCopy (bm->mins, starmod->mins);
		starmod->radius = bm->radius;
	
		if (i == 0)
			*loadmodel = *starmod;

		starmod->numleafs = bm->visleafs;
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j;
	dmdl_t				*pinmodel, *pheader;
	dstvert_t			*pinst, *poutst;
	dtriangle_t			*pintri, *pouttri;
	daliasframe_t		*pinframe, *poutframe;
	int					*pincmd, *poutcmd;
	int					version;

	pinmodel = (dmdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		ri.Sys_Error (ERR_DROP, "%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

	// FIX: Use the pre-allocated space that was set in Mod_ForName
	pheader = (dmdl_t *)mod->extradata;
	
	// Copy entire file to hunk memory
	memcpy_fast(pheader, buffer, LittleLong(pinmodel->ofs_end));
	
	// Now byte swap the header fields in place
	for (i=0 ; i<sizeof(dmdl_t)/4 ; i++)
		((int *)pheader)[i] = LittleLong (((int *)pheader)[i]);


	if (pheader->skinheight > MAX_LBM_HEIGHT)
		ri.Sys_Error (ERR_DROP, "model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	if (pheader->num_xyz <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no vertices", mod->name);

	if (pheader->num_xyz > MAX_VERTS)
		ri.Sys_Error (ERR_DROP, "model %s has too many vertices", mod->name);

	if (pheader->num_st <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no st vertices", mod->name);

 

	if (pheader->num_frames <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no frames", mod->name);

//
// load base s and t vertices (not used in gl version)
//
	// FIX: Since we copied the whole file, we now work entirely within pheader
	pinst = (dstvert_t *) ((byte *)pheader + pheader->ofs_st);
	poutst = (dstvert_t *) ((byte *)pheader + pheader->ofs_st);

	for (i=0 ; i<pheader->num_st ; i++)
	{
		poutst[i].s = LittleShort (pinst[i].s);
		poutst[i].t = LittleShort (pinst[i].t);
	}

//
// load triangle lists // which should never happen because ripped em out i hope. 
//
	pintri = (dtriangle_t *) ((byte *)pheader + pheader->ofs_tris);
	pouttri = (dtriangle_t *) ((byte *)pheader + pheader->ofs_tris);

	for (i=0 ; i<pheader->num_tris ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			pouttri[i].index_xyz[j] = LittleShort (pintri[i].index_xyz[j]);
			pouttri[i].index_st[j] = LittleShort (pintri[i].index_st[j]);
		}
	}

//
// load the frames
//
	for (i=0 ; i<pheader->num_frames ; i++)
	{
		pinframe = (daliasframe_t *) ((byte *)pheader 
			+ pheader->ofs_frames + i * pheader->framesize);
		poutframe = (daliasframe_t *) ((byte *)pheader 
			+ pheader->ofs_frames + i * pheader->framesize);

		// Already in same location, but need to byte-swap
		for (j=0 ; j<3 ; j++)
		{
			poutframe->scale[j] = LittleFloat (pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat (pinframe->translate[j]);
		}
		// verts are all 8 bit, so no swapping needed
		// (already in place from the memcpy_fast)
	}

	mod->type = mod_alias;

	//
	// load the glcmds
	//
	pincmd = (int *) ((byte *)pheader + pheader->ofs_glcmds);
	poutcmd = (int *) ((byte *)pheader + pheader->ofs_glcmds);
	for (i=0 ; i<pheader->num_glcmds ; i++)
		poutcmd[i] = LittleLong (pincmd[i]);

	// register all skins
	// Skins already in place from memcpy_fast, just need to register
	for (i=0 ; i<pheader->num_skins ; i++)
	{
		mod->skins[i] = GL_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME
			, it_skin);
	}

	mod->mins[0] = -32;
	mod->mins[1] = -32;
	mod->mins[2] = -32;
	mod->maxs[0] = 32;
	mod->maxs[1] = 32;
	mod->maxs[2] = 32;
}
/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	dsprite_t	*sprin, *sprout;
	int			i;

	sprin = (dsprite_t *)buffer;
	sprout = Hunk_Alloc (modfilelen);

	sprout->ident = LittleLong (sprin->ident);
	sprout->version = LittleLong (sprin->version);
	sprout->numframes = LittleLong (sprin->numframes);

	if (sprout->version != SPRITE_VERSION)
		ri.Sys_Error (ERR_DROP, "%s has wrong version number (%i should be %i)",
				 mod->name, sprout->version, SPRITE_VERSION);

	if (sprout->numframes > MAX_MD2SKINS)
		ri.Sys_Error (ERR_DROP, "%s has too many frames (%i > %i)",
				 mod->name, sprout->numframes, MAX_MD2SKINS);

	// byte swap everything
	for (i=0 ; i<sprout->numframes ; i++)
	{
		sprout->frames[i].width = LittleLong (sprin->frames[i].width);
		sprout->frames[i].height = LittleLong (sprin->frames[i].height);
		sprout->frames[i].origin_x = LittleLong (sprin->frames[i].origin_x);
		sprout->frames[i].origin_y = LittleLong (sprin->frames[i].origin_y);
		memcpy_fast (sprout->frames[i].name, sprin->frames[i].name, MAX_SKINNAME);
		mod->skins[i] = GL_FindImage (sprout->frames[i].name,
			it_sprite);
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginRegistration

Specifies the model that will be used as the world
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginRegistration (char *model)
{
    char fullname[MAX_QPATH];
    int i;

    registration_sequence++;
    r_oldviewcluster = -1;

    Com_sprintf (fullname, sizeof(fullname), "maps/%s.bsp", model);

    // Free all hunk memory used by models BEFORE loading new ones
    Hunk_FreeAll();

    // Free all models except inline models (they'll be recreated)
    for (i = mod_numknown - 1; i >= 0; i--) {
        if (mod_known[i].name[0] && mod_known[i].name[0] != '*') {
            Mod_Free(&mod_known[i]);
        }
    }
    
    // Reset inline models
    memset(mod_inline, 0, sizeof(mod_inline));
    
    // Reset mod_known count but keep inline model slots
    for (i = 0; i < mod_numknown; i++) {
        if (mod_known[i].name[0] == 0) {
            break;
        }
    }
    mod_numknown = i;
    
    r_worldmodel = Mod_ForName(fullname, true);
    r_viewcluster = -1;
}
/*
@@@@@@@@@@@@@@@@@@@@@
R_RegisterModel

@@@@@@@@@@@@@@@@@@@@@
*/
struct model_s *R_RegisterModel (char *name)
{
    model_t *mod;
    int i;
    dsprite_t *sprout;
    dmdl_t *pheader;

    mod = Mod_ForName (name, false);
    if (mod)
    {
        mod->registration_sequence = registration_sequence;

        // Skip texture registration for inline models (they share world model data)
        if (name[0] == '*') {
            return mod;
        }

        // register any images used by the models
        if (mod->type == mod_sprite)
        {
            sprout = (dsprite_t *)mod->extradata;
            if (sprout) {
                for (i=0 ; i<sprout->numframes ; i++)
                    mod->skins[i] = GL_FindImage (sprout->frames[i].name, it_sprite);
            }
        }
        else if (mod->type == mod_alias)
        {
            pheader = (dmdl_t *)mod->extradata;
            if (pheader) {
                for (i=0 ; i<pheader->num_skins ; i++)
                    mod->skins[i] = GL_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME, it_skin);
                mod->numframes = pheader->num_frames;
            }
        }
        else if (mod->type == mod_brush)
        {
            // Only process the main world model, not inline submodels
            if (mod == r_worldmodel && mod->extradata && mod->texinfo && mod->numtexinfo > 0) {
                for (i=0 ; i<mod->numtexinfo ; i++) {
                    if (mod->texinfo[i].image)
                        mod->texinfo[i].image->registration_sequence = registration_sequence;
                }
            }
        }
    }
    return mod;
}
/*
@@@@@@@@@@@@@@@@@@@@@
R_EndRegistration

@@@@@@@@@@@@@@@@@@@@@
*/
void R_EndRegistration (void)
{
	int		i;
	model_t	*mod;

	for (i=0, mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence != registration_sequence)
		{	// don't need this model
			Mod_Free (mod);
		}
	}

	GL_FreeUnusedImages ();
}


//=============================================================================


/*
================
Mod_Free
================
*/
void Mod_Free (model_t *mod)
{
    // DON'T free extradata here - Hunk_FreeAll() already freed everything
    // Just clear the model structure
    memset (mod, 0, sizeof(*mod));
}



/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll (void)
{
	int		i;

	for (i=0 ; i<mod_numknown ; i++)
	{
		if (mod_known[i].extradatasize)
			Mod_Free (&mod_known[i]);
			
	}
}