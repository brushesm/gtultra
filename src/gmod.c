//
// ProTracker 4-channel MOD state, parsing, saving, and .sng persistence.
//

#include "gmod.h"
#include "gmodplay.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

PTMOD_STATE ptmodState;

#define PTMOD_MAX_SAMPLE_BYTES 131070u
#define PTMOD_UNDO_LIMIT 64

typedef struct
{
	unsigned char *image;
	size_t size;
	char path[MAX_PATHNAME];
	char label[32];
} PTMOD_UNDO_ENTRY;

static PTMOD_UNDO_ENTRY ptmodUndoStack[PTMOD_UNDO_LIMIT];
static PTMOD_UNDO_ENTRY ptmodRedoStack[PTMOD_UNDO_LIMIT];
static int ptmodUndoCount;
static int ptmodRedoCount;

static const int ptmodPeriodTable[36] = {
	856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
	428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
	214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
};

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

static void set_error(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(ptmodState.error, sizeof ptmodState.error, fmt, args);
	va_end(args);
	ptmodState.error[sizeof ptmodState.error - 1] = 0;
	strncpy(ptmodState.status, ptmodState.error, sizeof ptmodState.status - 1);
	ptmodState.status[sizeof ptmodState.status - 1] = 0;
}

static int file_exists(const char *path)
{
	struct stat st;

	return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int path_is_safe(const char *path, char *error, size_t errorSize)
{
	size_t i;
	size_t len;

	if (!path || !path[0])
	{
		set_message(error, errorSize, "No MOD file selected");
		return 0;
	}

	len = strlen(path);
	if (len >= MAX_PATHNAME)
	{
		set_message(error, errorSize, "MOD path is too long");
		return 0;
	}

	for (i = 0; i < len; i++)
	{
		unsigned char ch = (unsigned char)path[i];
		if (ch < 0x20)
		{
			set_message(error, errorSize, "MOD path contains unsupported characters");
			return 0;
		}
	}

	return 1;
}

static const char *basename_ptr(const char *path)
{
	const char *slash;
	const char *backslash;

	if (!path)
		return "";

	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	if (backslash && (!slash || backslash > slash))
		slash = backslash;

	return slash ? slash + 1 : path;
}

static void name_from_path(char *dest, size_t destSize, const char *path)
{
	const char *base = basename_ptr(path);
	size_t len;
	const char *dot;

	if (!dest || destSize == 0)
		return;
	dest[0] = 0;
	if (!base || !base[0])
		return;

	dot = strrchr(base, '.');
	len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
	if (len >= destSize)
		len = destSize - 1;
	memcpy(dest, base, len);
	dest[len] = 0;
}

static int ends_with_ci(const char *text, const char *suffix)
{
	size_t textLen;
	size_t suffixLen;
	size_t i;

	if (!text || !suffix)
		return 0;

	textLen = strlen(text);
	suffixLen = strlen(suffix);
	if (textLen < suffixLen)
		return 0;

	text += textLen - suffixLen;
	for (i = 0; i < suffixLen; i++)
	{
		if (tolower((unsigned char)text[i]) != tolower((unsigned char)suffix[i]))
			return 0;
	}
	return 1;
}

static void free_state_samples(PTMOD_STATE *state)
{
	int i;

	if (!state)
		return;
	for (i = 0; i < PTMOD_MAX_SAMPLES; i++)
	{
		free(state->sample[i].data);
		state->sample[i].data = NULL;
	}
}

static void free_samples(void)
{
	free_state_samples(&ptmodState);
}

static void free_undo_entry(PTMOD_UNDO_ENTRY *entry)
{
	if (!entry)
		return;
	free(entry->image);
	memset(entry, 0, sizeof *entry);
}

static void clear_undo_stack(PTMOD_UNDO_ENTRY *stack, int *count)
{
	int i;

	if (!stack || !count)
		return;
	for (i = 0; i < *count; i++)
		free_undo_entry(&stack[i]);
	*count = 0;
}

void ptmod_undo_clear(void)
{
	clear_undo_stack(ptmodUndoStack, &ptmodUndoCount);
	clear_undo_stack(ptmodRedoStack, &ptmodRedoCount);
}

int ptmod_undo_available(void)
{
	return ptmodUndoCount > 0;
}

int ptmod_redo_available(void)
{
	return ptmodRedoCount > 0;
}

void ptmod_clear(void)
{
	ptmodplay_clear();
	ptmod_undo_clear();
	free_samples();
	memset(&ptmodState, 0, sizeof ptmodState);
	strcpy(ptmodState.status, "MOD off");
}

static int read_file_bytes(const char *path, unsigned char **data, size_t *size, char *error, size_t errorSize)
{
	FILE *handle;
	long fileSize;
	unsigned char *buffer;

	*data = NULL;
	*size = 0;

	handle = fopen(path, "rb");
	if (!handle)
	{
		set_message(error, errorSize, "Could not open MOD file: %s", strerror(errno));
		return 0;
	}

	if (fseek(handle, 0, SEEK_END) || (fileSize = ftell(handle)) < 0 || fseek(handle, 0, SEEK_SET))
	{
		fclose(handle);
		set_message(error, errorSize, "Could not size MOD file: %s", strerror(errno));
		return 0;
	}

	buffer = malloc((size_t)fileSize ? (size_t)fileSize : 1);
	if (!buffer)
	{
		fclose(handle);
		set_message(error, errorSize, "Out of memory loading MOD file");
		return 0;
	}

	if (fileSize && fread(buffer, 1, (size_t)fileSize, handle) != (size_t)fileSize)
	{
		free(buffer);
		fclose(handle);
		set_message(error, errorSize, "Could not read MOD file: %s", strerror(errno));
		return 0;
	}

	fclose(handle);
	*data = buffer;
	*size = (size_t)fileSize;
	return 1;
}

static unsigned read_be16(const unsigned char *data)
{
	return ((unsigned)data[0] << 8) | data[1];
}

static unsigned read_be32(const unsigned char *data)
{
	return ((unsigned)data[0] << 24) | ((unsigned)data[1] << 16) |
		((unsigned)data[2] << 8) | data[3];
}

static unsigned read_le16_bytes(const unsigned char *data)
{
	return data[0] | ((unsigned)data[1] << 8);
}

static unsigned read_le32_bytes(const unsigned char *data)
{
	return data[0] | ((unsigned)data[1] << 8) | ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void write_be16_bytes(unsigned char *dest, unsigned value)
{
	dest[0] = (unsigned char)((value >> 8) & 0xff);
	dest[1] = (unsigned char)(value & 0xff);
}

static void write_le16_bytes(unsigned char *dest, unsigned value)
{
	dest[0] = (unsigned char)(value & 0xff);
	dest[1] = (unsigned char)((value >> 8) & 0xff);
}

static void write_le32_bytes(unsigned char *dest, unsigned value)
{
	dest[0] = (unsigned char)(value & 0xff);
	dest[1] = (unsigned char)((value >> 8) & 0xff);
	dest[2] = (unsigned char)((value >> 16) & 0xff);
	dest[3] = (unsigned char)((value >> 24) & 0xff);
}

static void copy_mod_text(char *dest, size_t destSize, const unsigned char *src, size_t srcSize)
{
	size_t len = 0;

	if (!dest || destSize == 0)
		return;

	while (len < srcSize && src[len])
		len++;
	if (len >= destSize)
		len = destSize - 1;
	memcpy(dest, src, len);
	dest[len] = 0;
}

static void write_fixed_text(unsigned char *dest, size_t destSize, const char *text)
{
	size_t len = text ? strlen(text) : 0;

	memset(dest, 0, destSize);
	if (len > destSize)
		len = destSize;
	memcpy(dest, text, len);
}

static void copy_user_text(char *dest, size_t destSize, const char *text)
{
	size_t i = 0;

	if (!dest || destSize == 0)
		return;
	if (!text)
		text = "";
	while (text[i] && i < destSize - 1)
	{
		unsigned char ch = (unsigned char)text[i];

		dest[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : ' ';
		i++;
	}
	dest[i] = 0;
}

static int is_supported_4ch_magic(const unsigned char *magic)
{
	return !memcmp(magic, "M.K.", 4) ||
		!memcmp(magic, "M!K!", 4) ||
		!memcmp(magic, "4CHN", 4) ||
		!memcmp(magic, "FLT4", 4);
}

static int validate_order_table(const unsigned char *order, int songLength, int *patternCount, char *error, size_t errorSize)
{
	int i;
	int highest = 0;

	if (songLength < 1 || songLength > PTMOD_MAX_ORDERS)
	{
		set_message(error, errorSize, "MOD song length %d is outside 1-128", songLength);
		return 0;
	}

	for (i = 0; i < songLength; i++)
	{
		if (order[i] >= PTMOD_MAX_PATTERNS)
		{
			set_message(error, errorSize, "MOD order %d references unsupported pattern %d", i, order[i]);
			return 0;
		}
		if (order[i] > highest)
			highest = order[i];
	}

	*patternCount = highest + 1;
	return 1;
}

static int parse_mod_image_into(PTMOD_STATE *state, const char *path, const unsigned char *data, size_t size, char *error, size_t errorSize)
{
	size_t sampleDataOffset;
	size_t requiredSize;
	size_t sampleBytes = 0;
	int i;
	int p;
	int row;
	int channel;
	int patternCount;
	int orderPatternCount;

	if (!state)
	{
		set_message(error, errorSize, "Internal MOD parser error");
		return 0;
	}
	memset(state, 0, sizeof *state);
	strcpy(state->status, "MOD off");

	if (size < 1084)
	{
		set_message(error, errorSize, "MOD file is too small");
		return 0;
	}

	if (!is_supported_4ch_magic(data + 1080))
	{
		set_message(error, errorSize, "Only 31-sample 4-channel ProTracker MOD files are supported");
		return 0;
	}

	if (!validate_order_table(data + 952, data[950], &orderPatternCount, error, errorSize))
		return 0;

	for (i = 0; i < PTMOD_MAX_SAMPLES; i++)
	{
		const unsigned char *header = data + 20 + i * 30;
		unsigned length = read_be16(header + 22) * 2;
		unsigned loopStart = read_be16(header + 26) * 2;
		unsigned loopLength = read_be16(header + 28) * 2;
		int finetune = header[24] & 0x0f;
		int volume = header[25];

		if (finetune >= 8)
			finetune -= 16;
		if (volume > 64)
		{
			set_message(error, errorSize, "MOD sample %d has invalid volume %d", i + 1, volume);
			return 0;
		}
		if (loopStart > length)
			loopStart = length;
		if (loopStart + loopLength > length)
			loopLength = length - loopStart;

		sampleBytes += length;
	}

	if (sampleBytes > size - 1084)
	{
		set_message(error, errorSize, "MOD sample data is larger than the file");
		return 0;
	}

	patternCount = orderPatternCount;
	{
		size_t storedPatternBytes = size - 1084 - sampleBytes;

		if ((storedPatternBytes % 1024) == 0)
		{
			size_t storedPatternCount = storedPatternBytes / 1024;

			if (storedPatternCount < (size_t)orderPatternCount)
			{
				set_message(error, errorSize, "MOD file is truncated before the referenced patterns");
				return 0;
			}
			if (storedPatternCount > PTMOD_MAX_PATTERNS)
			{
				set_message(error, errorSize, "MOD contains unsupported pattern count %lu",
					(unsigned long)storedPatternCount);
				return 0;
			}
			patternCount = (int)storedPatternCount;
		}
	}

	sampleDataOffset = 1084 + (size_t)patternCount * 1024;
	requiredSize = sampleDataOffset + sampleBytes;
	if (requiredSize > size)
	{
		set_message(error, errorSize, "MOD file is truncated: need %lu bytes, found %lu",
			(unsigned long)requiredSize, (unsigned long)size);
		return 0;
	}

	copy_mod_text(state->title, sizeof state->title, data, PTMOD_TITLE_LEN);
	state->songLength = data[950];
	state->restartPosition = data[951];
	if (state->restartPosition < 0 || state->restartPosition >= state->songLength)
		state->restartPosition = 0;
	state->patternCount = patternCount;
	memcpy(state->order, data + 952, PTMOD_MAX_ORDERS);

	for (i = 0; i < PTMOD_MAX_SAMPLES; i++)
	{
		const unsigned char *header = data + 20 + i * 30;
		PTMOD_SAMPLE *sample = &state->sample[i];

		copy_mod_text(sample->name, sizeof sample->name, header, PTMOD_SAMPLE_NAME_LEN);
		sample->length = read_be16(header + 22) * 2;
		sample->finetune = header[24] & 0x0f;
		if (sample->finetune >= 8)
			sample->finetune -= 16;
		sample->volume = header[25];
		sample->loopStart = read_be16(header + 26) * 2;
		sample->loopLength = read_be16(header + 28) * 2;
		if (sample->loopStart > sample->length)
			sample->loopStart = sample->length;
		if (sample->loopStart + sample->loopLength > sample->length)
			sample->loopLength = sample->length - sample->loopStart;
	}

	for (p = 0; p < patternCount; p++)
	{
		const unsigned char *patternData = data + 1084 + p * 1024;

		for (row = 0; row < PTMOD_ROWS; row++)
		{
			for (channel = 0; channel < PTMOD_CHANNELS; channel++)
			{
				const unsigned char *cellData = patternData + (row * PTMOD_CHANNELS + channel) * 4;
				PTMOD_CELL *cell = &state->pattern[p][row][channel];

				cell->sample = (cellData[0] & 0xf0) | (cellData[2] >> 4);
				cell->period = ((cellData[0] & 0x0f) << 8) | cellData[1];
				cell->effect = cellData[2] & 0x0f;
				cell->param = cellData[3];
			}
		}
	}

	for (i = 0; i < PTMOD_MAX_SAMPLES; i++)
	{
		PTMOD_SAMPLE *sample = &state->sample[i];

		if (sample->length)
		{
			sample->data = malloc(sample->length);
			if (!sample->data)
			{
				free_state_samples(state);
				memset(state, 0, sizeof *state);
				set_message(error, errorSize, "Out of memory loading MOD sample data");
				return 0;
			}
			memcpy(sample->data, data + sampleDataOffset, sample->length);
			sampleDataOffset += sample->length;
		}
	}

	strncpy(state->path, path, sizeof state->path - 1);
	state->path[sizeof state->path - 1] = 0;
	state->enabled = 1;
	state->valid = 1;
	state->dirty = 0;
	set_message(state->status, sizeof state->status,
		"MOD loaded: %s (%d orders, %d patterns)", basename_ptr(path),
		state->songLength, state->patternCount);
	set_message(error, errorSize, "%s", state->status);
	return 1;
}

int ptmod_load_source(const char *path, char *error, size_t errorSize)
{
	unsigned char *data;
	size_t size;
	char pathError[256];
	PTMOD_STATE loadedState;
	int ok;

	if (!path_is_safe(path, pathError, sizeof pathError))
	{
		set_error("%s", pathError);
		set_message(error, errorSize, "%s", pathError);
		return 0;
	}

	if (!ends_with_ci(path, ".mod"))
	{
		set_error("Select a ProTracker .mod file");
		set_message(error, errorSize, "%s", ptmodState.error);
		return 0;
	}

	if (!file_exists(path))
	{
		set_error("MOD file does not exist");
		set_message(error, errorSize, "%s", ptmodState.error);
		return 0;
	}

	if (!read_file_bytes(path, &data, &size, error, errorSize))
	{
		set_error("%s", (error && error[0]) ? error : "Could not load MOD file");
		return 0;
	}

	ok = parse_mod_image_into(&loadedState, path, data, size, error, errorSize);
	free(data);
	if (!ok)
	{
		set_error("%s", (error && error[0]) ? error : "Could not load MOD file");
		return 0;
	}

	ptmod_clear();
	ptmodState = loadedState;
	memset(&loadedState, 0, sizeof loadedState);
	{
		char playError[256];

		if (!ptmodplay_load_current(playError, sizeof playError))
			set_message(ptmodState.status, sizeof ptmodState.status, "%s; %s", ptmodState.status, playError);
	}
	return 1;
}

int ptmod_restore(const char *path, int enabled, char *error, size_t errorSize)
{
	if (!enabled || !path || !path[0])
	{
		ptmod_clear();
		return 1;
	}

	return ptmod_load_source(path, error, errorSize);
}

int ptmod_has_path(void)
{
	return ptmodState.path[0] != 0;
}

int ptmod_is_dirty(void)
{
	return ptmodState.valid && ptmodState.dirty;
}

static int clamp_song_length(int length)
{
	if (length < 1)
		return 1;
	if (length > PTMOD_MAX_ORDERS)
		return PTMOD_MAX_ORDERS;
	return length;
}

static void mark_dirty_and_reload(void)
{
	ptmodState.dirty = 1;
	ptmodplay_reload_if_dirty();
}

static int ensure_pattern_count_for_edit(int pattern)
{
	int oldCount;

	if (!ptmodState.valid || pattern < 0 || pattern >= PTMOD_MAX_PATTERNS)
		return 0;
	if (pattern < ptmodState.patternCount)
		return 1;

	oldCount = ptmodState.patternCount;
	if (oldCount < 0)
		oldCount = 0;
	if (oldCount > PTMOD_MAX_PATTERNS)
		oldCount = PTMOD_MAX_PATTERNS;
	memset(&ptmodState.pattern[oldCount], 0,
		(size_t)(pattern + 1 - oldCount) * sizeof ptmodState.pattern[0]);
	ptmodState.patternCount = pattern + 1;
	return 1;
}

int ptmod_create_blank(void)
{
	ptmod_clear();
	ptmodState.enabled = 1;
	ptmodState.valid = 1;
	ptmodState.dirty = 1;
	ptmodState.songLength = 1;
	ptmodState.patternCount = 1;
	ptmodState.restartPosition = 0;
	ptmodState.order[0] = 0;
	strcpy(ptmodState.status, "New blank 4-channel MOD");
	ptmodplay_load_current(NULL, 0);
	return 1;
}

int ptmod_reload_current(char *error, size_t errorSize)
{
	char path[MAX_PATHNAME];

	if (!ptmodState.path[0])
	{
		set_message(error, errorSize, "No MOD path is loaded");
		return 0;
	}
	strncpy(path, ptmodState.path, sizeof path - 1);
	path[sizeof path - 1] = 0;
	return ptmod_load_source(path, error, errorSize);
}

int ptmod_ensure_pattern_count(int pattern)
{
	if (!ensure_pattern_count_for_edit(pattern))
		return 0;
	mark_dirty_and_reload();
	return 1;
}

int ptmod_set_song_length(int length)
{
	int oldLength;
	int fillPattern;
	int i;

	if (!ptmodState.valid)
		return 0;

	length = clamp_song_length(length);
	oldLength = ptmodState.songLength;
	if (oldLength < 1)
		oldLength = 1;
	if (oldLength > PTMOD_MAX_ORDERS)
		oldLength = PTMOD_MAX_ORDERS;

	if (length > oldLength)
	{
		fillPattern = ptmodState.order[oldLength - 1];
		if (fillPattern < 0 || fillPattern >= PTMOD_MAX_PATTERNS)
			fillPattern = 0;
		for (i = oldLength; i < length; i++)
			ptmodState.order[i] = (unsigned char)fillPattern;
	}
	ptmodState.songLength = length;
	if (ptmodState.restartPosition >= ptmodState.songLength)
		ptmodState.restartPosition = 0;
	mark_dirty_and_reload();
	return 1;
}

int ptmod_set_restart_position(int restartPosition)
{
	if (!ptmodState.valid || ptmodState.songLength < 1)
		return 0;
	if (restartPosition < 0)
		restartPosition = 0;
	if (restartPosition >= ptmodState.songLength)
		restartPosition = ptmodState.songLength - 1;
	ptmodState.restartPosition = restartPosition;
	mark_dirty_and_reload();
	return 1;
}

int ptmod_set_order_pattern(int orderIndex, int pattern)
{
	if (!ptmodState.valid || orderIndex < 0 || orderIndex >= ptmodState.songLength ||
		pattern < 0 || pattern >= PTMOD_MAX_PATTERNS)
		return 0;
	if (!ensure_pattern_count_for_edit(pattern))
		return 0;
	ptmodState.order[orderIndex] = (unsigned char)pattern;
	mark_dirty_and_reload();
	return 1;
}

int ptmod_insert_order(int orderIndex)
{
	int i;
	int pattern;

	if (!ptmodState.valid || ptmodState.songLength >= PTMOD_MAX_ORDERS)
		return 0;
	if (orderIndex < 0)
		orderIndex = 0;
	if (orderIndex > ptmodState.songLength)
		orderIndex = ptmodState.songLength;
	if (orderIndex < ptmodState.songLength)
		pattern = ptmodState.order[orderIndex];
	else if (ptmodState.songLength > 0)
		pattern = ptmodState.order[ptmodState.songLength - 1];
	else
		pattern = 0;
	for (i = ptmodState.songLength; i > orderIndex; i--)
		ptmodState.order[i] = ptmodState.order[i - 1];
	ptmodState.order[orderIndex] = (unsigned char)pattern;
	ptmodState.songLength++;
	if (ptmodState.restartPosition >= orderIndex)
		ptmodState.restartPosition++;
	if (ptmodState.restartPosition >= ptmodState.songLength)
		ptmodState.restartPosition = ptmodState.songLength - 1;
	mark_dirty_and_reload();
	return 1;
}

int ptmod_delete_order(int orderIndex)
{
	int i;

	if (!ptmodState.valid || ptmodState.songLength <= 1 ||
		orderIndex < 0 || orderIndex >= ptmodState.songLength)
		return 0;
	for (i = orderIndex; i < ptmodState.songLength - 1; i++)
		ptmodState.order[i] = ptmodState.order[i + 1];
	ptmodState.order[ptmodState.songLength - 1] = 0;
	ptmodState.songLength--;
	if (ptmodState.restartPosition > orderIndex)
		ptmodState.restartPosition--;
	if (ptmodState.restartPosition >= ptmodState.songLength)
		ptmodState.restartPosition = 0;
	mark_dirty_and_reload();
	return 1;
}

const char *ptmod_status_text(void)
{
	return ptmodState.status[0] ? ptmodState.status : "MOD off";
}

static size_t current_image_size(void)
{
	size_t size = 1084 + (size_t)ptmodState.patternCount * 1024;
	int i;

	for (i = 0; i < PTMOD_MAX_SAMPLES; i++)
		size += ptmodState.sample[i].length;
	return size;
}

int ptmod_build_image(unsigned char **data, size_t *size, char *error, size_t errorSize)
{
	unsigned char *image;
	size_t offset;
	int i;
	int p;
	int row;
	int channel;

	if (!data || !size)
		return 0;
	*data = NULL;
	*size = 0;

	if (!ptmodState.valid || ptmodState.patternCount < 1 || ptmodState.patternCount > PTMOD_MAX_PATTERNS)
	{
		set_message(error, errorSize, "No valid MOD is loaded");
		return 0;
	}

	*size = current_image_size();
	image = malloc(*size ? *size : 1);
	if (!image)
	{
		set_message(error, errorSize, "Out of memory building MOD image");
		return 0;
	}
	memset(image, 0, *size);

	write_fixed_text(image, PTMOD_TITLE_LEN, ptmodState.title);
	for (i = 0; i < PTMOD_MAX_SAMPLES; i++)
	{
		PTMOD_SAMPLE *sample = &ptmodState.sample[i];
		unsigned char *header = image + 20 + i * 30;
		int finetune = sample->finetune;

		if (finetune < -8)
			finetune = -8;
		if (finetune > 7)
			finetune = 7;
		if (finetune < 0)
			finetune += 16;

		write_fixed_text(header, PTMOD_SAMPLE_NAME_LEN, sample->name);
		write_be16_bytes(header + 22, sample->length / 2);
		header[24] = (unsigned char)(finetune & 0x0f);
		header[25] = (unsigned char)(sample->volume < 0 ? 0 : sample->volume > 64 ? 64 : sample->volume);
		write_be16_bytes(header + 26, sample->loopStart / 2);
		write_be16_bytes(header + 28, sample->loopLength / 2);
	}

	image[950] = (unsigned char)ptmodState.songLength;
	image[951] = (unsigned char)ptmodState.restartPosition;
	memcpy(image + 952, ptmodState.order, PTMOD_MAX_ORDERS);
	memcpy(image + 1080, "M.K.", 4);

	for (p = 0; p < ptmodState.patternCount; p++)
	{
		unsigned char *patternData = image + 1084 + p * 1024;

		for (row = 0; row < PTMOD_ROWS; row++)
		{
			for (channel = 0; channel < PTMOD_CHANNELS; channel++)
			{
				PTMOD_CELL *cell = &ptmodState.pattern[p][row][channel];
				unsigned char *cellData = patternData + (row * PTMOD_CHANNELS + channel) * 4;
				int sample = cell->sample < 0 ? 0 : cell->sample > 31 ? 31 : cell->sample;
				int period = cell->period < 0 ? 0 : cell->period > 0xfff ? 0xfff : cell->period;
				int effect = cell->effect < 0 ? 0 : cell->effect > 0x0f ? 0x0f : cell->effect;
				int param = cell->param < 0 ? 0 : cell->param > 0xff ? 0xff : cell->param;

				cellData[0] = (unsigned char)((sample & 0xf0) | ((period >> 8) & 0x0f));
				cellData[1] = (unsigned char)(period & 0xff);
				cellData[2] = (unsigned char)(((sample & 0x0f) << 4) | effect);
				cellData[3] = (unsigned char)param;
			}
		}
	}

	offset = 1084 + (size_t)ptmodState.patternCount * 1024;
	for (i = 0; i < PTMOD_MAX_SAMPLES; i++)
	{
		PTMOD_SAMPLE *sample = &ptmodState.sample[i];

		if (sample->length && sample->data)
			memcpy(image + offset, sample->data, sample->length);
		offset += sample->length;
	}

	*data = image;
	return 1;
}

static int push_undo_image(PTMOD_UNDO_ENTRY *stack, int *count,
	unsigned char *image, size_t size, const char *path, const char *label)
{
	PTMOD_UNDO_ENTRY *entry;

	if (!stack || !count || !image)
		return 0;

	if (*count >= PTMOD_UNDO_LIMIT)
	{
		free_undo_entry(&stack[0]);
		memmove(&stack[0], &stack[1], (size_t)(PTMOD_UNDO_LIMIT - 1) * sizeof stack[0]);
		*count = PTMOD_UNDO_LIMIT - 1;
	}

	entry = &stack[(*count)++];
	memset(entry, 0, sizeof *entry);
	entry->image = image;
	entry->size = size;
	strncpy(entry->path, path ? path : "", sizeof entry->path - 1);
	entry->path[sizeof entry->path - 1] = 0;
	copy_user_text(entry->label, sizeof entry->label, label ? label : "edit");
	return 1;
}

static int capture_undo_state(PTMOD_UNDO_ENTRY *stack, int *count,
	const char *label, char *error, size_t errorSize)
{
	unsigned char *image;
	size_t size;

	if (!ptmodState.valid)
		return 1;
	if (!ptmod_build_image(&image, &size, error, errorSize))
		return 0;
	if (!push_undo_image(stack, count, image, size, ptmodState.path, label))
	{
		free(image);
		set_message(error, errorSize, "Could not store MOD edit history");
		return 0;
	}
	return 1;
}

int ptmod_undo_push(const char *label, char *error, size_t errorSize)
{
	if (!capture_undo_state(ptmodUndoStack, &ptmodUndoCount, label, error, errorSize))
		return 0;
	clear_undo_stack(ptmodRedoStack, &ptmodRedoCount);
	return 1;
}

void ptmod_undo_cancel_last(void)
{
	if (ptmodUndoCount <= 0)
		return;
	free_undo_entry(&ptmodUndoStack[--ptmodUndoCount]);
}

static int ptmod_restore_from_history(PTMOD_UNDO_ENTRY *sourceStack, int *sourceCount,
	PTMOD_UNDO_ENTRY *targetStack, int *targetCount, const char *verb,
	char *error, size_t errorSize)
{
	PTMOD_UNDO_ENTRY entry;
	PTMOD_STATE restoredState;

	if (!sourceStack || !sourceCount || *sourceCount <= 0)
	{
		set_message(error, errorSize, "No MOD %s is available", verb ? verb : "history");
		return 0;
	}
	if (!capture_undo_state(targetStack, targetCount, verb && !strcmp(verb, "redo") ? "redo back" : "undo back", error, errorSize))
		return 0;

	entry = sourceStack[--(*sourceCount)];
	memset(&sourceStack[*sourceCount], 0, sizeof sourceStack[*sourceCount]);

	if (!parse_mod_image_into(&restoredState, entry.path, entry.image, entry.size, error, errorSize))
	{
		if (targetStack && targetCount && *targetCount > 0)
			free_undo_entry(&targetStack[--(*targetCount)]);
		push_undo_image(sourceStack, sourceCount, entry.image, entry.size, entry.path, entry.label);
		set_message(ptmodState.status, sizeof ptmodState.status, "%s",
			(error && error[0]) ? error : "Could not restore MOD edit history");
		return 0;
	}

	ptmodplay_clear();
	free_samples();
	ptmodState = restoredState;
	memset(&restoredState, 0, sizeof restoredState);
	ptmodState.dirty = 1;
	set_message(ptmodState.status, sizeof ptmodState.status, "MOD %s: %s",
		verb ? verb : "restore",
		entry.label[0] ? entry.label : "edit");
	set_message(error, errorSize, "%s", ptmodState.status);
	free(entry.image);
	ptmodplay_load_current(NULL, 0);
	return 1;
}

int ptmod_undo(char *error, size_t errorSize)
{
	return ptmod_restore_from_history(ptmodUndoStack, &ptmodUndoCount,
		ptmodRedoStack, &ptmodRedoCount, "undo", error, errorSize);
}

int ptmod_redo(char *error, size_t errorSize)
{
	return ptmod_restore_from_history(ptmodRedoStack, &ptmodRedoCount,
		ptmodUndoStack, &ptmodUndoCount, "redo", error, errorSize);
}

int ptmod_save_as(const char *path, char *error, size_t errorSize)
{
	unsigned char *image;
	size_t size;
	FILE *handle;

	if (!path_is_safe(path, error, errorSize))
		return 0;
	if (!ends_with_ci(path, ".mod"))
	{
		set_message(error, errorSize, "MOD save path must end in .mod");
		return 0;
	}
	if (!ptmod_build_image(&image, &size, error, errorSize))
		return 0;

	handle = fopen(path, "wb");
	if (!handle)
	{
		free(image);
		set_message(error, errorSize, "Could not write MOD file: %s", strerror(errno));
		return 0;
	}
	if (size && fwrite(image, 1, size, handle) != size)
	{
		free(image);
		fclose(handle);
		set_message(error, errorSize, "Could not write complete MOD file: %s", strerror(errno));
		return 0;
	}
	free(image);
	if (fclose(handle))
	{
		set_message(error, errorSize, "Could not close MOD file: %s", strerror(errno));
		return 0;
	}

	strncpy(ptmodState.path, path, sizeof ptmodState.path - 1);
	ptmodState.path[sizeof ptmodState.path - 1] = 0;
	ptmodState.dirty = 0;
	set_message(ptmodState.status, sizeof ptmodState.status, "MOD saved: %s", basename_ptr(path));
	set_message(error, errorSize, "%s", ptmodState.status);
	ptmodplay_load_current(NULL, 0);
	return 1;
}

int ptmod_save_current(char *error, size_t errorSize)
{
	if (!ptmodState.path[0])
	{
		set_message(error, errorSize, "No MOD path is loaded");
		return 0;
	}
	return ptmod_save_as(ptmodState.path, error, errorSize);
}

void ptmod_mark_clean(void)
{
	ptmodState.dirty = 0;
}

static int write_u8(FILE *handle, unsigned value)
{
	return handle && fputc((int)(value & 0xff), handle) != EOF;
}

static int write_u16le(FILE *handle, unsigned value)
{
	return write_u8(handle, value) && write_u8(handle, value >> 8);
}

static int write_bytes(FILE *handle, const void *data, size_t size)
{
	if (!handle || (!data && size))
		return 0;
	return size == 0 || fwrite(data, size, 1, handle) == 1;
}

static int read_u16le(FILE *handle)
{
	int lo = fgetc(handle);
	int hi = fgetc(handle);

	if (lo < 0 || hi < 0)
		return -1;
	return lo | (hi << 8);
}

int ptmod_write_sng_chunk(FILE *handle)
{
	const char *path = ptmodState.path;
	PTMOD_RUNTIME_SETTINGS runtimeSettings;
	unsigned pathLen;
	unsigned payloadLen;
	int i;

	if (!handle || !path[0])
		return 1;

	pathLen = (unsigned)strlen(path);
	if (pathLen > 0xffff)
		return 0;

	ptmodplay_get_runtime_settings(&runtimeSettings);

	payloadLen = 4 + 1 + 1 + 2 + pathLen + 1 + 2 + 1 + 1 + PTMOD_CHANNELS * 2 + PTMOD_CHANNELS;
	if (!write_u8(handle, PTMOD_SNG_CHUNK_ID) ||
		!write_u16le(handle, payloadLen) ||
		!write_bytes(handle, PTMOD_SNG_MAGIC, 4) ||
		!write_u8(handle, PTMOD_SNG_VERSION) ||
		!write_u8(handle, ptmodState.enabled ? 1 : 0) ||
		!write_u16le(handle, pathLen) ||
		!write_bytes(handle, path, pathLen) ||
		!write_u8(handle, runtimeSettings.enabled ? 1 : 0) ||
		!write_u16le(handle, (unsigned)runtimeSettings.masterVolume) ||
		!write_u8(handle, runtimeSettings.startDelayFrames & 0xff) ||
		!write_u8(handle, runtimeSettings.replayMode & 0xff))
		return 0;
	for (i = 0; i < PTMOD_CHANNELS; i++)
	{
		if (!write_u16le(handle, (unsigned)runtimeSettings.channelVolume[i]))
			return 0;
	}
	for (i = 0; i < PTMOD_CHANNELS; i++)
	{
		if (!write_u8(handle, runtimeSettings.channelMute[i] ? 1 : 0))
			return 0;
	}
	return 1;
}

int ptmod_read_sng_chunk(FILE *handle)
{
	int payloadLen;
	char magic[4];
	int version;
	int enabled;
	int pathLen;
	char path[MAX_PATHNAME];
	char error[256];
	long chunkEnd;
	PTMOD_RUNTIME_SETTINGS runtimeSettings;
	int hasRuntimeSettings = 0;
	int i;
	int ok;

	if (!handle)
		return 0;

	payloadLen = read_u16le(handle);
	if (payloadLen < 8)
		return 0;

	chunkEnd = ftell(handle) + payloadLen;
	if (fread(magic, 4, 1, handle) != 1 || memcmp(magic, PTMOD_SNG_MAGIC, 4))
	{
		fseek(handle, chunkEnd, SEEK_SET);
		return 0;
	}

	version = fgetc(handle);
	enabled = fgetc(handle);
	if (version < 1 || version > PTMOD_SNG_VERSION)
	{
		fseek(handle, chunkEnd, SEEK_SET);
		return 0;
	}

	pathLen = read_u16le(handle);
	if (pathLen < 0 || pathLen >= MAX_PATHNAME)
	{
		fseek(handle, chunkEnd, SEEK_SET);
		return 0;
	}

	if (pathLen && fread(path, (size_t)pathLen, 1, handle) != 1)
	{
		fseek(handle, chunkEnd, SEEK_SET);
		return 0;
	}
	path[pathLen] = 0;

	if (ftell(handle) + 16 <= chunkEnd)
	{
		memset(&runtimeSettings, 0, sizeof runtimeSettings);
		runtimeSettings.enabled = fgetc(handle);
		runtimeSettings.masterVolume = read_u16le(handle);
		runtimeSettings.startDelayFrames = fgetc(handle);
		runtimeSettings.replayMode = PTMOD_REPLAY_LIBXMP;
		if (version >= 2 && ftell(handle) < chunkEnd)
			runtimeSettings.replayMode = fgetc(handle);
		for (i = 0; i < PTMOD_CHANNELS; i++)
			runtimeSettings.channelVolume[i] = read_u16le(handle);
		for (i = 0; i < PTMOD_CHANNELS; i++)
			runtimeSettings.channelMute[i] = fgetc(handle);
		hasRuntimeSettings = 1;
	}
	fseek(handle, chunkEnd, SEEK_SET);

	ok = ptmod_restore(path, enabled, error, sizeof error);
	if (ok && hasRuntimeSettings)
		ptmodplay_set_runtime_settings(&runtimeSettings);
	return ok;
}

int ptmod_order_pattern(int orderIndex)
{
	if (!ptmodState.valid || orderIndex < 0 || orderIndex >= ptmodState.songLength)
		return -1;
	if (ptmodState.order[orderIndex] >= ptmodState.patternCount)
		return -1;
	return ptmodState.order[orderIndex];
}

int ptmod_get_pattern_cell(int pattern, int row, int channel, PTMOD_CELL *cell)
{
	if (!cell)
		return 0;
	memset(cell, 0, sizeof *cell);
	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount ||
		row < 0 || row >= PTMOD_ROWS || channel < 0 || channel >= PTMOD_CHANNELS)
		return 0;
	*cell = ptmodState.pattern[pattern][row][channel];
	return 1;
}

int ptmod_get_row(int channel, size_t frame, PTMOD_CELL *cell, int *orderIndex, int *pattern, int *row)
{
	int localOrder;
	int localRow;
	int localPattern;

	if (!ptmodState.valid || channel < 0 || channel >= PTMOD_CHANNELS || !cell)
		return 0;

	localOrder = (int)(frame / PTMOD_ROWS);
	localRow = (int)(frame % PTMOD_ROWS);
	if (localOrder < 0 || localOrder >= ptmodState.songLength)
		return 0;
	localPattern = ptmod_order_pattern(localOrder);
	if (localPattern < 0)
		return 0;

	if (!ptmod_get_pattern_cell(localPattern, localRow, channel, cell))
		return 0;
	if (orderIndex)
		*orderIndex = localOrder;
	if (pattern)
		*pattern = localPattern;
	if (row)
		*row = localRow;
	return 1;
}

const char *ptmod_effect_name(int effect)
{
	switch (effect & 0x0f)
	{
	case 0x0:
		return "Arpeggio";
	case 0x1:
		return "Porta up";
	case 0x2:
		return "Porta down";
	case 0x3:
		return "Tone porta";
	case 0x4:
		return "Vibrato";
	case 0x5:
		return "Tone porta+vol";
	case 0x6:
		return "Vibrato+vol";
	case 0x7:
		return "Tremolo";
	case 0x8:
		return "Panning";
	case 0x9:
		return "Sample offset";
	case 0xA:
		return "Volume slide";
	case 0xB:
		return "Position jump";
	case 0xC:
		return "Set volume";
	case 0xD:
		return "Pattern break";
	case 0xE:
		return "Extended";
	case 0xF:
		return "Speed/BPM";
	default:
		return "Effect";
	}
}

static int clamp_pattern_break_param(int param)
{
	int tens = (param >> 4) & 0x0f;
	int ones = param & 0x0f;
	int row;

	if (ones > 9)
		ones = 9;
	row = tens * 10 + ones;
	if (row > 63)
		row = 63;
	return ((row / 10) << 4) | (row % 10);
}

static int clamp_effect_param(int effect, int param)
{
	if (param < 0)
		param = 0;
	if (param > 0xff)
		param = 0xff;

	switch (effect & 0x0f)
	{
	case 0xB:
		if (param >= ptmodState.songLength && ptmodState.songLength > 0)
			param = ptmodState.songLength - 1;
		if (param > 0x7f)
			param = 0x7f;
		break;
	case 0xC:
		if (param > 0x40)
			param = 0x40;
		break;
	case 0xD:
		param = clamp_pattern_break_param(param);
		break;
	case 0xE:
	{
		int sub = (param >> 4) & 0x0f;
		int value = param & 0x0f;

		switch (sub)
		{
		case 0x0:
		case 0x3:
			value = value ? 1 : 0;
			break;
		case 0x4:
		case 0x7:
			if (value > 7)
				value = 7;
			break;
		case 0x9:
			if (value == 0)
				value = 1;
			break;
		default:
			break;
		}
		param = (sub << 4) | value;
		break;
	}
	case 0xF:
		if (param == 0)
			param = 1;
		break;
	default:
		break;
	}
	return param;
}

int ptmod_clamp_effect_param(int effect, int param)
{
	return clamp_effect_param(effect, param);
}

int ptmod_validate_effect_param(int effect, int param, char *message, size_t messageSize)
{
	int clamped = clamp_effect_param(effect, param);
	char help[96];

	ptmod_format_effect_help(effect, clamped, help, sizeof help);
	if (clamped != param)
	{
		set_message(message, messageSize, "Adjusted %X%02X to %X%02X: %s",
			effect & 0x0f, param & 0xff, effect & 0x0f, clamped & 0xff, help);
		return 0;
	}
	set_message(message, messageSize, "%s", help);
	return 1;
}

void ptmod_format_effect_help(int effect, int param, char *dest, size_t destSize)
{
	int sub = (param >> 4) & 0x0f;
	const char *range = "00-FF";

	if (!dest || destSize == 0)
		return;

	switch (effect & 0x0f)
	{
	case 0x0:
		range = "x/y notes";
		break;
	case 0x4:
	case 0x7:
		range = "speed/depth";
		break;
	case 0x5:
	case 0x6:
	case 0xA:
		range = "up/down";
		break;
	case 0xB:
		range = "order 00-7F";
		break;
	case 0xC:
		range = "volume 00-40";
		break;
	case 0xD:
		range = "row 00-63 BCD";
		break;
	case 0xE:
		switch (sub)
		{
		case 0x1:
			range = "fine porta up";
			break;
		case 0x2:
			range = "fine porta down";
			break;
		case 0x5:
			range = "finetune 0-F";
			break;
		case 0x6:
			range = "loop 0-F";
			break;
		case 0x9:
			range = "retrig 1-F";
			break;
		case 0xA:
			range = "fine vol up";
			break;
		case 0xB:
			range = "fine vol down";
			break;
		case 0xC:
			range = "cut tick 0-F";
			break;
		case 0xD:
			range = "delay tick 0-F";
			break;
		case 0xE:
			range = "delay rows 0-F";
			break;
		case 0xF:
			range = "invert loop";
			break;
		default:
			range = "Exy extended";
			break;
		}
		break;
	case 0xF:
		range = param <= 0x1f ? "speed 01-1F" : "tempo 20-FF";
		break;
	default:
		break;
	}

	snprintf(dest, destSize, "%X%02X %s (%s)", effect & 0x0f, param & 0xff,
		ptmod_effect_name(effect), range);
	dest[destSize - 1] = 0;
}

static int clamp_row_value(int field, int value)
{
	switch (field)
	{
	case PTMOD_ROW_FIELD_SAMPLE:
		if (value < 0)
			return 0;
		if (value > PTMOD_MAX_SAMPLES)
			return PTMOD_MAX_SAMPLES;
		return value;
	case PTMOD_ROW_FIELD_PERIOD:
		if (value < 0)
			return 0;
		if (value > 0x0fff)
			return 0x0fff;
		return value;
	case PTMOD_ROW_FIELD_EFFECT:
		if (value < 0)
			return 0;
		if (value > 0x0f)
			return 0x0f;
		return value;
	case PTMOD_ROW_FIELD_PARAM:
		if (value < 0)
			return 0;
		if (value > 0xff)
			return 0xff;
		return value;
	default:
		return 0;
	}
}

int ptmod_set_pattern_cell_value(int pattern, int row, int channel, int field, int value)
{
	PTMOD_CELL *cell;

	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount ||
		row < 0 || row >= PTMOD_ROWS || channel < 0 || channel >= PTMOD_CHANNELS ||
		field < 0 || field >= PTMOD_ROW_FIELD_COUNT)
		return 0;

	cell = &ptmodState.pattern[pattern][row][channel];
	value = clamp_row_value(field, value);
	switch (field)
	{
	case PTMOD_ROW_FIELD_SAMPLE:
		cell->sample = value;
		break;
	case PTMOD_ROW_FIELD_PERIOD:
		cell->period = value;
		break;
	case PTMOD_ROW_FIELD_EFFECT:
		cell->effect = value;
		cell->param = clamp_effect_param(cell->effect, cell->param);
		break;
	case PTMOD_ROW_FIELD_PARAM:
		cell->param = clamp_effect_param(cell->effect, value);
		break;
	default:
		return 0;
	}

	ptmodState.dirty = 1;
	ptmodplay_reload_if_dirty();
	return 1;
}

int ptmod_set_pattern_cell_note(int pattern, int row, int channel, int period, int sample)
{
	PTMOD_CELL *cell;

	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount ||
		row < 0 || row >= PTMOD_ROWS || channel < 0 || channel >= PTMOD_CHANNELS)
		return 0;

	cell = &ptmodState.pattern[pattern][row][channel];
	cell->period = clamp_row_value(PTMOD_ROW_FIELD_PERIOD, period);
	cell->sample = clamp_row_value(PTMOD_ROW_FIELD_SAMPLE, sample);
	ptmodState.dirty = 1;
	ptmodplay_reload_if_dirty();
	return 1;
}

