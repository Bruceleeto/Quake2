 #include <ctype.h>
#include <dc/pvr.h>
#include <kos.h>
#include "gl_local.h"
#include "pvrtex.h"

image_t		gltextures[MAX_GLTEXTURES];
int			numgltextures;
int			base_textureid;

static byte			 intensitytable[256];
static unsigned char gammatable[256];

cvar_t		*intensity;

unsigned	d_8to24table[256];

 
 pvr_texture_t pvr_textures[MAX_GLTEXTURES];
 int next_texture_id = 1;  // Start at 1, 0 means unloaded

int		gl_solid_format = 3;
int		gl_alpha_format = 4;
int		gl_tex_solid_format = 3;
int		gl_tex_alpha_format = 4;
int		gl_filter_min = 0;  // Not used in PVR
int		gl_filter_max = 0;  // Not used in PVR

void GL_SetTexturePalette( unsigned palette[256] )
{
    // Not needed for PVR
}

void GL_EnableMultitexture( qboolean enable )
{
    // Stub for now
}

void GL_SelectTexture( GLenum texture )
{
 
}

void GL_TexEnv( GLenum mode )
{
    // Not needed for PVR - texture environment is set per-polygon
}

void GL_Bind (int texnum)
{
    extern image_t *draw_chars;
    
    if (gl_nobind->value && draw_chars)
        texnum = draw_chars->texnum;
    
    // Just track current texture for state management
    // Actual texture is specified in polygon header when rendering
    gl_state.currenttextures[gl_state.currenttmu] = texnum;
}

void GL_MBind( GLenum target, int texnum )
{
    // Stub
}

void GL_TextureMode( char *string )
{
    // PVR filtering is set per-polygon header, not globally
}

void GL_TextureAlphaMode( char *string )
{
    // Stub
}

void GL_TextureSolidMode( char *string )
{
    // Stub
}

void GL_ImageList_f (void)
{
    int i;
    image_t *image;
    int texels = 0;
    
    ri.Con_Printf(PRINT_ALL, "------------------\n");
    for (i = 0, image = gltextures; i < numgltextures; i++, image++)
    {
        if (image->texnum == 0)
            continue;
            
        texels += image->upload_width * image->upload_height;
        
        switch (image->type)
        {
        case it_skin:
            ri.Con_Printf(PRINT_ALL, "M");
            break;
        case it_sprite:
            ri.Con_Printf(PRINT_ALL, "S");
            break;
        case it_wall:
            ri.Con_Printf(PRINT_ALL, "W");
            break;
        case it_pic:
            ri.Con_Printf(PRINT_ALL, "P");
            break;
        default:
            ri.Con_Printf(PRINT_ALL, " ");
            break;
        }
        
        ri.Con_Printf(PRINT_ALL, " %3i %3i: %s\n",
            image->upload_width, image->upload_height, image->name);
    }
    ri.Con_Printf(PRINT_ALL, "Total texel count: %i\n", texels);
}

// Scrap allocation - not really used with PVR but kept for compatibility
#define	MAX_SCRAPS		1
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT];
qboolean	scrap_dirty;
static int scrap_texture_id = 0;

int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
    // Stub - not used with PVR
    return -1;
}

void Scrap_Upload (void)
{
    // Stub - not used with PVR
}

void LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height)
{
    // Stub - not loading PCX on Dreamcast
    *pic = NULL;
    *palette = NULL;
}

void R_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
    // Stub - not needed
}

void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out,  int outwidth, int outheight)
{
    // Stub - PVR handles texture scaling
}

void GL_LightScaleTexture (unsigned *in, int inwidth, int inheight, qboolean only_gamma )
{
    // Stub - not needed for PVR
}

void GL_MipMap (byte *in, int width, int height)
{
    // Stub - PVR handles mipmaps if needed
}

int upload_width, upload_height;
qboolean uploaded_paletted;

qboolean GL_Upload32 (unsigned *data, int width, int height, qboolean mipmap)
{
    // Stub - not used with PVR, textures are pre-converted
    return false;
}

qboolean GL_Upload8 (byte *data, int width, int height,  qboolean mipmap, qboolean is_sky )
{
    // Stub - not used with PVR
    return false;
}

