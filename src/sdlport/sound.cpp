// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 2001 Anthony Kruize <trandor@labyrinth.net.au>
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "sound.h"
#include "hmi.h"
#include "specs.h"
#include "setup.h"

extern flags_struct flags;
static MIX_Mixer *mixer = nullptr;
static bool midi_available = false;
static std::array<MIX_Track *, 50> effects = {};

int sound_init(int, char **)
{
    if (flags.nosound) return 0;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
        return 0;
    }
    if (!MIX_Init()) {
        fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 44100};
    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!mixer) {
        fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    for (auto &track : effects) {
        track = MIX_CreateTrack(mixer);
        if (!track) { sound_uninit(); return 0; }
    }
    midi_available = false;
    for (int i = 0; i < MIX_GetNumAudioDecoders(); ++i) {
        const char *decoder = MIX_GetAudioDecoder(i);
        if (decoder && (strstr(decoder, "MIDI") || strstr(decoder, "TIMIDITY") || strstr(decoder, "FLUIDSYNTH"))) midi_available = true;
    }
    if (!midi_available)
        fprintf(stderr, "Music disabled: no MIDI instrument setup. On Ubuntu install freepats, or set TIMIDITY_CFG to a valid patch configuration. Sound effects remain enabled.\n");
    printf("Audio: SDL3_mixer, 50 effect tracks\n");
    return SFX_INITIALIZED | (midi_available ? MUSIC_INITIALIZED : 0);
}
void sound_uninit()
{
    if (!mixer) return;
    MIX_DestroyMixer(mixer);
    mixer = nullptr; midi_available = false;
    effects.fill(nullptr);
    MIX_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

sound_effect::sound_effect(const char *filename)
{
    if (!mixer) return;
    jFILE file(filename, "rb");
    if (file.open_failure()) return;
    const long size = file.file_size();
    if (size <= 0) return;
    std::vector<Uint8> bytes(size);
    if (file.read(bytes.data(), size) != size) return;
    // Predecode while the temporary memory stream is alive.
    audio = MIX_LoadAudio_IO(mixer, SDL_IOFromConstMem(bytes.data(), bytes.size()), true, true);
    if (!audio) fprintf(stderr, "Cannot load sound %s: %s\n", filename, SDL_GetError());
}
sound_effect::~sound_effect()
{
    if (!mixer || !audio) return;
    for (auto *track : effects) if (MIX_GetTrackAudio(track) == audio) {
        MIX_StopTrack(track, 0);
        MIX_SetTrackAudio(track, nullptr);
    }
    MIX_DestroyAudio(audio);
}
void sound_effect::play(int volume, int pitch, int panpot)
{
    if (!mixer || !audio) return;
    for (auto *track : effects) if (!MIX_TrackPlaying(track)) {
        MIX_SetTrackAudio(track, audio);
        MIX_SetTrackGain(track, std::clamp(volume, 0, 127) / 127.0f);
        MIX_SetTrackFrequencyRatio(track, std::clamp(pitch, 1, 255) / 128.0f);
        float left = flags.mono ? 0.5f : std::clamp(panpot, 0, 255) / 255.0f;
        MIX_StereoGains gains = {left, 1.0f - left};
        MIX_SetTrackStereo(track, &gains);
        MIX_PlayTrack(track, 0);
        return;
    }
}

song::song(const char *filename)
{
    Name = strdup(filename);
    if (!mixer || !midi_available) return;
    const std::string path = std::string(get_filename_prefix()) + filename;
    uint32_t size = 0;
    uint8_t *bytes = load_hmi(path.c_str(), size);
    if (!bytes) return;
    audio = MIX_LoadAudio_IO(mixer, SDL_IOFromConstMem(bytes, size), true, true);
    free(bytes);
    if (!audio) {
        fprintf(stderr, "Cannot decode music %s: %s\n", filename, SDL_GetError());
        return;
    }
    track = MIX_CreateTrack(mixer);
    if (track) MIX_SetTrackAudio(track, audio);
}
song::~song()
{
    if (mixer) {
        MIX_DestroyTrack(track);
        MIX_DestroyAudio(audio);
    }
    free(Name);
}
void song::play(unsigned char volume)
{
    if (!mixer || !track) return;
    set_volume(volume);
    MIX_PlayTrack(track, 0);
}
void song::stop(long fadeout_time)
{
    if (mixer && track) MIX_StopTrack(track, MIX_TrackMSToFrames(track, std::max(0L, fadeout_time)));
}
int song::playing() { return mixer && track && MIX_TrackPlaying(track); }
void song::set_volume(int volume)
{
    if (mixer && track) MIX_SetTrackGain(track, std::clamp(volume, 0, 127) / 127.0f);
}