int ptmod_clear_pattern(int pattern)
{
	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount)
		return 0;
	memset(ptmodState.pattern[pattern], 0, sizeof ptmodState.pattern[pattern]);
	set_message(ptmodState.status, sizeof ptmodState.status, "MOD pattern %02X cleared", pattern);
	mark_dirty_and_reload();
	return 1;
}

int ptmod_clone_pattern(int sourcePattern, int destPattern)
{
	if (!ptmodState.valid || sourcePattern < 0 || sourcePattern >= ptmodState.patternCount ||
		destPattern < 0 || destPattern >= PTMOD_MAX_PATTERNS)
		return 0;
	if (!ptmod_ensure_pattern_count(destPattern))
		return 0;
	memcpy(ptmodState.pattern[destPattern], ptmodState.pattern[sourcePattern],
		sizeof ptmodState.pattern[destPattern]);
	set_message(ptmodState.status, sizeof ptmodState.status,
		"MOD pattern %02X cloned to %02X", sourcePattern, destPattern);
	mark_dirty_and_reload();
	return 1;
}

int ptmod_insert_pattern_row(int pattern, int row)
{
	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount ||
		row < 0 || row >= PTMOD_ROWS)
		return 0;
	if (row < PTMOD_ROWS - 1)
		memmove(&ptmodState.pattern[pattern][row + 1], &ptmodState.pattern[pattern][row],
			(size_t)(PTMOD_ROWS - row - 1) * sizeof ptmodState.pattern[pattern][0]);
	memset(&ptmodState.pattern[pattern][row], 0, sizeof ptmodState.pattern[pattern][row]);
	set_message(ptmodState.status, sizeof ptmodState.status,
		"MOD pattern %02X row %02X inserted", pattern, row);
	mark_dirty_and_reload();
	return 1;
}

