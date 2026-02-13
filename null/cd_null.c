// cd_dreamcast.c - DCA audio for Dreamcast
// Music: PCM16 streaming, Sound effects: ADPCM cached
#include "../client/client.h"
#include <kos.h>
#include <dc/sound/stream.h>
#include <dc/sound/sfxmgr.h>

#define DCAUDIO_IMPLEMENTATION
#include "dca_file.h"

#define AICA_FMT_PCM16  16
#define AICA_FMT_PCM8   8
#define AICA_FMT_ADPCM  4

static FILE *music_file = NULL;
static qboolean playing = false;
static qboolean paused = false;
static qboolean looping = false;
static qboolean initialized = false;
static int current_track = 0;
static snd_stream_hnd_t stream_hnd = SND_STREAM_INVALID;

static fDcAudioHeader music_header;
static long music_data_start = 0;
static int music_channels = 1;
static int music_sample_rate = 44100;

static uint8_t stream_buffer[65536] __attribute__((aligned(32)));

cvar_t *cd_volume;
cvar_t *cd_nocd;

#define MAX_CACHED_SOUNDS 44
static struct {
    char name[64];
    sfxhnd_t handle;
    int channel;
    uint32_t last_used;
} sound_cache[MAX_CACHED_SOUNDS];
static uint32_t cache_timer = 0;
static int cache_initialized = 0;

static uint8_t sfx_buffer[65536] __attribute__((aligned(32)));

void S_InitADPCMCache(void)
{
    int i, actual_max = 0;
    
    for (i = 0; i < MAX_CACHED_SOUNDS; i++) {
        sound_cache[i].handle = SFXHND_INVALID;
        sound_cache[i].channel = i + 2;
        sound_cache[i].name[0] = '\0';
        
        if (sound_cache[i].channel >= 64) {
            actual_max = i;
            break;
        }
        actual_max = i + 1;
    }
    
    for (; i < MAX_CACHED_SOUNDS; i++)
        sound_cache[i].channel = -1;
    
    cache_initialized = 1;
}

void S_PlayADPCM(const char *name)
{
    char filename[256];
    FILE *f;
    fDcAudioHeader hdr;
    size_t data_size;
    int i, oldest_slot;
    uint32_t oldest_time;
    int format, sample_rate;
    sfxhnd_t new_handle;
    char *ext;
    
    if (!cache_initialized)
        S_InitADPCMCache();
    
    cache_timer++;
    
    for (i = 0; i < MAX_CACHED_SOUNDS; i++) {
        if (sound_cache[i].handle != SFXHND_INVALID && 
            strcmp(sound_cache[i].name, name) == 0) {
            sound_cache[i].last_used = cache_timer;
            snd_sfx_stop(sound_cache[i].channel);
            snd_sfx_play_chn(sound_cache[i].channel, sound_cache[i].handle, 200, 128); // Fuck with later. 
            return;
        }
    }
    
    sprintf(filename, "/cd/baseq2/sound/%s", name);
    ext = strstr(filename, ".wav");
    if (ext) strcpy(ext, ".dca");
    
    f = fopen(filename, "rb");
    if (!f)
        return;
    
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return;
    }
    
    if (!fDaFourccMatches(&hdr)) {
        fclose(f);
        return;
    }
    
    data_size = fDaGetDataSize(&hdr);
    sample_rate = (int)fDaCalcSampleRateHz(&hdr);
    
    switch (fDaGetSampleFormat(&hdr)) {
        case DCA_FLAG_FORMAT_ADPCM: format = AICA_FMT_ADPCM; break;
        case DCA_FLAG_FORMAT_PCM16: format = AICA_FMT_PCM16; break;
        case DCA_FLAG_FORMAT_PCM8:  format = AICA_FMT_PCM8;  break;
        default:
            fclose(f);
            return;
    }
    
    if (data_size > sizeof(sfx_buffer)) {
        fclose(f);
        return;
    }
    
    if (fread(sfx_buffer, data_size, 1, f) != 1) {
        fclose(f);
        return;
    }
    fclose(f);
    
    new_handle = snd_sfx_load_raw_buf((char*)sfx_buffer, data_size, 
                                       sample_rate, format, 
                                       fDaGetChannelCount(&hdr));
    if (new_handle == SFXHND_INVALID)
        return;
    
    oldest_slot = 0;
    oldest_time = cache_timer;
    
    for (i = 0; i < MAX_CACHED_SOUNDS; i++) {
        if (sound_cache[i].channel == -1) continue;
        
        if (sound_cache[i].handle == SFXHND_INVALID) {
            oldest_slot = i;
            break;
        }
        if (sound_cache[i].last_used < oldest_time) {
            oldest_time = sound_cache[i].last_used;
            oldest_slot = i;
        }
    }
    
    if (sound_cache[oldest_slot].handle != SFXHND_INVALID) {
        snd_sfx_stop(sound_cache[oldest_slot].channel);
        snd_sfx_unload(sound_cache[oldest_slot].handle);
    }
    
    strncpy(sound_cache[oldest_slot].name, name, sizeof(sound_cache[oldest_slot].name) - 1);
    sound_cache[oldest_slot].handle = new_handle;
    sound_cache[oldest_slot].last_used = cache_timer;
    
    snd_sfx_stop(sound_cache[oldest_slot].channel);
    snd_sfx_play_chn(sound_cache[oldest_slot].channel, new_handle, 200, 128);
}

