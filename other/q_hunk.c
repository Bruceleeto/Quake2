#include <stdio.h>
#include <ctype.h>
#include "../qcommon/qcommon.h"
#include <malloc.h>

//===============================================================================

byte *membase;
int maxhunksize;
int curhunksize;
int hunk_count;
int hunk_peak;

/* Track ALL hunk allocations so Hunk_FreeAll can actually free them */
#define MAX_HUNK_ALLOCS 256
static void *hunk_allocs[MAX_HUNK_ALLOCS];
static int num_hunk_allocs = 0;

void Hunk_Stats_f(void)
{
    Com_Printf("\nHunk Statistics:\n");
    Com_Printf("----------------\n");
    Com_Printf("Active hunks: %d\n", num_hunk_allocs);
    Com_Printf("Current Size: %d bytes (%.2f MB)\n", curhunksize, curhunksize/(1024.0f*1024.0f));
    Com_Printf("Peak Usage:   %d bytes (%.2f MB)\n", hunk_peak, hunk_peak/(1024.0f*1024.0f));
    Com_Printf("Maximum Size: %d bytes (%.2f MB)\n", maxhunksize, maxhunksize/(1024.0f*1024.0f));
    Com_Printf("\n");
}

void *Hunk_Begin(int maxsize)
{
    Com_Printf("=== HUNK_BEGIN ===\n");
    Com_Printf("Requested size: %.1fMB (%d bytes)\n", maxsize/(1024.0f*1024.0f), maxsize);
    
    maxhunksize = maxsize;
    curhunksize = 0;
    hunk_peak = 0;

    membase = malloc(maxsize);
    
    if (!membase) {
        Sys_Error("Hunk_Begin: Failed to allocate %d bytes", maxsize);
        return NULL;
    }

    memset(membase, 0, maxsize);

    /* Track this allocation */
    if (num_hunk_allocs < MAX_HUNK_ALLOCS) {
        hunk_allocs[num_hunk_allocs++] = membase;
    } else {
        Com_Printf("WARNING: hunk_allocs full, this hunk won't be tracked!\n");
    }

    Com_Printf("Hunk allocated at %p (tracked: %d/%d)\n", 
               membase, num_hunk_allocs, MAX_HUNK_ALLOCS);
    
    return membase;
}

void *Hunk_Alloc(int size)
{
    byte *buf;

    size = (size+3)&~3;

    if (curhunksize + size > maxhunksize) {
        Com_Printf("HUNK OVERFLOW! Requested %d bytes, only %d remaining\n", 
                   size, maxhunksize - curhunksize);
        malloc_stats();
        Sys_Error("Hunk_Alloc: overflow - requested %d bytes, only %d remaining", 
                 size, maxhunksize - curhunksize);
    }

    buf = membase + curhunksize;
    curhunksize += size;

    if (curhunksize > hunk_peak)
        hunk_peak = curhunksize;

    return buf;
}

int Hunk_End(void)
{
    Com_Printf("=== HUNK_END ===\n");
    Com_Printf("Used: %.1fMB (%d bytes) of %.1fMB allocated\n", 
               curhunksize/(1024.0f*1024.0f), curhunksize, maxhunksize/(1024.0f*1024.0f));
    Com_Printf("Efficiency: %.1f%% (%.1fMB wasted)\n", 
               maxhunksize > 0 ? (curhunksize * 100.0f) / maxhunksize : 0,
               (maxhunksize - curhunksize) / (1024.0f*1024.0f));

    hunk_count++;
    return curhunksize;
}

void Hunk_Free(void *base)
{
    int i;
    if (!base) return;

    /* Remove from tracking list and free */
    for (i = 0; i < num_hunk_allocs; i++) {
        if (hunk_allocs[i] == base) {
            free(base);
            /* Swap with last entry */
            hunk_allocs[i] = hunk_allocs[num_hunk_allocs - 1];
            hunk_allocs[num_hunk_allocs - 1] = NULL;
            num_hunk_allocs--;
            
            if (base == membase) {
                membase = NULL;
                curhunksize = 0;
                maxhunksize = 0;
            }
            Com_Printf("Hunk freed at %p (%d remaining)\n", base, num_hunk_allocs);
            return;
        }
    }
    Com_Printf("WARNING: Hunk_Free: %p not in tracking list!\n", base);
}

void Hunk_FreeAll(void)
{
    int i;
    Com_Printf("=== HUNK_FREE_ALL ===\n");
    Com_Printf("Freeing %d tracked hunks\n", num_hunk_allocs);
    
    for (i = 0; i < num_hunk_allocs; i++) {
        if (hunk_allocs[i]) {
            free(hunk_allocs[i]);
            hunk_allocs[i] = NULL;
        }
    }
    
    num_hunk_allocs = 0;
    membase = NULL;
    curhunksize = 0;
    maxhunksize = 0;
    hunk_count = 0;
    hunk_peak = 0;
    
    Com_Printf("All hunks freed, state reset\n");
}

void Hunk_Reset(void)
{
    if (!membase) {
        Com_Printf("Hunk_Reset: No active hunk to reset\n");
        return;
    }
    
    Com_Printf("=== HUNK_RESET ===\n");
    Com_Printf("Resetting hunk usage from %d to 0 bytes\n", curhunksize);
    curhunksize = 0;
    memset(membase, 0, maxhunksize);
    Com_Printf("Hunk reset complete\n");
}

void *Hunk_GetBase(void)
{
    return membase;
}

void Hunk_GetInfo(int *current_size, int *max_size, int *peak_size)
{
    if (current_size) *current_size = curhunksize;
    if (max_size) *max_size = maxhunksize;
    if (peak_size) *peak_size = hunk_peak;
}