int ptmod_delete_pattern_row(int pattern, int row)
{
	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount ||
		row < 0 || row >= PTMOD_ROWS)
		return 0;
	if (row < PTMOD_ROWS - 1)
		memmove(&ptmodState.pattern[pattern][row], &ptmodState.pattern[pattern][row + 1],
			(size_t)(PTMOD_ROWS - row - 1) * sizeof ptmodState.pattern[pattern][0]);
	memset(&ptmodState.pattern[pattern][PTMOD_ROWS - 1], 0,
		sizeof ptmodState.pattern[pattern][PTMOD_ROWS - 1]);
	set_message(ptmodState.status, sizeof ptmodState.status,
		"MOD pattern %02X row %02X deleted", pattern, row);
	mark_dirty_and_reload();
	return 1;
}

static int nearest_period_index(int period)
{
	int best = 0;
	int bestDistance = 0x7fffffff;
	int i;

	if (period <= 0)
		return -1;
	for (i = 0; i < (int)(sizeof ptmodPeriodTable / sizeof ptmodPeriodTable[0]); i++)
	{
		int distance = period - ptmodPeriodTable[i];

		if (distance < 0)
			distance = -distance;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			best = i;
		}
	}
	return best;
}

int ptmod_transpose_pattern_block(int pattern, int rowStart, int rowEnd,
	int channelStart, int channelEnd, int semitones)
{
	int row;
	int channel;
	int changed = 0;
	int maxNote = (int)(sizeof ptmodPeriodTable / sizeof ptmodPeriodTable[0]) - 1;

	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount ||
		semitones == 0)
		return 0;
	if (rowStart > rowEnd)
	{
		int temp = rowStart;
		rowStart = rowEnd;
		rowEnd = temp;
	}
	if (channelStart > channelEnd)
	{
		int temp = channelStart;
		channelStart = channelEnd;
		channelEnd = temp;
	}
	if (rowStart < 0)
		rowStart = 0;
	if (rowEnd >= PTMOD_ROWS)
		rowEnd = PTMOD_ROWS - 1;
	if (channelStart < 0)
		channelStart = 0;
	if (channelEnd >= PTMOD_CHANNELS)
		channelEnd = PTMOD_CHANNELS - 1;

	for (row = rowStart; row <= rowEnd; row++)
	{
		for (channel = channelStart; channel <= channelEnd; channel++)
		{
			PTMOD_CELL *cell = &ptmodState.pattern[pattern][row][channel];
			int note = nearest_period_index(cell->period);

			if (note < 0)
				continue;
			note += semitones;
			if (note < 0)
				note = 0;
			if (note > maxNote)
				note = maxNote;
			if (cell->period != ptmodPeriodTable[note])
			{
				cell->period = ptmodPeriodTable[note];
				changed = 1;
			}
		}
	}
	if (!changed)
		return 0;
	set_message(ptmodState.status, sizeof ptmodState.status,
		"MOD pattern %02X rows %02X-%02X ch%d-%d transposed %+d",
		pattern, rowStart, rowEnd, channelStart + 1, channelEnd + 1, semitones);
	mark_dirty_and_reload();
	return 1;
}

