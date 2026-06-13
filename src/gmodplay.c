//
// Host-side ProTracker MOD preview mixer.
//

#include "gmodplay.h"
#include "gsid.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GTULTRA_LIBXMP
#include <xmp.h>
#endif

#define PTMOD_THC_FRAME_SAMPLES 156
#define PTMOD_THC_BASE_PERIOD 453
#define PTMOD_THC_MIN_PERIOD 113
#define PTMOD_THC_MAX_PERIOD 856
#define PTMOD_THC_WAVE_CENTER 0x80
#define PTMOD_THC_WAVE_GAIN_DIVISOR 4

typedef struct
{
	int active;
	int sampleIndex;
	int period;
	int basePeriod;
	int volume;
	int effect;
	int param;
	unsigned positionFixed;
	unsigned stepFixed;
} PTMOD_THC_CHANNEL;

typedef struct
{
	int initialized;
	int tick;
	int speed;
	int bpm;
	int orderIndex;
	int pattern;
	int row;
	int pendingJump;
	int pendingOrder;
	int pendingRow;
	int frameSampleIndex;
	double eventCursor;
	unsigned char frame[PTMOD_THC_FRAME_SAMPLES];
	PTMOD_THC_CHANNEL channel[PTMOD_CHANNELS];
} PTMOD_THC_STATE;