// Main texture loading function using pvrtex
image_t *GL_LoadPic (char *name, byte *pic, int width, int height, imagetype_t type, int bits)
{
    image_t *image;
    int i;
    
    // Find a free image_t
    for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
    {
        if (!image->texnum)
            break;
    }
    if (i == numgltextures)
    {
        if (numgltextures == MAX_GLTEXTURES)
            ri.Sys_Error (ERR_DROP, "MAX_GLTEXTURES");
        numgltextures++;
    }
    image = &gltextures[i];
    
    if (strlen(name) >= sizeof(image->name))
        ri.Sys_Error (ERR_DROP, "Draw_LoadPic: \"%s\" is too long", name);
    strcpy (image->name, name);
    image->registration_sequence = registration_sequence;
    
    image->width = width;
    image->height = height;
    image->type = type;
    image->texnum = next_texture_id++;
    image->scrap = false;
    image->upload_width = width;
    image->upload_height = height;
    image->paletted = false;
    image->sl = 0;
    image->sh = 1;
    image->tl = 0;
    image->th = 1;
    
    // Actually upload particle texture to PVR memory
    if (pic && bits == 32 && strcmp(name, "***particle***") == 0) {
        // Convert RGBA to ARGB4444 for particle texture
        uint16_t converted[64];  // 8x8
        byte *src = pic;
        for (i = 0; i < 64; i++) {
            byte r = src[i*4 + 0] >> 4;
            byte g = src[i*4 + 1] >> 4;
            byte b = src[i*4 + 2] >> 4;
            byte a = src[i*4 + 3] >> 4;
            converted[i] = (a << 12) | (r << 8) | (g << 4) | b;
        }
        
        // Allocate and upload to PVR
        pvr_ptr_t tex_ptr = pvr_mem_malloc(128);  // 8x8x2 bytes
        pvr_txr_load(converted, tex_ptr, 128);
        
        // Store in pvr_textures array
        pvr_textures[image->texnum].ptr = tex_ptr;
        pvr_textures[image->texnum].format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
        pvr_textures[image->texnum].width = 8;
        pvr_textures[image->texnum].height = 8;
        pvr_textures[image->texnum].loaded = 1;
        
        image->has_alpha = true;
    }
    else {
        image->has_alpha = false;
    }
    
    return image;
}

// Helper function to check if a number is power of 2
static qboolean IsPowerOf2(int value)
{
    return (value > 0) && ((value & (value - 1)) == 0);
}

// Main function for finding/loading textures
image_t *GL_FindImage (char *name, imagetype_t type)
{
    image_t *image;
    int i, len;
    
    if (!name)
        return NULL;
    len = strlen(name);
    if (len < 5)
        return NULL;
    
    // Look for already loaded texture
    for (i = 0, image = gltextures; i < numgltextures; i++, image++)
    {
        if (!strcmp(name, image->name))
        {
            image->registration_sequence = registration_sequence;
            return image;
        }
    }
    
    // Convert all texture requests to .dt format
    char dt_name[MAX_OSPATH];
    
    if (!strcmp(name+len-4, ".pcx") || !strcmp(name+len-4, ".wal") || 
        !strcmp(name+len-4, ".tga") || !strcmp(name+len-4, ".png"))
    {
        // Convert extension to .dt
        strcpy(dt_name, name);
        strcpy(dt_name+len-4, ".dt");
    }
    else
    {
        ri.Con_Printf(PRINT_ALL, "GL_FindImage: Unknown extension for %s\n", name);
        return NULL;
    }
    
    // Convert to lowercase for filesystem lookup
    char lower_name[MAX_OSPATH];
    strcpy(lower_name, dt_name);
    for (char *p = lower_name; *p; p++) {
        *p = tolower(*p);
    }
    
  //  ri.Con_Printf(PRINT_ALL, "GL_FindImage: Trying to load %s...\n", lower_name);
    
    // Load DTEX texture using pvrtex loader
    dttex_info_t tex_info;
    if (pvrtex_load(lower_name, &tex_info))
    {
        // Find a free image_t slot
        for (i=0, image=gltextures ; i<numgltextures ; i++,image++)
        {
            if (!image->texnum)
                break;
        }
        if (i == numgltextures)
        {
            if (numgltextures == MAX_GLTEXTURES)
                ri.Sys_Error (ERR_DROP, "MAX_GLTEXTURES");
            numgltextures++;
        }
        image = &gltextures[i];
        
        // IMPORTANT: Store the ORIGINAL name, not lowercase version
        // This ensures cache lookups work correctly
        if (strlen(name) >= sizeof(image->name))
            ri.Sys_Error (ERR_DROP, "GL_FindImage: \"%s\" is too long", name);
        strcpy(image->name, name);  // Keep original name for cache
        image->registration_sequence = registration_sequence;
        image->width = tex_info.width;
        image->height = tex_info.height;
        image->type = type;
        
        // Use next available texture ID
        image->texnum = next_texture_id++;
        
        // Store PVR texture info
        pvr_textures[image->texnum].ptr = tex_info.ptr;
        pvr_textures[image->texnum].format = tex_info.pvrformat;
        pvr_textures[image->texnum].width = tex_info.width;
        pvr_textures[image->texnum].height = tex_info.height;
        pvr_textures[image->texnum].loaded = 1;
        
        image->upload_width = tex_info.width;
        image->upload_height = tex_info.height;
        image->paletted = false;
        image->scrap = false;
        image->sl = 0;
        image->sh = 1;
        image->tl = 0;
        image->th = 1;
        
        // Check if texture has alpha
        if (tex_info.pvrformat == PVR_TXRFMT_ARGB1555 ||
            tex_info.pvrformat == PVR_TXRFMT_ARGB4444)
        {
            image->has_alpha = true;
        }
        else
        {
            image->has_alpha = false;
        }
        
     //   ri.Con_Printf(PRINT_ALL, "GL_FindImage: SUCCESS! %s loaded as texnum=%d\n", 
       //              lower_name, image->texnum);
      //  ri.Con_Printf(PRINT_ALL, "  Size: %dx%d, Format: 0x%08x, Ptr: %p\n",
      //               tex_info.width, tex_info.height, tex_info.pvrformat, tex_info.ptr);
        
        return image;
    }
    
    // If DTEX loading failed
   // ri.Con_Printf(PRINT_ALL, "GL_FindImage: FAILED to load %s\n", lower_name);
    return r_notexture;
}

