//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	WAD I/O functions.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

typedef struct
{
    wad_file_t wad;
    FILE *fstream;
} stdc_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static wad_file_t *W_StdC_OpenFile(char *path)
{
    stdc_wad_file_t *result;
    FILE *fstream;

    fstream = fopen(path, "rb");

    if (fstream == NULL)
    {
        return NULL;
    }

    // Create a new stdc_wad_file_t to hold the file handle.

    result = Z_Malloc(sizeof(stdc_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = M_FileLength(fstream);
    result->fstream = fstream;

    // Preload WAD sequentially into RAM so R_InitData and lump lookups are instant.
    if (result->wad.length > 0)
    {
        byte *buf = (byte *)malloc(result->wad.length);
        if (buf != NULL)
        {
            size_t total = 0;
            fseek(fstream, 0, SEEK_SET);
            while (total < result->wad.length)
            {
                size_t got = fread(buf + total, 1, result->wad.length - total, fstream);
                if (got == 0)
                {
                    break;
                }
                total += got;
            }
            if (total == result->wad.length)
            {
                result->wad.mapped = buf;
            }
            else
            {
                free(buf);
                fseek(fstream, 0, SEEK_SET);
            }
        }
    }

    return &result->wad;
}

static void W_StdC_CloseFile(wad_file_t *wad)
{
    stdc_wad_file_t *stdc_wad;

    stdc_wad = (stdc_wad_file_t *) wad;

    if (stdc_wad->wad.mapped != NULL)
    {
        free(stdc_wad->wad.mapped);
        stdc_wad->wad.mapped = NULL;
    }
    fclose(stdc_wad->fstream);
    Z_Free(stdc_wad);
}

// Read data from the specified position in the file into the 
// provided buffer.  Returns the number of bytes read.

size_t W_StdC_Read(wad_file_t *wad, unsigned int offset,
                   void *buffer, size_t buffer_len)
{
    stdc_wad_file_t *stdc_wad;
    size_t result;

    if (wad->mapped != NULL)
    {
        if (offset >= wad->length)
        {
            return 0;
        }
        if (offset + buffer_len > wad->length)
        {
            buffer_len = wad->length - offset;
        }
        memcpy(buffer, wad->mapped + offset, buffer_len);
        return buffer_len;
    }

    stdc_wad = (stdc_wad_file_t *) wad;

    // Jump to the specified position in the file.

    fseek(stdc_wad->fstream, offset, SEEK_SET);

    // Read into the buffer.

    result = fread(buffer, 1, buffer_len, stdc_wad->fstream);

    return result;
}


wad_file_class_t stdc_wad_file = 
{
    W_StdC_OpenFile,
    W_StdC_CloseFile,
    W_StdC_Read,
};