int ptmod_set_row_value(int channel, size_t frame, int field, int value)
{
	int orderIndex;
	int pattern;
	int row;
	PTMOD_CELL unused;

	if (!ptmod_get_row(channel, frame, &unused, &orderIndex, &pattern, &row) ||
		field < 0 || field >= PTMOD_ROW_FIELD_COUNT)
		return 0;

	(void)orderIndex;
	return ptmod_set_pattern_cell_value(pattern, row, channel, field, value);
}

int ptmod_get_sample(int index, PTMOD_SAMPLE *sample)
{
	if (!sample || index < 0 || index >= PTMOD_MAX_SAMPLES || !ptmodState.valid)
		return 0;
	*sample = ptmodState.sample[index];
	return 1;
}

int ptmod_set_sample_value(int index, int field, int value)
{
	PTMOD_SAMPLE *sample;

	if (index < 0 || index >= PTMOD_MAX_SAMPLES || field < 0 ||
		field >= PTMOD_SAMPLE_FIELD_COUNT || !ptmodState.valid)
		return 0;

	sample = &ptmodState.sample[index];
	switch (field)
	{
	case PTMOD_SAMPLE_FIELD_FINETUNE:
		if (value < -8)
			value = -8;
		if (value > 7)
			value = 7;
		sample->finetune = value;
		break;
	case PTMOD_SAMPLE_FIELD_VOLUME:
		if (value < 0)
			value = 0;
		if (value > 64)
			value = 64;
		sample->volume = value;
		break;
	case PTMOD_SAMPLE_FIELD_LOOP_START:
		if (value < 0)
			value = 0;
		if ((unsigned)value > sample->length)
			value = (int)sample->length;
		value &= ~1;
		sample->loopStart = (unsigned)value;
		if (sample->loopStart + sample->loopLength > sample->length)
			sample->loopLength = sample->length - sample->loopStart;
		break;
	case PTMOD_SAMPLE_FIELD_LOOP_LENGTH:
		if (value < 0)
			value = 0;
		if ((unsigned)value > sample->length - sample->loopStart)
			value = (int)(sample->length - sample->loopStart);
		value &= ~1;
		sample->loopLength = (unsigned)value;
		break;
	default:
		return 0;
	}

	ptmodState.dirty = 1;
	ptmodplay_reload_if_dirty();
	return 1;
}