struct image_s *R_RegisterSkin (char *name)
{
    return GL_FindImage (name, it_skin);
}

void GL_FreeUnusedImages (void)
{
    int		i;
    image_t	*image;
    
    // Never free r_notexture or particle texture
    r_notexture->registration_sequence = registration_sequence;
    r_particletexture->registration_sequence = registration_sequence;
    
    for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
    {
        if (image->registration_sequence == registration_sequence)
            continue;		// used this sequence
        if (!image->registration_sequence)
            continue;		// free image_t slot
        if (image->type == it_pic)
            continue;		// don't free pics
            
        // Free PVR texture memory
        if (image->texnum > 0 && pvr_textures[image->texnum].loaded)
        {
            if (pvr_textures[image->texnum].ptr)
            {
                pvr_mem_free(pvr_textures[image->texnum].ptr);
            }
            pvr_textures[image->texnum].loaded = 0;
        }
        
        memset (image, 0, sizeof(*image));
    }
}

int Draw_GetPalette (void)
{
    int i;
    
    // Hardcoded Quake 2 palette (256 RGB triplets)
    static unsigned char quake2_palette[768] = {
        0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,
        123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,
        99,75,35,91,67,31,83,63,31,79,59,27,71,55,27,63,47,23,59,43,23,51,39,19,
        47,35,19,43,31,19,39,27,15,35,23,15,27,19,11,23,15,11,19,15,7,15,11,7,
        95,95,111,91,91,103,91,83,95,87,79,91,83,75,83,79,71,75,71,63,67,63,59,59,
        59,55,55,51,47,47,47,43,43,39,39,39,35,35,35,27,27,27,23,23,23,19,19,19,
        143,119,83,123,99,67,115,91,59,103,79,47,207,151,75,167,123,59,139,103,47,111,83,39,
        235,159,39,203,139,35,175,119,31,147,99,27,119,79,23,91,59,15,63,39,11,35,23,7,
        167,59,43,159,47,35,151,43,27,139,39,19,127,31,15,115,23,11,103,23,7,87,19,0,
        75,15,0,67,15,0,59,15,0,51,11,0,43,11,0,35,11,0,27,7,0,19,7,0,
        123,95,75,115,87,67,107,83,63,103,79,59,95,71,55,87,67,51,83,63,47,75,55,43,
        67,51,39,63,47,35,55,39,27,47,35,23,39,27,19,31,23,15,23,15,11,15,11,7,
        111,59,23,95,55,23,83,47,23,67,43,23,55,35,19,39,27,15,27,19,11,15,11,7,
        179,91,79,191,123,111,203,155,147,215,187,183,203,215,223,179,199,211,159,183,195,135,167,183,
        115,151,167,91,135,155,71,119,139,47,103,127,23,83,111,19,75,103,15,67,91,11,63,83,
        7,55,75,7,47,63,7,39,51,0,31,43,0,23,31,0,15,19,0,7,11,0,0,0,
        139,87,87,131,79,79,123,71,71,115,67,67,107,59,59,99,51,51,91,47,47,87,43,43,
        75,35,35,63,31,31,51,27,27,43,19,19,31,15,15,19,11,11,11,7,7,0,0,0,
        151,159,123,143,151,115,135,139,107,127,131,99,119,123,95,115,115,87,107,107,79,99,99,71,
        91,91,67,79,79,59,67,67,51,55,55,43,47,47,35,35,35,27,23,23,19,15,15,11,
        159,75,63,147,67,55,139,59,47,127,55,39,119,47,35,107,43,27,99,35,23,87,31,19,
        79,27,15,67,23,11,55,19,11,43,15,7,31,11,7,23,7,0,11,0,0,0,0,0,
        119,123,207,111,115,195,103,107,183,99,99,167,91,91,155,83,87,143,75,79,127,71,71,115,
        63,63,103,55,55,87,47,47,75,39,39,63,35,31,47,27,23,35,19,15,23,11,7,7,
        155,171,123,143,159,111,135,151,99,123,139,87,115,131,75,103,119,67,95,111,59,87,103,51,
        75,91,39,63,79,27,55,67,19,47,59,11,35,47,7,27,35,0,19,23,0,11,15,0,
        0,255,0,35,231,15,63,211,27,83,187,39,95,167,47,95,143,51,95,123,51,255,255,255,
        255,255,211,255,255,167,255,255,127,255,255,83,255,255,39,255,235,31,255,215,23,255,191,15,
        255,171,7,255,147,0,239,127,0,227,107,0,211,87,0,199,71,0,183,59,0,171,43,0,
        155,31,0,143,23,0,127,15,0,115,7,0,95,0,0,71,0,0,47,0,0,27,0,0,
        239,0,0,55,55,255,255,0,0,0,0,255,43,43,35,27,27,23,19,19,15,235,151,127,
        195,115,83,159,87,51,123,63,27,235,211,199,199,171,155,167,139,119,135,107,87,159,91,83
    };
    
    // Convert palette to d_8to24table
    for (i = 0; i < 256; i++)
    {
        int r = quake2_palette[i*3+0];
        int g = quake2_palette[i*3+1]; 
        int b = quake2_palette[i*3+2];
        
        unsigned v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
        d_8to24table[i] = LittleLong(v);
    }
    
    d_8to24table[255] &= LittleLong(0xffffff);  // 255 is transparent
    
    return 0;
}

