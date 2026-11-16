/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#pragma once
struct MIX_Audio;
struct MIX_Track;
#define SFX_INITIALIZED 1
#define MUSIC_INITIALIZED 2
int sound_init(int argc, char **argv);
void sound_uninit();
void print_sound_options();
class sound_effect
{
public:
    sound_effect(const char *filename);
    ~sound_effect();
    void play(int volume = 127, int pitch = 128, int panpot = 128);
private:
    MIX_Audio *audio = nullptr;
};
class song
{
public:
    const char *name() { return Name; }
    song(const char *filename);
    ~song();
    void play(unsigned char volume = 127);
    void stop(long fadeout_time = 0);
    int playing();
    void set_volume(int volume);
private:
    char *Name = nullptr;
    MIX_Audio *audio = nullptr;
    MIX_Track *track = nullptr;
};