int ptmod_set_title(const char *title)
{
	if (!ptmodState.valid)
		return 0;
	copy_user_text(ptmodState.title, sizeof ptmodState.title, title);
	mark_dirty_and_reload();
	return 1;
}

int ptmod_set_sample_name(int index, const char *name)
{
	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
		return 0;
	copy_user_text(ptmodState.sample[index].name, sizeof ptmodState.sample[index].name, name);
	mark_dirty_and_reload();
	return 1;
}

static int pad_even_sample(unsigned char **data, size_t *size, char *error, size_t errorSize)
{
	unsigned char *padded;

	if (!data || !*data || !size)
		return 0;
	if (*size > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "MOD sample is too large; maximum is %u bytes", PTMOD_MAX_SAMPLE_BYTES);
		return 0;
	}
	if ((*size & 1) == 0)
		return 1;
	if (*size + 1 > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "MOD sample is too large after word padding");
		return 0;
	}
	padded = realloc(*data, *size + 1);
	if (!padded)
	{
		set_message(error, errorSize, "Out of memory padding MOD sample");
		return 0;
	}
	padded[*size] = 0;
	*data = padded;
	(*size)++;
	return 1;
}

void ptmod_default_sample_import_options(PTMOD_SAMPLE_IMPORT_OPTIONS *options)
{
	if (!options)
		return;
	memset(options, 0, sizeof *options);
	options->rawFormat = PTMOD_RAW_SIGNED_8;
	options->rawChannels = 1;
	options->sourceRate = 8363;
	options->targetRate = 8363;
}