void GL_InitImages (void)
{
    int i, j;
    float g = vid_gamma->value;
    
    registration_sequence = 1;
    
    // Init intensity conversions
    intensity = ri.Cvar_Get ("intensity", "2", 0);
    
    if ( intensity->value <= 1 )
        ri.Cvar_Set( "intensity", "1" );
    
    gl_state.inverse_intensity = 1 / intensity->value;
    
    Draw_GetPalette ();
    
    if ( g == 1 )
    {
        for ( i = 0; i < 256; i++ )
            gammatable[i] = i;
    }
    else
    {
        for ( i = 0; i < 256; i++ )
        {
            float inf = 255 * pow ( (i+0.5)/255.5 , g ) + 0.5;
            if (inf < 0)
                inf = 0;
            if (inf > 255)
                inf = 255;
            gammatable[i] = inf;
        }
    }
    
    for (i=0 ; i<256 ; i++)
    {
        j = i*intensity->value;
        if (j > 255)
            j = 255;
        intensitytable[i] = j;
    }
    
    // Initialize PVR texture array
    memset(pvr_textures, 0, sizeof(pvr_textures));
    
    // Create a small default white texture for missing textures
    // This fixes the issue where texnum=2 has no data
    uint16_t white_tex[8*8];
    for (i = 0; i < 8*8; i++) {
        white_tex[i] = 0xFFFF; // White in RGB565 format
    }
    
    // Allocate PVR memory for default texture
    pvr_ptr_t default_tex = pvr_mem_malloc(8*8*2);
    if (default_tex) {
        pvr_txr_load(white_tex, default_tex, 8*8*2);
        
        // Fill in slots 1-10 with default texture so early IDs work
        for (i = 1; i <= 10; i++) {
            pvr_textures[i].ptr = default_tex;
            pvr_textures[i].format = PVR_TXRFMT_RGB565 | PVR_TXRFMT_TWIDDLED;
            pvr_textures[i].width = 8;
            pvr_textures[i].height = 8;
            pvr_textures[i].loaded = 1;
        }
    }
}

void GL_ShutdownImages (void)
{
    int		i;
    image_t	*image;
    
    for (i=0, image=gltextures ; i<numgltextures ; i++, image++)
    {
        if (!image->registration_sequence)
            continue;		// free image_t slot
            
        // Free PVR texture memory
        if (image->texnum > 0 && pvr_textures[image->texnum].loaded)
        {
            if (pvr_textures[image->texnum].ptr)
            {
                pvr_mem_free(pvr_textures[image->texnum].ptr);
            }
        }
        
        memset (image, 0, sizeof(*image));
    }
}