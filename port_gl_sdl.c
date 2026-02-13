#include <stdio.h>

#include <kos.h>
#include <kos/thread.h>
#include <dc/pvr.h>
#include <arch/timer.h>
#include <malloc.h>


#include "ref_gl/gl_local.h"
#include "client/keys.h"
#include "quake2.h"

static int _width = 640;
static int _height = 480;
static int frame_count = 0;
static int frame_time_total = 0;
static int last_report = 0;
static maple_device_t *kbd_dev = NULL;
static kbd_state_t *kbd_state = NULL;

KOS_INIT_FLAGS(INIT_DEFAULT);
int _old_button_state = 0;
static int _old_start = 0;




/*****************************************************************************/

void InitKeyboard(void) {
    kbd_dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
    if (kbd_dev) {
        printf("Keyboard found at %c%d\n", 'A' + (kbd_dev->port), kbd_dev->unit);
        kbd_state = maple_dev_status(kbd_dev);
    } else {
        printf("No keyboard detected\n");
    }
}

qboolean GLimp_InitGL(void);

static void setupWindow(qboolean fullscreen) { }


int GLimp_SetMode(int *pwidth, int *pheight, int mode, qboolean fullscreen) {
    int width = 0, height = 0;

    ri.Con_Printf(PRINT_ALL, "Initializing OpenGL display\n");
    ri.Con_Printf(PRINT_ALL, "...setting mode %d:", mode);

    if (!ri.Vid_GetModeInfo(&width, &height, mode)) {
        ri.Con_Printf(PRINT_ALL, " invalid mode\n");
        return rserr_invalid_mode;
    }

    ri.Con_Printf(PRINT_ALL, " %d %d\n", width, height);

    _width = width;
    _height = height;
    *pwidth = width;
    *pheight = height;

    setupWindow(fullscreen);
    ri.Vid_NewWindow(width, height);

    return rserr_ok;
}

void GLimp_Shutdown(void) {

}

int GLimp_Init(void *hinstance, void *wndproc) {
    setupWindow(false);

    return true;
}

void GLimp_BeginFrame(float camera_seperation) { }

void GLimp_EndFrame(void) {
    Draw_FlushBatch();
    pvr_scene_finish();

}

void GLimp_AppActivate(qboolean active) { }

void HandleInput(void) {
    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, state)
        int button_state = 0;

        // Quit combo: Start + A + B + X + Y (instant)
        if ((state->buttons & CONT_START) &&
            (state->buttons & CONT_A) &&
            (state->buttons & CONT_B) &&
            (state->buttons & CONT_X) &&
            (state->buttons & CONT_Y))
        {
            ri.Cmd_ExecuteText(EXEC_NOW, "quit");
            break;
        }

        // D-Pad: movement
        Quake2_SendKey(K_UPARROW,    (state->buttons & CONT_DPAD_UP)    ? true : false);
        Quake2_SendKey(K_DOWNARROW,  (state->buttons & CONT_DPAD_DOWN)  ? true : false);
        Quake2_SendKey(K_LEFTARROW,  (state->buttons & CONT_DPAD_LEFT)  ? true : false);
        Quake2_SendKey(K_RIGHTARROW, (state->buttons & CONT_DPAD_RIGHT) ? true : false);

        // A: jump
        Quake2_SendKey(K_SPACE, (state->buttons & CONT_A) ? true : false);

        // B: next weapon
        Quake2_SendKey('/', (state->buttons & CONT_B) ? true : false);

        // X: enter / use
        Quake2_SendKey(K_ENTER, (state->buttons & CONT_X) ? true : false);

        // Y: inventory
        Quake2_SendKey(K_TAB, (state->buttons & CONT_Y) ? true : false);

        // Start: escape — edge detect only (press, not hold)
        int start_now = (state->buttons & CONT_START) ? 1 : 0;
        if (start_now && !_old_start)
            Quake2_SendKey(K_ESCAPE, true);
        if (!start_now && _old_start)
            Quake2_SendKey(K_ESCAPE, false);
        _old_start = start_now;

        // R Trigger: fire
        if (state->rtrig > 64)
            button_state |= (1 << 0);

        // L Trigger: jump (alt)
        if (state->ltrig > 64)
            button_state |= (1 << 1);

        // Edge detect triggers as mouse/key events
        for (int i = 0; i < 2; i++) {
            if ((button_state & (1 << i)) && !(_old_button_state & (1 << i)))
                Quake2_SendKey(i == 0 ? K_MOUSE1 : K_SPACE, true);
            if (!(button_state & (1 << i)) && (_old_button_state & (1 << i)))
                Quake2_SendKey(i == 0 ? K_MOUSE1 : K_SPACE, false);
        }

        _old_button_state = button_state;
    MAPLE_FOREACH_END()
}

void QG_GetMouseDiff(int* dx, int* dy) {
    *dx = 0;
    *dy = 0;

    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, state)
        int jx = state->joyx;
        int jy = state->joyy;

        if (jx > -25 && jx < 25) jx = 0;
        if (jy > -25 && jy < 25) jy = 0;

        *dx = jx / 4;
        *dy = jy / 4;
    MAPLE_FOREACH_END()
}

int QG_Milliseconds(void) {	
    return timer_ms_gettime64();
}

void QG_CaptureMouse(void) { }
void QG_ReleaseMouse(void) { }


int main(int argc, char **argv) {
    pvr_init_params_t params = {
        { PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16 },
        1024 * 1024,
        1,
        0,
        0,
        1,
        0
    };
    pvr_init(&params);

    int time, oldtime, newtime;
    Quake2_Init(argc, argv);



    oldtime = QG_Milliseconds();
    last_report = oldtime;
    
    while (1) {
        HandleInput();
        
        do {
            newtime = QG_Milliseconds();
            time = newtime - oldtime;
        } while (time < 1);

        Quake2_Frame(time);
        
        // Update FPS history

    
        
        oldtime = newtime;
        thd_pass();
    }

    return 0;
}
