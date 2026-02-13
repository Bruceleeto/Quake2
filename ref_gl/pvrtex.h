#ifndef PVTEX_H
#define PVTEX_H

#include <dc/pvr.h>
#include <errno.h>
#include <pvrtex/file_dctex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// These types should be available from whatever includes pvrtex.h
// Don't redefine them here

typedef fDtHeader dt_header_t;

typedef struct {
  dt_header_t hdr;
  uint32_t pvrformat;
  union flags {
    uint32_t raw;
    struct {
      uint32_t palettised : 1;
      uint32_t twiddled : 1;
      uint32_t compressed : 1;
      uint32_t strided : 1;
      uint32_t mipmapped : 1;
      uint32_t : 27;
    };
  } flags;
  uint16_t width;
  uint16_t height;
  pvr_ptr_t ptr;
} dttex_info_t;

// Declare ri as extern - it should be defined elsewhere
extern refimport_t ri;

/**
 * @brief Load a texture from a file using Quake 2's filesystem
 *
 * @param filename The filename of the texture
 * @param texinfo The texture texinfo struct
 * @return int 1 on success, 0 on failure
 */
static inline int pvrtex_load(const char *filename, dttex_info_t *texinfo) {
  byte *file_data;
  int file_size;
  
  file_size = ri.FS_LoadFile(filename, (void **)&file_data);
  if (!file_data || file_size <= 0) {
    printf("Error: pvrtex_load: can't load %s\n", filename);
    return 0;
  }
  
  byte *ptr = file_data;
  memcpy_fast(&(texinfo->hdr), ptr, sizeof(dt_header_t));
  
  size_t tdatasize = texinfo->hdr.chunk_size - ((1 + texinfo->hdr.header_size) << 5);
  
  texinfo->flags.compressed = fDtIsCompressed(&texinfo->hdr);
  texinfo->flags.mipmapped = fDtIsMipmapped(&texinfo->hdr);
  texinfo->flags.palettised = fDtIsPalettized(&texinfo->hdr);
  texinfo->flags.strided = fDtIsStrided(&texinfo->hdr);
  texinfo->flags.twiddled = fDtIsTwiddled(&texinfo->hdr);
  texinfo->width = fDtGetPvrWidth(&texinfo->hdr);
  texinfo->height = fDtGetPvrHeight(&texinfo->hdr);
  
  texinfo->pvrformat = texinfo->hdr.pvr_type & 0xFFC00000;
  
  texinfo->ptr = pvr_mem_malloc(tdatasize);
  if (texinfo->ptr == NULL) {
    printf("Error: pvr_mem_malloc failed for %s\n", filename);
    ri.FS_FreeFile(file_data);
    return 0;
  }
  
  // Skip header and load texture data
  ptr = file_data + ((1 + texinfo->hdr.header_size) << 5);
  pvr_txr_load(ptr, texinfo->ptr, tdatasize);
  
  ri.FS_FreeFile(file_data);
  return 1;
}

/**
 * @brief Load a palette from a file using Quake 2's filesystem
 * @param filename The filename of the palette
 * @param fmt The format of the palette
 * @param offset The offset to load the palette
 * @return int 1 on success, 0 on failure
 */
static inline int pvrtex_load_palette(const char *filename, int fmt, size_t offset) {
  struct {
    char fourcc[4];
    size_t colors;
  } palette_hdr;
  
  byte *file_data;
  int file_size;
  
  file_size = ri.FS_LoadFile(filename, (void **)&file_data);
  if (!file_data || file_size <= 0) {
    printf("Error: pvrtex_load_palette: can't load %s\n", filename);
    return 0;
  }
  
  byte *ptr = file_data;
  memcpy_fast(&palette_hdr, ptr, sizeof(palette_hdr));
  ptr += sizeof(palette_hdr);
  
  uint32_t *colors = (uint32_t *)ptr;
  
  pvr_set_pal_format(fmt);
  for (size_t i = 0; i < palette_hdr.colors; i++) {
    uint32_t color = colors[i];
    switch (fmt) {
      case PVR_PAL_ARGB8888:
        break;
      case PVR_PAL_ARGB4444:
        color = ((color & 0xF0000000) >> 16) | ((color & 0x00F00000) >> 12) | 
                ((color & 0x0000F000) >> 8) | ((color & 0x000000F0) >> 4);
        break;
      case PVR_PAL_RGB565:
        color = ((color & 0x00F80000) >> 8) | ((color & 0x0000FC00) >> 5) |
                ((color & 0x000000F8) >> 3);
        break;
      case PVR_PAL_ARGB1555:
        color = ((color & 0x80000000) >> 16) | ((color & 0x00F80000) >> 9) |
                ((color & 0x0000F800) >> 6) | ((color & 0x000000F8) >> 3);
        break;
      default:
        break;
    }
    pvr_set_pal_entry(i + offset, color);
  }
  
  ri.FS_FreeFile(file_data);
  return 1;
}

/**
 * @brief Unload a texture from memory
 * @param texinfo The texture texinfo struct
 * @return int 1 on success, 0 on failure
 */
static inline int pvrtex_unload(dttex_info_t *texinfo) {
  if (texinfo->ptr != NULL) {
    pvr_mem_free(texinfo->ptr);
    texinfo->ptr = NULL;
    return 1;
  }
  return 0;
}

#endif  // PVTEX_H