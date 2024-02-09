/*
  SDL_mixer:  An audio mixer library based on the SDL library
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* This file supports playing GBA minigsf files with libGSF */

#include "SDL_loadso.h"

#include "music_gsf.h"
#include "utils.h"

#include <gsf.h>

typedef struct {
    int loaded;
    void *handle;

    GsfError (*gsf_load_file)(GsfEmu *emu, const char *filename);
    int (*gsf_num_channels)(GsfEmu *emu);
    GsfError (*gsf_new)(GsfEmu **out, int sample_rate, int flags);
    bool (*gsf_ended)(const GsfEmu *emu);
    int (*gsf_sample_rate)(GsfEmu *emu);
    GsfError (*gsf_get_tags)(const GsfEmu *emu, GsfTags **out);
    void (*gsf_free_tags)(GsfTags *tags);
    long (*gsf_default_length)(const GsfEmu *emu);
    void (*gsf_seek)(GsfEmu *emu, long millis);
    long (*gsf_tell)(const GsfEmu *emu);
    void (*gsf_play)(GsfEmu *emu, short *out, long size);
    void (*gsf_delete)(GsfEmu *emu);
} gsf_loader;

static gsf_loader gsf;

static int GSF_Load(void)
{
    if (gsf.loaded == 0) {
        FUNCTION_LOADER(gsf_load_file, GsfEmu *emu, GsfError (*)(const char *filename))
        FUNCTION_LOADER(gsf_num_channels, int (*)(GsfEmu *emu);
        FUNCTION_LOADER(gsf_new, GsfError (*)(GsfEmu **out, int sample_rate, int flags);
        FUNCTION_LOADER(gsf_ended, bool (*)(const GsfEmu *emu);
        FUNCTION_LOADER(gsf_sample_rate, int (*)(GsfEmu *emu);
        FUNCTION_LOADER(gsf_get_tags, GsfError (*)(const GsfEmu *emu, GsfTags **out);
        FUNCTION_LOADER(gsf_free_tags, void (*)(GsfTags *tags);
        FUNCTION_LOADER(gsf_default_length, long (*)(const GsfEmu *emu);
        FUNCTION_LOADER(gsf_seek, void (*)(GsfEmu *emu, long millis);
        FUNCTION_LOADER(gsf_tell, long (*)(const GsfEmu *emu);
        FUNCTION_LOADER(gsf_play, void (*)(GsfEmu *emu, short *out, long size);
        FUNCTION_LOADER(gsf_delete, void (*)(GsfEmu *emu);
    }
    ++gsf.loaded;

    return 0;
}

static void GSF_Unload(void)
{
    if (gsf.loaded == 0) {
        return;
    }
    if (gsf.loaded == 1) {
    }
    --gsf.loaded;
}

/* This file supports Game Music Emulator music streams */
typedef struct
{
    int play_count;
    GsfEmu* gsf_emu;
    SDL_bool has_track_length;
    int echo_disabled;
    int track_length;
    int intro_length;
    int loop_length;
    int volume;
    SDL_AudioStream *stream;
    void *buffer;
    size_t buffer_size;
    Mix_MusicMetaTags tags;
} GSF_Music;

static void GSF_Delete(void *context);

/* Set the volume for a GSF stream */
static void GSF_SetVolume(void *music_p, int volume)
{
    GSF_Music *music = (GSF_Music*)music_p;
    float v = SDL_floorf(((float)(volume) * music->gain) + 0.5f);
    music->volume = (int)v;
}

/* Get the volume for a GSF stream */
static int GSF_GetVolume(void *music_p)
{
    GSF_Music *music = (GSF_Music*)music_p;
    float v = SDL_floorf(((float)(music->volume) / music->gain) + 0.5f);
    return (int)v;
}

static int initialize(GSF_Music *music)
{
    gsf_info_t *mus_info;
    SDL_bool has_loop_length = SDL_TRUE;
    const char *err;

    err = gsf.gsf_get_tags(music->game_emu, &mus_info);
    if (err != 0) {
        Mix_SetError("GSF: %s", err);
        return -1;
    }

    meta_tags_init(&music->tags);
    meta_tags_set(&music->tags, MIX_META_TITLE, mus_info->title);
    meta_tags_set(&music->tags, MIX_META_ARTIST, mus_info->artist);
    meta_tags_set(&music->tags, MIX_META_ALBUM, mus_info->game);
    meta_tags_set(&music->tags, MIX_META_COPYRIGHT, mus_info->copyright);
    gme.gsf_free_tags(mus_info);

    return 0;
}

static GSF_Music *GSF_CreateFromRW(SDL_RWops *src, const char *args)
{
    void *mem = 0;
    size_t size;
    GSF_Music *music;
    GsfError *err;

    if (src == NULL) {
        Mix_SetError("GME: Empty source given");
        return NULL;
    }

    music = (GSF_Music *)SDL_calloc(1, sizeof(GSF_Music));

    music->stream = SDL_NewAudioStream(AUDIO_S16SYS, 2, music_spec.freq,
                                       music_spec.format, music_spec.channels, music_spec.freq);
    if (!music->stream) {
        GSF_Delete(music);
        return NULL;
    }

    music->buffer_size = music_spec.samples * sizeof(Sint16) * gsf_num_channels(music); /*channels*/
    music->buffer = SDL_malloc(music->buffer_size);
    if (!music->buffer) {
        SDL_OutOfMemory();
        GSF_Delete(music);
        return NULL;
    }

    SDL_RWseek(src, 0, RW_SEEK_SET);
    mem = SDL_LoadFile_RW(src, &size, SDL_FALSE);
    if (mem) {
        err = gsf.gsf_new(mem, music_spec.freq, 0);
        SDL_free(mem);
        if (err.code != 0) {
            GSF_Delete(music);
            Mix_SetError("GSF: %s", err);
            return NULL;
        }
    } else {
        SDL_OutOfMemory();
        GSF_Delete(music);
        return NULL;
    }

    setup.track_number = gsf.gsf_num_channels(music->gsf_emu);
    
    short samples[BUF_SIZE];
    err = gsf.gsf_play(music->gsf_emu, samples, BUF_SIZE);
    if (err != 0) {
        GSF_Delete(music);
        Mix_SetError("GSF: %s", err);
        return NULL;
    }

    music->volume = MIX_MAX_VOLUME;

    if (initialize(music) == -1) {
        GSF_Delete(music);
        return NULL;
    }

    return music;
}

/* Load stream from a SDL_RWops object */
static void *GSF_NewRWEx(struct SDL_RWops *src, int freesrc, const char *args)
{
    GSF_Music *gsf_music = GSF_CreateFromRW(src, args);
    if (!gsf_music) {
        return NULL;
    }
    if (freesrc) {
        SDL_RWclose(src);
    }
    return gsf_music;
}

static void *GSF_NewRW(struct SDL_RWops *src, int freesrc)
{
    return GSF_NewRWEx(src, freesrc, "0");
}

/* Start playback of a stream */
static int GSF_Play(void *music_p, int play_count)
{
    GSF_Music *music = (GSF_Music*)music_p;
    if (music) {
        SDL_AudioStreamClear(music->stream);
        music->play_count = play_count;
        gsf.gsf_seek(music->game_emu, 0);
    }
    return 0;
}

static int GSF_GetSome(void *context, void *data, int bytes, SDL_bool *done)
{
    GSF_Music *music = (GSF_Music*)context;
    int filled;
    const char *err = NULL;

    filled = SDL_AudioStreamGet(music->stream, data, bytes);
    if (filled != 0) {
        return filled;
    }

    if (gsf.gsf_ended(music->game_emu)) {
        /* All done */
        *done = SDL_TRUE;
        return 0;
    }

    err = gsf.gsf_play(music->game_emu, (music->buffer_size / 2), (short*)music->buffer);
    if (err != NULL) {
        Mix_SetError("GSF: %s", err);
        return 0;
    }

    if (SDL_AudioStreamPut(music->stream, music->buffer, music->buffer_size) < 0) {
        return -1;
    }
    return 0;
}

/* Play some of a stream previously started with GSF_play() */
static int GSF_PlayAudio(void *music_p, void *data, int bytes)
{
    GSF_Music *music = (GSF_Music*)music_p;
    return music_pcm_getaudio(music_p, data, bytes, music->volume, GSF_GetSome);
}

/* Close the given Game Music Emulators stream */
static void GSF_Delete(void *context)
{
    GSF_Music *music = (GSF_Music*)context;
    if (music) {
        meta_tags_clear(&music->tags);
        if (music->gsf_emu) {
            gsf.gsf_delete(music->game_emu);
            music->game_emu = NULL;
        }
        if (music->stream) {
            SDL_FreeAudioStream(music->stream);
        }
        if (music->buffer) {
            SDL_free(music->buffer);
        }
        SDL_free(music);
    }
}

static const char* GSF_GetMetaTag(void *context, Mix_MusicMetaTag tag_type)
{
    GSF_Music *music = (GSF_Music *)context;
    return meta_tags_get(&music->tags, tag_type);
}

/* Jump (seek) to a given position (time is in seconds) */
static int GSF_Seek(void *music_p, double time)
{
    GSF_Music *music = (GSF_Music*)music_p;
    gsf.gsf_seek(music->gsf_emu, (int)(SDL_floor((time * 1000.0) + 0.5)));
    return 0;
}

static double GSF_Tell(void *music_p)
{
    GSF_Music *music = (GSF_Music*)music_p;
    return (double)(gsf.gsf_tell(music->gsf_emu)) / 1000.0;
}

static double GSF_Duration(void *music_p)
{
    GSF_Music *music = (GSF_Music*)music_p;
    if (music->has_track_length) {
        return (double)(music->track_length) / 1000.0;
    } else {

        return -1.0;
    }
}

static int GSF_StartTrack(void *music_p)
{
    GSF_Music *music = (GSF_Music *)music_p;
    GsfError *err;

    err = gsf.gsf_new(music->game_emu, music_spec.freq, 0);
    if (err != 0) {
        Mix_SetError("GSF: %s", err);
        return -1;
    }

    GSF_Play(music);

    if (initialize(music) == -1) {
        return -1;
    }

    return 0;
}

static int GSF_GetNumTracks(void *music_p)
{
    GSF_Music *music = (GSF_Music *)music_p;
    return gsf.gsf_num_channels(music->gsf_emu);
}


Mix_MusicInterface Mix_MusicInterface_GSF =
{
    "GSF",
    MIX_MUSIC_GSF,
    MUS_GSF,
    SDL_FALSE,
    SDL_FALSE,

    GSF_Load,
    NULL,   /* Open */
    GSF_NewRW,
    GSF_NewRWEx,   /* CreateFromRWex [MIXER-X]*/
    NULL,   /* CreateFromFile */
    NULL,   /* CreateFromFileEx [MIXER-X]*/
    GSF_SetVolume,
    GSF_GetVolume,   /* GetVolume [MIXER-X]*/
    GSF_Play,
    NULL,   /* IsPlaying */
    GSF_PlayAudio,
    NULL,       /* Jump */
    GSF_Seek,   /* Seek */
    GSF_Tell,   /* Tell [MIXER-X]*/
    GSF_Duration,
    NULL,   /* [MIXER-X] */
    NULL,   /* [MIXER-X] */
    NULL,   /* SetSpeed [MIXER-X] */
    NULL,   /* GetSpeed [MIXER-X] */
    NULL,   /* SetPitch [MIXER-X] */
    NULL,   /* GetPitch [MIXER-X] */
    NULL,
    NULL,
    NULL,   /* LoopStart [MIXER-X]*/
    NULL,   /* LoopEnd [MIXER-X]*/
    NULL,   /* LoopLength [MIXER-X]*/
    GSF_GetMetaTag,/* GetMetaTag [MIXER-X]*/
    GSF_GetNumTracks,
    GSF_StartTrack,
    NULL,   /* Pause */
    NULL,   /* Resume */
    NULL,   /* Stop */
    GSF_Delete,
    NULL,   /* Close */
    GSF_Unload
};

#endif /* MUSIC_GME */