static void normalize_sample_buffer(unsigned char *data, size_t size)
{
	size_t i;
	int peak = 0;

	if (!data || size == 0)
		return;
	for (i = 0; i < size; i++)
	{
		int value = (signed char)data[i];
		int absValue = value < 0 ? -value : value;

		if (absValue > peak)
			peak = absValue;
	}
	if (peak <= 0 || peak >= 127)
		return;
	for (i = 0; i < size; i++)
	{
		int value = (signed char)data[i];

		value = (value * 127) / peak;
		if (value < -128)
			value = -128;
		if (value > 127)
			value = 127;
		data[i] = (unsigned char)(value & 0xff);
	}
}

static int resample_buffer(unsigned char **data, size_t *size,
	unsigned sourceRate, unsigned targetRate, char *error, size_t errorSize)
{
	unsigned oldLength;
	unsigned newLength;
	unsigned char *resampled;
	unsigned i;

	if (!data || !*data || !size || *size == 0 || sourceRate == 0 || targetRate == 0 ||
		sourceRate == targetRate)
		return 1;
	if (*size > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "Sample is too long to resample");
		return 0;
	}
	oldLength = (unsigned)*size;
	newLength = (unsigned)(((unsigned long long)oldLength * targetRate + sourceRate / 2) / sourceRate);
	if (newLength < 2)
		newLength = 2;
	if (newLength > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "Resampled sample would exceed %u bytes", PTMOD_MAX_SAMPLE_BYTES);
		return 0;
	}
	newLength &= ~1u;
	if (!newLength)
		newLength = 2;
	resampled = malloc(newLength);
	if (!resampled)
	{
		set_message(error, errorSize, "Out of memory resampling imported sample");
		return 0;
	}
	for (i = 0; i < newLength; i++)
	{
		unsigned long long srcFixed = (unsigned long long)i * sourceRate * 65536ull / targetRate;
		unsigned srcIndex = (unsigned)(srcFixed >> 16);
		unsigned frac = (unsigned)(srcFixed & 0xffffu);
		int a;
		int b;
		int value;

		if (srcIndex >= oldLength)
			srcIndex = oldLength - 1;
		a = (signed char)(*data)[srcIndex];
		b = (signed char)(*data)[srcIndex + 1 < oldLength ? srcIndex + 1 : srcIndex];
		value = a + ((b - a) * (int)frac) / 65536;
		if (value < -128)
			value = -128;
		if (value > 127)
			value = 127;
		resampled[i] = (unsigned char)(value & 0xff);
	}
	free(*data);
	*data = resampled;
	*size = newLength;
	return 1;
}

static int apply_sample_import_options(unsigned char **sampleData, size_t *sampleSize,
	unsigned detectedRate, const PTMOD_SAMPLE_IMPORT_OPTIONS *options,
	char *error, size_t errorSize)
{
	PTMOD_SAMPLE_IMPORT_OPTIONS defaults;
	unsigned sourceRate;
	unsigned targetRate;

	if (!sampleData || !*sampleData || !sampleSize)
		return 0;
	if (!options)
	{
		ptmod_default_sample_import_options(&defaults);
		options = &defaults;
	}
	if (options->normalize)
		normalize_sample_buffer(*sampleData, *sampleSize);
	sourceRate = detectedRate ? detectedRate : options->sourceRate;
	targetRate = options->targetRate ? options->targetRate : sourceRate;
	if (options->resample && sourceRate && targetRate && sourceRate != targetRate)
	{
		if (!resample_buffer(sampleData, sampleSize, sourceRate, targetRate, error, errorSize))
			return 0;
	}
	return pad_even_sample(sampleData, sampleSize, error, errorSize);
}

static int finalize_imported_sample(unsigned char **sampleData, size_t *sampleSize,
	unsigned detectedRate, const PTMOD_SAMPLE_IMPORT_OPTIONS *options,
	char *error, size_t errorSize)
{
	if (apply_sample_import_options(sampleData, sampleSize, detectedRate, options, error, errorSize))
		return 1;
	free(sampleData ? *sampleData : NULL);
	if (sampleData)
		*sampleData = NULL;
	if (sampleSize)
		*sampleSize = 0;
	return 0;
}

static int convert_raw_sample(const unsigned char *fileData, size_t fileSize,
	const PTMOD_SAMPLE_IMPORT_OPTIONS *options, unsigned char **sampleData,
	size_t *sampleSize, char *error, size_t errorSize)
{
	PTMOD_SAMPLE_IMPORT_OPTIONS defaults;
	size_t bytesPerSample;
	size_t frames;
	unsigned char *out;
	size_t i;
	int channels;
	int rawFormat;

	*sampleData = NULL;
	*sampleSize = 0;
	if (!options)
	{
		ptmod_default_sample_import_options(&defaults);
		options = &defaults;
	}
	rawFormat = options->rawFormat;
	channels = options->rawChannels <= 1 ? 1 : options->rawChannels;
	if (channels > 8)
		channels = 8;
	bytesPerSample = (rawFormat == PTMOD_RAW_SIGNED_16_LE ||
		rawFormat == PTMOD_RAW_UNSIGNED_16_LE ||
		rawFormat == PTMOD_RAW_SIGNED_16_BE ||
		rawFormat == PTMOD_RAW_UNSIGNED_16_BE) ? 2 : 1;
	if (fileSize < bytesPerSample * (size_t)channels)
	{
		set_message(error, errorSize, "Raw sample file is empty");
		return 0;
	}
	frames = fileSize / (bytesPerSample * (size_t)channels);
	if (frames > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "Raw sample is too large; maximum is %u bytes", PTMOD_MAX_SAMPLE_BYTES);
		return 0;
	}
	out = malloc(frames ? frames : 1);
	if (!out)
	{
		set_message(error, errorSize, "Out of memory converting raw sample");
		return 0;
	}
	for (i = 0; i < frames; i++)
	{
		int sum = 0;
		int ch;

		for (ch = 0; ch < channels; ch++)
		{
			const unsigned char *src = fileData + (i * (size_t)channels + (size_t)ch) * bytesPerSample;
			int value;

			switch (rawFormat)
			{
			case PTMOD_RAW_UNSIGNED_8:
				value = (int)src[0] - 128;
				break;
			case PTMOD_RAW_SIGNED_16_LE:
				value = ((int16_t)read_le16_bytes(src)) / 256;
				break;
			case PTMOD_RAW_UNSIGNED_16_LE:
				value = ((int)read_le16_bytes(src) - 32768) / 256;
				break;
			case PTMOD_RAW_SIGNED_16_BE:
				value = ((int16_t)read_be16(src)) / 256;
				break;
			case PTMOD_RAW_UNSIGNED_16_BE:
				value = ((int)read_be16(src) - 32768) / 256;
				break;
			case PTMOD_RAW_SIGNED_8:
			default:
				value = (signed char)src[0];
				break;
			}
			sum += value;
		}
		sum /= channels;
		if (sum < -128)
			sum = -128;
		if (sum > 127)
			sum = 127;
		out[i] = (unsigned char)(sum & 0xff);
	}
	*sampleData = out;
	*sampleSize = frames;
	return 1;
}

