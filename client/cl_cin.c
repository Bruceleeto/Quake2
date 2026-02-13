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

#include "client.h"
#include <kos.h>
#include "mpeg.h"

static mpeg_player_t *mpeg_player = NULL;

/*
==================
SCR_StopCinematic
==================
*/
void SCR_StopCinematic(void)
{
    cl.cinematictime = 0;

    if(mpeg_player) {
        mpeg_player_destroy(mpeg_player);
        mpeg_player = NULL;
    }
}

/*
====================
SCR_FinishCinematic

Called when either the cinematic completes, or it is aborted
====================
*/
void SCR_FinishCinematic(void)
{
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Print(&cls.netchan.message, va("nextserver %i\n", cl.servercount));
}

/*
==================
SCR_RunCinematic
==================
*/
void SCR_RunCinematic(void)
{
    if(cl.cinematictime <= 0) {
        SCR_StopCinematic();
        return;
    }

    if(!mpeg_player)
        return;

    /* Check for skip */
    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
        if(st->buttons & CONT_START) {
            SCR_StopCinematic();
            SCR_FinishCinematic();
            return;
        }
    MAPLE_FOREACH_END()

    mpeg_decode_result_t result = mpeg_decode_step(mpeg_player);
    if(result == MPEG_DECODE_EOF) {
        SCR_StopCinematic();
        SCR_FinishCinematic();
        cl.cinematictime = 1;
        SCR_BeginLoadingPlaque();
        cl.cinematictime = 0;
    }
}

/*
==================
SCR_DrawCinematic

Returns true if a cinematic is active, meaning the view rendering
should be skipped
==================
*/
qboolean SCR_DrawCinematic(void)
{
    if(cl.cinematictime <= 0)
        return false;

    if(!mpeg_player)
        return false;

    pvr_wait_ready();
    pvr_scene_begin();
    mpeg_upload_frame(mpeg_player);
    pvr_list_begin(PVR_LIST_OP_POLY);
    mpeg_draw_frame(mpeg_player);
    pvr_list_finish();
    pvr_scene_finish();

    return true;
}

/*
==================
SCR_PlayCinematic
==================
*/
void SCR_PlayCinematic(char *arg)
{
    char name[MAX_OSPATH];
    char basename[MAX_OSPATH];
    char *dot;

    CDAudio_Stop();

    strncpy(basename, arg, sizeof(basename) - 1);
    basename[sizeof(basename) - 1] = '\0';

    dot = strstr(basename, ".cin");
    if(dot)
        *dot = '\0';

    Com_sprintf(name, sizeof(name), "/pc/baseq2/video/%s.mpg", basename);

    printf("SCR_PlayCinematic: Playing MPEG '%s'\n", name);

    SCR_EndLoadingPlaque();
    cls.state = ca_active;

    mpeg_player = mpeg_player_create(name);
    if(!mpeg_player) {
        printf("SCR_PlayCinematic: Failed to open '%s'\n", name);
        SCR_FinishCinematic();
        cl.cinematictime = 0;
        return;
    }

    cl.cinematictime = Sys_Milliseconds();
}

/* Stubs — kept so the rest of the engine links */
void SCR_LoadPCX(char *filename, byte **pic, byte **palette, int *width, int *height) { }
byte *SCR_ReadNextFrame(void) { return NULL; }