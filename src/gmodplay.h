#ifndef GMODPLAY_H
#define GMODPLAY_H

#include <stddef.h>

#include "bme/SDL/SDL_stdinc.h"
#include "gmod.h"

#define PTMOD_MAX_PREVIEW_CHANNELS PTMOD_CHANNELS

enum
{
	PTMOD_REPLAY_LIBXMP,
	PTMOD_REPLAY_THC_WAVEFORM,
	PTMOD_REPLAY_MODE_COUNT
};

typedef struct
{
	int loaded;
	int active;
	int finished;
	int channels;
	int songLength;
	int patternCount;
	int startDelayFrames;
	int delayFramesRemaining;
	size_t frames;
	size_t frameIndex;
	int orderIndex;
	int pattern;
	int row;
	int speed;
	int bpm;
	unsigned long long mixedSamples;
	int lastPeak;
	int peakSinceReset;
	int usingLibxmp;
	int replayMode;
	int voice3Reserved;
	int voice3Conflict;
	int channelPeriod[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelPosition[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelNote[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelInstrument[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelSample[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelLevel[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelPan[PTMOD_MAX_PREVIEW_CHANNELS];
} PTMOD_PREVIEW_STATS;

typedef struct
{
	int sample;
	int period;
	int effect;
	int param;
	int orderIndex;
	int pattern;
	int row;
} PTMOD_PREVIEW_ROW;

typedef struct
{
	int enabled;
	int masterVolume;
	int startDelayFrames;
	int replayMode;
	int channelVolume[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelMute[PTMOD_MAX_PREVIEW_CHANNELS];
} PTMOD_RUNTIME_SETTINGS;

enum
{
	PTMOD_ROW_FIELD_PERIOD,
	PTMOD_ROW_FIELD_SAMPLE,
	PTMOD_ROW_FIELD_EFFECT,
	PTMOD_ROW_FIELD_PARAM,
	PTMOD_ROW_FIELD_COUNT
};

enum
{
	PTMOD_SAMPLE_FIELD_FINETUNE,
	PTMOD_SAMPLE_FIELD_VOLUME,
	PTMOD_SAMPLE_FIELD_LOOP_START,
	PTMOD_SAMPLE_FIELD_LOOP_LENGTH,
	PTMOD_SAMPLE_FIELD_COUNT
};

void ptmodplay_clear(void);
int ptmodplay_load_current(char *error, size_t errorSize);
void ptmodplay_reload_if_dirty(void);
void ptmodplay_reset(void);
void ptmodplay_start_at(int orderIndex, int row);
void ptmodplay_seek(int orderIndex, int row);
void ptmodplay_sync_to_sid(int hostActive, int orderIndex, int row);
void ptmodplay_set_loop_range(int enabled, int startOrder, int startRow, int endOrder, int endRow);
void ptmodplay_stop(void);
void ptmodplay_prepare_sid_events(unsigned samples, unsigned sampleRate, unsigned frameRate, int hostActive);
void ptmodplay_mix(Sint32 *dest, unsigned samples, unsigned sampleRate, unsigned frameRate, int hostActive);
void ptmodplay_get_stats(PTMOD_PREVIEW_STATS *stats);
int ptmodplay_get_row(int channel, size_t frame, PTMOD_PREVIEW_ROW *row);
int ptmodplay_set_row_value(int channel, size_t frame, int field, int value);
void ptmodplay_get_runtime_settings(PTMOD_RUNTIME_SETTINGS *settings);
void ptmodplay_set_runtime_settings(const PTMOD_RUNTIME_SETTINGS *settings);
void ptmodplay_set_enabled(int enabled);
void ptmodplay_set_replay_mode(int replayMode);
const char *ptmodplay_replay_mode_name(int replayMode);
void ptmodplay_set_master_volume(int volume);
void ptmodplay_set_start_delay(int delayFrames);
void ptmodplay_set_channel_volume(int channel, int volume);
void ptmodplay_set_channel_mute(int channel, int muted);
void ptmodplay_audition_sample(int index);
void ptmodplay_audition_sample_with_loop(int index, unsigned loopStart, unsigned loopLength);
void ptmodplay_stop_audition(void);

#endif