static void *stream_callback(snd_stream_hnd_t hnd, int req, int *done)
{
    size_t read;
    
    (void)hnd;
    
    if (!playing || paused || !music_file) {
        *done = 0;
        return stream_buffer;
    }
    
    read = fread(stream_buffer, 1, req, music_file);
    
    if (read == 0) {
        if (looping) {
            fseek(music_file, music_data_start, SEEK_SET);
            read = fread(stream_buffer, 1, req, music_file);
        }
        
        if (read == 0) {
            *done = 0;
            return stream_buffer;
        }
    }
    
    *done = read;
    return stream_buffer;
}

void CDAudio_Play(int track, qboolean loop)
{
    char filename[256];
    int stereo;
    
    if (!initialized)
        return;
    
    CDAudio_Stop();
    
    sprintf(filename, "/cd/baseq2/music/track%02d.dca", track);
    
    music_file = fopen(filename, "rb");
    if (!music_file)
        return;
    
    if (fread(&music_header, sizeof(music_header), 1, music_file) != 1) {
        fclose(music_file);
        music_file = NULL;
        return;
    }
    
    if (!fDaFourccMatches(&music_header)) {
        fclose(music_file);
        music_file = NULL;
        return;
    }
    
    if (fDaGetSampleFormat(&music_header) != DCA_FLAG_FORMAT_PCM16) {
        fclose(music_file);
        music_file = NULL;
        return;
    }
    
    music_channels = fDaGetChannelCount(&music_header);
    music_sample_rate = (int)fDaCalcSampleRateHz(&music_header);
    music_data_start = ftell(music_file);
    
    stream_hnd = snd_stream_alloc(stream_callback, sizeof(stream_buffer));
    if (stream_hnd == SND_STREAM_INVALID) {
        fclose(music_file);
        music_file = NULL;
        return;
    }
    
    stereo = (music_channels == 2) ? 1 : 0;
    snd_stream_start(stream_hnd, music_sample_rate, stereo);
    snd_stream_volume(stream_hnd, 255);
    
    current_track = track;
    playing = true;
    paused = false;
    looping = loop;
}

void CDAudio_Update(void)
{
    if (!playing || paused)
        return;
    
    if (stream_hnd != SND_STREAM_INVALID)
        snd_stream_poll(stream_hnd);
}

void CDAudio_Stop(void)
{
    if (!initialized)
        return;
    
    playing = false;
    paused = false;
    
    if (stream_hnd != SND_STREAM_INVALID) {
        snd_stream_stop(stream_hnd);
        snd_stream_destroy(stream_hnd);
        stream_hnd = SND_STREAM_INVALID;
    }
    
    if (music_file) {
        fclose(music_file);
        music_file = NULL;
    }
}

void CDAudio_Pause(void)
{
    if (!initialized || !playing)
        return;
    paused = true;
}

void CDAudio_Resume(void)
{
    if (!initialized || !playing)
        return;
    paused = false;
}

int CDAudio_Init(void)
{
    cd_volume = Cvar_Get("cd_volume", "1", CVAR_ARCHIVE);
    cd_nocd = Cvar_Get("cd_nocd", "0", CVAR_ARCHIVE);
    
    if (cd_nocd->value)
        return -1;
    
    snd_stream_init();
    initialized = true;
    return 0;
}

void CDAudio_Shutdown(void)
{
    if (!initialized)
        return;
    
    CDAudio_Stop();
    snd_stream_shutdown();
    initialized = false;
}

void CDAudio_Activate(qboolean active)
{
    if (active)
        CDAudio_Resume();
    else
        CDAudio_Pause();
}

#include "../client/snd_loc.h"

static int snd_inited = 0;
static int fake_samplepos = 0;

qboolean SNDDMA_Init(void)
{
    memset(&dma, 0, sizeof(dma));
    dma.speed = 22050;
    dma.samplebits = 16;
    dma.channels = 2;
    dma.samples = 2048;
    dma.buffer = NULL;
    dma.samplepos = 0;
    dma.submission_chunk = 1;

    fake_samplepos = 0;
    snd_inited = 1;
    return true;
}

int SNDDMA_GetDMAPos(void)
{
    fake_samplepos += 256;
    if (fake_samplepos >= dma.samples)
        fake_samplepos = 0;
    return fake_samplepos;
}

void SNDDMA_Shutdown(void)
{
    if (snd_inited)
        snd_inited = 0;
    fake_samplepos = 0;
}

void SNDDMA_Submit(void) {}
void SNDDMA_BeginPainting(void) {}