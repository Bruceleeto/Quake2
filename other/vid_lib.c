// Main windowed and fullscreen graphics interface module
// Simplified for OpenGL-only at 640x480

#include "../client/client.h"
#include "../quakegeneric.h"

// Structure containing functions exported from refresh DLL
refexport_t	re;

// Console variables that we need to access from this module
cvar_t		*vid_gamma;
cvar_t		*vid_fullscreen;

// Global variables used internally by this module
viddef_t	viddef;				// global video state; used by other modules
qboolean	reflib_active = 0;

keydest_t _oldKeyDest = key_game;

/** MOUSE *****************************************************************/

typedef struct in_state {
	vec_t *viewangles;
	int *in_strafe_state;
} in_state_t;

in_state_t in_state;

void Real_IN_Init (void);
void MouseMoveImplementation(usercmd_t *cmd, int mx, int my);

/*
==========================================================================
DLL GLUE
==========================================================================
*/

#define	MAXPRINTMSG	4096
void VID_Printf (int print_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	
	va_start (argptr,fmt);
	vsnprintf (msg,MAXPRINTMSG,fmt,argptr);
	va_end (argptr);

	if (print_level == PRINT_ALL)
		Com_Printf ("%s", msg);
	else
		Com_DPrintf ("%s", msg);
}

void VID_Error (int err_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	
	va_start (argptr,fmt);
	vsprintf (msg,fmt,argptr);
	va_end (argptr);

	Com_Error (err_level,"%s", msg);
}

/*
============
VID_Restart_f
============
*/
void VID_Restart_f (void)
{
	// Just reinit the renderer
	if ( reflib_active )
	{
		re.Shutdown();
		re.Init( 0, 0 );
	}
}

/*
** VID_GetModeInfo
** Always returns 640x480 (mode 3)
*/
qboolean VID_GetModeInfo( int *width, int *height, int mode )
{
	*width  = 640;
	*height = 480;
	return true;
}

/*
** VID_NewWindow
*/
void VID_NewWindow ( int width, int height)
{
	viddef.width  = width;
	viddef.height = height;
}

void VID_FreeReflib (void)
{
	memset (&re, 0, sizeof(re));
	reflib_active  = false;
}

/*
==============
VID_LoadRefresh
==============
*/
qboolean VID_LoadRefresh( void )
{
	refimport_t	ri;
	GetRefAPI_t	getRefAPI;
	
	if ( reflib_active )
	{
		re.Shutdown();
		VID_FreeReflib ();
	}

	Com_Printf( "------- Loading OpenGL Renderer -------\n" );

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Con_Printf = VID_Printf;
	ri.Sys_Error = VID_Error;
	ri.FS_LoadFile = FS_LoadFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_Gamedir = FS_Gamedir;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.Vid_GetModeInfo = VID_GetModeInfo;
	ri.Vid_MenuInit = VID_MenuInit;
	ri.Vid_NewWindow = VID_NewWindow;

	extern refexport_t GetRefAPI (refimport_t);
	getRefAPI = GetRefAPI;

	re = getRefAPI( ri );

	if (re.api_version != API_VERSION)
	{
		VID_FreeReflib ();
		Com_Error (ERR_FATAL, "OpenGL renderer has incompatible api_version");
	}

	/* Init IN (Mouse) */
	in_state.viewangles = cl.viewangles;
	in_state.in_strafe_state = &in_strafe.state;

	Real_IN_Init();

	if ( re.Init( 0, 0 ) == -1 )
	{
		re.Shutdown();
		VID_FreeReflib ();
		Com_Error (ERR_FATAL, "Couldn't initialize OpenGL renderer");
		return false;
	}

	Com_Printf( "------------------------------------\n");
	reflib_active = true;
	return true;
}

/*
============
VID_CheckChanges
============
*/
void VID_CheckChanges (void)
{
	// Simplified - just handle fullscreen changes
	if ( vid_fullscreen->modified )
	{
		vid_fullscreen->modified = false;
		if ( reflib_active )
		{
			// The renderer will handle the mode change internally
			VID_Restart_f();
		}
	}
}

/*
============
VID_Init
============
*/
void VID_Init (void)
{
	/* Create the video variables */
	vid_fullscreen = Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_gamma = Cvar_Get( "vid_gamma", "1", CVAR_ARCHIVE );

	/* Add some console commands that we want to handle */
	Cmd_AddCommand ("vid_restart", VID_Restart_f);
		
	/* Start the graphics mode */
	VID_LoadRefresh();
}

/*
============
VID_Shutdown
============
*/
void VID_Shutdown (void)
{
	if ( reflib_active )
	{
		re.Shutdown ();
		VID_FreeReflib ();
	}
}

/*****************************************************************************/
/* INPUT                                                                     */
/*****************************************************************************/

cvar_t	*in_joystick = NULL;
cvar_t	*in_mouse = NULL;
cvar_t	*m_filter = NULL;

void IN_Init (void)
{
	in_mouse = Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
	in_joystick	= Cvar_Get ("in_joystick", "0", CVAR_ARCHIVE);
	m_filter = Cvar_Get ("m_filter", "0", 0);
}

void Real_IN_Init (void)
{
	// Empty - we handle mouse directly
}

void IN_Shutdown (void)
{
	// Empty
}

void IN_Commands (void)
{
	// Empty
}

void IN_Move (usercmd_t *cmd)
{
	int mx = 0;
	int my = 0;
	QG_GetMouseDiff(&mx, &my);
	if (cls.key_dest == key_game)
	{
		MouseMoveImplementation(cmd, mx, my);
	}
}

void IN_Frame (void)
{
	if (cls.key_dest != _oldKeyDest)
	{
		if (cls.key_dest == key_game)
		{
			QG_CaptureMouse();
		}
		else
		{
			QG_ReleaseMouse();
		}
		_oldKeyDest = cls.key_dest;	
	}
}

void IN_Activate (qboolean active)
{
	// Empty
}

int	old_mouse_x, old_mouse_y;

extern cvar_t	*m_side;
extern	cvar_t	*m_pitch;
extern	cvar_t	*m_yaw;
extern	cvar_t	*m_forward;
extern cvar_t	*lookstrafe;
extern cvar_t	*freelook;
extern cvar_t	*sensitivity;

void MouseMoveImplementation(usercmd_t *cmd, int mx, int my)
{
	if (0 == mx && 0 == my)
	{
		return;
	}

	int mouse_x = 0;
	int mouse_y = 0;
	if (m_filter->value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	}
	else
	{
		mouse_x = mx;
		mouse_y = my;
	}

	old_mouse_x = mx;
	old_mouse_y = my;

	mouse_x *= sensitivity->value;
	mouse_y *= sensitivity->value;

	// add mouse X/Y movement to cmd
	if ( (*in_state.in_strafe_state & 1) || (lookstrafe->value ))
		cmd->sidemove += m_side->value * mouse_x;
	else
		in_state.viewangles[YAW] -= m_yaw->value * mouse_x;

	if ( (freelook->value) && !(*in_state.in_strafe_state & 1))
	{
		in_state.viewangles[PITCH] += m_pitch->value * mouse_y;
	}
	else
	{
		cmd->forwardmove -= m_forward->value * mouse_y;
	}
}