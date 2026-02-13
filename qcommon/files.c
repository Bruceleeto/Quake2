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

#include "qcommon.h"

/*
=============================================================================

SIMPLIFIED QUAKE FILESYSTEM - NO PAK FILES

=============================================================================
*/

char	fs_gamedir[MAX_OSPATH];
cvar_t	*fs_basedir;
cvar_t	*fs_gamedirvar;  // Add this line

int file_from_pak = 0;  // Keep for compatibility

/*
================
FS_filelength
================
*/
int FS_filelength (FILE *f)
{
	int		pos;
	int		end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
void FS_CreatePath (char *path)
{
	char	*ofs;
	
	for (ofs = path+1 ; *ofs ; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			Sys_Mkdir (path);
			*ofs = '/';
		}
	}
}

/*
==============
FS_FCloseFile
==============
*/
void FS_FCloseFile (FILE *f)
{
	fclose (f);
}

/*
===========
FS_FOpenFile

Simple file opening - just try to open the file directly
===========
*/
int FS_FOpenFile (char *filename, FILE **file)
{
	char netpath[MAX_OSPATH];
	
	file_from_pak = 0;
	
	// Try in game directory first
	Com_sprintf (netpath, sizeof(netpath), "%s/%s", fs_gamedir, filename);
	*file = fopen (netpath, "rb");
	
	if (*file) {
		Com_DPrintf ("FindFile: %s\n", netpath);
		return FS_filelength (*file);
	}
	
	// Try relative to current directory as fallback
	*file = fopen (filename, "rb");
	if (*file) {
		Com_DPrintf ("FindFile: %s\n", filename);
		return FS_filelength (*file);
	}
	
	printf("DEBUG: Failed to find file: %s\n", filename);
	Com_DPrintf ("FindFile: can't find %s\n", filename);
	
	*file = NULL;
	return -1;
}

/*
=================
FS_Read

Properly handles partial reads
=================
*/
#define	MAX_READ	0x10000		// read in blocks of 64k
void FS_Read (void *buffer, int len, FILE *f)
{
	int		block, remaining;
	int		read;
	byte	*buf;

	buf = (byte *)buffer;

	// read in chunks
	remaining = len;
	while (remaining)
	{
		block = remaining;
		if (block > MAX_READ)
			block = MAX_READ;
		read = fread (buf, 1, block, f);
		
		if (read == 0 || read == -1)
			Com_Error (ERR_FATAL, "FS_Read: %d bytes read", read);

		remaining -= read;
		buf += read;
	}
}

/*
============
FS_LoadFile

Load entire file into memory
============
*/
int FS_LoadFile (char *path, void **buffer)
{
	FILE	*h;
	byte	*buf;
	int		len;

	// look for the file
	len = FS_FOpenFile (path, &h);
	if (!h)
	{
		if (buffer)
			*buffer = NULL;
		return -1;
	}
	
	if (!buffer)
	{
		fclose (h);
		return len;
	}

	buf = Z_Malloc(len);
	*buffer = buf;

	FS_Read (buf, len, h);
	fclose (h);

	return len;
}

/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile (void *buffer)
{
	Z_Free (buffer);
}

/*
============
FS_Gamedir

Called to find where to write a file (demos, savegames, etc)
============
*/
char *FS_Gamedir (void)
{
	return fs_gamedir;
}

/*
=============
FS_ExecAutoexec
=============
*/
void FS_ExecAutoexec (void)
{
	char name [MAX_QPATH];

	Com_sprintf(name, sizeof(name), "%s/autoexec.cfg", fs_gamedir); 
	if (Sys_FindFirst(name, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM))
		Cbuf_AddText ("exec autoexec.cfg\n");
	Sys_FindClose();
}

/*
================
FS_SetGamedir

Sets the gamedir to a different directory.
================
*/
void FS_SetGamedir (char *dir)
{
	if (strstr(dir, "..") || strstr(dir, "/")
		|| strstr(dir, "\\") || strstr(dir, ":") )
	{
		Com_Printf ("Gamedir should be a single filename, not a path\n");
		return;
	}

	Com_sprintf (fs_gamedir, sizeof(fs_gamedir), "%s/%s", fs_basedir->string, dir);
}

/*
** FS_ListFiles
*/
char **FS_ListFiles( char *findname, int *numfiles, unsigned musthave, unsigned canthave )
{
	char *s;
	int nfiles = 0;
	char **list = 0;

	s = Sys_FindFirst( findname, musthave, canthave );
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
			nfiles++;
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	if ( !nfiles )
		return NULL;

	nfiles++; // add space for a guard
	*numfiles = nfiles;

	list = malloc( sizeof( char * ) * nfiles );
	memset( list, 0, sizeof( char * ) * nfiles );

	s = Sys_FindFirst( findname, musthave, canthave );
	nfiles = 0;
	while ( s )
	{
		if ( s[strlen(s)-1] != '.' )
		{
			list[nfiles] = strdup( s );
#ifdef _WIN32
			strlwr( list[nfiles] );
#endif
			nfiles++;
		}
		s = Sys_FindNext( musthave, canthave );
	}
	Sys_FindClose ();

	return list;
}

/*
** FS_Dir_f
*/
void FS_Dir_f( void )
{
	char	findname[1024];
	char	wildcard[1024] = "*.*";
	char	**dirnames;
	int		ndirs;

	if ( Cmd_Argc() != 1 )
	{
		strcpy( wildcard, Cmd_Argv( 1 ) );
	}

	Com_sprintf( findname, sizeof(findname), "%s/%s", fs_gamedir, wildcard );

	Com_Printf( "Directory of %s\n", findname );
	Com_Printf( "----\n" );

	if ( ( dirnames = FS_ListFiles( findname, &ndirs, 0, 0 ) ) != 0 )
	{
		int i;

		for ( i = 0; i < ndirs-1; i++ )
		{
			if ( strrchr( dirnames[i], '/' ) )
				Com_Printf( "%s\n", strrchr( dirnames[i], '/' ) + 1 );
			else
				Com_Printf( "%s\n", dirnames[i] );

			free( dirnames[i] );
		}
		free( dirnames );
	}
	Com_Printf( "\n" );
}

/*
============
FS_Path_f
============
*/
void FS_Path_f (void)
{
	Com_Printf ("Current game directory: %s\n", fs_gamedir);
}

/*
================
FS_NextPath

Simple version - just return the game directory
================
*/
char *FS_NextPath (char *prevpath)
{
	if (!prevpath)
		return fs_gamedir;
	return NULL;
}

/*
================
FS_InitFilesystem
================
*/
void FS_InitFilesystem (void)
{
	// Remove this line: cvar_t *fs_gamedirvar;
	
	Cmd_AddCommand ("path", FS_Path_f);
	Cmd_AddCommand ("dir", FS_Dir_f );

	fs_basedir = Cvar_Get ("basedir", ".", CVAR_NOSET);
	Com_sprintf(fs_gamedir, sizeof(fs_gamedir), "%s/cd/baseq2", fs_basedir->string);

	fs_gamedirvar = Cvar_Get ("game", "", CVAR_LATCH|CVAR_SERVERINFO);
	if (fs_gamedirvar->string[0])
		FS_SetGamedir (fs_gamedirvar->string);
		
	Com_Printf("Game directory: %s\n", fs_gamedir);
}

// Stub functions that some parts of the code might expect
int Developer_searchpath (int who) { return 0; }
void FS_Link_f (void) { Com_Printf("Links not supported in simplified filesystem\n"); }