typedef struct
{
	int loaded;
	int active;
	int finished;
	int runtimeEnabled;
	int masterVolume;
	int channelVolume[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelMute[PTMOD_MAX_PREVIEW_CHANNELS];
	int startDelayFrames;
	int delayFramesRemaining;
	double delayFrameCursor;
	size_t frames;
	size_t frameIndex;
	int orderIndex;
	int pattern;
	int row;
	int pendingOrderIndex;
	int pendingRow;
	int pendingSeek;
	int speed;
	int bpm;
	unsigned long long mixedSamples;
	int lastPeak;
	int peakSinceReset;
	int usingLibxmp;
	int replayMode;
	int voice3Conflict;
	int channelPeriod[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelPosition[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelNote[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelInstrument[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelSample[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelLevel[PTMOD_MAX_PREVIEW_CHANNELS];
	int channelPan[PTMOD_MAX_PREVIEW_CHANNELS];
	unsigned sampleRate;
	short *mixBuffer;
	unsigned mixBufferSamples;
	unsigned char *moduleImage;
	size_t moduleImageSize;
	int syncHostActive;
	int syncOrderIndex;
	int syncRow;
	int hasSyncPosition;
	int loopEnabled;
	int loopStartOrder;
	int loopStartRow;
	int loopEndOrder;
	int loopEndRow;
	PTMOD_THC_STATE thc;
#ifdef GTULTRA_LIBXMP
	xmp_context context;
	int playerStarted;
#endif
} PTMOD_PREVIEW_STATE;

typedef struct
{
	int active;
	int sampleIndex;
	unsigned long long positionFixed;
	int loopEnabled;
	unsigned loopStart;
	unsigned loopLength;
} PTMOD_AUDITION_STATE;

static PTMOD_PREVIEW_STATE preview;
static PTMOD_AUDITION_STATE audition;

static const int thcPeriodTable[] = {
	856,808,762,720,678,640,604,570,538,508,480,453,
	428,404,381,360,339,320,302,285,269,254,240,226,
	214,202,190,180,170,160,151,143,135,127,120,113
};

static int clamp_replay_mode(int replayMode)
{
	if (replayMode < 0 || replayMode >= PTMOD_REPLAY_MODE_COUNT)
		return PTMOD_REPLAY_LIBXMP;
	return replayMode;
}

static int clamp_percent(int value)
{
	if (value < 0)
		return 0;
	if (value > 200)
		return 200;
	return value;
}

static int clamp_delay(int value)
{
	if (value < 0)
		return 0;
	if (value > 127)
		return 127;
	return value;
}

static void init_runtime_settings(void)
{
	int i;

	preview.runtimeEnabled = 1;
	preview.masterVolume = 100;
	preview.startDelayFrames = 6;
	preview.replayMode = PTMOD_REPLAY_LIBXMP;
	for (i = 0; i < PTMOD_MAX_PREVIEW_CHANNELS; i++)
	{
		preview.channelVolume[i] = 100;
		preview.channelMute[i] = 0;
	}
}

static void set_message(char *dest, size_t destSize, const char *fmt, ...)
{
	va_list args;

	if (!dest || destSize == 0)
		return;

	va_start(args, fmt);
	vsnprintf(dest, destSize, fmt, args);
	va_end(args);
	dest[destSize - 1] = 0;
}

static Sint32 clamp_s16(long value)
{
	if (value > 32767)
		return 32767;
	if (value < -32768)
		return -32768;
	return (Sint32)value;
}

static void clamp_play_position(int *orderIndex, int *row)
{
	int maxOrder = ptmodState.valid && ptmodState.songLength > 0 ? ptmodState.songLength - 1 : 0;

	if (!orderIndex || !row)
		return;
	if (*orderIndex < 0)
		*orderIndex = 0;
	if (*orderIndex > maxOrder)
		*orderIndex = maxOrder;
	if (*row < 0)
		*row = 0;
	if (*row >= PTMOD_ROWS)
		*row = PTMOD_ROWS - 1;
}

static void set_preview_position_fields(int orderIndex, int row)
{
	int pattern;

	clamp_play_position(&orderIndex, &row);
	preview.orderIndex = orderIndex;
	preview.row = row;
	pattern = ptmod_order_pattern(orderIndex);
	preview.pattern = pattern < 0 ? 0 : pattern;
	preview.frameIndex = (size_t)orderIndex * PTMOD_ROWS + (size_t)row;
}

static void thc_set_position(int orderIndex, int row);
const char *ptmodplay_replay_mode_name(int replayMode);

static void queue_preview_seek(int orderIndex, int row)
{
	clamp_play_position(&orderIndex, &row);
	preview.pendingOrderIndex = orderIndex;
	preview.pendingRow = row;
	preview.pendingSeek = 1;
	set_preview_position_fields(orderIndex, row);
	if (preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM)
		thc_set_position(orderIndex, row);
}

static int position_after(int orderIndex, int row, int endOrder, int endRow)
{
	if (orderIndex > endOrder)
		return 1;
	if (orderIndex == endOrder && row > endRow)
		return 1;
	return 0;
}

static int preview_loop_should_wrap(int orderIndex, int row)
{
	return preview.loopEnabled &&
		position_after(orderIndex, row, preview.loopEndOrder, preview.loopEndRow);
}

#ifdef GTULTRA_LIBXMP
static void apply_pending_seek(void)
{
	if (!preview.context || !preview.playerStarted || !preview.pendingSeek)
		return;

	xmp_set_position(preview.context, preview.pendingOrderIndex);
	xmp_set_row(preview.context, preview.pendingRow);
	set_preview_position_fields(preview.pendingOrderIndex, preview.pendingRow);
	preview.pendingSeek = 0;
}
#endif

static int thc_clamp_period(int period)
{
	if (period < PTMOD_THC_MIN_PERIOD)
		return PTMOD_THC_MIN_PERIOD;
	if (period > PTMOD_THC_MAX_PERIOD)
		return PTMOD_THC_MAX_PERIOD;
	return period;
}

static int thc_period_note_index(int period)
{
	int bestIndex = 0;
	int bestDiff = 0x7fffffff;
	int i;

	for (i = 0; i < (int)(sizeof thcPeriodTable / sizeof thcPeriodTable[0]); i++)
	{
		int diff = abs(period - thcPeriodTable[i]);
		if (diff < bestDiff)
		{
			bestDiff = diff;
			bestIndex = i;
		}
	}
	return bestIndex;
}

static int thc_transpose_period(int period, int semitones)
{
	int index;
	int maxIndex = (int)(sizeof thcPeriodTable / sizeof thcPeriodTable[0]) - 1;

	if (period <= 0)
		return 0;
	index = thc_period_note_index(period) + semitones;
	if (index < 0)
		index = 0;
	if (index > maxIndex)
		index = maxIndex;
	return thcPeriodTable[index];
}

static void thc_update_step(PTMOD_THC_CHANNEL *channel)
{
	if (!channel || channel->period <= 0)
	{
		if (channel)
			channel->stepFixed = 0;
		return;
	}
	channel->period = thc_clamp_period(channel->period);
	channel->basePeriod = channel->period;
	channel->stepFixed = (unsigned)(((unsigned long)PTMOD_THC_BASE_PERIOD << 16) / (unsigned)channel->period);
	if (!channel->stepFixed)
		channel->stepFixed = 1;
}

static int thc_pattern_for_order(int orderIndex)
{
	int pattern;

	if (!ptmodState.valid || ptmodState.songLength <= 0)
		return -1;
	if (orderIndex < 0)
		orderIndex = 0;
	if (orderIndex >= ptmodState.songLength)
		orderIndex = ptmodState.restartPosition;
	if (orderIndex < 0 || orderIndex >= ptmodState.songLength)
		orderIndex = 0;
	pattern = ptmod_order_pattern(orderIndex);
	return pattern;
}

static void thc_publish_position(void)
{
	int i;
	PTMOD_THC_STATE *thc = &preview.thc;

	preview.orderIndex = thc->orderIndex;
	preview.pattern = thc->pattern < 0 ? 0 : thc->pattern;
	preview.row = thc->row;
	preview.speed = thc->speed;
	preview.bpm = thc->bpm;
	if (preview.orderIndex >= 0 && preview.orderIndex < ptmodState.songLength)
		preview.frameIndex = (size_t)preview.orderIndex * PTMOD_ROWS + (size_t)preview.row;
	for (i = 0; i < PTMOD_CHANNELS; i++)
	{
		const PTMOD_THC_CHANNEL *channel = &thc->channel[i];

		preview.channelPeriod[i] = channel->period;
		preview.channelPosition[i] = (int)(channel->positionFixed >> 16);
		preview.channelNote[i] = channel->period > 0 ? thc_period_note_index(channel->period) + 1 : 0;
		preview.channelInstrument[i] = channel->active ? channel->sampleIndex + 1 : 0;
		preview.channelSample[i] = channel->active ? channel->sampleIndex + 1 : 0;
		preview.channelLevel[i] = channel->active ? channel->volume : 0;
		preview.channelPan[i] = 0x80;
	}
}

static void thc_set_position(int orderIndex, int row)
{
	PTMOD_THC_STATE *thc = &preview.thc;

	clamp_play_position(&orderIndex, &row);
	memset(thc, 0, sizeof *thc);
	thc->initialized = 1;
	thc->speed = 6;
	thc->bpm = 125;
	thc->orderIndex = orderIndex;
	thc->row = row;
	thc->pattern = thc_pattern_for_order(orderIndex);
	thc->frameSampleIndex = PTMOD_THC_FRAME_SAMPLES;
	thc_publish_position();
}

static void thc_schedule_jump(int orderIndex, int row)
{
	PTMOD_THC_STATE *thc = &preview.thc;

	if (orderIndex < 0)
		orderIndex = 0;
	if (ptmodState.songLength > 0 && orderIndex >= ptmodState.songLength)
		orderIndex = ptmodState.restartPosition;
	if (row < 0)
		row = 0;
	if (row >= PTMOD_ROWS)
		row = PTMOD_ROWS - 1;
	thc->pendingJump = 1;
	thc->pendingOrder = orderIndex;
	thc->pendingRow = row;
}

static int thc_pattern_break_row(int param)
{
	int row = ((param >> 4) & 0x0f) * 10 + (param & 0x0f);

	if (row < 0)
		row = 0;
	if (row >= PTMOD_ROWS)
		row = PTMOD_ROWS - 1;
	return row;
}

static void thc_advance_row(void)
{
	PTMOD_THC_STATE *thc = &preview.thc;

	if (thc->pendingJump)
	{
		thc->orderIndex = thc->pendingOrder;
		thc->row = thc->pendingRow;
		thc->pendingJump = 0;
	}
	else
	{
		thc->row++;
		if (thc->row >= PTMOD_ROWS)
		{
			thc->row = 0;
			thc->orderIndex++;
		}
	}
	if (ptmodState.songLength > 0 && thc->orderIndex >= ptmodState.songLength)
		thc->orderIndex = ptmodState.restartPosition;
	if (thc->orderIndex < 0 || thc->orderIndex >= ptmodState.songLength)
		thc->orderIndex = 0;
	if (preview_loop_should_wrap(thc->orderIndex, thc->row))
	{
		thc_set_position(preview.loopStartOrder, preview.loopStartRow);
		return;
	}
	thc->pattern = thc_pattern_for_order(thc->orderIndex);
}

static void thc_apply_row_effect(int channelIndex, const PTMOD_CELL *cell)
{
	PTMOD_THC_CHANNEL *channel = &preview.thc.channel[channelIndex];
	PTMOD_SAMPLE sample;
	int sampleChanged = 0;

	if (!cell)
		return;
	if (cell->sample > 0 && cell->sample <= PTMOD_MAX_SAMPLES)
	{
		channel->sampleIndex = cell->sample - 1;
		if (ptmod_get_sample(channel->sampleIndex, &sample))
			channel->volume = sample.volume;
		if (channel->volume < 0)
			channel->volume = 0;
		if (channel->volume > 64)
			channel->volume = 64;
		sampleChanged = 1;
	}
	channel->effect = cell->effect & 0x0f;
	channel->param = cell->param & 0xff;
	if (cell->period > 0)
	{
		channel->period = cell->period;
		channel->basePeriod = cell->period;
		channel->positionFixed = 0;
		channel->active = 1;
		thc_update_step(channel);
	}
	else if (sampleChanged && channel->period > 0)
	{
		channel->active = 1;
		thc_update_step(channel);
	}

	switch (channel->effect)
	{
	case 0x9:
		channel->positionFixed = (unsigned)(channel->param << 8) << 16;
		break;
	case 0xB:
		thc_schedule_jump(channel->param, 0);
		break;
	case 0xC:
		channel->volume = channel->param;
		if (channel->volume > 64)
			channel->volume = 64;
		break;
	case 0xD:
		thc_schedule_jump(preview.thc.orderIndex + 1, thc_pattern_break_row(channel->param));
		break;
	case 0xF:
		if (channel->param > 0 && channel->param <= 31)
			preview.thc.speed = channel->param;
		else if (channel->param > 31)
			preview.thc.bpm = channel->param;
		break;
	default:
		break;
	}
}

static void thc_process_row(void)
{
	PTMOD_THC_STATE *thc = &preview.thc;
	int c;

	thc->pattern = thc_pattern_for_order(thc->orderIndex);
	if (thc->pattern < 0)
	{
		preview.active = 0;
		preview.finished = 1;
		return;
	}
	for (c = 0; c < PTMOD_CHANNELS; c++)
	{
		PTMOD_CELL cell;

		if (ptmod_get_pattern_cell(thc->pattern, thc->row, c, &cell))
			thc_apply_row_effect(c, &cell);
	}
}

static void thc_apply_tick_effects(void)
{
	int c;

	for (c = 0; c < PTMOD_CHANNELS; c++)
	{
		PTMOD_THC_CHANNEL *channel = &preview.thc.channel[c];
		int param = channel->param;

		if (!channel->active)
			continue;
		switch (channel->effect)
		{
		case 0x1:
			channel->period -= param;
			thc_update_step(channel);
			break;
		case 0x2:
			channel->period += param;
			thc_update_step(channel);
			break;
		case 0xA:
			if (param & 0xf0)
				channel->volume += (param >> 4) & 0x0f;
			else
				channel->volume -= param & 0x0f;
			if (channel->volume < 0)
				channel->volume = 0;
			if (channel->volume > 64)
				channel->volume = 64;
			break;
		default:
			break;
		}
	}
}

static int thc_channel_period_for_tick(const PTMOD_THC_CHANNEL *channel)
{
	int nibble;

	if (!channel || channel->effect != 0x0 || channel->param == 0 || preview.thc.tick == 0)
		return channel ? channel->period : 0;
	nibble = (preview.thc.tick % 3) == 1 ? ((channel->param >> 4) & 0x0f) : (channel->param & 0x0f);
	return thc_transpose_period(channel->basePeriod, nibble);
}

static unsigned char thc_sample_to_wave(unsigned char rawSample)
{
	return (unsigned char)(((unsigned)rawSample + 0x80u) & 0xffu);
}

static unsigned char thc_volume_scale_wave(unsigned char sampleValue, int volume, int channelVolume)
{
	long centered;
	long scaled;

	if (volume < 0)
		volume = 0;
	if (volume > 64)
		volume = 64;
	channelVolume = clamp_percent(channelVolume);
	centered = (long)sampleValue - PTMOD_THC_WAVE_CENTER;
	scaled = (centered * volume * channelVolume) / (64L * 100L);
	scaled += PTMOD_THC_WAVE_CENTER;
	if (scaled < 0)
		scaled = 0;
	if (scaled > 255)
		scaled = 255;
	return (unsigned char)scaled;
}

static void thc_advance_channel(PTMOD_THC_CHANNEL *channel)
{
	int period;
	unsigned step;

	if (!channel || !channel->active)
		return;
	period = thc_channel_period_for_tick(channel);
	if (period > 0 && period != channel->period)
	{
		step = (unsigned)(((unsigned long)PTMOD_THC_BASE_PERIOD << 16) / (unsigned)thc_clamp_period(period));
		channel->positionFixed += step ? step : 1;
	}
	else
	{
		channel->positionFixed += channel->stepFixed;
	}
}

static unsigned char thc_channel_output_wave(int channelIndex)
{
	PTMOD_THC_CHANNEL *channel = &preview.thc.channel[channelIndex];
	PTMOD_SAMPLE sample;
	unsigned pos;

	if (!channel->active)
		return PTMOD_THC_WAVE_CENTER;
	if (!ptmod_get_sample(channel->sampleIndex, &sample) || !sample.data || sample.length == 0)
	{
		channel->active = 0;
		return PTMOD_THC_WAVE_CENTER;
	}

	pos = channel->positionFixed >> 16;
	if (pos >= sample.length)
	{
		if (sample.loopLength > 2 && sample.loopStart < sample.length)
		{
			unsigned loopEnd = sample.loopStart + sample.loopLength;
			if (loopEnd > sample.length)
				loopEnd = sample.length;
			while (pos >= loopEnd && sample.loopLength > 0)
				pos -= sample.loopLength;
			if (pos < sample.loopStart)
				pos = sample.loopStart;
			channel->positionFixed = (channel->positionFixed & 0xffff) | (pos << 16);
		}
		else
		{
			channel->active = 0;
			return PTMOD_THC_WAVE_CENTER;
		}
	}

	thc_advance_channel(channel);
	if (preview.channelMute[channelIndex])
		return PTMOD_THC_WAVE_CENTER;
	return thc_volume_scale_wave(thc_sample_to_wave(sample.data[pos]),
		channel->volume, preview.channelVolume[channelIndex]);
}

static void thc_mix_frame(void)
{
	int i;
	int c;

	if (preview.thc.tick == 0)
		thc_process_row();
	else
		thc_apply_tick_effects();
	if (!preview.active)
		return;

	for (i = 0; i < PTMOD_THC_FRAME_SAMPLES; i++)
	{
		long mix = PTMOD_THC_WAVE_CENTER;

		for (c = 0; c < PTMOD_CHANNELS; c++)
			mix += (long)thc_channel_output_wave(c) - PTMOD_THC_WAVE_CENTER;

		mix = PTMOD_THC_WAVE_CENTER +
			(((mix - PTMOD_THC_WAVE_CENTER) * clamp_percent(preview.masterVolume)) /
			 (100L * PTMOD_THC_WAVE_GAIN_DIVISOR));
		if (mix < 0)
			mix = 0;
		if (mix > 255)
			mix = 255;
		preview.thc.frame[i] = (unsigned char)mix;
	}

	preview.thc.frameSampleIndex = 0;
	preview.thc.tick++;
	if (preview.thc.tick >= preview.thc.speed)
	{
		preview.thc.tick = 0;
		thc_advance_row();
	}
	thc_publish_position();
}

static unsigned char thc_next_output_sample(void)
{
	if (!preview.thc.initialized)
		thc_set_position(preview.orderIndex, preview.row);
	if (preview.thc.frameSampleIndex >= PTMOD_THC_FRAME_SAMPLES)
		thc_mix_frame();
	if (!preview.active || preview.thc.frameSampleIndex >= PTMOD_THC_FRAME_SAMPLES)
		return PTMOD_THC_WAVE_CENTER;
	return preview.thc.frame[preview.thc.frameSampleIndex++];
}

static void thc_silence_voice3(void)
{
	sidreg[0x0e] = 0x00;
	sidreg[0x0f] = PTMOD_THC_WAVE_CENTER;
	sidreg[0x10] = 0x00;
	sidreg[0x11] = 0x00;
	sidreg[0x12] = 0x00;
	sidreg[0x13] = 0x00;
	sidreg[0x14] = 0xf0;
}

static void ptmodplay_reset_audio_state(int silenceVoice3)
{
	sid_clear_write_events();
	preview.pendingSeek = 0;
	preview.delayFrameCursor = 0.0;
	preview.mixedSamples = 0;
	preview.lastPeak = 0;
	preview.peakSinceReset = 0;
	memset(preview.channelPeriod, 0, sizeof preview.channelPeriod);
	memset(preview.channelPosition, 0, sizeof preview.channelPosition);
	memset(preview.channelNote, 0, sizeof preview.channelNote);
	memset(preview.channelInstrument, 0, sizeof preview.channelInstrument);
	memset(preview.channelSample, 0, sizeof preview.channelSample);
	memset(preview.channelLevel, 0, sizeof preview.channelLevel);
	memset(preview.channelPan, 0, sizeof preview.channelPan);
	memset(&preview.thc, 0, sizeof preview.thc);
	preview.thc.frameSampleIndex = PTMOD_THC_FRAME_SAMPLES;
	if (silenceVoice3)
		thc_silence_voice3();
}

static void release_player(void)
{
	ptmodplay_reset_audio_state(preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM);
#ifdef GTULTRA_LIBXMP
	if (preview.context)
	{
		if (preview.playerStarted)
			xmp_end_player(preview.context);
		xmp_release_module(preview.context);
		xmp_free_context(preview.context);
		preview.context = NULL;
		preview.playerStarted = 0;
	}
#endif
	free(preview.mixBuffer);
	preview.mixBuffer = NULL;
	preview.mixBufferSamples = 0;
	free(preview.moduleImage);
	preview.moduleImage = NULL;
	preview.moduleImageSize = 0;
}

#ifdef GTULTRA_LIBXMP
static short *ensure_mix_buffer(unsigned samples)
{
	short *newBuffer;

	if (samples == 0)
		return NULL;
	if (samples <= preview.mixBufferSamples)
		return preview.mixBuffer;
	newBuffer = realloc(preview.mixBuffer, samples * sizeof *preview.mixBuffer);
	if (!newBuffer)
		return NULL;
	preview.mixBuffer = newBuffer;
	preview.mixBufferSamples = samples;
	return preview.mixBuffer;
}
#endif

void ptmodplay_clear(void)
{
	release_player();
	memset(&preview, 0, sizeof preview);
	memset(&audition, 0, sizeof audition);
	init_runtime_settings();
}

static void restore_runtime_settings(const PTMOD_RUNTIME_SETTINGS *settings)
{
	if (settings)
		ptmodplay_set_runtime_settings(settings);
	else
		init_runtime_settings();
}

int ptmodplay_load_current(char *error, size_t errorSize)
{
	PTMOD_RUNTIME_SETTINGS runtimeSettings;
	unsigned char *image = NULL;
	size_t imageSize = 0;
	int syncHostActive;
	int syncOrderIndex;
	int syncRow;
	int hasSyncPosition;

	ptmodplay_get_runtime_settings(&runtimeSettings);
	syncHostActive = preview.syncHostActive;
	syncOrderIndex = preview.syncOrderIndex;
	syncRow = preview.syncRow;
	hasSyncPosition = preview.hasSyncPosition;
	release_player();

	if (!ptmodState.valid)
	{
		memset(&preview, 0, sizeof preview);
		restore_runtime_settings(&runtimeSettings);
		preview.syncHostActive = syncHostActive;
		preview.syncOrderIndex = syncOrderIndex;
		preview.syncRow = syncRow;
		preview.hasSyncPosition = hasSyncPosition;
		set_message(error, errorSize, "No valid MOD is loaded");
		return 0;
	}

	if (!ptmod_build_image(&image, &imageSize, error, errorSize))
	{
		memset(&preview, 0, sizeof preview);
		restore_runtime_settings(&runtimeSettings);
		preview.syncHostActive = syncHostActive;
		preview.syncOrderIndex = syncOrderIndex;
		preview.syncRow = syncRow;
		preview.hasSyncPosition = hasSyncPosition;
		return 0;
	}

	memset(&preview, 0, sizeof preview);
	restore_runtime_settings(&runtimeSettings);
	preview.syncHostActive = syncHostActive;
	preview.syncOrderIndex = syncOrderIndex;
	preview.syncRow = syncRow;
	preview.hasSyncPosition = hasSyncPosition;
	preview.loaded = 1;
	preview.frames = (size_t)ptmodState.songLength * PTMOD_ROWS;
	preview.pattern = ptmodState.patternCount ? ptmodState.order[0] : 0;
	preview.speed = 6;
	preview.bpm = 125;
	preview.moduleImage = image;
	preview.moduleImageSize = imageSize;

#ifdef GTULTRA_LIBXMP
	preview.context = xmp_create_context();
	if (!preview.context)
	{
		release_player();
		memset(&preview, 0, sizeof preview);
		restore_runtime_settings(&runtimeSettings);
		preview.syncHostActive = syncHostActive;
		preview.syncOrderIndex = syncOrderIndex;
		preview.syncRow = syncRow;
		preview.hasSyncPosition = hasSyncPosition;
		set_message(error, errorSize, "libxmp could not create a playback context");
		return 0;
	}
	if (xmp_load_module_from_memory(preview.context, preview.moduleImage, (long)preview.moduleImageSize) != 0)
	{
		release_player();
		memset(&preview, 0, sizeof preview);
		restore_runtime_settings(&runtimeSettings);
		preview.syncHostActive = syncHostActive;
		preview.syncOrderIndex = syncOrderIndex;
		preview.syncRow = syncRow;
		preview.hasSyncPosition = hasSyncPosition;
		set_message(error, errorSize, "libxmp could not load the MOD image");
		return 0;
	}
	preview.usingLibxmp = 1;
	set_message(error, errorSize, "MOD playback ready (%s)", ptmodplay_replay_mode_name(preview.replayMode));
	#else
		preview.usingLibxmp = 0;
		if (preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM)
			set_message(error, errorSize, "MOD THCMOD waveform playback ready");
		else
			set_message(error, errorSize, "Built without libxmp; MOD editor/save is available but audio preview is disabled");
	#endif
	return 1;
}

void ptmodplay_reload_if_dirty(void)
{
	if (preview.loaded && ptmodState.valid)
	{
		int wasActive = preview.active && !preview.finished;
		int orderIndex = preview.hasSyncPosition ? preview.syncOrderIndex : preview.orderIndex;
		int row = preview.hasSyncPosition ? preview.syncRow : preview.row;

		if (ptmodplay_load_current(NULL, 0) && wasActive)
			ptmodplay_start_at(orderIndex, row);
	}
}

#ifdef GTULTRA_LIBXMP
static int start_player(unsigned sampleRate)
{
	int c;

	if (!preview.context || sampleRate == 0)
		return 0;

	if (preview.playerStarted && preview.sampleRate == sampleRate)
	{
		apply_pending_seek();
		return 1;
	}

	if (preview.playerStarted)
	{
		xmp_end_player(preview.context);
		preview.playerStarted = 0;
	}

	if (xmp_start_player(preview.context, (int)sampleRate, XMP_FORMAT_MONO) != 0)
		return 0;

	xmp_set_player(preview.context, XMP_PLAYER_MODE, XMP_MODE_PROTRACKER);
	xmp_set_player(preview.context, XMP_PLAYER_INTERP, XMP_INTERP_LINEAR);
	xmp_set_player(preview.context, XMP_PLAYER_VOLUME, 100);
	for (c = 0; c < PTMOD_CHANNELS; c++)
	{
		xmp_channel_mute(preview.context, c, preview.channelMute[c] ? 1 : 0);
		xmp_channel_vol(preview.context, c, clamp_percent(preview.channelVolume[c]));
	}
	preview.playerStarted = 1;
	preview.sampleRate = sampleRate;
	apply_pending_seek();
	return 1;
}
#endif

void ptmodplay_start_at(int orderIndex, int row)
{
	if (!preview.loaded)
		return;

	ptmodplay_reset_audio_state(preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM);
	queue_preview_seek(orderIndex, row);
	preview.syncHostActive = 1;
	preview.syncOrderIndex = orderIndex;
	preview.syncRow = row;
	preview.hasSyncPosition = 1;
	preview.active = 1;
	preview.finished = 0;
	preview.delayFramesRemaining = preview.startDelayFrames;
	preview.delayFrameCursor = 0.0;
	preview.mixedSamples = 0;
	preview.lastPeak = 0;
	preview.peakSinceReset = 0;
#ifdef GTULTRA_LIBXMP
	if (preview.context && preview.playerStarted)
	{
		xmp_restart_module(preview.context);
		apply_pending_seek();
	}
#endif
}

void ptmodplay_seek(int orderIndex, int row)
{
	ptmodplay_start_at(orderIndex, row);
}

void ptmodplay_sync_to_sid(int hostActive, int orderIndex, int row)
{
	int positionChanged;

	clamp_play_position(&orderIndex, &row);
	positionChanged = !preview.hasSyncPosition ||
		!preview.syncHostActive ||
		preview.syncOrderIndex != orderIndex ||
		preview.syncRow != row;
	preview.syncHostActive = hostActive ? 1 : 0;
	preview.syncOrderIndex = orderIndex;
	preview.syncRow = row;
	preview.hasSyncPosition = 1;

	if (!hostActive)
	{
		if (preview.active)
			ptmodplay_stop();
		return;
	}
	if (!preview.loaded || !preview.runtimeEnabled)
		return;
	if (!preview.active || preview.finished)
	{
		ptmodplay_start_at(orderIndex, row);
		return;
	}
	if (!positionChanged)
		return;

	queue_preview_seek(orderIndex, row);
#ifdef GTULTRA_LIBXMP
	if (preview.context && preview.playerStarted)
		apply_pending_seek();
#endif
}

void ptmodplay_set_loop_range(int enabled, int startOrder, int startRow, int endOrder, int endRow)
{
	if (!enabled)
	{
		preview.loopEnabled = 0;
		return;
	}
	clamp_play_position(&startOrder, &startRow);
	clamp_play_position(&endOrder, &endRow);
	if (endOrder < startOrder || (endOrder == startOrder && endRow < startRow))
	{
		int tempOrder = startOrder;
		int tempRow = startRow;

		startOrder = endOrder;
		startRow = endRow;
		endOrder = tempOrder;
		endRow = tempRow;
	}
	preview.loopStartOrder = startOrder;
	preview.loopStartRow = startRow;
	preview.loopEndOrder = endOrder;
	preview.loopEndRow = endRow;
	preview.loopEnabled = 1;
}

void ptmodplay_reset(void)
{
	ptmodplay_start_at(0, 0);
}

void ptmodplay_stop(void)
{
	preview.active = 0;
	preview.finished = 1;
#ifdef GTULTRA_LIBXMP
	if (preview.context && preview.playerStarted)
		xmp_stop_module(preview.context);
#endif
	ptmodplay_reset_audio_state(preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM);
}

static void thc_prepare_sid_waveform(void)
{
	int ownVoice3Setup =
		(sidreg[0x0e] == 0x00) && (sidreg[0x0f] == PTMOD_THC_WAVE_CENTER) &&
		(sidreg[0x10] == 0x00) && (sidreg[0x11] == 0x00) &&
		(sidreg[0x12] == 0x00 || sidreg[0x12] == 0x01) && (sidreg[0x13] == 0x00) &&
		(sidreg[0x14] == 0xf0) && (sidreg[0x17] == 0x08) &&
		((sidreg[0x18] & 0x0f) == 0x0f);

	preview.voice3Conflict = !ownVoice3Setup && (
		(sidreg[0x0e] != 0) || (sidreg[0x0f] != PTMOD_THC_WAVE_CENTER) ||
		(sidreg[0x10] != 0) || (sidreg[0x11] != 0) ||
		((sidreg[0x12] & 0xfe) != 0) ||
		(sidreg[0x13] != 0) || (sidreg[0x14] != 0 && sidreg[0x14] != 0xf0) ||
		(sidreg[0x17] != 0 && sidreg[0x17] != 0x08));
	sidreg[0x0e] = 0x00;
	sidreg[0x0f] = PTMOD_THC_WAVE_CENTER;
	sidreg[0x10] = 0x00;
	sidreg[0x11] = 0x00;
	sidreg[0x12] = 0x01;
	sidreg[0x13] = 0x00;
	sidreg[0x14] = 0xf0;
	sidreg[0x17] = 0x08;
	sidreg[0x18] = (sidreg[0x18] & 0xf0) | 0x0f;
}

static int consume_delay(unsigned samples, unsigned sampleRate, unsigned frameRate)
{
	unsigned i;

	if (preview.delayFramesRemaining <= 0 || sampleRate == 0 || frameRate == 0)
		return 0;

	for (i = 0; i < samples; i++)
	{
		preview.delayFrameCursor += (double)frameRate / (double)sampleRate;
		while (preview.delayFrameCursor >= 1.0 && preview.delayFramesRemaining > 0)
		{
			preview.delayFrameCursor -= 1.0;
			preview.delayFramesRemaining--;
		}
		if (preview.delayFramesRemaining <= 0)
			return (int)i + 1;
	}

	return (int)samples;
}

void ptmodplay_prepare_sid_events(unsigned samples, unsigned sampleRate, unsigned frameRate, int hostActive)
{
	double step;
	unsigned startSample = 0;
	int blockPeak = 0;
	int mixModule = 1;
	int clockrate;

	sid_clear_write_events();
	if (preview.replayMode != PTMOD_REPLAY_THC_WAVEFORM)
		return;
	if (sampleRate == 0 || frameRate == 0 || samples == 0)
		return;
	if (!hostActive && !preview.active)
		return;
	if (!preview.loaded || !preview.runtimeEnabled)
	{
		if (preview.active)
			ptmodplay_stop();
		return;
	}
	if (!preview.active)
	{
		if (preview.finished)
			mixModule = 0;
		else
		{
			ptmodplay_start_at(preview.orderIndex, preview.row);
			if (!preview.active)
				mixModule = 0;
		}
	}
	if (!mixModule)
		return;

	thc_prepare_sid_waveform();
	if (preview.delayFramesRemaining > 0)
	{
		startSample = (unsigned)consume_delay(samples, sampleRate, frameRate);
		if (startSample >= samples)
			return;
	}

	clockrate = sid_get_clockrate();
	if (clockrate <= 0)
		return;
	step = (double)sampleRate / ((double)frameRate * (double)PTMOD_THC_FRAME_SAMPLES);
	if (step <= 0.0)
		return;
	while (preview.thc.eventCursor < (double)samples)
	{
		unsigned sampleOffset = (unsigned)preview.thc.eventCursor;

			if (sampleOffset >= startSample)
			{
				unsigned char value = thc_next_output_sample();
				long centered = ((long)value - PTMOD_THC_WAVE_CENTER) * 256L;
				long absCentered = centered < 0 ? -centered : centered;
				int cycle = (int)((preview.thc.eventCursor * (double)clockrate) / (double)sampleRate);

				if (absCentered > blockPeak)
					blockPeak = (int)absCentered;
				sid_queue_write_event(0, cycle, 0x12, 0x11);
				sid_queue_write_event(0, cycle + 4, 0x12, 0x09);
				sid_queue_write_event(0, cycle + 8, 0x0f, value);
				sid_queue_write_event(0, cycle + 12, 0x12, 0x01);
				preview.mixedSamples++;
			}
		preview.thc.eventCursor += step;
	}
	preview.thc.eventCursor -= (double)samples;
	if (preview.thc.eventCursor < 0.0)
		preview.thc.eventCursor = 0.0;
	preview.lastPeak = blockPeak;
	if (blockPeak > preview.peakSinceReset)
		preview.peakSinceReset = blockPeak;
}

#ifdef GTULTRA_LIBXMP
static void update_position_from_libxmp(void)
{
	struct xmp_frame_info info;

	if (!preview.context || !preview.playerStarted)
		return;

	memset(&info, 0, sizeof info);
	xmp_get_frame_info(preview.context, &info);
	preview.orderIndex = info.pos < 0 ? 0 : info.pos;
	preview.pattern = info.pattern < 0 ? 0 : info.pattern;
	preview.row = info.row < 0 ? 0 : info.row;
	preview.speed = info.speed;
	preview.bpm = info.bpm;
	if (preview.orderIndex < ptmodState.songLength)
		preview.frameIndex = (size_t)preview.orderIndex * PTMOD_ROWS + (size_t)preview.row;
	if (preview_loop_should_wrap(preview.orderIndex, preview.row))
	{
		queue_preview_seek(preview.loopStartOrder, preview.loopStartRow);
		apply_pending_seek();
	}
	for (int i = 0; i < PTMOD_CHANNELS; i++)
	{
		preview.channelPeriod[i] = (int)(info.channel_info[i].period / 4096);
		preview.channelPosition[i] = (int)info.channel_info[i].position;
		preview.channelNote[i] = info.channel_info[i].note;
		preview.channelInstrument[i] = info.channel_info[i].instrument;
		preview.channelSample[i] = info.channel_info[i].sample;
		preview.channelLevel[i] = info.channel_info[i].volume;
		preview.channelPan[i] = info.channel_info[i].pan;
	}
}
#endif

void ptmodplay_audition_sample_with_loop(int index, unsigned loopStart, unsigned loopLength)
{
	PTMOD_SAMPLE sample;

	if (!ptmod_get_sample(index, &sample) || !sample.data || sample.length == 0)
		return;
	if (loopStart > sample.length)
		loopStart = sample.length;
	loopStart &= ~1u;
	if (loopLength > sample.length - loopStart)
		loopLength = sample.length - loopStart;
	loopLength &= ~1u;
	audition.active = 1;
	audition.sampleIndex = index;
	audition.positionFixed = 0;
	audition.loopEnabled = loopLength > 2 && loopStart < sample.length;
	audition.loopStart = loopStart;
	audition.loopLength = loopLength;
}

void ptmodplay_audition_sample(int index)
{
	PTMOD_SAMPLE sample;

	if (!ptmod_get_sample(index, &sample))
		return;
	ptmodplay_audition_sample_with_loop(index, sample.loopStart, sample.loopLength);
}

void ptmodplay_stop_audition(void)
{
	memset(&audition, 0, sizeof audition);
}

static void mix_audition_sample(Sint32 *dest, unsigned samples, unsigned sampleRate, int *blockPeak)
{
	PTMOD_SAMPLE sample;
	unsigned long long stepFixed;
	unsigned loopStart;
	unsigned loopEnd;
	unsigned i;
	int volume;

	if (!audition.active || !dest || sampleRate == 0)
		return;
	if (!ptmod_get_sample(audition.sampleIndex, &sample) || !sample.data || sample.length == 0)
	{
		ptmodplay_stop_audition();
		return;
	}

	stepFixed = (8363ull << 16) / sampleRate;
	if (!stepFixed)
		stepFixed = 1;
	loopStart = audition.loopStart;
	loopEnd = loopStart + audition.loopLength;
	if (!audition.loopEnabled || loopStart >= sample.length || audition.loopLength <= 2)
	{
		loopStart = 0;
		loopEnd = 0;
	}
	else
	{
		if (loopEnd > sample.length)
			loopEnd = sample.length;
		if (loopEnd <= loopStart)
		{
			loopStart = 0;
			loopEnd = 0;
		}
	}
	volume = sample.volume > 0 ? sample.volume : 64;
	if (volume > 64)
		volume = 64;

	for (i = 0; i < samples; i++)
	{
		unsigned index;
		long centered;
		long absCentered;
		unsigned destIndex = i * 2;

		if (loopEnd > loopStart)
		{
			unsigned long long loopStartFixed = (unsigned long long)loopStart << 16;
			unsigned long long loopEndFixed = (unsigned long long)loopEnd << 16;
			unsigned long long loopLengthFixed = loopEndFixed - loopStartFixed;

			if (audition.positionFixed >= loopEndFixed)
				audition.positionFixed = loopStartFixed +
					((audition.positionFixed - loopStartFixed) % loopLengthFixed);
		}
		index = (unsigned)(audition.positionFixed >> 16);
		if (index >= sample.length)
		{
			ptmodplay_stop_audition();
			break;
		}
		centered = ((long)(signed char)sample.data[index] * 256L * volume) / 64L;
		centered = (centered * clamp_percent(preview.masterVolume)) / 100L;
		absCentered = centered < 0 ? -centered : centered;
		if (blockPeak && absCentered > *blockPeak)
			*blockPeak = (int)absCentered;
		dest[destIndex] = clamp_s16((long)dest[destIndex] + centered);
		dest[destIndex + 1] = clamp_s16((long)dest[destIndex + 1] + centered);
		audition.positionFixed += stepFixed;
	}
}

void ptmodplay_mix(Sint32 *dest, unsigned samples, unsigned sampleRate, unsigned frameRate, int hostActive)
{
	unsigned startSample = 0;
	int blockPeak = preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM ? preview.lastPeak : 0;
	int mixModule = 1;

	if (!dest || sampleRate == 0 || frameRate == 0)
		return;
	if (!hostActive && !preview.active)
	{
		mixModule = 0;
	}
	if (!preview.loaded || !preview.runtimeEnabled)
	{
		if (preview.active)
			ptmodplay_stop();
		mixModule = 0;
	}
	if (preview.replayMode != PTMOD_REPLAY_LIBXMP)
		mixModule = 0;
	if (mixModule && !preview.active)
	{
		if (preview.finished)
			mixModule = 0;
		else
		{
			ptmodplay_start_at(preview.orderIndex, preview.row);
			if (!preview.active)
				mixModule = 0;
		}
	}

	if (mixModule && preview.delayFramesRemaining > 0)
	{
		startSample = (unsigned)consume_delay(samples, sampleRate, frameRate);
		if (startSample >= samples)
			mixModule = 0;
	}

	if (mixModule)
	{
#ifdef GTULTRA_LIBXMP
		if (preview.usingLibxmp && start_player(sampleRate))
		{
			unsigned renderSamples = samples - startSample;
			short *mix = ensure_mix_buffer(renderSamples);
			int result;
			unsigned i;

			for (i = 0; i < PTMOD_CHANNELS; i++)
			{
				xmp_channel_mute(preview.context, (int)i, preview.channelMute[i] ? 1 : 0);
				xmp_channel_vol(preview.context, (int)i, clamp_percent(preview.channelVolume[i]));
			}

			if (mix)
			{
				result = xmp_play_buffer(preview.context, mix, (int)(renderSamples * sizeof *mix), 0);
				for (i = 0; i < renderSamples; i++)
				{
					long centered = ((long)mix[i] * clamp_percent(preview.masterVolume)) / 100;
					long absCentered = centered < 0 ? -centered : centered;
					unsigned destIndex = (startSample + i) * 2;

					if (absCentered > blockPeak)
						blockPeak = (int)absCentered;
					dest[destIndex] = clamp_s16((long)dest[destIndex] + centered);
					dest[destIndex + 1] = clamp_s16((long)dest[destIndex + 1] + centered);
				}
				preview.mixedSamples += renderSamples;
				update_position_from_libxmp();
				if (result == XMP_END)
				{
					preview.active = 0;
					preview.finished = 1;
				}
			}
		}
#else
		(void)startSample;
#endif
	}

	mix_audition_sample(dest, samples, sampleRate, &blockPeak);

	if (preview.replayMode != PTMOD_REPLAY_THC_WAVEFORM || audition.active || blockPeak > preview.lastPeak)
		preview.lastPeak = blockPeak;
	if (blockPeak > preview.peakSinceReset)
		preview.peakSinceReset = blockPeak;
}

void ptmodplay_get_stats(PTMOD_PREVIEW_STATS *stats)
{
	int i;

	if (!stats)
		return;

	memset(stats, 0, sizeof *stats);
	stats->loaded = preview.loaded;
	stats->active = preview.active;
	stats->finished = preview.finished;
	stats->channels = PTMOD_CHANNELS;
	stats->songLength = ptmodState.songLength;
	stats->patternCount = ptmodState.patternCount;
	stats->startDelayFrames = preview.startDelayFrames;
	stats->delayFramesRemaining = preview.delayFramesRemaining;
	stats->frames = preview.frames;
	stats->frameIndex = preview.frameIndex;
	stats->orderIndex = preview.orderIndex;
	stats->pattern = preview.pattern;
	stats->row = preview.row;
	stats->speed = preview.speed;
	stats->bpm = preview.bpm;
	stats->mixedSamples = preview.mixedSamples;
	stats->lastPeak = preview.lastPeak;
	stats->peakSinceReset = preview.peakSinceReset;
	stats->usingLibxmp = preview.usingLibxmp;
	stats->replayMode = preview.replayMode;
	stats->voice3Reserved = preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM;
	stats->voice3Conflict = preview.voice3Conflict;
	for (i = 0; i < PTMOD_MAX_PREVIEW_CHANNELS; i++)
	{
		stats->channelPeriod[i] = preview.channelPeriod[i];
		stats->channelPosition[i] = preview.channelPosition[i];
		stats->channelNote[i] = preview.channelNote[i];
		stats->channelInstrument[i] = preview.channelInstrument[i];
		stats->channelSample[i] = preview.channelSample[i];
		stats->channelLevel[i] = preview.channelLevel[i];
		stats->channelPan[i] = preview.channelPan[i];
	}
}

int ptmodplay_get_row(int channel, size_t frame, PTMOD_PREVIEW_ROW *row)
{
	PTMOD_CELL cell;
	int orderIndex;
	int pattern;
	int patternRow;

	if (!row)
		return 0;
	memset(row, 0, sizeof *row);
	if (!ptmod_get_row(channel, frame, &cell, &orderIndex, &pattern, &patternRow))
		return 0;

	row->sample = cell.sample;
	row->period = cell.period;
	row->effect = cell.effect;
	row->param = cell.param;
	row->orderIndex = orderIndex;
	row->pattern = pattern;
	row->row = patternRow;
	return 1;
}

int ptmodplay_set_row_value(int channel, size_t frame, int field, int value)
{
	return ptmod_set_row_value(channel, frame, field, value);
}

void ptmodplay_get_runtime_settings(PTMOD_RUNTIME_SETTINGS *settings)
{
	int i;

	if (!settings)
		return;

	memset(settings, 0, sizeof *settings);
	settings->enabled = preview.runtimeEnabled;
	settings->masterVolume = preview.masterVolume;
	settings->startDelayFrames = preview.startDelayFrames;
	settings->replayMode = preview.replayMode;
	for (i = 0; i < PTMOD_MAX_PREVIEW_CHANNELS; i++)
	{
		settings->channelVolume[i] = preview.channelVolume[i];
		settings->channelMute[i] = preview.channelMute[i];
	}
}

void ptmodplay_set_runtime_settings(const PTMOD_RUNTIME_SETTINGS *settings)
{
	int i;

	if (!settings)
		return;

	preview.runtimeEnabled = settings->enabled ? 1 : 0;
	preview.masterVolume = clamp_percent(settings->masterVolume);
	preview.startDelayFrames = clamp_delay(settings->startDelayFrames);
	preview.replayMode = clamp_replay_mode(settings->replayMode);
	for (i = 0; i < PTMOD_MAX_PREVIEW_CHANNELS; i++)
	{
		preview.channelVolume[i] = clamp_percent(settings->channelVolume[i]);
		preview.channelMute[i] = settings->channelMute[i] ? 1 : 0;
	}
}

void ptmodplay_set_enabled(int enabled)
{
	int wasEnabled = preview.runtimeEnabled;

	preview.runtimeEnabled = enabled ? 1 : 0;
	if (!preview.runtimeEnabled)
	{
		if (preview.active)
			ptmodplay_stop();
		return;
	}
	if (!wasEnabled && preview.syncHostActive && preview.hasSyncPosition)
		ptmodplay_start_at(preview.syncOrderIndex, preview.syncRow);
}

const char *ptmodplay_replay_mode_name(int replayMode)
{
	switch (clamp_replay_mode(replayMode))
	{
	case PTMOD_REPLAY_THC_WAVEFORM:
		return "THCMOD waveform";
	case PTMOD_REPLAY_LIBXMP:
	default:
		return "libxmp";
	}
}

void ptmodplay_set_replay_mode(int replayMode)
{
	int newMode = clamp_replay_mode(replayMode);
	int wasActive = preview.active && !preview.finished;
	int wasThc = preview.replayMode == PTMOD_REPLAY_THC_WAVEFORM;
	int orderIndex = preview.hasSyncPosition ? preview.syncOrderIndex : preview.orderIndex;
	int row = preview.hasSyncPosition ? preview.syncRow : preview.row;

	if (preview.replayMode == newMode)
		return;
	preview.replayMode = newMode;
	preview.voice3Conflict = 0;
#ifdef GTULTRA_LIBXMP
	if (preview.context && preview.playerStarted)
	{
		xmp_stop_module(preview.context);
		preview.playerStarted = 0;
	}
#endif
	ptmodplay_reset_audio_state(wasThc || newMode == PTMOD_REPLAY_THC_WAVEFORM);
	preview.active = 0;
	preview.finished = 0;
	set_preview_position_fields(orderIndex, row);
	if (wasActive || (preview.syncHostActive && preview.hasSyncPosition))
		ptmodplay_start_at(orderIndex, row);
}

void ptmodplay_set_master_volume(int volume)
{
	preview.masterVolume = clamp_percent(volume);
}

void ptmodplay_set_start_delay(int delayFrames)
{
	preview.startDelayFrames = clamp_delay(delayFrames);
	if (preview.active && preview.mixedSamples == 0)
	{
		preview.delayFramesRemaining = preview.startDelayFrames;
		preview.delayFrameCursor = 0.0;
	}
}

void ptmodplay_set_channel_volume(int channel, int volume)
{
	if (channel < 0 || channel >= PTMOD_MAX_PREVIEW_CHANNELS)
		return;
	preview.channelVolume[channel] = clamp_percent(volume);
}

void ptmodplay_set_channel_mute(int channel, int muted)
{
	if (channel < 0 || channel >= PTMOD_MAX_PREVIEW_CHANNELS)
		return;
	preview.channelMute[channel] = muted ? 1 : 0;
}