static int convert_wav_sample(const unsigned char *fileData, size_t fileSize,
	unsigned char **sampleData, size_t *sampleSize, unsigned *sourceRate,
	char *error, size_t errorSize)
{
	size_t pos = 12;
	const unsigned char *fmt = NULL;
	const unsigned char *pcm = NULL;
	size_t fmtSize = 0;
	size_t pcmSize = 0;
	unsigned audioFormat;
	unsigned channels;
	unsigned bitsPerSample;
	unsigned blockAlign;
	unsigned sampleRate;
	size_t frames;
	unsigned char *out;
	size_t i;

	*sampleData = NULL;
	*sampleSize = 0;
	if (fileSize < 12 || memcmp(fileData, "RIFF", 4) || memcmp(fileData + 8, "WAVE", 4))
	{
		set_message(error, errorSize, "WAV sample is not a RIFF/WAVE file");
		return 0;
	}

	while (pos + 8 <= fileSize)
	{
		const unsigned char *chunk = fileData + pos;
		size_t chunkSize = read_le32_bytes(chunk + 4);
		size_t dataPos = pos + 8;
		size_t nextPos = dataPos + chunkSize + (chunkSize & 1);

		if (dataPos + chunkSize > fileSize || nextPos < dataPos)
			break;
		if (!memcmp(chunk, "fmt ", 4))
		{
			fmt = fileData + dataPos;
			fmtSize = chunkSize;
		}
		else if (!memcmp(chunk, "data", 4))
		{
			pcm = fileData + dataPos;
			pcmSize = chunkSize;
		}
		pos = nextPos;
	}

	if (!fmt || fmtSize < 16 || !pcm)
	{
		set_message(error, errorSize, "WAV sample is missing fmt or data chunks");
		return 0;
	}

	audioFormat = read_le16_bytes(fmt);
	channels = read_le16_bytes(fmt + 2);
	sampleRate = read_le32_bytes(fmt + 4);
	blockAlign = read_le16_bytes(fmt + 12);
	bitsPerSample = read_le16_bytes(fmt + 14);
	if (audioFormat != 1 || channels < 1 || channels > 8 || (bitsPerSample != 8 && bitsPerSample != 16))
	{
		set_message(error, errorSize, "WAV import supports PCM 8/16-bit mono or stereo data");
		return 0;
	}
	if (blockAlign < channels * (bitsPerSample / 8))
	{
		set_message(error, errorSize, "WAV sample has an invalid block alignment");
		return 0;
	}

	frames = pcmSize / blockAlign;
	if (frames > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "WAV sample is too long for a ProTracker sample");
		return 0;
	}

	out = malloc(frames ? frames : 1);
	if (!out)
	{
		set_message(error, errorSize, "Out of memory converting WAV sample");
		return 0;
	}

	for (i = 0; i < frames; i++)
	{
		const unsigned char *frame = pcm + i * blockAlign;
		int sum = 0;
		unsigned ch;

		for (ch = 0; ch < channels; ch++)
		{
			const unsigned char *src = frame + ch * (bitsPerSample / 8);
			int value;

			if (bitsPerSample == 8)
				value = (int)src[0] - 128;
			else
				value = ((int16_t)read_le16_bytes(src)) / 256;
			sum += value;
		}
		sum /= (int)channels;
		if (sum < -128)
			sum = -128;
		if (sum > 127)
			sum = 127;
		out[i] = (unsigned char)(sum & 0xff);
	}

	*sampleData = out;
	*sampleSize = frames;
	if (sourceRate)
		*sourceRate = sampleRate;
	return 1;
}

static int convert_8svx_sample(const unsigned char *fileData, size_t fileSize,
	unsigned char **sampleData, size_t *sampleSize, unsigned *sourceRate,
	char *error, size_t errorSize)
{
	size_t pos = 12;
	const unsigned char *vhdr = NULL;
	const unsigned char *body = NULL;
	size_t vhdrSize = 0;
	size_t bodySize = 0;
	int compression = 0;
	unsigned char *out;

	*sampleData = NULL;
	*sampleSize = 0;
	if (fileSize < 12 || memcmp(fileData, "FORM", 4) || memcmp(fileData + 8, "8SVX", 4))
	{
		set_message(error, errorSize, "8SVX sample is not an IFF FORM/8SVX file");
		return 0;
	}

	while (pos + 8 <= fileSize)
	{
		const unsigned char *chunk = fileData + pos;
		size_t chunkSize = read_be32(chunk + 4);
		size_t dataPos = pos + 8;
		size_t nextPos = dataPos + chunkSize + (chunkSize & 1);

		if (dataPos + chunkSize > fileSize || nextPos < dataPos)
			break;
		if (!memcmp(chunk, "VHDR", 4))
		{
			vhdr = fileData + dataPos;
			vhdrSize = chunkSize;
		}
		else if (!memcmp(chunk, "BODY", 4))
		{
			body = fileData + dataPos;
			bodySize = chunkSize;
		}
		pos = nextPos;
	}

	if (!body)
	{
		set_message(error, errorSize, "8SVX sample is missing a BODY chunk");
		return 0;
	}
	if (vhdr && vhdrSize >= 20)
	{
		compression = vhdr[15];
		if (sourceRate)
			*sourceRate = read_be16(vhdr + 12);
	}
	if (compression != 0)
	{
		set_message(error, errorSize, "8SVX Fibonacci-compressed samples are not supported");
		return 0;
	}
	if (bodySize > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "8SVX sample is too long for a ProTracker sample");
		return 0;
	}

	out = malloc(bodySize ? bodySize : 1);
	if (!out)
	{
		set_message(error, errorSize, "Out of memory converting 8SVX sample");
		return 0;
	}
	if (bodySize)
		memcpy(out, body, bodySize);
	*sampleData = out;
	*sampleSize = bodySize;
	return 1;
}

static int load_sample_data_from_file(const char *path, unsigned char **sampleData,
	size_t *sampleSize, const PTMOD_SAMPLE_IMPORT_OPTIONS *options,
	char *error, size_t errorSize)
{
	unsigned char *fileData;
	size_t fileSize;
	unsigned detectedRate = 0;
	int ok;

	*sampleData = NULL;
	*sampleSize = 0;
	if (!path_is_safe(path, error, errorSize))
		return 0;
	if (!file_exists(path))
	{
		set_message(error, errorSize, "Sample file does not exist");
		return 0;
	}
	if (!read_file_bytes(path, &fileData, &fileSize, error, errorSize))
		return 0;

	if (ends_with_ci(path, ".wav"))
	{
		ok = convert_wav_sample(fileData, fileSize, sampleData, sampleSize, &detectedRate, error, errorSize);
			free(fileData);
			if (!ok)
				return 0;
			return finalize_imported_sample(sampleData, sampleSize, detectedRate, options, error, errorSize);
		}
	if (ends_with_ci(path, ".iff") || ends_with_ci(path, ".8svx") ||
		(fileSize >= 12 && !memcmp(fileData, "FORM", 4) && !memcmp(fileData + 8, "8SVX", 4)))
	{
		ok = convert_8svx_sample(fileData, fileSize, sampleData, sampleSize, &detectedRate, error, errorSize);
			free(fileData);
			if (!ok)
				return 0;
			return finalize_imported_sample(sampleData, sampleSize, detectedRate, options, error, errorSize);
		}

	ok = convert_raw_sample(fileData, fileSize, options, sampleData, sampleSize, error, errorSize);
	free(fileData);
		if (!ok)
			return 0;
		return finalize_imported_sample(sampleData, sampleSize, 0, options, error, errorSize);
	}

int ptmod_replace_sample_from_file(int index, const char *path, char *error, size_t errorSize)
{
	return ptmod_replace_sample_from_file_with_options(index, path, NULL, error, errorSize);
}

int ptmod_replace_sample_from_file_with_options(int index, const char *path,
	const PTMOD_SAMPLE_IMPORT_OPTIONS *options, char *error, size_t errorSize)
{
	PTMOD_SAMPLE *sample;
	unsigned char *data;
	size_t size;
	char sampleName[PTMOD_SAMPLE_NAME_LEN + 1];
	int volume;
	int finetune;

	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
	{
		set_message(error, errorSize, "No MOD sample slot is selected");
		return 0;
	}
	if (!load_sample_data_from_file(path, &data, &size, options, error, errorSize))
		return 0;

	sample = &ptmodState.sample[index];
	volume = sample->volume > 0 ? sample->volume : 64;
	finetune = sample->finetune;
	name_from_path(sampleName, sizeof sampleName, path);

	free(sample->data);
	memset(sample, 0, sizeof *sample);
	copy_user_text(sample->name, sizeof sample->name, sampleName);
	sample->length = (unsigned)size;
	sample->finetune = finetune;
	sample->volume = volume;
	sample->loopStart = 0;
	sample->loopLength = 0;
	sample->data = data;

	set_message(ptmodState.status, sizeof ptmodState.status, "MOD sample %02d imported: %s",
		index + 1, basename_ptr(path));
	mark_dirty_and_reload();
	set_message(error, errorSize, "%s", ptmodState.status);
	return 1;
}

int ptmod_delete_sample(int index)
{
	PTMOD_SAMPLE *sample;

	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
		return 0;
	sample = &ptmodState.sample[index];
	free(sample->data);
	memset(sample, 0, sizeof *sample);
	set_message(ptmodState.status, sizeof ptmodState.status, "MOD sample %02d deleted", index + 1);
	mark_dirty_and_reload();
	return 1;
}

static int write_sample_wav(FILE *handle, const PTMOD_SAMPLE *sample)
{
	unsigned char header[44];
	unsigned dataSize = sample ? sample->length : 0;
	unsigned i;

	memset(header, 0, sizeof header);
	memcpy(header, "RIFF", 4);
	write_le32_bytes(header + 4, 36 + dataSize);
	memcpy(header + 8, "WAVEfmt ", 8);
	write_le32_bytes(header + 16, 16);
	write_le16_bytes(header + 20, 1);
	write_le16_bytes(header + 22, 1);
	write_le32_bytes(header + 24, 8363);
	write_le32_bytes(header + 28, 8363);
	write_le16_bytes(header + 32, 1);
	write_le16_bytes(header + 34, 8);
	memcpy(header + 36, "data", 4);
	write_le32_bytes(header + 40, dataSize);
	if (fwrite(header, 1, sizeof header, handle) != sizeof header)
		return 0;
	for (i = 0; i < dataSize; i++)
	{
		unsigned char value = (unsigned char)(((int)(signed char)sample->data[i] + 128) & 0xff);
		if (fwrite(&value, 1, 1, handle) != 1)
			return 0;
	}
	return 1;
}

