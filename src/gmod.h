#ifndef GMOD_H
#define GMOD_H

#include <stddef.h>
#include <stdio.h>

#ifndef MAX_PATHNAME
#define MAX_PATHNAME 256
#endif

#define PTMOD_SNG_CHUNK_ID 0x9c
#define PTMOD_SNG_MAGIC "PTMD"
#define PTMOD_SNG_VERSION 2

#define PTMOD_CHANNELS 4
#define PTMOD_MAX_SAMPLES 31
#define PTMOD_MAX_ORDERS 128
#define PTMOD_MAX_PATTERNS 128
#define PTMOD_ROWS 64
#define PTMOD_TITLE_LEN 20
#define PTMOD_SAMPLE_NAME_LEN 22
#define PTMOD_ORDER_VISIBLE_ROWS 12

typedef struct
{
	int sample;
	int period;
	int effect;
	int param;
} PTMOD_CELL;

typedef struct
{
	char name[PTMOD_SAMPLE_NAME_LEN + 1];
	unsigned length;
	int finetune;
	int volume;
	unsigned loopStart;
	unsigned loopLength;
	unsigned char *data;
} PTMOD_SAMPLE;

enum
{
	PTMOD_RAW_SIGNED_8,
	PTMOD_RAW_UNSIGNED_8,
	PTMOD_RAW_SIGNED_16_LE,
	PTMOD_RAW_UNSIGNED_16_LE,
	PTMOD_RAW_SIGNED_16_BE,
	PTMOD_RAW_UNSIGNED_16_BE
};

typedef struct
{
	int rawFormat;
	int rawChannels;
	int normalize;
	int resample;
	unsigned sourceRate;
	unsigned targetRate;
} PTMOD_SAMPLE_IMPORT_OPTIONS;

typedef struct
{
	int enabled;
	int valid;
	int dirty;
	int patternCount;
	int songLength;
	int restartPosition;
	unsigned char order[PTMOD_MAX_ORDERS];
	char title[PTMOD_TITLE_LEN + 1];
	char path[MAX_PATHNAME];
	char error[256];
	char status[256];
	PTMOD_SAMPLE sample[PTMOD_MAX_SAMPLES];
	PTMOD_CELL pattern[PTMOD_MAX_PATTERNS][PTMOD_ROWS][PTMOD_CHANNELS];
} PTMOD_STATE;

extern PTMOD_STATE ptmodState;

void ptmod_clear(void);
int ptmod_create_blank(void);
int ptmod_load_source(const char *path, char *error, size_t errorSize);
int ptmod_reload_current(char *error, size_t errorSize);
int ptmod_restore(const char *path, int enabled, char *error, size_t errorSize);
int ptmod_save_current(char *error, size_t errorSize);
int ptmod_save_as(const char *path, char *error, size_t errorSize);
int ptmod_has_path(void);
int ptmod_is_dirty(void);
const char *ptmod_status_text(void);
int ptmod_write_sng_chunk(FILE *handle);
int ptmod_read_sng_chunk(FILE *handle);
int ptmod_write_export_artifacts(const char *outputPath, int playerAddress, int packedSize, int playAddress);
int ptmod_order_pattern(int orderIndex);
int ptmod_set_song_length(int length);
int ptmod_set_restart_position(int restartPosition);
int ptmod_set_order_pattern(int orderIndex, int pattern);
int ptmod_insert_order(int orderIndex);
int ptmod_delete_order(int orderIndex);
int ptmod_ensure_pattern_count(int pattern);
int ptmod_get_pattern_cell(int pattern, int row, int channel, PTMOD_CELL *cell);
int ptmod_set_pattern_cell_note(int pattern, int row, int channel, int period, int sample);
int ptmod_set_pattern_cell_value(int pattern, int row, int channel, int field, int value);
int ptmod_clear_pattern(int pattern);
int ptmod_clone_pattern(int sourcePattern, int destPattern);
int ptmod_insert_pattern_row(int pattern, int row);
int ptmod_delete_pattern_row(int pattern, int row);
int ptmod_transpose_pattern_block(int pattern, int rowStart, int rowEnd,
	int channelStart, int channelEnd, int semitones);
int ptmod_get_row(int channel, size_t frame, PTMOD_CELL *cell, int *orderIndex, int *pattern, int *row);
int ptmod_set_row_value(int channel, size_t frame, int field, int value);
int ptmod_get_sample(int index, PTMOD_SAMPLE *sample);
int ptmod_set_sample_value(int index, int field, int value);
int ptmod_set_title(const char *title);
int ptmod_set_sample_name(int index, const char *name);
void ptmod_default_sample_import_options(PTMOD_SAMPLE_IMPORT_OPTIONS *options);
int ptmod_replace_sample_from_file(int index, const char *path, char *error, size_t errorSize);
int ptmod_replace_sample_from_file_with_options(int index, const char *path,
	const PTMOD_SAMPLE_IMPORT_OPTIONS *options, char *error, size_t errorSize);
int ptmod_delete_sample(int index);
int ptmod_export_sample_to_file(int index, const char *path, char *error, size_t errorSize);
int ptmod_crop_sample(int index, unsigned start, unsigned end, char *error, size_t errorSize);
int ptmod_trim_sample(int index, int threshold, char *error, size_t errorSize);
int ptmod_set_sample_loop(int index, unsigned loopStart, unsigned loopLength);
int ptmod_resample_sample(int index, unsigned sourceRate, unsigned targetRate, char *error, size_t errorSize);
int ptmod_clamp_effect_param(int effect, int param);
int ptmod_validate_effect_param(int effect, int param, char *message, size_t messageSize);
const char *ptmod_effect_name(int effect);
void ptmod_format_effect_help(int effect, int param, char *dest, size_t destSize);
int ptmod_build_image(unsigned char **data, size_t *size, char *error, size_t errorSize);
void ptmod_mark_clean(void);
int ptmod_undo_push(const char *label, char *error, size_t errorSize);
int ptmod_undo(char *error, size_t errorSize);
int ptmod_redo(char *error, size_t errorSize);
void ptmod_undo_cancel_last(void);
void ptmod_undo_clear(void);
int ptmod_undo_available(void);
int ptmod_redo_available(void);

#endif