int ptmod_export_sample_to_file(int index, const char *path, char *error, size_t errorSize)
{
	PTMOD_SAMPLE *sample;
	FILE *handle;
	int ok;

	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
	{
		set_message(error, errorSize, "No MOD sample slot is selected");
		return 0;
	}
	if (!path_is_safe(path, error, errorSize))
		return 0;
	sample = &ptmodState.sample[index];
	if (sample->length && !sample->data)
	{
		set_message(error, errorSize, "MOD sample %02d has no data to export", index + 1);
		return 0;
	}

	handle = fopen(path, "wb");
	if (!handle)
	{
		set_message(error, errorSize, "Could not write sample file: %s", strerror(errno));
		return 0;
	}
	if (ends_with_ci(path, ".wav"))
		ok = write_sample_wav(handle, sample);
	else
		ok = sample->length == 0 || fwrite(sample->data, 1, sample->length, handle) == sample->length;
	if (!ok)
	{
		fclose(handle);
		set_message(error, errorSize, "Could not write complete sample file: %s", strerror(errno));
		return 0;
	}
	if (fclose(handle))
	{
		set_message(error, errorSize, "Could not close sample file: %s", strerror(errno));
		return 0;
	}
	set_message(error, errorSize, "MOD sample %02d exported: %s", index + 1, basename_ptr(path));
	return 1;
}

int ptmod_set_sample_loop(int index, unsigned loopStart, unsigned loopLength)
{
	PTMOD_SAMPLE *sample;

	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
		return 0;
	sample = &ptmodState.sample[index];
	if (loopStart > sample->length)
		loopStart = sample->length;
	loopStart &= ~1u;
	if (loopLength > sample->length - loopStart)
		loopLength = sample->length - loopStart;
	loopLength &= ~1u;
	sample->loopStart = loopStart;
	sample->loopLength = loopLength;
	set_message(ptmodState.status, sizeof ptmodState.status,
		"MOD sample %02d loop %u +%u", index + 1, loopStart, loopLength);
	mark_dirty_and_reload();
	return 1;
}

int ptmod_crop_sample(int index, unsigned start, unsigned end, char *error, size_t errorSize)
{
	PTMOD_SAMPLE *sample;
	unsigned length;
	unsigned char *cropped;

	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
	{
		set_message(error, errorSize, "No MOD sample slot is selected");
		return 0;
	}
	sample = &ptmodState.sample[index];
	if (!sample->data || sample->length == 0)
	{
		set_message(error, errorSize, "MOD sample %02d is empty", index + 1);
		return 0;
	}
	if (start > sample->length)
		start = sample->length;
	if (end > sample->length)
		end = sample->length;
	start &= ~1u;
	end &= ~1u;
	if (end <= start)
	{
		set_message(error, errorSize, "Sample crop range is empty");
		return 0;
	}

	length = end - start;
	cropped = malloc(length ? length : 1);
	if (!cropped)
	{
		set_message(error, errorSize, "Out of memory cropping MOD sample");
		return 0;
	}
	memcpy(cropped, sample->data + start, length);
	free(sample->data);
	sample->data = cropped;
	sample->length = length;
	if (sample->loopStart < start || sample->loopStart >= end)
	{
		sample->loopStart = 0;
		sample->loopLength = 0;
	}
	else
	{
		sample->loopStart -= start;
		if (sample->loopStart + sample->loopLength > sample->length)
			sample->loopLength = sample->length - sample->loopStart;
		sample->loopStart &= ~1u;
		sample->loopLength &= ~1u;
	}
	set_message(ptmodState.status, sizeof ptmodState.status,
		"MOD sample %02d cropped to %u bytes", index + 1, sample->length);
	mark_dirty_and_reload();
	set_message(error, errorSize, "%s", ptmodState.status);
	return 1;
}

int ptmod_trim_sample(int index, int threshold, char *error, size_t errorSize)
{
	PTMOD_SAMPLE *sample;
	unsigned start = 0;
	unsigned end;

	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
	{
		set_message(error, errorSize, "No MOD sample slot is selected");
		return 0;
	}
	sample = &ptmodState.sample[index];
	if (!sample->data || sample->length == 0)
	{
		set_message(error, errorSize, "MOD sample %02d is empty", index + 1);
		return 0;
	}
	if (threshold < 0)
		threshold = 0;
	if (threshold > 127)
		threshold = 127;

	end = sample->length;
	while (start < sample->length)
	{
		int value = (signed char)sample->data[start];
		if ((value < 0 ? -value : value) > threshold)
			break;
		start++;
	}
	while (end > start)
	{
		int value = (signed char)sample->data[end - 1];
		if ((value < 0 ? -value : value) > threshold)
			break;
		end--;
	}
	start &= ~1u;
	end &= ~1u;
	if (end <= start)
	{
		set_message(error, errorSize, "Trim would remove the whole sample");
		return 0;
	}
	return ptmod_crop_sample(index, start, end, error, errorSize);
}

int ptmod_resample_sample(int index, unsigned sourceRate, unsigned targetRate, char *error, size_t errorSize)
{
	PTMOD_SAMPLE *sample;
	unsigned newLength;
	unsigned oldLength;
	unsigned char *resampled;
	unsigned i;

	if (!ptmodState.valid || index < 0 || index >= PTMOD_MAX_SAMPLES)
	{
		set_message(error, errorSize, "No MOD sample slot is selected");
		return 0;
	}
	if (sourceRate == 0 || targetRate == 0)
	{
		set_message(error, errorSize, "Sample resample rates must be non-zero");
		return 0;
	}
	sample = &ptmodState.sample[index];
	if (!sample->data || sample->length == 0)
	{
		set_message(error, errorSize, "MOD sample %02d is empty", index + 1);
		return 0;
	}

	oldLength = sample->length;
	newLength = (unsigned)(((unsigned long long)oldLength * targetRate + sourceRate / 2) / sourceRate);
	if (newLength < 2)
		newLength = 2;
	if (newLength > PTMOD_MAX_SAMPLE_BYTES)
	{
		set_message(error, errorSize, "Resampled MOD sample would exceed %u bytes", PTMOD_MAX_SAMPLE_BYTES);
		return 0;
	}
	newLength &= ~1u;
	if (!newLength)
		newLength = 2;
	resampled = malloc(newLength);
	if (!resampled)
	{
		set_message(error, errorSize, "Out of memory resampling MOD sample");
		return 0;
	}

	for (i = 0; i < newLength; i++)
	{
		unsigned long long srcFixed = (unsigned long long)i * sourceRate * 65536ull / targetRate;
		unsigned srcIndex = (unsigned)(srcFixed >> 16);
		unsigned frac = (unsigned)(srcFixed & 0xffffu);
		int a;
		int b;
		int value;

		if (srcIndex >= oldLength)
			srcIndex = oldLength - 1;
		a = (signed char)sample->data[srcIndex];
		b = (signed char)sample->data[srcIndex + 1 < oldLength ? srcIndex + 1 : srcIndex];
		value = a + ((b - a) * (int)frac) / 65536;
		if (value < -128)
			value = -128;
		if (value > 127)
			value = 127;
		resampled[i] = (unsigned char)(value & 0xff);
	}

	free(sample->data);
	sample->data = resampled;
	sample->length = newLength;
	sample->loopStart = (unsigned)(((unsigned long long)sample->loopStart * targetRate + sourceRate / 2) / sourceRate) & ~1u;
	sample->loopLength = (unsigned)(((unsigned long long)sample->loopLength * targetRate + sourceRate / 2) / sourceRate) & ~1u;
	if (sample->loopStart > sample->length)
		sample->loopStart = sample->length;
	if (sample->loopStart + sample->loopLength > sample->length)
		sample->loopLength = sample->length - sample->loopStart;
	sample->loopLength &= ~1u;
	set_message(ptmodState.status, sizeof ptmodState.status,
		"MOD sample %02d resampled %u->%u Hz (%u bytes)",
		index + 1, sourceRate, targetRate, newLength);
	mark_dirty_and_reload();
	set_message(error, errorSize, "%s", ptmodState.status);
	return 1;
}

static void map_path_from_output(char *dest, size_t destSize, const char *outputPath, const char *suffix)
{
	size_t len;
	const char *dot;

	if (!dest || destSize == 0)
		return;

	dest[0] = 0;
	if (!outputPath || !outputPath[0])
		return;

	strncpy(dest, outputPath, destSize - 1);
	dest[destSize - 1] = 0;
	dot = strrchr(dest, '.');
	if (dot)
		((char *)dot)[0] = 0;

	len = strlen(dest);
	if (len + strlen(suffix) < destSize)
		strcat(dest, suffix);
}

int ptmod_write_export_artifacts(const char *outputPath, int playerAddress, int packedSize, int playAddress)
{
	char path[MAX_PATHNAME];
	FILE *handle;

	if (!ptmodState.enabled)
		return 1;

	map_path_from_output(path, sizeof path, outputPath, ".mod.map");
	if (!path[0])
		return 0;

	handle = fopen(path, "wt");
	if (!handle)
		return 0;

	fprintf(handle, "GTUltra + ProTracker MOD export map\n");
	fprintf(handle, "\n");
	fprintf(handle, "Status: %s\n", ptmod_status_text());
	fprintf(handle, "MOD path: %s\n", ptmodState.path);
	fprintf(handle, "MOD title: %s\n", ptmodState.title);
	fprintf(handle, "MOD orders: %d\n", ptmodState.songLength);
	fprintf(handle, "MOD patterns: %d\n", ptmodState.patternCount);
	fprintf(handle, "MOD samples: %d\n", PTMOD_MAX_SAMPLES);
	fprintf(handle, "\n");
	fprintf(handle, "GT player load: $%04x\n", playerAddress & 0xffff);
	fprintf(handle, "GT player play: $%04x\n", playAddress & 0xffff);
	fprintf(handle, "GT packed size: %d\n", packedSize);
	fprintf(handle, "GT packed end: $%04x\n", (playerAddress + packedSize) & 0xffff);
	fprintf(handle, "\n");
	fprintf(handle, "Host playback is mixed through the vendored libxmp player by default.\n");
	fprintf(handle, "C64 combined MOD replay export is not emitted by this build.\n");
	fclose(handle);
	return 1;
}
