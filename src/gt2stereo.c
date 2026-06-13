//
// GTUltraPro V2.0.0
// Based on source code of GOATTRACKER v2.76 Stereo
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#define GOATTRK2_C

#ifdef __WIN32__
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>

#include "goattrk2.h"
#include "bme.h"
#include "gmod.h"
#include "gmodplay.h"
#include "gorder.h"

extern char infoTextBuffer[256];
extern int hexnybble;
extern int recordmode;
extern int autoadvance;
extern char transportLoopPattern;
extern char transportLoopPatternSelectArea;
extern char songpath[MAX_PATHNAME];
extern char ptmodsamplefilter[MAX_FILENAME];
extern char ptmodsamplefilename[MAX_PATHNAME];
extern char textbuffer[MAX_PATHNAME];

static int ptmodSaveCurrentOrAs(GTOBJECT *gt);
static int ptmodSaveAs(GTOBJECT *gt);
static int ptmodConfirmDiscard(GTOBJECT *gt, const char *action);
static void ptmodResetEditorPosition(void);
static int ptmodOpenSampleEditor(GTOBJECT *gt);
static int ptmodShowEffectTemplateMenu(GTOBJECT *gt, const PTMOD_PREVIEW_STATS *stats);
static const char *ptmodFilenameFromPath(const char *path);
static int ptmodShowSampleImportOptions(GTOBJECT *gt, const char *path,
	PTMOD_SAMPLE_IMPORT_OPTIONS *options);
static void ptmodDrawOpaqueBox(int x, int y, int width, int height, int color);

typedef struct
{
	int valid;
	int rows;
	int channels;
	PTMOD_CELL cell[PTMOD_ROWS][PTMOD_CHANNELS];
} PTMOD_BLOCK_CLIPBOARD;

static PTMOD_BLOCK_CLIPBOARD ptmodBlockClipboard;

static void showStartupError(const char* message)
{
#ifdef __WIN32__
	MessageBoxA(NULL, message, "GTUltraPro startup error", MB_OK | MB_ICONERROR);
#else
	fprintf(stderr, "%s\n", message);
#endif
}

static int makeSelectorPath(char *dest, size_t destSize, const char *path, const char *name)
{
	size_t pathLen;
	size_t nameLen;
	int needsSlash;

	if (!dest || destSize == 0 || !name || !name[0])
		return 0;

	if (name[0] == '/' || name[0] == '\\' || (strlen(name) > 2 && name[1] == ':'))
	{
		if (strlen(name) >= destSize)
			return 0;
		strcpy(dest, name);
		return 1;
	}

	if (!path || !path[0])
		path = ".";

	pathLen = strlen(path);
	nameLen = strlen(name);
	needsSlash = pathLen > 0 && path[pathLen - 1] != '/' && path[pathLen - 1] != '\\';
	if (pathLen + (needsSlash ? 1 : 0) + nameLen >= destSize)
		return 0;

	strcpy(dest, path);
	if (needsSlash)
		strcat(dest, "/");
	strcat(dest, name);
	return 1;
}

static int ptmodEditableRowCount(void)
{
	PTMOD_PREVIEW_STATS stats;

	ptmodplay_get_stats(&stats);
	(void)stats;
	if (!ptmodState.valid)
		return 1;
	return 4 + PTMOD_ORDER_VISIBLE_ROWS + 4 + PTMOD_MAX_PREVIEW_CHANNELS + 8;
}

#define PTMOD_SETTINGS_FIRST_EDIT_ROW 4
#define PTMOD_SIDE_ROW_TITLE 0
#define PTMOD_SIDE_ROW_LENGTH 1
#define PTMOD_SIDE_ROW_RESTART 2
#define PTMOD_SIDE_ROW_FOLLOW 3
#define PTMOD_SIDE_ROW_ORDER_FIRST 4
#define PTMOD_STREAM_SUBCOLUMN_COUNT 6

enum
{
	PTMOD_STREAM_SUBCOLUMN_NOTE,
	PTMOD_STREAM_SUBCOLUMN_SAMPLE_HI,
	PTMOD_STREAM_SUBCOLUMN_SAMPLE_LO,
	PTMOD_STREAM_SUBCOLUMN_EFFECT,
	PTMOD_STREAM_SUBCOLUMN_PARAM_HI,
	PTMOD_STREAM_SUBCOLUMN_PARAM_LO
};

static int ptmodSideRuntimeBase(void)
{
	return PTMOD_SIDE_ROW_ORDER_FIRST + PTMOD_ORDER_VISIBLE_ROWS;
}

static int ptmodSideChannelBase(void)
{
	return ptmodSideRuntimeBase() + 4;
}

static int ptmodSideSampleBase(const PTMOD_PREVIEW_STATS *stats)
{
	int channels = stats && stats->loaded ? stats->channels : PTMOD_MAX_PREVIEW_CHANNELS;

	if (channels < 0)
		channels = 0;
	if (channels > PTMOD_MAX_PREVIEW_CHANNELS)
		channels = PTMOD_MAX_PREVIEW_CHANNELS;
	return ptmodSideChannelBase() + channels;
}

static void clampPtmodEditRow(void)
{
	int rows = ptmodEditableRowCount();

	if (rows < 1)
		rows = 1;
	if (editorInfo.ptmodEditRow < 0)
		editorInfo.ptmodEditRow = 0;
	if (editorInfo.ptmodEditRow >= rows)
		editorInfo.ptmodEditRow = rows - 1;
}

static int ptmodStreamChannelCount(const PTMOD_PREVIEW_STATS *stats)
{
	if (!stats || !stats->loaded || stats->channels <= 0)
		return 1;
	if (stats->channels > PTMOD_MAX_PREVIEW_CHANNELS)
		return PTMOD_MAX_PREVIEW_CHANNELS;
	return stats->channels;
}

static int ptmodStreamMaxRow(const PTMOD_PREVIEW_STATS *stats)
{
	if (!stats || !stats->loaded)
		return 0;
	return PTMOD_ROWS - 1;
}

static int ptmodOrderMax(const PTMOD_PREVIEW_STATS *stats)
{
	(void)stats;
	if (!ptmodState.valid || ptmodState.songLength <= 0)
		return 0;
	return ptmodState.songLength - 1;
}

static int ptmodOrderListStart(const PTMOD_PREVIEW_STATS *stats)
{
	int maxOrder = ptmodOrderMax(stats);
	int selected = editorInfo.ptmodOrderIndex;
	int start;
	int maxStart;

	if (stats && stats->loaded && stats->active && editorInfo.ptmodStreamFollow)
		selected = stats->orderIndex;
	if (selected < 0)
		selected = 0;
	if (selected > maxOrder)
		selected = maxOrder;

	start = selected - PTMOD_ORDER_VISIBLE_ROWS / 2;
	maxStart = maxOrder - PTMOD_ORDER_VISIBLE_ROWS + 1;
	if (maxStart < 0)
		maxStart = 0;
	if (start < 0)
		start = 0;
	if (start > maxStart)
		start = maxStart;
	return start;
}

static int ptmodOrderIndexFromSideRow(const PTMOD_PREVIEW_STATS *stats, int row)
{
	int orderIndex;

	if (!ptmodState.valid ||
		row < PTMOD_SIDE_ROW_ORDER_FIRST ||
		row >= PTMOD_SIDE_ROW_ORDER_FIRST + PTMOD_ORDER_VISIBLE_ROWS)
		return -1;
	orderIndex = ptmodOrderListStart(stats) + row - PTMOD_SIDE_ROW_ORDER_FIRST;
	if (orderIndex < 0 || orderIndex > ptmodOrderMax(stats))
		return -1;
	return orderIndex;
}

static void ptmodSelectOrderFromSideRow(const PTMOD_PREVIEW_STATS *stats)
{
	int orderIndex = ptmodOrderIndexFromSideRow(stats, editorInfo.ptmodEditRow);

	if (orderIndex >= 0)
	{
		editorInfo.ptmodOrderIndex = orderIndex;
		editorInfo.ptmodStreamFollow = 0;
	}
}

static int ptmodEditorOrderIndex(const PTMOD_PREVIEW_STATS *stats)
{
	int orderIndex;
	int maxOrder = ptmodOrderMax(stats);

	if (stats && stats->loaded && stats->active && editorInfo.ptmodStreamFollow)
		orderIndex = stats->orderIndex;
	else
		orderIndex = editorInfo.ptmodOrderIndex;

	if (orderIndex < 0)
		orderIndex = 0;
	if (orderIndex > maxOrder)
		orderIndex = maxOrder;
	return orderIndex;
}

static int ptmodStreamCursorRow(const PTMOD_PREVIEW_STATS *stats)
{
	if (stats && stats->loaded && stats->active && editorInfo.ptmodStreamFollow)
		return stats->row;
	return editorInfo.ptmodStreamRow;
}

static int ptmodFieldFromSubColumn(int subColumn)
{
	switch (subColumn)
	{
	case PTMOD_STREAM_SUBCOLUMN_NOTE:
		return PTMOD_ROW_FIELD_PERIOD;
	case PTMOD_STREAM_SUBCOLUMN_SAMPLE_HI:
	case PTMOD_STREAM_SUBCOLUMN_SAMPLE_LO:
		return PTMOD_ROW_FIELD_SAMPLE;
	case PTMOD_STREAM_SUBCOLUMN_EFFECT:
		return PTMOD_ROW_FIELD_EFFECT;
	case PTMOD_STREAM_SUBCOLUMN_PARAM_HI:
	case PTMOD_STREAM_SUBCOLUMN_PARAM_LO:
		return PTMOD_ROW_FIELD_PARAM;
	default:
		return PTMOD_ROW_FIELD_PERIOD;
	}
}

static int ptmodSubColumnFromField(int field)
{
	switch (field)
	{
	case PTMOD_ROW_FIELD_SAMPLE:
		return PTMOD_STREAM_SUBCOLUMN_SAMPLE_HI;
	case PTMOD_ROW_FIELD_EFFECT:
		return PTMOD_STREAM_SUBCOLUMN_EFFECT;
	case PTMOD_ROW_FIELD_PARAM:
		return PTMOD_STREAM_SUBCOLUMN_PARAM_HI;
	case PTMOD_ROW_FIELD_PERIOD:
	default:
		return PTMOD_STREAM_SUBCOLUMN_NOTE;
	}
}

static void ptmodSetStreamSubColumn(int subColumn)
{
	if (subColumn < 0)
		subColumn = 0;
	if (subColumn >= PTMOD_STREAM_SUBCOLUMN_COUNT)
		subColumn = PTMOD_STREAM_SUBCOLUMN_COUNT - 1;
	editorInfo.ptmodStreamSubColumn = subColumn;
	editorInfo.ptmodStreamField = ptmodFieldFromSubColumn(subColumn);
}

static void clampPtmodStreamCursor(const PTMOD_PREVIEW_STATS *stats)
{
	int maxRow = ptmodStreamMaxRow(stats);
	int maxChannel = ptmodStreamChannelCount(stats) - 1;
	int maxView = maxRow - VISIBLEPATTROWS + 1;
	int cursorRow;
	int fieldFromSubColumn;

	if (maxView < 0)
		maxView = 0;

	if (editorInfo.ptmodStreamRow < 0)
		editorInfo.ptmodStreamRow = 0;
	if (editorInfo.ptmodStreamRow > maxRow)
		editorInfo.ptmodStreamRow = maxRow;
	if (editorInfo.ptmodStreamView < 0)
		editorInfo.ptmodStreamView = 0;
	if (editorInfo.ptmodStreamView > maxView)
		editorInfo.ptmodStreamView = maxView;
	if (editorInfo.ptmodOrderIndex < 0)
		editorInfo.ptmodOrderIndex = 0;
	if (editorInfo.ptmodOrderIndex > ptmodOrderMax(stats))
		editorInfo.ptmodOrderIndex = ptmodOrderMax(stats);
	if (editorInfo.ptmodStreamChannel < 0)
		editorInfo.ptmodStreamChannel = 0;
	if (editorInfo.ptmodStreamChannel > maxChannel)
		editorInfo.ptmodStreamChannel = maxChannel;
	if (editorInfo.ptmodStreamField < 0)
		editorInfo.ptmodStreamField = 0;
	if (editorInfo.ptmodStreamField >= PTMOD_ROW_FIELD_COUNT)
		editorInfo.ptmodStreamField = PTMOD_ROW_FIELD_COUNT - 1;
	fieldFromSubColumn = ptmodFieldFromSubColumn(editorInfo.ptmodStreamSubColumn);
	if (editorInfo.ptmodStreamSubColumn < 0 ||
		editorInfo.ptmodStreamSubColumn >= PTMOD_STREAM_SUBCOLUMN_COUNT ||
		fieldFromSubColumn != editorInfo.ptmodStreamField)
		editorInfo.ptmodStreamSubColumn = ptmodSubColumnFromField(editorInfo.ptmodStreamField);
	editorInfo.ptmodStreamField = ptmodFieldFromSubColumn(editorInfo.ptmodStreamSubColumn);

	cursorRow = ptmodStreamCursorRow(stats);
	if (cursorRow < editorInfo.ptmodStreamView)
		editorInfo.ptmodStreamView = cursorRow;
	if (cursorRow >= editorInfo.ptmodStreamView + VISIBLEPATTROWS)
		editorInfo.ptmodStreamView = cursorRow - VISIBLEPATTROWS + 1;
}

static void ptmodMoveStreamRow(const PTMOD_PREVIEW_STATS *stats, int delta)
{
	int maxRow = ptmodStreamMaxRow(stats);
	int maxOrder = ptmodOrderMax(stats);
	int orderIndex = ptmodEditorOrderIndex(stats);
	int row = ptmodStreamCursorRow(stats);

	editorInfo.ptmodEditPage = 0;
	editorInfo.ptmodStreamFollow = 0;
	row += delta;
	while (row < 0 && orderIndex > 0)
	{
		orderIndex--;
		row += PTMOD_ROWS;
	}
	while (row > maxRow && orderIndex < maxOrder)
	{
		orderIndex++;
		row -= PTMOD_ROWS;
	}
	if (row < 0)
		row = 0;
	if (row > maxRow)
		row = maxRow;
	editorInfo.ptmodOrderIndex = orderIndex;
	editorInfo.ptmodStreamRow = row;
	clampPtmodStreamCursor(stats);
}

static void ptmodMoveOrder(const PTMOD_PREVIEW_STATS *stats, int delta)
{
	int orderIndex = ptmodEditorOrderIndex(stats) + delta;
	int maxOrder = ptmodOrderMax(stats);

	editorInfo.ptmodEditPage = 0;
	editorInfo.ptmodStreamFollow = 0;
	if (orderIndex < 0)
		orderIndex = 0;
	if (orderIndex > maxOrder)
		orderIndex = maxOrder;
	editorInfo.ptmodOrderIndex = orderIndex;
	clampPtmodStreamCursor(stats);
}

static void ptmodMoveStreamColumn(const PTMOD_PREVIEW_STATS *stats, int delta)
{
	int channels = ptmodStreamChannelCount(stats);
	int total = channels * PTMOD_STREAM_SUBCOLUMN_COUNT;
	int cursor = editorInfo.ptmodStreamChannel * PTMOD_STREAM_SUBCOLUMN_COUNT +
		editorInfo.ptmodStreamSubColumn;

	editorInfo.ptmodEditPage = 0;
	cursor += delta;
	while (cursor < 0)
		cursor += total;
	while (cursor >= total)
		cursor -= total;
	editorInfo.ptmodStreamChannel = cursor / PTMOD_STREAM_SUBCOLUMN_COUNT;
	ptmodSetStreamSubColumn(cursor % PTMOD_STREAM_SUBCOLUMN_COUNT);
	clampPtmodStreamCursor(stats);
}

static int ptmodStreamFieldWidth(int field)
{
	switch (field)
	{
	case PTMOD_ROW_FIELD_SAMPLE:
		return 2;
	case PTMOD_ROW_FIELD_PERIOD:
		return 4;
	case PTMOD_ROW_FIELD_EFFECT:
		return 1;
	case PTMOD_ROW_FIELD_PARAM:
		return 2;
	default:
		return 2;
	}
}

static int ptmodStreamFieldValue(const PTMOD_CELL *cell, int field)
{
	if (!cell)
		return 0;
	switch (field)
	{
	case PTMOD_ROW_FIELD_SAMPLE:
		return cell->sample;
	case PTMOD_ROW_FIELD_PERIOD:
		return cell->period;
	case PTMOD_ROW_FIELD_EFFECT:
		return cell->effect;
	case PTMOD_ROW_FIELD_PARAM:
		return cell->param;
	default:
		return 0;
	}
}

static const char *ptmodStreamFieldName(int field)
{
	switch (field)
	{
	case PTMOD_ROW_FIELD_PERIOD:
		return "note";
	case PTMOD_ROW_FIELD_SAMPLE:
		return "sample";
	case PTMOD_ROW_FIELD_EFFECT:
		return "effect";
	case PTMOD_ROW_FIELD_PARAM:
		return "param";
	default:
		return "field";
	}
}

static int ptmodPeriodFromGtNote(int note)
{
	static const int protrackerPeriods[] = {
		856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
		428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
		214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
	};
	int periodIndex = note - FIRSTNOTE - 12;

	if (periodIndex < 0 || periodIndex >= (int)(sizeof protrackerPeriods / sizeof protrackerPeriods[0]))
		return 0;
	return protrackerPeriods[periodIndex];
}

static int ptmodSelectedSampleNumber(void)
{
	int sample = editorInfo.ptmodSampleIndex + 1;

	if (sample < 1)
		sample = 1;
	if (sample > PTMOD_MAX_SAMPLES)
		sample = PTMOD_MAX_SAMPLES;
	return sample;
}

static int ptmodSelectedSampleIndex(void)
{
	int sampleIndex = editorInfo.ptmodSampleIndex;

	if (sampleIndex < 0)
		sampleIndex = 0;
	if (sampleIndex >= PTMOD_MAX_SAMPLES)
		sampleIndex = PTMOD_MAX_SAMPLES - 1;
	editorInfo.ptmodSampleIndex = sampleIndex;
	return sampleIndex;
}

static void ptmodSelectSampleDelta(int delta)
{
	int sampleIndex = editorInfo.ptmodSampleIndex + delta;

	while (sampleIndex < 0)
		sampleIndex += PTMOD_MAX_SAMPLES;
	while (sampleIndex >= PTMOD_MAX_SAMPLES)
		sampleIndex -= PTMOD_MAX_SAMPLES;
	editorInfo.ptmodSampleIndex = sampleIndex;
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD sample %02d selected", sampleIndex + 1);
	forceInfoLine = 1;
}

static void ptmodChangeOctave(int delta)
{
	editorInfo.epoctave += delta;
	if (editorInfo.epoctave < 0)
		editorInfo.epoctave = 0;
	if (editorInfo.epoctave > 6)
		editorInfo.epoctave = 6;
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD octave %d", editorInfo.epoctave);
	forceInfoLine = 1;
}

static int ptmodHandleSampleOctaveShortcut(void)
{
	if (shiftOrCtrlPressed)
		return 0;
	switch (rawkey)
	{
	case KEY_MINUS:
	case KEY_KPMINUS:
		ptmodSelectSampleDelta(-1);
		return 1;
	case KEY_EQUAL:
	case KEY_KPPLUS:
		ptmodSelectSampleDelta(1);
		return 1;
	case KEY_KPMULTIPLY:
		ptmodChangeOctave(1);
		return 1;
	case KEY_SLASH:
	case KEY_KPDIVIDE:
		ptmodChangeOctave(-1);
		return 1;
	default:
		break;
	}
	if (key == '+')
	{
		ptmodSelectSampleDelta(1);
		return 1;
	}
	if (key == '*')
	{
		ptmodChangeOctave(1);
		return 1;
	}
	if (key == '/')
	{
		ptmodChangeOctave(-1);
		return 1;
	}
	return 0;
}

static int ptmodPushUndo(const char *label)
{
	char error[256];

	if (ptmod_undo_push(label, error, sizeof error))
		return 1;
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error[0] ? error : "Could not capture MOD undo");
	forceInfoLine = 1;
	return 0;
}

static void ptmodCancelUndo(void)
{
	ptmod_undo_cancel_last();
}

static void ptmodShowEffectInfo(int effect, int param)
{
	char help[96];

	ptmod_format_effect_help(effect, param, help, sizeof help);
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD effect %s", help);
	forceInfoLine = 1;
}

static void ptmodToggleFollowNow(const PTMOD_PREVIEW_STATS *stats)
{
	editorInfo.ptmodEditPage = 0;
	editorInfo.ptmodStreamFollow = !editorInfo.ptmodStreamFollow;
	if (editorInfo.ptmodStreamFollow && stats && stats->active)
	{
		editorInfo.ptmodOrderIndex = stats->orderIndex;
		editorInfo.ptmodStreamRow = stats->row;
	}
	clampPtmodStreamCursor(stats);
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD follow/scroll %s",
		editorInfo.ptmodStreamFollow ? "on" : "off");
	forceInfoLine = 1;
}

static int ptmodToggleFollow(const PTMOD_PREVIEW_STATS *stats)
{
	if (rawkey != KEY_F || !ctrlpressed)
		return 0;

	ptmodToggleFollowNow(stats);
	return 1;
}

static int ptmodEditNoteKey(const PTMOD_PREVIEW_STATS *stats)
{
	int note;
	int period;
	int cursorRow;
	int orderIndex;
	int pattern;
	int sample;

	if (editorInfo.ptmodEditPage != 0 || editorInfo.ptmodStreamField != PTMOD_ROW_FIELD_PERIOD ||
		!stats || !stats->loaded || !rawkey || shiftOrCtrlPressed)
		return 0;

	note = getNote(rawkey);
	if (note < FIRSTNOTE || note > LASTNOTE)
		return 0;

	period = ptmodPeriodFromGtNote(note);
	if (!period)
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD notes are C-1..B-3; change octave");
		forceInfoLine = 1;
		return 1;
	}
	if (!recordmode)
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD record mode is off");
		forceInfoLine = 1;
		return 1;
	}

	clampPtmodStreamCursor(stats);
	cursorRow = ptmodStreamCursorRow(stats);
	orderIndex = ptmodEditorOrderIndex(stats);
	pattern = ptmod_order_pattern(orderIndex);
	sample = ptmodSelectedSampleNumber();
	if (!ptmodPushUndo("pattern note"))
		return 1;
	if (!ptmod_set_pattern_cell_note(pattern, cursorRow, editorInfo.ptmodStreamChannel, period, sample))
		return 0;

	editorInfo.ptmodEditPage = 0;
	editorInfo.ptmodStreamFollow = 0;
	editorInfo.ptmodOrderIndex = orderIndex;
	ptmodSetStreamSubColumn(PTMOD_STREAM_SUBCOLUMN_NOTE);
	snprintf(infoTextBuffer, sizeof infoTextBuffer,
		"MOD ord %02d pat %02X row %02X ch%d note period=%03X sample=%02X",
		orderIndex, pattern, cursorRow, editorInfo.ptmodStreamChannel + 1,
		period, sample);
	forceInfoLine = 1;
	if (autoadvance < 2)
		ptmodMoveStreamRow(stats, 1);
	return 1;
}

static int ptmodEditStreamHex(const PTMOD_PREVIEW_STATS *stats)
{
	PTMOD_CELL cell;
	int cursorRow;
	int orderIndex;
	int pattern;
	int oldValue;
	int width;
	int subColumn;
	int newValue;

	if (editorInfo.ptmodEditPage != 0 || editorInfo.ptmodStreamField == PTMOD_ROW_FIELD_PERIOD ||
		!stats || !stats->loaded || hexnybble < 0 || shiftOrCtrlPressed)
		return 0;

	clampPtmodStreamCursor(stats);
	cursorRow = ptmodStreamCursorRow(stats);
	orderIndex = ptmodEditorOrderIndex(stats);
	pattern = ptmod_order_pattern(orderIndex);
	if (!ptmod_get_pattern_cell(pattern, cursorRow, editorInfo.ptmodStreamChannel, &cell))
		return 0;

	width = ptmodStreamFieldWidth(editorInfo.ptmodStreamField);
	oldValue = ptmodStreamFieldValue(&cell, editorInfo.ptmodStreamField);
	subColumn = editorInfo.ptmodStreamSubColumn;
	if (subColumn == PTMOD_STREAM_SUBCOLUMN_SAMPLE_HI ||
		subColumn == PTMOD_STREAM_SUBCOLUMN_PARAM_HI)
		newValue = (oldValue & 0x0f) | ((hexnybble & 0x0f) << 4);
	else if (subColumn == PTMOD_STREAM_SUBCOLUMN_SAMPLE_LO ||
		subColumn == PTMOD_STREAM_SUBCOLUMN_PARAM_LO)
		newValue = (oldValue & 0xf0) | (hexnybble & 0x0f);
	else if (editorInfo.ptmodStreamField == PTMOD_ROW_FIELD_EFFECT)
		newValue = hexnybble & 0x0f;
	else
		newValue = ((oldValue << 4) | hexnybble) & 0xff;
	if (newValue == oldValue)
		return 1;
	if (!ptmodPushUndo("pattern field"))
		return 1;
	if (!ptmod_set_pattern_cell_value(pattern, cursorRow, editorInfo.ptmodStreamChannel,
		editorInfo.ptmodStreamField, newValue))
	{
		ptmodCancelUndo();
		return 1;
	}
	ptmod_get_pattern_cell(pattern, cursorRow, editorInfo.ptmodStreamChannel, &cell);
	newValue = ptmodStreamFieldValue(&cell, editorInfo.ptmodStreamField);

	editorInfo.ptmodEditPage = 0;
	editorInfo.ptmodStreamFollow = 0;
	editorInfo.ptmodOrderIndex = orderIndex;
	if (editorInfo.ptmodStreamField == PTMOD_ROW_FIELD_EFFECT ||
		editorInfo.ptmodStreamField == PTMOD_ROW_FIELD_PARAM)
	{
		ptmodShowEffectInfo(cell.effect, cell.param);
	}
	else
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer,
			"MOD ord %02d pat %02X row %02X ch%d %s=%0*X",
			orderIndex, pattern, cursorRow, editorInfo.ptmodStreamChannel + 1,
			ptmodStreamFieldName(editorInfo.ptmodStreamField),
			width, newValue);
		forceInfoLine = 1;
	}
	return 1;
}

static int ptmodClearStreamField(const PTMOD_PREVIEW_STATS *stats)
{
	int cursorRow;
	int orderIndex;
	int pattern;

	if (editorInfo.ptmodEditPage != 0 || !stats || !stats->loaded)
		return 0;

	clampPtmodStreamCursor(stats);
	cursorRow = ptmodStreamCursorRow(stats);
	orderIndex = ptmodEditorOrderIndex(stats);
	pattern = ptmod_order_pattern(orderIndex);
	if (!ptmodPushUndo("pattern clear"))
		return 1;
	if (!ptmod_set_pattern_cell_value(pattern, cursorRow, editorInfo.ptmodStreamChannel,
		editorInfo.ptmodStreamField, 0))
		return 0;

	editorInfo.ptmodEditPage = 0;
	editorInfo.ptmodStreamFollow = 0;
	editorInfo.ptmodOrderIndex = orderIndex;
	snprintf(infoTextBuffer, sizeof infoTextBuffer,
		"MOD ord %02d pat %02X row %02X ch%d %s cleared",
		orderIndex, pattern, cursorRow, editorInfo.ptmodStreamChannel + 1,
		ptmodStreamFieldName(editorInfo.ptmodStreamField));
	forceInfoLine = 1;
	return 1;
}

static void ptmodCurrentPatternCursor(const PTMOD_PREVIEW_STATS *stats,
	int *orderIndex, int *pattern, int *row, int *channel)
{
	int localOrder = ptmodEditorOrderIndex(stats);
	int localPattern = ptmod_order_pattern(localOrder);
	int localRow = ptmodStreamCursorRow(stats);
	int localChannel = editorInfo.ptmodStreamChannel;

	if (localRow < 0)
		localRow = 0;
	if (localRow >= PTMOD_ROWS)
		localRow = PTMOD_ROWS - 1;
	if (localChannel < 0)
		localChannel = 0;
	if (localChannel >= PTMOD_CHANNELS)
		localChannel = PTMOD_CHANNELS - 1;
	if (orderIndex)
		*orderIndex = localOrder;
	if (pattern)
		*pattern = localPattern;
	if (row)
		*row = localRow;
	if (channel)
		*channel = localChannel;
}

static int ptmodNormalizeBlockBounds(int *rowStart, int *rowEnd, int *channelStart, int *channelEnd)
{
	if (!editorInfo.ptmodBlockActive)
		return 0;
	*rowStart = editorInfo.ptmodBlockRowStart;
	*rowEnd = editorInfo.ptmodBlockRowEnd;
	*channelStart = editorInfo.ptmodBlockChannelStart;
	*channelEnd = editorInfo.ptmodBlockChannelEnd;
	if (*rowStart > *rowEnd)
	{
		int temp = *rowStart;
		*rowStart = *rowEnd;
		*rowEnd = temp;
	}
	if (*channelStart > *channelEnd)
	{
		int temp = *channelStart;
		*channelStart = *channelEnd;
		*channelEnd = temp;
	}
	if (*rowStart < 0)
		*rowStart = 0;
	if (*rowEnd >= PTMOD_ROWS)
		*rowEnd = PTMOD_ROWS - 1;
	if (*channelStart < 0)
		*channelStart = 0;
	if (*channelEnd >= PTMOD_CHANNELS)
		*channelEnd = PTMOD_CHANNELS - 1;
	return *rowStart <= *rowEnd && *channelStart <= *channelEnd;
}

static int ptmodBlockBoundsForPattern(int pattern, int *rowStart, int *rowEnd, int *channelStart, int *channelEnd)
{
	if (pattern < 0 || editorInfo.ptmodBlockPattern != pattern)
		return 0;
	return ptmodNormalizeBlockBounds(rowStart, rowEnd, channelStart, channelEnd);
}

static void ptmodSetBlockMark(int orderIndex, int pattern, int row, int channel, int wholePattern)
{
	if (wholePattern)
	{
		editorInfo.ptmodBlockActive = 1;
		editorInfo.ptmodBlockOrder = orderIndex;
		editorInfo.ptmodBlockPattern = pattern;
		editorInfo.ptmodBlockRowStart = 0;
		editorInfo.ptmodBlockRowEnd = PTMOD_ROWS - 1;
		editorInfo.ptmodBlockChannelStart = 0;
		editorInfo.ptmodBlockChannelEnd = PTMOD_CHANNELS - 1;
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD pattern %02X marked", pattern);
	}
	else if (!editorInfo.ptmodBlockActive || editorInfo.ptmodBlockPattern != pattern)
	{
		editorInfo.ptmodBlockActive = 1;
		editorInfo.ptmodBlockOrder = orderIndex;
		editorInfo.ptmodBlockPattern = pattern;
		editorInfo.ptmodBlockRowStart = row;
		editorInfo.ptmodBlockRowEnd = row;
		editorInfo.ptmodBlockChannelStart = channel;
		editorInfo.ptmodBlockChannelEnd = channel;
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD block anchor row %02X ch%d", row, channel + 1);
	}
	else
	{
		editorInfo.ptmodBlockOrder = orderIndex;
		editorInfo.ptmodBlockPattern = pattern;
		editorInfo.ptmodBlockRowEnd = row;
		editorInfo.ptmodBlockChannelEnd = channel;
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD block marked to row %02X ch%d", row, channel + 1);
	}
	forceInfoLine = 1;
}

static void ptmodFillBlockClipboard(int pattern, int rowStart, int rowEnd, int channelStart, int channelEnd)
{
	int row;
	int channel;

	memset(&ptmodBlockClipboard, 0, sizeof ptmodBlockClipboard);
	ptmodBlockClipboard.valid = 1;
	ptmodBlockClipboard.rows = rowEnd - rowStart + 1;
	ptmodBlockClipboard.channels = channelEnd - channelStart + 1;
	for (row = 0; row < ptmodBlockClipboard.rows; row++)
	{
		for (channel = 0; channel < ptmodBlockClipboard.channels; channel++)
			ptmod_get_pattern_cell(pattern, rowStart + row, channelStart + channel,
				&ptmodBlockClipboard.cell[row][channel]);
	}
}

static int ptmodCopyBlock(int pattern)
{
	int rowStart;
	int rowEnd;
	int channelStart;
	int channelEnd;

	if (!ptmodBlockBoundsForPattern(pattern, &rowStart, &rowEnd, &channelStart, &channelEnd))
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "No MOD block is marked in this pattern");
		forceInfoLine = 1;
		return 1;
	}

	ptmodFillBlockClipboard(pattern, rowStart, rowEnd, channelStart, channelEnd);
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD block copied %dx%d",
		ptmodBlockClipboard.rows, ptmodBlockClipboard.channels);
	forceInfoLine = 1;
	return 1;
}

static int ptmodCutBlock(int pattern)
{
	int rowStart;
	int rowEnd;
	int channelStart;
	int channelEnd;
	int row;
	int channel;

	if (!ptmodBlockBoundsForPattern(pattern, &rowStart, &rowEnd, &channelStart, &channelEnd))
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "No MOD block is marked in this pattern");
		forceInfoLine = 1;
		return 1;
	}
	ptmodFillBlockClipboard(pattern, rowStart, rowEnd, channelStart, channelEnd);
	if (!ptmodPushUndo("block cut"))
		return 1;
	for (row = rowStart; row <= rowEnd; row++)
	{
		for (channel = channelStart; channel <= channelEnd; channel++)
			memset(&ptmodState.pattern[pattern][row][channel], 0,
				sizeof ptmodState.pattern[pattern][row][channel]);
	}
	ptmodState.dirty = 1;
	ptmodplay_reload_if_dirty();
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD block cut");
	forceInfoLine = 1;
	return 1;
}

static int ptmodPasteBlock(int pattern, int rowStart, int channelStart)
{
	int row;
	int channel;

	if (!ptmodBlockClipboard.valid)
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD block clipboard is empty");
		forceInfoLine = 1;
		return 1;
	}
	if (pattern < 0 || rowStart < 0 || rowStart >= PTMOD_ROWS ||
		channelStart < 0 || channelStart >= PTMOD_CHANNELS)
		return 0;
	if (!ptmodPushUndo("block paste"))
		return 1;
	for (row = 0; row < ptmodBlockClipboard.rows && rowStart + row < PTMOD_ROWS; row++)
	{
		for (channel = 0; channel < ptmodBlockClipboard.channels &&
			channelStart + channel < PTMOD_CHANNELS; channel++)
		{
			ptmodState.pattern[pattern][rowStart + row][channelStart + channel] =
				ptmodBlockClipboard.cell[row][channel];
		}
	}
	ptmodState.dirty = 1;
	ptmodplay_reload_if_dirty();
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD block pasted at row %02X ch%d",
		rowStart, channelStart + 1);
	forceInfoLine = 1;
	return 1;
}

static int ptmodCloneCurrentPattern(int orderIndex, int pattern)
{
	int destPattern;

	if (pattern < 0)
		return 0;
	destPattern = ptmodState.patternCount;
	if (destPattern >= PTMOD_MAX_PATTERNS)
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "No free MOD pattern slot");
		forceInfoLine = 1;
		return 1;
	}
	if (!ptmodPushUndo("pattern clone"))
		return 1;
	if (ptmod_clone_pattern(pattern, destPattern) && ptmod_set_order_pattern(orderIndex, destPattern))
	{
		editorInfo.ptmodOrderIndex = orderIndex;
		editorInfo.ptmodBlockActive = 0;
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD pattern %02X cloned to %02X", pattern, destPattern);
		forceInfoLine = 1;
		return 1;
	}
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "Could not clone MOD pattern");
	forceInfoLine = 1;
	return 1;
}

static int ptmodHandlePatternToolKey(const PTMOD_PREVIEW_STATS *stats)
{
	int orderIndex;
	int pattern;
	int row;
	int channel;
	int rowStart;
	int rowEnd;
	int channelStart;
	int channelEnd;

	if (editorInfo.ptmodEditPage != 0 || !ptmodState.valid || !stats || !stats->loaded)
		return 0;
	ptmodCurrentPatternCursor(stats, &orderIndex, &pattern, &row, &channel);
	if (pattern < 0)
		return 0;

	if (ctrlpressed && rawkey == KEY_M)
	{
		editorInfo.ptmodScopeView = !editorInfo.ptmodScopeView;
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD scopes/meters %s",
			editorInfo.ptmodScopeView ? "on" : "off");
		forceInfoLine = 1;
		return 1;
	}
	if (ctrlpressed && rawkey == KEY_A)
	{
		ptmodSetBlockMark(orderIndex, pattern, row, channel, 1);
		return 1;
	}
	if (ctrlpressed && rawkey == KEY_B)
	{
		ptmodSetBlockMark(orderIndex, pattern, row, channel, 0);
		return 1;
	}
	if (ctrlpressed && rawkey == KEY_C)
		return ptmodCopyBlock(pattern);
	if (ctrlpressed && rawkey == KEY_X)
		return ptmodCutBlock(pattern);
	if (ctrlpressed && rawkey == KEY_V)
		return ptmodPasteBlock(pattern, row, channel);
	if (ctrlpressed && rawkey == KEY_T)
	{
		if (editorInfo.ptmodBlockPattern != pattern ||
			!ptmodNormalizeBlockBounds(&rowStart, &rowEnd, &channelStart, &channelEnd))
		{
			rowStart = rowEnd = row;
			channelStart = channelEnd = channel;
		}
		if (!ptmodPushUndo("block transpose"))
			return 1;
		if (ptmod_transpose_pattern_block(pattern, rowStart, rowEnd, channelStart, channelEnd,
			shiftpressed ? -1 : 1))
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
		else
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "No MOD notes to transpose");
		forceInfoLine = 1;
		return 1;
	}
	if (ctrlpressed && rawkey == KEY_INS)
	{
		if (!ptmodPushUndo("row insert"))
			return 1;
		if (ptmod_insert_pattern_row(pattern, row))
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
		else
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "Could not insert MOD row");
		forceInfoLine = 1;
		return 1;
	}
	if (ctrlpressed && rawkey == KEY_DEL)
	{
		if (!ptmodPushUndo("row delete"))
			return 1;
		if (ptmod_delete_pattern_row(pattern, row))
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
		else
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "Could not delete MOD row");
		forceInfoLine = 1;
		return 1;
	}
	if (ctrlpressed && rawkey == KEY_P)
	{
		if (shiftpressed)
		{
			if (!ptmodPushUndo("pattern clear"))
				return 1;
			if (ptmod_clear_pattern(pattern))
				snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
			else
				snprintf(infoTextBuffer, sizeof infoTextBuffer, "Could not clear MOD pattern");
			editorInfo.ptmodBlockActive = 0;
			forceInfoLine = 1;
			return 1;
		}
		return ptmodCloneCurrentPattern(orderIndex, pattern);
	}

	return 0;
}

static int ptmodCurrentOrderAndRow(const PTMOD_PREVIEW_STATS *stats, int startOfPattern,
	int *orderIndex, int *row)
{
	int localOrder;
	int localRow;

	if (!stats || !stats->loaded || !ptmodState.valid)
		return 0;
	if (editorInfo.ptmodEditPage == 1)
	{
		localOrder = ptmodOrderIndexFromSideRow(stats, editorInfo.ptmodEditRow);
		if (localOrder < 0)
			localOrder = ptmodEditorOrderIndex(stats);
		localRow = 0;
	}
	else
	{
		localOrder = ptmodEditorOrderIndex(stats);
		localRow = startOfPattern ? 0 : ptmodStreamCursorRow(stats);
	}
	if (localRow < 0)
		localRow = 0;
	if (localRow >= PTMOD_ROWS)
		localRow = PTMOD_ROWS - 1;
	if (orderIndex)
		*orderIndex = localOrder;
	if (row)
		*row = localRow;
	return 1;
}

static int ptmodPlayFromCursor(GTOBJECT *gt, int startOfPattern)
{
	PTMOD_PREVIEW_STATS stats;
	int orderIndex;
	int row;

	ptmodplay_get_stats(&stats);
	if (!ptmodCurrentOrderAndRow(&stats, startOfPattern, &orderIndex, &row))
		return 0;
	editorInfo.ptmodOrderIndex = orderIndex;
	editorInfo.ptmodStreamRow = row;
	editorInfo.ptmodStreamFollow = 0;
	orderPlayFromPosition(gt, row * 4, orderIndex, editorInfo.eschn, 1);
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD/SID play order %02d row %02X", orderIndex, row);
	forceInfoLine = 1;
	return 1;
}

static int ptmodToggleSelectedLoopFromMod(GTOBJECT *gt)
{
	PTMOD_PREVIEW_STATS stats;
	int rowStart;
	int rowEnd;
	int channelStart;
	int channelEnd;
	int orderIndex;

	if (transportLoopPatternSelectArea)
	{
		transportLoopPatternSelectArea = 0;
		ptmodplay_set_loop_range(0, 0, 0, 0, 0);
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD selected pattern area looping: Disabled");
		forceInfoLine = 1;
		return 1;
	}

	ptmodplay_get_stats(&stats);
	if (!ptmodState.valid || !stats.loaded || !editorInfo.ptmodBlockActive ||
		!ptmodNormalizeBlockBounds(&rowStart, &rowEnd, &channelStart, &channelEnd))
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "Mark a MOD block first (Ctrl+click or Ctrl+B)");
		forceInfoLine = 1;
		return 1;
	}
	(void)channelStart;
	(void)channelEnd;

	orderIndex = editorInfo.ptmodBlockOrder;
	if (orderIndex < 0)
		orderIndex = ptmodEditorOrderIndex(&stats);
	if (orderIndex >= ptmodState.songLength)
		orderIndex = ptmodState.songLength - 1;
	if (orderIndex < 0)
		orderIndex = 0;

	transportLoopPattern = 1;
	transportLoopPatternSelectArea = 1;
	editorInfo.ptmodOrderIndex = orderIndex;
	editorInfo.ptmodStreamRow = rowStart;
	editorInfo.ptmodStreamFollow = 0;
	editorInfo.epmarkchn = editorInfo.eschn;
	editorInfo.epmarkstart = rowStart;
	editorInfo.epmarkend = rowEnd;
	editorInfo.highlightLoopStart = rowStart;
	editorInfo.highlightLoopEnd = rowEnd;
	editorInfo.highlightLoopPatternNumber = ptmod_order_pattern(orderIndex);
	editorInfo.highlightLoopChannel = getActualChannel(editorInfo.esnum, editorInfo.epmarkchn);
	ptmodplay_set_loop_range(1, orderIndex, rowStart, orderIndex, rowEnd);
	orderPlayFromPosition(gt, rowStart * 4, orderIndex, editorInfo.eschn, 1);
	snprintf(infoTextBuffer, sizeof infoTextBuffer,
		"MOD/SID selected loop order %02d rows %02X-%02X", orderIndex, rowStart, rowEnd);
	forceInfoLine = 1;
	return 1;
}

static int ptmodEditSideHex(const PTMOD_PREVIEW_STATS *stats)
{
	int orderIndex;
	int oldPattern;
	int newPattern;

	if (editorInfo.ptmodEditPage != 1 || !ptmodState.valid ||
		hexnybble < 0 || shiftOrCtrlPressed)
		return 0;

	orderIndex = ptmodOrderIndexFromSideRow(stats, editorInfo.ptmodEditRow);
	if (orderIndex < 0)
		return 0;

	oldPattern = ptmod_order_pattern(orderIndex);
	if (oldPattern < 0)
		oldPattern = 0;
	newPattern = ((oldPattern << 4) | hexnybble) & 0x7f;
	if (newPattern == oldPattern)
		return 1;
	if (!ptmodPushUndo("order pattern"))
		return 1;
	if (!ptmod_set_order_pattern(orderIndex, newPattern))
		return 0;

	editorInfo.ptmodOrderIndex = orderIndex;
	editorInfo.ptmodStreamFollow = 0;
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD order %02d pattern %02X", orderIndex, newPattern);
	forceInfoLine = 1;
	return 1;
}

static int ptmodHandleRuntimeKey(const PTMOD_PREVIEW_STATS *stats, const PTMOD_RUNTIME_SETTINGS *runtimeSettings)
{
	int runtimeFocus = editorInfo.ptmodEditPage == 1;
	int step;
	int channel;
	int sampleRowBase;
	int sampleIndex;
	int runtimeBase = ptmodSideRuntimeBase();
	int channelBase = ptmodSideChannelBase();
	int orderIndex;
	int pattern;

	switch (rawkey)
	{
	case KEY_UP:
		if (!runtimeFocus && !shiftpressed)
			return 0;
		editorInfo.ptmodEditPage = 1;
		editorInfo.ptmodEditRow--;
		clampPtmodEditRow();
		ptmodSelectOrderFromSideRow(stats);
		return 1;
	case KEY_DOWN:
		if (!runtimeFocus && !shiftpressed)
			return 0;
		editorInfo.ptmodEditPage = 1;
		editorInfo.ptmodEditRow++;
		clampPtmodEditRow();
		ptmodSelectOrderFromSideRow(stats);
		return 1;
	case KEY_HOME:
		if (!runtimeFocus && !shiftpressed)
			return 0;
		editorInfo.ptmodEditPage = 1;
		editorInfo.ptmodEditRow = 0;
		return 1;
	case KEY_END:
		if (!runtimeFocus && !shiftpressed)
			return 0;
		editorInfo.ptmodEditPage = 1;
		editorInfo.ptmodEditRow = ptmodEditableRowCount() - 1;
		clampPtmodEditRow();
		return 1;
	case KEY_INS:
		if (!runtimeFocus || !ptmodState.valid)
			return 0;
		orderIndex = ptmodOrderIndexFromSideRow(stats, editorInfo.ptmodEditRow);
		if (orderIndex < 0)
			return 0;
		if (!ptmodPushUndo("order insert"))
			return 1;
		if (ptmod_insert_order(orderIndex))
		{
			editorInfo.ptmodOrderIndex = orderIndex;
			editorInfo.ptmodStreamFollow = 0;
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD order %02d inserted", orderIndex);
			forceInfoLine = 1;
			return 1;
		}
		return 0;
	case KEY_BACKSPACE:
	case KEY_DEL:
		if (!runtimeFocus || !ptmodState.valid)
			return 0;
		orderIndex = ptmodOrderIndexFromSideRow(stats, editorInfo.ptmodEditRow);
		if (orderIndex < 0)
			return 0;
		if (!ptmodPushUndo("order delete"))
			return 1;
		if (ptmod_delete_order(orderIndex))
		{
			if (editorInfo.ptmodOrderIndex >= ptmodState.songLength)
				editorInfo.ptmodOrderIndex = ptmodState.songLength - 1;
			if (editorInfo.ptmodOrderIndex < 0)
				editorInfo.ptmodOrderIndex = 0;
			editorInfo.ptmodStreamFollow = 0;
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD order %02d deleted", orderIndex);
			forceInfoLine = 1;
			return 1;
		}
		return 0;
	case KEY_ENTER:
	case KEY_SPACE:
		orderIndex = ptmodOrderIndexFromSideRow(stats, editorInfo.ptmodEditRow);
		if (orderIndex >= 0)
		{
			editorInfo.ptmodOrderIndex = orderIndex;
			editorInfo.ptmodEditPage = 0;
			editorInfo.ptmodStreamFollow = 0;
			clampPtmodStreamCursor(stats);
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD order %02d selected", orderIndex);
			forceInfoLine = 1;
			return 1;
		}
		if (editorInfo.ptmodEditRow == PTMOD_SIDE_ROW_FOLLOW)
		{
			ptmodToggleFollowNow(stats);
			return 1;
		}
		if (editorInfo.ptmodEditRow == runtimeBase)
		{
			ptmodplay_set_enabled(!runtimeSettings->enabled);
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD preview %s", runtimeSettings->enabled ? "off" : "on");
			forceInfoLine = 1;
			return 1;
		}
		if (editorInfo.ptmodEditRow == runtimeBase + 1)
		{
			int mode = runtimeSettings->replayMode == PTMOD_REPLAY_THC_WAVEFORM ?
				PTMOD_REPLAY_LIBXMP : PTMOD_REPLAY_THC_WAVEFORM;
			ptmodplay_set_replay_mode(mode);
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD replay %s", ptmodplay_replay_mode_name(mode));
			forceInfoLine = 1;
			return 1;
		}
		if (stats && editorInfo.ptmodEditRow >= channelBase && editorInfo.ptmodEditRow < channelBase + stats->channels)
		{
			channel = editorInfo.ptmodEditRow - channelBase;
			ptmodplay_set_channel_mute(channel, !runtimeSettings->channelMute[channel]);
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD channel %d mute %s", channel + 1,
				runtimeSettings->channelMute[channel] ? "off" : "on");
			forceInfoLine = 1;
			return 1;
		}
		return 0;
	case KEY_LEFT:
	case KEY_RIGHT:
		if (!runtimeFocus && !shiftpressed)
			return 0;
		editorInfo.ptmodEditPage = 1;
		step = ctrlpressed || (runtimeFocus && shiftpressed) ? 10 : 1;
		if (rawkey == KEY_LEFT)
			step = -step;
		if (editorInfo.ptmodEditRow == PTMOD_SIDE_ROW_LENGTH)
		{
			if (!ptmodPushUndo("song length"))
				return 1;
			ptmod_set_song_length(ptmodState.songLength + step);
			if (editorInfo.ptmodOrderIndex >= ptmodState.songLength)
				editorInfo.ptmodOrderIndex = ptmodState.songLength - 1;
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD song length %d", ptmodState.songLength);
			forceInfoLine = 1;
			return 1;
		}
		if (editorInfo.ptmodEditRow == PTMOD_SIDE_ROW_RESTART)
		{
			if (!ptmodPushUndo("restart"))
				return 1;
			ptmod_set_restart_position(ptmodState.restartPosition + step);
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD restart order %02d", ptmodState.restartPosition);
			forceInfoLine = 1;
			return 1;
		}
		orderIndex = ptmodOrderIndexFromSideRow(stats, editorInfo.ptmodEditRow);
		if (orderIndex >= 0)
		{
			pattern = ptmod_order_pattern(orderIndex);
			if (pattern < 0)
				pattern = 0;
			pattern += step;
			if (pattern < 0)
				pattern = 0;
			if (pattern >= PTMOD_MAX_PATTERNS)
				pattern = PTMOD_MAX_PATTERNS - 1;
			if (pattern == ptmod_order_pattern(orderIndex))
				return 1;
			if (!ptmodPushUndo("order pattern"))
				return 1;
			if (!ptmod_set_order_pattern(orderIndex, pattern))
				return 0;
			editorInfo.ptmodOrderIndex = orderIndex;
			editorInfo.ptmodStreamFollow = 0;
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD order %02d pattern %02X", orderIndex, pattern);
			forceInfoLine = 1;
			return 1;
		}
		if (editorInfo.ptmodEditRow == runtimeBase + 1)
		{
			int mode = runtimeSettings->replayMode == PTMOD_REPLAY_THC_WAVEFORM ?
				PTMOD_REPLAY_LIBXMP : PTMOD_REPLAY_THC_WAVEFORM;
			ptmodplay_set_replay_mode(mode);
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD replay %s", ptmodplay_replay_mode_name(mode));
			forceInfoLine = 1;
			return 1;
		}
		if (editorInfo.ptmodEditRow == runtimeBase + 2)
		{
			ptmodplay_set_master_volume(runtimeSettings->masterVolume + step * 5);
		}
		else if (editorInfo.ptmodEditRow == runtimeBase + 3)
		{
			ptmodplay_set_start_delay(runtimeSettings->startDelayFrames + step);
		}
		else if (stats && editorInfo.ptmodEditRow >= channelBase && editorInfo.ptmodEditRow < channelBase + stats->channels)
		{
			channel = editorInfo.ptmodEditRow - channelBase;
			if (channel < 0 || channel >= stats->channels || channel >= PTMOD_MAX_PREVIEW_CHANNELS)
				return 0;
			ptmodplay_set_channel_volume(channel, runtimeSettings->channelVolume[channel] + step * 5);
		}
		else if (ptmodState.valid)
		{
			PTMOD_SAMPLE sample;

			sampleRowBase = ptmodSideSampleBase(stats);
			sampleIndex = editorInfo.ptmodSampleIndex;
			if (sampleIndex < 0)
				sampleIndex = 0;
			if (sampleIndex >= PTMOD_MAX_SAMPLES)
				sampleIndex = PTMOD_MAX_SAMPLES - 1;

			if (editorInfo.ptmodEditRow == sampleRowBase)
			{
				editorInfo.ptmodSampleIndex = sampleIndex + step;
				if (editorInfo.ptmodSampleIndex < 0)
					editorInfo.ptmodSampleIndex = 0;
				if (editorInfo.ptmodSampleIndex >= PTMOD_MAX_SAMPLES)
					editorInfo.ptmodSampleIndex = PTMOD_MAX_SAMPLES - 1;
			}
			else if (ptmod_get_sample(sampleIndex, &sample))
			{
				switch (editorInfo.ptmodEditRow - sampleRowBase)
				{
				case 2:
				case 3:
				case 4:
				case 5:
					if (!ptmodPushUndo("sample setting"))
						return 1;
					if (editorInfo.ptmodEditRow - sampleRowBase == 2)
						ptmod_set_sample_value(sampleIndex, PTMOD_SAMPLE_FIELD_FINETUNE, sample.finetune + step);
					else if (editorInfo.ptmodEditRow - sampleRowBase == 3)
						ptmod_set_sample_value(sampleIndex, PTMOD_SAMPLE_FIELD_VOLUME, sample.volume + step);
					else if (editorInfo.ptmodEditRow - sampleRowBase == 4)
						ptmod_set_sample_value(sampleIndex, PTMOD_SAMPLE_FIELD_LOOP_START, (int)sample.loopStart + step * 2);
					else
						ptmod_set_sample_value(sampleIndex, PTMOD_SAMPLE_FIELD_LOOP_LENGTH, (int)sample.loopLength + step * 2);
					break;
				default:
					return 0;
				}
			}
		}
		else
		{
			return 0;
		}
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD setting changed");
		forceInfoLine = 1;
		return 1;
	default:
		return 0;
	}
}

static int consumePtmodSettingsKey(void)
{
	key = 0;
	rawkey = 0;
	return 1;
}

static int ptmodHasTextInput(void)
{
	if (ctrlpressed)
		return 0;
	return (key >= 32 && key < 127) || rawkey == KEY_BACKSPACE;
}

static int ptmodEditSideText(const PTMOD_PREVIEW_STATS *stats)
{
	char before[PTMOD_SAMPLE_NAME_LEN + 1];
	char title[PTMOD_TITLE_LEN + 1];

	(void)stats;
	if (editorInfo.ptmodEditPage != 1 || !ptmodState.valid || !ptmodHasTextInput())
		return 0;

	if (editorInfo.ptmodEditRow != PTMOD_SIDE_ROW_TITLE)
		return 0;
	strncpy(title, ptmodState.title, sizeof title - 1);
	title[sizeof title - 1] = 0;
	strncpy(before, title, sizeof before - 1);
	before[sizeof before - 1] = 0;
	editstring(title, PTMOD_TITLE_LEN + 1);
	if (!strcmp(before, title))
		return 0;
	if (!ptmodPushUndo("title"))
		return 1;
	ptmod_set_title(title);
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD title: %.20s", ptmodState.title);
	forceInfoLine = 1;
	return 1;
}

static void ptmodDefaultSampleFilename(char *dest, size_t destSize, int sampleIndex)
{
	PTMOD_SAMPLE sample;
	size_t len;

	if (!dest || destSize == 0)
		return;
	dest[0] = 0;
	if (ptmod_get_sample(sampleIndex, &sample) && sample.name[0])
	{
		size_t i;

		for (i = 0; sample.name[i] && i < destSize - 5; i++)
		{
			unsigned char ch = (unsigned char)sample.name[i];
			dest[i] = (ch >= 0x20 && ch < 0x7f && ch != '/' && ch != '\\') ? (char)ch : '_';
		}
		dest[i] = 0;
	}
	if (!dest[0])
		snprintf(dest, destSize, "sample%02d", sampleIndex + 1);
	len = strlen(dest);
	if (len + 4 < destSize && !strchr(dest, '.'))
		strcat(dest, ".raw");
}

static const char *ptmodRawFormatName(int rawFormat)
{
	switch (rawFormat)
	{
	case PTMOD_RAW_UNSIGNED_8:
		return "unsigned 8-bit";
	case PTMOD_RAW_SIGNED_16_LE:
		return "signed 16-bit LE";
	case PTMOD_RAW_UNSIGNED_16_LE:
		return "unsigned 16-bit LE";
	case PTMOD_RAW_SIGNED_16_BE:
		return "signed 16-bit BE";
	case PTMOD_RAW_UNSIGNED_16_BE:
		return "unsigned 16-bit BE";
	case PTMOD_RAW_SIGNED_8:
	default:
		return "signed 8-bit";
	}
}

static void ptmodAdjustImportOption(PTMOD_SAMPLE_IMPORT_OPTIONS *options, int row, int delta)
{
	if (!options || delta == 0)
		return;
	switch (row)
	{
	case 0:
		options->rawFormat += delta;
		while (options->rawFormat < PTMOD_RAW_SIGNED_8)
			options->rawFormat = PTMOD_RAW_UNSIGNED_16_BE;
		while (options->rawFormat > PTMOD_RAW_UNSIGNED_16_BE)
			options->rawFormat = PTMOD_RAW_SIGNED_8;
		break;
	case 1:
		options->rawChannels += delta;
		if (options->rawChannels < 1)
			options->rawChannels = 1;
		if (options->rawChannels > 8)
			options->rawChannels = 8;
		break;
	case 2:
		options->normalize = !options->normalize;
		break;
	case 3:
		options->resample = !options->resample;
		break;
	case 4:
	{
		int rate = (int)options->sourceRate + delta;

		if (rate < 1000)
			rate = 1000;
		if (rate > 96000)
			rate = 96000;
		options->sourceRate = (unsigned)rate;
		break;
	}
	case 5:
	{
		int rate = (int)options->targetRate + delta;

		if (rate < 1000)
			rate = 1000;
		if (rate > 96000)
			rate = 96000;
		options->targetRate = (unsigned)rate;
		break;
	}
	default:
		break;
	}
}

static int ptmodShowSampleImportOptions(GTOBJECT *gt, const char *path,
	PTMOD_SAMPLE_IMPORT_OPTIONS *options)
{
	int selected = 0;
	int boxW = 72;
	int boxH = 13;
	int boxX = (getactivescreencolumns() - boxW) / 2;
	int boxY = 5;

	(void)gt;
	if (!options)
		return 0;
	if (boxX < 0)
		boxX = 0;
	stopScreenDisplay();
	for (;;)
	{
		int color = getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);
		int highlight = getColor(CORDER_INST_TABLE_EDITING, CORDER_INST_BACKGROUND);
		int rateStep = ctrlpressed ? 1000 : shiftpressed ? 100 : 1;
		int i;
		const char *rows[6];
		char rowText[6][80];

		snprintf(rowText[0], sizeof rowText[0], "Raw format:   %s", ptmodRawFormatName(options->rawFormat));
		snprintf(rowText[1], sizeof rowText[1], "Raw channels: %d", options->rawChannels);
		snprintf(rowText[2], sizeof rowText[2], "Normalize:    %s", options->normalize ? "ON" : "OFF");
		snprintf(rowText[3], sizeof rowText[3], "Resample:     %s", options->resample ? "ON" : "OFF");
		snprintf(rowText[4], sizeof rowText[4], "Source Hz:    %u", options->sourceRate);
		snprintf(rowText[5], sizeof rowText[5], "Target Hz:    %u", options->targetRate);
		for (i = 0; i < 6; i++)
			rows[i] = rowText[i];

		ptmodDrawOpaqueBox(boxX, boxY, boxW, boxH,
			getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND));
		printtext(boxX + 2, boxY + 1, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND),
			"MOD SAMPLE IMPORT OPTIONS");
		snprintf(textbuffer, sizeof textbuffer, "File: %.58s", ptmodFilenameFromPath(path));
		printtext(boxX + 2, boxY + 2, color, textbuffer);
		for (i = 0; i < 6; i++)
			printtext(boxX + 4, boxY + 4 + i, selected == i ? highlight : color, rows[i]);
		printtext(boxX + 2, boxY + boxH - 2, color,
			"Left/Right changes, Shift/Ctrl changes rate step, Enter imports, Esc cancels");
		fliptoscreen();
		waitkeymousenoupdate();
		if (win_quitted || rawkey == KEY_ESC)
		{
			restartScreenDisplay();
			key = 0;
			rawkey = 0;
			return 0;
		}
		if (rawkey == KEY_ENTER || rawkey == KEY_SPACE)
		{
			restartScreenDisplay();
			key = 0;
			rawkey = 0;
			return 1;
		}
		if (rawkey == KEY_UP)
		{
			selected--;
			if (selected < 0)
				selected = 5;
		}
		else if (rawkey == KEY_DOWN)
		{
			selected++;
			if (selected > 5)
				selected = 0;
		}
		else if (rawkey == KEY_LEFT || rawkey == KEY_RIGHT)
		{
			int delta = rawkey == KEY_RIGHT ? 1 : -1;

			if (selected >= 4)
				delta *= rateStep;
			ptmodAdjustImportOption(options, selected, delta);
		}
		key = 0;
		rawkey = 0;
	}
}

static int ptmodImportSample(GTOBJECT *gt)
{
	static PTMOD_SAMPLE_IMPORT_OPTIONS importOptions;
	static int importOptionsInitialized = 0;
	char error[256];
	char path[MAX_PATHNAME];
	int sampleIndex = ptmodSelectedSampleIndex();

	if (!ptmodState.valid)
		return 0;
	if (!importOptionsInitialized)
	{
		ptmod_default_sample_import_options(&importOptions);
		importOptionsInitialized = 1;
	}
	if (!ptmodsamplefilter[0])
		strcpy(ptmodsamplefilter, "*");
	if (!fileselector(ptmodsamplefilename, songpath, ptmodsamplefilter, "IMPORT MOD SAMPLE", 0, gt, CEDIT, 0))
		return 1;
	if (!makeSelectorPath(path, sizeof path, songpath, ptmodsamplefilename))
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD sample path is too long");
		forceInfoLine = 1;
		return 1;
	}
	if (!ptmodShowSampleImportOptions(gt, path, &importOptions))
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD sample import cancelled");
		forceInfoLine = 1;
		return 1;
	}
	if (!ptmodPushUndo("sample import"))
		return 1;
	error[0] = 0;
	if (ptmod_replace_sample_from_file_with_options(sampleIndex, path, &importOptions, error, sizeof error))
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error);
	else
	{
		ptmodCancelUndo();
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error[0] ? error : "Could not import MOD sample");
	}
	forceInfoLine = 1;
	return 1;
}

static int ptmodExportSample(GTOBJECT *gt)
{
	char error[256];
	char path[MAX_PATHNAME];
	int sampleIndex = ptmodSelectedSampleIndex();

	if (!ptmodState.valid)
		return 0;
	if (!ptmodsamplefilter[0])
		strcpy(ptmodsamplefilter, "*.raw");
	if (!ptmodsamplefilename[0])
		ptmodDefaultSampleFilename(ptmodsamplefilename, sizeof ptmodsamplefilename, sampleIndex);
	if (!fileselector(ptmodsamplefilename, songpath, ptmodsamplefilter, "EXPORT MOD SAMPLE", 3, gt, CEDIT, 1))
		return 1;
	if (!makeSelectorPath(path, sizeof path, songpath, ptmodsamplefilename))
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD sample path is too long");
		forceInfoLine = 1;
		return 1;
	}
	if (ptmod_export_sample_to_file(sampleIndex, path, error, sizeof error))
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error);
	else
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error[0] ? error : "Could not export MOD sample");
	forceInfoLine = 1;
	return 1;
}

static int ptmodDeleteSample(GTOBJECT *gt)
{
	int sampleIndex = ptmodSelectedSampleIndex();

	if (!ptmodState.valid)
		return 0;
	snprintf(textbuffer, sizeof textbuffer, "Delete MOD sample %02d (y/n)?", sampleIndex + 1);
	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), textbuffer);
	waitkey(gt);
	printbyterow(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), 32, 39);
	if (key != 'y' && key != 'Y')
	{
		key = 0;
		rawkey = 0;
		return 1;
	}
	if (!ptmodPushUndo("sample delete"))
		return 1;
	if (ptmod_delete_sample(sampleIndex))
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD sample %02d deleted", sampleIndex + 1);
	else
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "Could not delete MOD sample %02d", sampleIndex + 1);
	forceInfoLine = 1;
	key = 0;
	rawkey = 0;
	return 1;
}

static unsigned ptmodClampEvenSamplePos(unsigned pos, unsigned length)
{
	if (pos > length)
		pos = length;
	return pos & ~1u;
}

static unsigned ptmodAdjustEvenSamplePos(unsigned pos, int delta, unsigned length)
{
	long next = (long)pos + delta;

	if (next < 0)
		next = 0;
	if ((unsigned long)next > length)
		next = length;
	return ptmodClampEvenSamplePos((unsigned)next, length);
}

static void ptmodDrawOpaqueBox(int x, int y, int width, int height, int color)
{
	fillArea(x, y, width, height, color, ' ');
	drawbox(x, y, color, width, height);
	if (width > 2 && height > 2)
		fillArea(x + 1, y + 1, width - 2, height - 2, color, ' ');
}

typedef struct
{
	const PTMOD_SAMPLE *sample;
	int x;
	int y;
	int width;
	int height;
	unsigned cropStart;
	unsigned cropEnd;
	unsigned loopStart;
	unsigned loopEnd;
	int selected;
	unsigned char backgroundColor;
	unsigned char waveformColor;
	unsigned char centerColor;
	unsigned char cropColor;
	unsigned char loopColor;
	unsigned char selectedColor;
} PTMOD_SAMPLE_WAVEFORM_OVERLAY;

static void ptmodFillPixelRectClipped(int x, int y, int width, int height, unsigned char color)
{
	int row;

	if (!gfx_screen || !gfx_screen->pixels || width <= 0 || height <= 0)
		return;
	if (x < 0)
	{
		width += x;
		x = 0;
	}
	if (y < 0)
	{
		height += y;
		y = 0;
	}
	if (x >= gfx_screen->w || y >= gfx_screen->h)
		return;
	if (x + width > gfx_screen->w)
		width = gfx_screen->w - x;
	if (y + height > gfx_screen->h)
		height = gfx_screen->h - y;
	if (width <= 0 || height <= 0)
		return;

	for (row = 0; row < height; row++)
	{
		unsigned char *dest = (unsigned char *)gfx_screen->pixels + (y + row) * gfx_screen->pitch + x;
		memset(dest, color, (size_t)width);
	}
}

static void ptmodDrawPixelVerticalLineClipped(int x, int y1, int y2, unsigned char color)
{
	int y;

	if (!gfx_screen || !gfx_screen->pixels || x < 0 || x >= gfx_screen->w)
		return;
	if (y1 > y2)
	{
		int temp = y1;
		y1 = y2;
		y2 = temp;
	}
	if (y2 < 0 || y1 >= gfx_screen->h)
		return;
	if (y1 < 0)
		y1 = 0;
	if (y2 >= gfx_screen->h)
		y2 = gfx_screen->h - 1;
	for (y = y1; y <= y2; y++)
		*((unsigned char *)gfx_screen->pixels + y * gfx_screen->pitch + x) = color;
}

static int ptmodSamplePosToPixel(unsigned pos, unsigned length, int width)
{
	if (!length || width <= 0)
		return -1;
	if (pos > length)
		pos = length;
	if (width == 1)
		return 0;
	return (int)(((unsigned long long)pos * (unsigned long long)(width - 1)) / length);
}

static void ptmodDrawSampleMarker(const PTMOD_SAMPLE_WAVEFORM_OVERLAY *overlay,
	unsigned pos, unsigned char color, int selected)
{
	int markerX;
	int dx;
	int radius = selected ? 1 : 0;

	if (!overlay || !overlay->sample || !overlay->sample->length)
		return;
	markerX = ptmodSamplePosToPixel(pos, overlay->sample->length, overlay->width);
	if (markerX < 0)
		return;
	markerX += overlay->x;
	for (dx = -radius; dx <= radius; dx++)
		ptmodDrawPixelVerticalLineClipped(markerX + dx, overlay->y, overlay->y + overlay->height - 1,
			selected ? overlay->selectedColor : color);
	if (selected)
	{
		ptmodFillPixelRectClipped(markerX - 2, overlay->y, 5, 2, overlay->selectedColor);
		ptmodFillPixelRectClipped(markerX - 2, overlay->y + overlay->height - 2, 5, 2,
			overlay->selectedColor);
	}
}

static int ptmodDrawSampleWaveformOverlay(void *userdata)
{
	const PTMOD_SAMPLE_WAVEFORM_OVERLAY *overlay = (const PTMOD_SAMPLE_WAVEFORM_OVERLAY *)userdata;
	const PTMOD_SAMPLE *sample;
	int center;
	int x;

	if (!overlay || overlay->width <= 0 || overlay->height <= 0)
		return 0;
	sample = overlay->sample;
	center = overlay->y + overlay->height / 2;
	ptmodFillPixelRectClipped(overlay->x, overlay->y, overlay->width, overlay->height,
		overlay->backgroundColor);
	ptmodFillPixelRectClipped(overlay->x, center, overlay->width, 1, overlay->centerColor);

	if (sample && sample->data && sample->length)
	{
		int topRange = center - overlay->y;
		int bottomRange = overlay->y + overlay->height - 1 - center;

		for (x = 0; x < overlay->width; x++)
		{
			size_t start = ((size_t)sample->length * (size_t)x) / (size_t)overlay->width;
			size_t end = ((size_t)sample->length * (size_t)(x + 1)) / (size_t)overlay->width;
			size_t p;
			int minValue = 0;
			int maxValue = 0;
			int top;
			int bottom;

			if (end <= start)
				end = start + 1;
			if (end > sample->length)
				end = sample->length;
			for (p = start; p < end; p++)
			{
				int value = (signed char)sample->data[p];

				if (value < minValue)
					minValue = value;
				if (value > maxValue)
					maxValue = value;
			}
			top = center - (maxValue * topRange) / 128;
			bottom = center - (minValue * bottomRange) / 128;
			if (top > bottom)
			{
				int temp = top;
				top = bottom;
				bottom = temp;
			}
			ptmodDrawPixelVerticalLineClipped(overlay->x + x, top, bottom, overlay->waveformColor);
		}

		ptmodDrawSampleMarker(overlay, overlay->cropStart, overlay->cropColor, overlay->selected == 0);
		ptmodDrawSampleMarker(overlay, overlay->cropEnd, overlay->cropColor, overlay->selected == 1);
		ptmodDrawSampleMarker(overlay, overlay->loopStart, overlay->loopColor, overlay->selected == 2);
		ptmodDrawSampleMarker(overlay, overlay->loopEnd, overlay->loopColor, overlay->selected == 3);
	}
	return 1;
}

static void ptmodSetupSampleWaveformOverlay(PTMOD_SAMPLE_WAVEFORM_OVERLAY *overlay,
	const PTMOD_SAMPLE *sample, int x, int y, int width, int height,
	unsigned cropStart, unsigned cropEnd, unsigned loopStart, unsigned loopEnd, int selected)
{
	int color = getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);

	if (!overlay)
		return;
	fillArea(x, y, width, height, color, ' ');
	memset(overlay, 0, sizeof *overlay);
	overlay->sample = sample;
	overlay->x = x * getfontwidth();
	overlay->y = y * getfontheight();
	overlay->width = width * getfontwidth();
	overlay->height = height * getfontheight();
	overlay->cropStart = cropStart;
	overlay->cropEnd = cropEnd;
	overlay->loopStart = loopStart;
	overlay->loopEnd = loopEnd;
	overlay->selected = selected;
	overlay->backgroundColor = (unsigned char)((color >> 8) & 0xff);
	overlay->waveformColor = (unsigned char)(color & 0xff);
	overlay->centerColor = (unsigned char)CORDER_INST_TABLE_EDITING;
	overlay->cropColor = (unsigned char)CTITLES_FOREGROUND;
	overlay->loopColor = (unsigned char)CPATTERN_LOOP_MARKER_FOREGROUND;
	overlay->selectedColor = (unsigned char)CPATTERN_HIGHLIGHT_FOREGROUND;
	setConsolePixelOverlay(ptmodDrawSampleWaveformOverlay, overlay);
}

static void ptmodAdjustSampleEditorValue(int selected, int delta, PTMOD_SAMPLE *sample,
	unsigned *cropStart, unsigned *cropEnd, unsigned *loopStart, unsigned *loopEnd,
	unsigned *sourceRate, unsigned *targetRate)
{
	unsigned length = sample ? sample->length : 0;

	if (selected == 0)
	{
		*cropStart = ptmodAdjustEvenSamplePos(*cropStart, delta, length);
		if (*cropStart >= *cropEnd && *cropEnd > 2)
			*cropStart = *cropEnd - 2;
	}
	else if (selected == 1)
	{
		*cropEnd = ptmodAdjustEvenSamplePos(*cropEnd, delta, length);
		if (*cropEnd <= *cropStart)
			*cropEnd = *cropStart + 2 <= length ? *cropStart + 2 : length;
	}
	else if (selected == 2)
	{
		*loopStart = ptmodAdjustEvenSamplePos(*loopStart, delta, length);
		if (*loopStart > *loopEnd)
			*loopEnd = *loopStart;
	}
	else if (selected == 3)
	{
		*loopEnd = ptmodAdjustEvenSamplePos(*loopEnd, delta, length);
		if (*loopEnd < *loopStart)
			*loopStart = *loopEnd;
	}
	else if (selected == 4)
	{
		int rate = (int)*sourceRate + delta;
		if (rate < 1000)
			rate = 1000;
		if (rate > 96000)
			rate = 96000;
		*sourceRate = (unsigned)rate;
	}
	else if (selected == 5)
	{
		int rate = (int)*targetRate + delta;
		if (rate < 1000)
			rate = 1000;
		if (rate > 96000)
			rate = 96000;
		*targetRate = (unsigned)rate;
	}
}

static int ptmodOpenSampleEditor(GTOBJECT *gt)
{
	static unsigned sourceRate = 8363;
	static unsigned targetRate = 8363;
	PTMOD_SAMPLE sample;
	unsigned cropStart = 0;
	unsigned cropEnd = 0;
	unsigned loopStart = 0;
	unsigned loopEnd = 0;
	int sampleIndex = ptmodSelectedSampleIndex();
	int selected = 0;
	int activeColumns = getactivescreencolumns();
	int boxW;
	int boxH = 29;
	int boxX;
	int boxY = 3;
	int waveX;
	int waveY;
	int waveW;
	int waveH;
	int rowY;
	char error[256];

	if (!ptmodState.valid)
		return 0;
	if (activeColumns <= 0 || activeColumns > MAX_COLUMNS)
		activeColumns = MAX_COLUMNS;
	boxW = activeColumns - 8;
	if (boxW > 128)
		boxW = 128;
	if (boxW < 80)
		boxW = activeColumns > 84 ? 80 : activeColumns - 4;
	if (boxW < 20)
		boxW = activeColumns;
	boxX = (activeColumns - boxW) / 2;
	if (boxX < 0)
		boxX = 0;
	waveX = boxX + 2;
	waveY = boxY + 4;
	waveW = boxW - 4;
	waveH = 11;
	rowY = waveY + waveH + 1;
	if (!ptmod_get_sample(sampleIndex, &sample))
		memset(&sample, 0, sizeof sample);
	cropEnd = sample.length;
	loopStart = sample.loopStart;
	loopEnd = sample.loopStart + sample.loopLength;

	stopScreenDisplay();
	for (;;)
	{
		int color = getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);
		int highlight = getColor(CORDER_INST_TABLE_EDITING, CORDER_INST_BACKGROUND);
		int step = ctrlpressed ? 1024 : shiftpressed ? 128 : 2;
		PTMOD_SAMPLE_WAVEFORM_OVERLAY waveformOverlay;

		if (!ptmod_get_sample(sampleIndex, &sample))
			memset(&sample, 0, sizeof sample);
		if (cropEnd > sample.length)
			cropEnd = sample.length;
		if (loopEnd > sample.length)
			loopEnd = sample.length;
		cropStart = ptmodClampEvenSamplePos(cropStart, sample.length);
		cropEnd = ptmodClampEvenSamplePos(cropEnd, sample.length);
		loopStart = ptmodClampEvenSamplePos(loopStart, sample.length);
		loopEnd = ptmodClampEvenSamplePos(loopEnd, sample.length);
		if (cropEnd <= cropStart && sample.length >= 2)
			cropEnd = sample.length;
		if (loopEnd < loopStart)
			loopEnd = loopStart;

		ptmodDrawOpaqueBox(boxX, boxY, boxW, boxH,
			getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND));
		snprintf(textbuffer, sizeof textbuffer, "MOD SAMPLE EDITOR  %02d  %.60s",
			sampleIndex + 1, sample.name[0] ? sample.name : "(empty)");
		printtext(boxX + 2, boxY + 1, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Length:%u  Volume:%d  Finetune:%d",
			sample.length, sample.volume, sample.finetune);
		printtext(boxX + 2, boxY + 2, color, textbuffer);
		ptmodSetupSampleWaveformOverlay(&waveformOverlay, &sample, waveX, waveY, waveW, waveH,
			cropStart, cropEnd, loopStart, loopEnd, selected);

		snprintf(textbuffer, sizeof textbuffer, "Crop start:%6u", cropStart);
		printtext(boxX + 2, rowY + 0, selected == 0 ? highlight : color, textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Crop end:  %6u", cropEnd);
		printtext(boxX + 2, rowY + 1, selected == 1 ? highlight : color, textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Loop start:%6u", loopStart);
		printtext(boxX + 2, rowY + 2, selected == 2 ? highlight : color, textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Loop end:  %6u", loopEnd);
		printtext(boxX + 2, rowY + 3, selected == 3 ? highlight : color, textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Source Hz:%6u", sourceRate);
		printtext(boxX + 30, rowY + 0, selected == 4 ? highlight : color, textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Target Hz:%6u", targetRate);
		printtext(boxX + 30, rowY + 1, selected == 5 ? highlight : color, textbuffer);
		printtext(boxX + 30, rowY + 2, color, "Mouse-drag selected marker on waveform");
		printtext(boxX + 2, rowY + 5, color,
			"A audition  C crop  T trim  L loop  R resample  I import  E export  D delete");
		printtext(boxX + 2, rowY + 6, color,
			"Arrows move marker/value, Shift/Ctrl = larger steps, Esc/Enter closes");

		fliptoscreen();
		waitkeymousenoupdate();
		clearConsolePixelOverlay();
		if (win_quitted)
			break;
		{
			int wavePixelX = waveX * getfontwidth();
			int wavePixelY = waveY * getfontheight();
			int wavePixelW = waveW * getfontwidth();
			int wavePixelH = waveH * getfontheight();
			int mx = (int)getmousepixelx();
			int my = (int)getmousepixely();

			if (mouseb && sample.length && wavePixelW > 0 && wavePixelH > 0 &&
				mx >= wavePixelX && mx < wavePixelX + wavePixelW &&
				my >= wavePixelY && my < wavePixelY + wavePixelH)
			{
				unsigned denom = (unsigned)(wavePixelW > 1 ? wavePixelW - 1 : 1);
				unsigned pos = (unsigned)(((unsigned long long)sample.length *
					(unsigned long long)(mx - wavePixelX)) / denom);

				pos = ptmodClampEvenSamplePos(pos, sample.length);
				if (selected == 0)
					cropStart = pos;
				else if (selected == 1)
					cropEnd = pos;
				else if (selected == 2)
					loopStart = pos;
				else if (selected == 3)
					loopEnd = pos;
				if (cropEnd <= cropStart && sample.length >= 2)
					cropEnd = sample.length;
				if (loopEnd < loopStart)
					loopEnd = loopStart;
				key = 0;
				rawkey = 0;
				continue;
			}
		}
		if (rawkey == KEY_ESC || rawkey == KEY_ENTER)
			break;
		if (rawkey == KEY_UP)
		{
			selected--;
			if (selected < 0)
				selected = 5;
		}
		else if (rawkey == KEY_DOWN)
		{
			selected++;
			if (selected > 5)
				selected = 0;
		}
		else if (rawkey == KEY_LEFT || rawkey == KEY_RIGHT)
		{
			ptmodAdjustSampleEditorValue(selected, rawkey == KEY_LEFT ? -step : step,
				&sample, &cropStart, &cropEnd, &loopStart, &loopEnd, &sourceRate, &targetRate);
		}
		else if (ptmodHandleSampleOctaveShortcut())
		{
			if (sampleIndex != ptmodSelectedSampleIndex())
			{
				sampleIndex = ptmodSelectedSampleIndex();
				if (ptmod_get_sample(sampleIndex, &sample))
				{
					cropStart = 0;
					cropEnd = sample.length;
					loopStart = sample.loopStart;
					loopEnd = sample.loopStart + sample.loopLength;
					sourceRate = targetRate = 8363;
				}
			}
		}
			else if (rawkey == KEY_A)
			{
				ptmodplay_audition_sample_with_loop(sampleIndex,
					loopStart, loopEnd >= loopStart ? loopEnd - loopStart : 0);
			}
			else if (rawkey == KEY_C)
			{
				int ok = 0;
				int pushed;

				error[0] = 0;
				pushed = ptmodPushUndo("sample crop");
				if (pushed)
					ok = ptmod_crop_sample(sampleIndex, cropStart, cropEnd, error, sizeof error);
				if (pushed && ok)
				{
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error);
					cropStart = 0;
					if (ptmod_get_sample(sampleIndex, &sample))
						cropEnd = sample.length;
				}
				else if (pushed)
				{
					ptmodCancelUndo();
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error[0] ? error : "Could not crop sample");
				}
				forceInfoLine = 1;
			}
			else if (rawkey == KEY_T)
			{
				int ok = 0;
				int pushed;

				error[0] = 0;
				pushed = ptmodPushUndo("sample trim");
				if (pushed)
					ok = ptmod_trim_sample(sampleIndex, 1, error, sizeof error);
				if (pushed && ok)
				{
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error);
					cropStart = 0;
					if (ptmod_get_sample(sampleIndex, &sample))
						cropEnd = sample.length;
				}
				else if (pushed)
				{
					ptmodCancelUndo();
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error[0] ? error : "Could not trim sample");
				}
				forceInfoLine = 1;
			}
			else if (rawkey == KEY_L)
			{
				int ok = 0;
				int pushed = ptmodPushUndo("sample loop");

				if (pushed)
					ok = ptmod_set_sample_loop(sampleIndex, loopStart, loopEnd >= loopStart ? loopEnd - loopStart : 0);
				if (pushed && ok)
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD sample %02d loop updated", sampleIndex + 1);
				else if (pushed)
				{
					ptmodCancelUndo();
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "Could not update MOD sample loop");
				}
				forceInfoLine = 1;
			}
			else if (rawkey == KEY_R)
			{
				int ok = 0;
				int pushed;

				error[0] = 0;
				pushed = ptmodPushUndo("sample resample");
				if (pushed)
					ok = ptmod_resample_sample(sampleIndex, sourceRate, targetRate, error, sizeof error);
				if (pushed && ok)
				{
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error);
					cropStart = 0;
					if (ptmod_get_sample(sampleIndex, &sample))
						cropEnd = sample.length;
					loopStart = sample.loopStart;
					loopEnd = sample.loopStart + sample.loopLength;
				}
				else if (pushed)
				{
					ptmodCancelUndo();
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", error[0] ? error : "Could not resample sample");
				}
				forceInfoLine = 1;
			}
		else if (rawkey == KEY_I)
		{
			restartScreenDisplay();
			ptmodImportSample(gt);
			stopScreenDisplay();
			if (ptmod_get_sample(sampleIndex, &sample))
			{
				cropStart = 0;
				cropEnd = sample.length;
				loopStart = sample.loopStart;
				loopEnd = sample.loopStart + sample.loopLength;
			}
		}
		else if (rawkey == KEY_E)
		{
			restartScreenDisplay();
			ptmodExportSample(gt);
			stopScreenDisplay();
		}
		else if (rawkey == KEY_D)
		{
			restartScreenDisplay();
			ptmodDeleteSample(gt);
			stopScreenDisplay();
			cropStart = cropEnd = loopStart = loopEnd = 0;
		}
		key = 0;
		rawkey = 0;
		}
		clearConsolePixelOverlay();
		ptmodplay_stop_audition();
		restartScreenDisplay();
		key = 0;
	rawkey = 0;
	return 1;
}

typedef struct
{
	int effect;
	int sub;
	int defaultParam;
	const char *name;
	const char *detail;
} PTMOD_EFFECT_TEMPLATE;

static const PTMOD_EFFECT_TEMPLATE ptmodEffectTemplates[] = {
	{ 0x0, -1, 0x00, "Arpeggio / none", "x/y semitone offsets; 000 is no effect" },
	{ 0x1, -1, 0x01, "Portamento up", "slide speed 00-FF" },
	{ 0x2, -1, 0x01, "Portamento down", "slide speed 00-FF" },
	{ 0x3, -1, 0x10, "Tone portamento", "target-note slide speed" },
	{ 0x4, -1, 0x47, "Vibrato", "x speed, y depth" },
	{ 0x5, -1, 0x0f, "Tone porta + volslide", "x up or y down volume slide" },
	{ 0x6, -1, 0x0f, "Vibrato + volslide", "x up or y down volume slide" },
	{ 0x7, -1, 0x47, "Tremolo", "x speed, y depth" },
	{ 0x8, -1, 0x80, "Set panning", "00 left, 80 center, FF right where supported" },
	{ 0x9, -1, 0x10, "Sample offset", "start at xx00 in sample" },
	{ 0xA, -1, 0x0f, "Volume slide", "x up or y down" },
	{ 0xB, -1, 0x00, "Position jump", "order number" },
	{ 0xC, -1, 0x40, "Set volume", "00-40" },
	{ 0xD, -1, 0x00, "Pattern break", "BCD row 00-63" },
	{ 0xF, -1, 0x06, "Speed / BPM", "01-1F speed, 20-FF BPM" },
	{ 0xE, 0x0, 0x00, "E0 filter", "Amiga low-pass filter toggle" },
	{ 0xE, 0x1, 0x10, "E1 fine porta up", "fine slide amount 0-F" },
	{ 0xE, 0x2, 0x20, "E2 fine porta down", "fine slide amount 0-F" },
	{ 0xE, 0x3, 0x30, "E3 glissando", "0 off, 1 on" },
	{ 0xE, 0x4, 0x40, "E4 vibrato waveform", "0 sine, 1 ramp, 2 square, +4 no retrig" },
	{ 0xE, 0x5, 0x50, "E5 set finetune", "sample finetune 0-F" },
	{ 0xE, 0x6, 0x60, "E6 pattern loop", "0 set loop, 1-F repeat" },
	{ 0xE, 0x7, 0x70, "E7 tremolo waveform", "0 sine, 1 ramp, 2 square, +4 no retrig" },
	{ 0xE, 0x8, 0x80, "E8 panning", "legacy 4-bit panning where supported" },
	{ 0xE, 0x9, 0x90, "E9 retrigger note", "tick interval 1-F" },
	{ 0xE, 0xA, 0xA0, "EA fine volume up", "fine volume amount 0-F" },
	{ 0xE, 0xB, 0xB0, "EB fine volume down", "fine volume amount 0-F" },
	{ 0xE, 0xC, 0xC0, "EC note cut", "cut at tick 0-F" },
	{ 0xE, 0xD, 0xD0, "ED note delay", "delay note to tick 0-F" },
	{ 0xE, 0xE, 0xE0, "EE pattern delay", "delay rows 0-F" },
	{ 0xE, 0xF, 0xF0, "EF invert loop", "funk/invert speed 0-F" }
};

static int ptmodApplyEffectTemplate(const PTMOD_PREVIEW_STATS *stats, int effect, int param)
{
	int cursorRow;
	int orderIndex;
	int pattern;

	if (!stats || !stats->loaded || editorInfo.ptmodEditPage != 0)
		return 0;
	clampPtmodStreamCursor(stats);
	cursorRow = ptmodStreamCursorRow(stats);
	orderIndex = ptmodEditorOrderIndex(stats);
	pattern = ptmod_order_pattern(orderIndex);
	if (pattern < 0)
		return 0;
	if (!ptmodPushUndo("effect template"))
		return 1;
	if (!ptmod_set_pattern_cell_value(pattern, cursorRow, editorInfo.ptmodStreamChannel,
		PTMOD_ROW_FIELD_EFFECT, effect))
		return 0;
	if (!ptmod_set_pattern_cell_value(pattern, cursorRow, editorInfo.ptmodStreamChannel,
		PTMOD_ROW_FIELD_PARAM, param))
		return 0;
	ptmodSetStreamSubColumn(PTMOD_STREAM_SUBCOLUMN_EFFECT);
	editorInfo.ptmodStreamFollow = 0;
	ptmodShowEffectInfo(effect, param);
	return 1;
}

static int ptmodEffectTemplateMatchesCell(const PTMOD_EFFECT_TEMPLATE *item, const PTMOD_CELL *cell)
{
	if (!item || !cell || item->effect != cell->effect)
		return 0;
	if (item->effect == 0xE && item->sub >= 0)
		return ((cell->param >> 4) & 0x0f) == item->sub;
	return item->sub < 0;
}

static int ptmodEffectParamForTemplate(const PTMOD_EFFECT_TEMPLATE *item, const PTMOD_CELL *cell)
{
	if (ptmodEffectTemplateMatchesCell(item, cell))
		return cell->param;
	return item ? item->defaultParam : 0;
}

static int ptmodPatternBreakToRow(int param)
{
	int row = ((param >> 4) & 0x0f) * 10 + (param & 0x0f);

	if (row > 63)
		row = 63;
	return row;
}

static int ptmodRowToPatternBreak(int row)
{
	if (row < 0)
		row = 0;
	if (row > 63)
		row = 63;
	return ((row / 10) << 4) | (row % 10);
}

static int ptmodAdjustEffectEditorParam(const PTMOD_EFFECT_TEMPLATE *item,
	int param, int delta)
{
	int value;
	int maxValue = 0xff;

	if (!item || delta == 0)
		return param;
	if (item->effect == 0xE && item->sub >= 0)
	{
		value = param & 0x0f;
		value += delta;
		if (value < 0)
			value = 0;
		if (value > 0x0f)
			value = 0x0f;
		return (item->sub << 4) | value;
	}
	if (item->effect == 0xD)
		return ptmodRowToPatternBreak(ptmodPatternBreakToRow(param) + delta);
	if (item->effect == 0xB && ptmodState.songLength > 0)
		maxValue = ptmodState.songLength - 1;
	else if (item->effect == 0xC)
		maxValue = 0x40;
	value = param + delta;
	if (value < 0)
		value = 0;
	if (value > maxValue)
		value = maxValue;
	return ptmod_clamp_effect_param(item->effect, value);
}

static int ptmodShowEffectTemplateMenu(GTOBJECT *gt, const PTMOD_PREVIEW_STATS *stats)
{
	PTMOD_CELL cell;
	int orderIndex;
	int pattern;
	int row;
	int selected = 0;
	int param = 0;
	int count = (int)(sizeof ptmodEffectTemplates / sizeof ptmodEffectTemplates[0]);
	int boxW = 76;
	int boxH = 24;
	int boxX = (MAX_COLUMNS - boxW) / 2;
	int boxY = 4;
	int i;

	(void)gt;
	if (!stats || !stats->loaded || editorInfo.ptmodEditPage != 0 ||
		(editorInfo.ptmodStreamField != PTMOD_ROW_FIELD_EFFECT &&
		editorInfo.ptmodStreamField != PTMOD_ROW_FIELD_PARAM))
		return 0;
	orderIndex = ptmodEditorOrderIndex(stats);
	pattern = ptmod_order_pattern(orderIndex);
	row = ptmodStreamCursorRow(stats);
	if (pattern < 0 || !ptmod_get_pattern_cell(pattern, row, editorInfo.ptmodStreamChannel, &cell))
		return 0;
	for (i = 0; i < count; i++)
	{
		if (ptmodEffectTemplateMatchesCell(&ptmodEffectTemplates[i], &cell))
		{
			selected = i;
			break;
		}
	}
	param = ptmodEffectParamForTemplate(&ptmodEffectTemplates[selected], &cell);
	if (boxH > TRANSPORT_BAR_Y - 1)
		boxH = TRANSPORT_BAR_Y - 1;
	if (boxX < 0)
		boxX = 0;

	stopScreenDisplay();
	for (;;)
	{
		const PTMOD_EFFECT_TEMPLATE *selectedItem = &ptmodEffectTemplates[selected];
		int first = selected - (boxH - 9) / 2;
		int visible = boxH - 8;
		int step = ctrlpressed ? 0x10 : shiftpressed ? 4 : 1;
		char help[96];
		char validation[128];

		if (first < 0)
			first = 0;
		if (first + visible > count)
			first = count - visible;
		if (first < 0)
			first = 0;
		ptmodDrawOpaqueBox(boxX, boxY, boxW, boxH,
			getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND));
		printtext(boxX + 2, boxY + 1, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND),
			"MOD EFFECT EDITOR");
		for (i = 0; i < visible && first + i < count; i++)
		{
			const PTMOD_EFFECT_TEMPLATE *item = &ptmodEffectTemplates[first + i];
			int lineColor = first + i == selected ?
				getColor(CORDER_INST_TABLE_EDITING, CORDER_INST_BACKGROUND) :
				getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);
			int itemParam = first + i == selected ? param : item->defaultParam;

			ptmod_format_effect_help(item->effect, itemParam, help, sizeof help);
			snprintf(textbuffer, sizeof textbuffer, "%02d  %X%02X  %-24.24s %.34s",
				first + i + 1, item->effect, itemParam & 0xff, item->name, help);
			printtext(boxX + 2, boxY + 3 + i, lineColor, textbuffer);
		}
		ptmod_validate_effect_param(selectedItem->effect, param, validation, sizeof validation);
		ptmod_format_effect_help(selectedItem->effect, param, help, sizeof help);
		snprintf(textbuffer, sizeof textbuffer, "Selected: %X%02X  %.50s",
			selectedItem->effect, param & 0xff, selectedItem->name);
		printtext(boxX + 2, boxY + boxH - 4, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Param: %.64s", selectedItem->detail);
		printtext(boxX + 2, boxY + boxH - 3, getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND), textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "Valid: %.64s", validation);
		printtext(boxX + 2, boxY + boxH - 2, getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND), textbuffer);
		printtext(boxX + 2, boxY + boxH - 1, getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND),
			"Up/Down selects, Left/Right edits param, Enter applies, Esc cancels");
		fliptoscreen();
		waitkeymousenoupdate();
		if (win_quitted || rawkey == KEY_ESC)
			break;
		if (mouseb && mousex >= boxX + 2 && mousex < boxX + boxW - 2 &&
			mousey >= boxY + 3 && mousey < boxY + 3 + visible)
		{
			int clicked = first + mousey - (boxY + 3);

			if (clicked >= 0 && clicked < count)
			{
				selected = clicked;
				param = ptmodEffectParamForTemplate(&ptmodEffectTemplates[selected], &cell);
			}
		}
		else if (rawkey == KEY_UP)
		{
			selected--;
			if (selected < 0)
				selected = count - 1;
			param = ptmodEffectParamForTemplate(&ptmodEffectTemplates[selected], &cell);
		}
		else if (rawkey == KEY_DOWN)
		{
			selected++;
			if (selected >= count)
				selected = 0;
			param = ptmodEffectParamForTemplate(&ptmodEffectTemplates[selected], &cell);
		}
		else if (rawkey == KEY_HOME)
		{
			selected = 0;
			param = ptmodEffectParamForTemplate(&ptmodEffectTemplates[selected], &cell);
		}
		else if (rawkey == KEY_END)
		{
			selected = count - 1;
			param = ptmodEffectParamForTemplate(&ptmodEffectTemplates[selected], &cell);
		}
		else if (rawkey == KEY_LEFT || rawkey == KEY_RIGHT)
		{
			param = ptmodAdjustEffectEditorParam(&ptmodEffectTemplates[selected],
				param, rawkey == KEY_RIGHT ? step : -step);
		}
		else if (rawkey == KEY_ENTER || rawkey == KEY_SPACE)
		{
			ptmodApplyEffectTemplate(stats, ptmodEffectTemplates[selected].effect,
				ptmod_clamp_effect_param(ptmodEffectTemplates[selected].effect, param));
			break;
		}
		key = 0;
		rawkey = 0;
	}
	restartScreenDisplay();
	key = 0;
	rawkey = 0;
	return 1;
}

static int ptmodHandleFileKey(GTOBJECT *gt)
{
	char ptmodError[256];

	if (!ctrlpressed)
		return 0;

	switch (rawkey)
	{
	case KEY_N:
		if (!ptmodConfirmDiscard(gt, "new"))
			return 1;
		ptmod_create_blank();
		ptmodResetEditorPosition();
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
		forceInfoLine = 1;
		return 1;
	case KEY_R:
		if (!ptmodConfirmDiscard(gt, "reload"))
			return 1;
		if (ptmod_reload_current(ptmodError, sizeof ptmodError))
		{
			ptmodResetEditorPosition();
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
		}
		else
		{
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmodError);
		}
		forceInfoLine = 1;
		return 1;
	case KEY_U:
		if (!ptmodConfirmDiscard(gt, "unload"))
			return 1;
		ptmod_clear();
		ptmodResetEditorPosition();
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
		forceInfoLine = 1;
		return 1;
	case KEY_I:
		return ptmodImportSample(gt);
	case KEY_E:
		return ptmodExportSample(gt);
	case KEY_D:
		return ptmodDeleteSample(gt);
	case KEY_W:
		return ptmodOpenSampleEditor(gt);
	default:
		return 0;
	}
}

static int ptmodStreamDisplayRowStart(const PTMOD_PREVIEW_STATS *stats)
{
	int cursorRow = ptmodStreamCursorRow(stats);
	int maxRowStart = PTMOD_ROWS > VISIBLEPATTROWS ? PTMOD_ROWS - VISIBLEPATTROWS : 0;
	int rowStart;

	if (cursorRow < 0)
		cursorRow = 0;
	if (cursorRow >= PTMOD_ROWS)
		cursorRow = PTMOD_ROWS - 1;

	if (stats && editorInfo.ptmodStreamFollow && stats->active)
	{
		rowStart = cursorRow - VISIBLEPATTROWS / 2;
		if (rowStart < 0)
			rowStart = 0;
	}
	else
	{
		rowStart = editorInfo.ptmodStreamView;
		if (cursorRow < rowStart)
			rowStart = cursorRow;
		if (cursorRow >= rowStart + VISIBLEPATTROWS)
			rowStart = cursorRow - VISIBLEPATTROWS + 1;
	}
	if (rowStart > maxRowStart)
		rowStart = maxRowStart;
	if (rowStart < 0)
		rowStart = 0;
	return rowStart;
}

static int ptmodSubColumnFromMouseX(int fieldX)
{
	if (fieldX >= 0 && fieldX <= 2)
		return PTMOD_STREAM_SUBCOLUMN_NOTE;
	if (fieldX == 4)
		return PTMOD_STREAM_SUBCOLUMN_SAMPLE_HI;
	if (fieldX == 5)
		return PTMOD_STREAM_SUBCOLUMN_SAMPLE_LO;
	if (fieldX == 7)
		return PTMOD_STREAM_SUBCOLUMN_EFFECT;
	if (fieldX == 8)
		return PTMOD_STREAM_SUBCOLUMN_PARAM_HI;
	if (fieldX == 9)
		return PTMOD_STREAM_SUBCOLUMN_PARAM_LO;
	return -1;
}

static int ptmodMouseCommands(GTOBJECT *gt)
{
	PTMOD_PREVIEW_STATS stats;
	int rowCount;
	int rowStart;
	int chnWidth = getPatternChannelWidth();

	(void)gt;
	if (editorInfo.editmode != EDIT_MOD || !mouseb)
		return 0;

	ptmodplay_get_stats(&stats);
	clampPtmodEditRow();
	clampPtmodStreamCursor(&stats);

	if (mousex >= PANEL_ORDER_X && mousex < PANEL_ORDER_X + getSidePanelWidth() &&
		mousey >= PANEL_ORDER_Y - 1 && mousey <= TRANSPORT_BAR_Y + 2)
	{
		editorInfo.ptmodEditPage = 1;
		rowCount = ptmodEditableRowCount();
		if (ptmodState.valid && mousey >= PANEL_ORDER_Y + PTMOD_SETTINGS_FIRST_EDIT_ROW &&
			mousey < PANEL_ORDER_Y + PTMOD_SETTINGS_FIRST_EDIT_ROW + rowCount)
		{
			editorInfo.ptmodEditRow = mousey - (PANEL_ORDER_Y + PTMOD_SETTINGS_FIRST_EDIT_ROW);
			clampPtmodEditRow();
			ptmodSelectOrderFromSideRow(&stats);
			if (!prevmouseb && editorInfo.ptmodEditRow == PTMOD_SIDE_ROW_FOLLOW)
			{
				ptmodToggleFollowNow(&stats);
			}
		}
		return 1;
	}

	if (mousex >= PATTERN_X && mousex < PATTERN_X + getPatternAreaWidth() &&
		mousey >= PATTERN_Y && mousey <= PATTERN_Y + VISIBLEPATTROWS)
	{
		editorInfo.ptmodEditPage = 0;
		if (!ptmodState.valid)
			return 1;

		rowStart = ptmodStreamDisplayRowStart(&stats);
		if (mousey > PATTERN_Y)
		{
			int row = rowStart + (mousey - (PATTERN_Y + 1));
			if (row < 0)
				row = 0;
			if (row >= PTMOD_ROWS)
				row = PTMOD_ROWS - 1;
			editorInfo.ptmodOrderIndex = ptmodEditorOrderIndex(&stats);
			editorInfo.ptmodStreamRow = row;
			editorInfo.ptmodStreamView = rowStart;
			editorInfo.ptmodStreamFollow = 0;
		}

		if (mousex >= PATTERN_X + 5)
		{
			int channel = (mousex - (PATTERN_X + 4)) / chnWidth;
			int fieldX = mousex - (PATTERN_X + 5 + channel * chnWidth);
			int subColumn = ptmodSubColumnFromMouseX(fieldX);

			if (channel >= 0 && channel < ptmodStreamChannelCount(&stats))
				editorInfo.ptmodStreamChannel = channel;
			if (subColumn >= 0)
				ptmodSetStreamSubColumn(subColumn);
			clampPtmodStreamCursor(&stats);
			if (ctrlpressed && mousey > PATTERN_Y && !prevmouseb)
			{
				int orderIndex;
				int pattern;
				int row;
				int markChannel;

				ptmodCurrentPatternCursor(&stats, &orderIndex, &pattern, &row, &markChannel);
				if (pattern >= 0)
					ptmodSetBlockMark(orderIndex, pattern, row, markChannel, 0);
			}
		}
		return 1;
	}

	return 1;
}

static void ptmodsettingscommands(GTOBJECT *gt)
{
	PTMOD_PREVIEW_STATS stats;
	PTMOD_RUNTIME_SETTINGS runtimeSettings;
	int handled = 0;

	(void)gt;
	ptmodplay_get_stats(&stats);
	ptmodplay_get_runtime_settings(&runtimeSettings);
	clampPtmodEditRow();
	clampPtmodStreamCursor(&stats);

	if (ptmodHandleSampleOctaveShortcut())
	{
		handled = 1;
	}
	else if (ptmodHandleFileKey(gt))
	{
		handled = 1;
	}
	else if (ptmodToggleFollow(&stats))
	{
		handled = 1;
	}
	else if (ptmodHandlePatternToolKey(&stats))
	{
		handled = 1;
	}
	else if (editorInfo.ptmodEditPage == 0 && ptmodEditNoteKey(&stats))
	{
		handled = 1;
	}
	else if (editorInfo.ptmodEditPage == 0 && ptmodEditStreamHex(&stats))
	{
		handled = 1;
	}
	else if (editorInfo.ptmodEditPage == 1 && ptmodEditSideText(&stats))
	{
		handled = 1;
	}
	else if (editorInfo.ptmodEditPage == 1 && ptmodEditSideHex(&stats))
	{
		handled = 1;
	}
	else if (editorInfo.ptmodEditPage == 1 && ptmodState.valid &&
		(rawkey == KEY_ENTER || rawkey == KEY_SPACE))
	{
		int sampleBase = ptmodSideSampleBase(&stats);

		if (editorInfo.ptmodEditRow >= sampleBase && editorInfo.ptmodEditRow <= sampleBase + 7)
			handled = ptmodOpenSampleEditor(gt);
	}
	else if (ptmodHandleRuntimeKey(&stats, &runtimeSettings))
	{
		handled = 1;
	}
	else
	{
		if (editorInfo.ptmodEditPage == 0)
		{
			switch (rawkey)
			{
			case KEY_ENTER:
			case KEY_SPACE:
				if (editorInfo.ptmodStreamField == PTMOD_ROW_FIELD_EFFECT ||
					editorInfo.ptmodStreamField == PTMOD_ROW_FIELD_PARAM)
					handled = ptmodShowEffectTemplateMenu(gt, &stats);
				break;
			case KEY_UP:
				if (ctrlpressed)
					ptmodMoveOrder(&stats, -1);
				else
					ptmodMoveStreamRow(&stats, -1);
				handled = 1;
				break;
			case KEY_DOWN:
				if (ctrlpressed)
					ptmodMoveOrder(&stats, 1);
				else
					ptmodMoveStreamRow(&stats, 1);
				handled = 1;
				break;
			case KEY_PGUP:
				ptmodMoveStreamRow(&stats, -PGUPDNREPEAT);
				handled = 1;
				break;
			case KEY_PGDN:
				ptmodMoveStreamRow(&stats, PGUPDNREPEAT);
				handled = 1;
				break;
			case KEY_HOME:
				editorInfo.ptmodEditPage = 0;
				editorInfo.ptmodStreamFollow = 0;
				editorInfo.ptmodOrderIndex = ptmodEditorOrderIndex(&stats);
				editorInfo.ptmodStreamRow = 0;
				clampPtmodStreamCursor(&stats);
				handled = 1;
				break;
			case KEY_END:
				editorInfo.ptmodEditPage = 0;
				editorInfo.ptmodStreamFollow = 0;
				editorInfo.ptmodOrderIndex = ptmodEditorOrderIndex(&stats);
				editorInfo.ptmodStreamRow = ptmodStreamMaxRow(&stats);
				clampPtmodStreamCursor(&stats);
				handled = 1;
				break;
			case KEY_LEFT:
				ptmodMoveStreamColumn(&stats, -1);
				handled = 1;
				break;
			case KEY_RIGHT:
				ptmodMoveStreamColumn(&stats, 1);
				handled = 1;
				break;
			case KEY_BACKSPACE:
			case KEY_DEL:
				handled = ptmodClearStreamField(&stats);
				break;
			}
		}
	}

	if (handled)
		consumePtmodSettingsKey();
}

static int isDebugEnvEnabled(const char* name)
{
	const char* value = getenv(name);

	return value && value[0] != '\0' && strcmp(value, "0");
}

int songExportSuccessFlag = 0;
int sidAddr1 = 0xd400;
int sidAddr2 = 0xd420;
int sidAddr3 = 0xd440;
int sidAddr4 = 0xd460;
int songExported = 0;
int doExportToWAV = 0;
int menu = 0;
int autoNextPattern = 0;
int recordmode = 1;
int followplay = 0;
int hexnybble = -1;
int stepsize = 4;
int autoadvance = 0;
int defaultpatternlength = 64;
int cursorflash = 0;
int cursorcolortable[] = { 1,2,7,2 };
int exitprogram = 0;
//int editorInfo.eacolumn = 0;
int eamode = 0;
int paletteChanged = 0;
int backupTimeSeconds = 30;
int debugTicks;	// used to measure CPU use when looking to improve performance
int midiEnabled = 0;
int forceSave3ChannelSng = 0;
int normalizeWAV = 0;
int useRepeatsWhenCompressing = 1;
int debugEnabled = 0;

int selectingInOrderList = 0;
int selectingInOrderListDeltaTime = 0;
int selectingInOrderListDeltaTicks = 0;


int leftKeyTicksDelta = 0;
int leftKeyTicks = 0;

char appFileName[MAX_PATHNAME];
char packedsongname[MAX_FILENAME];


int SID_StereoPanPositions[4][4] = {
									{ 7,0,0,0 },
									{ 0,14,0,0 },
									{0, 14, 7, 0},
									{0, 14, 0, 14}
};

int sidPanInts[4] = { 0x0007, 0x00e0, 0x07e0, 0xe0e0 };

//int SID2_StereoPanPositions[] = { 0,14 };
//int SID3_StereoPanPositions[] = { 0,14,7 };
//int SID4_StereoPanPositions[] = { 0,14,0,14 };
char editPan = 0;


unsigned keypreset = KEY_TRACKER;
unsigned playerversion = 0;
int fileformat = FORMAT_PRG;
int zeropageadr = 0xfc;
int playeradr = 0x1000;


//unsigned editorInfo.adparam = 0x0f00;
//unsigned editorInfo.ntsc = 0;
unsigned patterndispmode = 0;
unsigned sidaddress = 0xd400d420;
//unsigned finevibrato = 1;
//unsigned editorInfo.optimizepulse = 1;
//unsigned editorInfo.optimizerealtime = 1;
unsigned customclockrate = 0;
//unsigned editorInfo.usefinevib = 0;
unsigned b = DEFAULTBUF;
unsigned mr = DEFAULTMIXRATE;
unsigned writer = 0;
unsigned hardsid = 0;
unsigned catweasel = 0;
unsigned interpolate = 3;
unsigned residdelay = 0;
unsigned hardsidbufinteractive = 20;
unsigned hardsidbufplayback = 400;
unsigned monomode = 0;
unsigned stereoMode = 1;	// 0=mono, 1 = SID Stereo (SID 0+2 = Left, SID 1+3 = Right), 2 = True Stereo (emulation only - uses pan value per voice)
float basepitch = 0.0f;
float equaldivisionsperoctave = 12.0f;
int tuningcount = 0;
double tuning[96];
extern unsigned bigwindow;
int checkUndoFlag = 0;
unsigned int lmanMode = 1;
unsigned int editPaletteMode = 0;
unsigned int enablekeyrepeat = 0;
unsigned int enableAntiAlias = 1;
int useOriginalGTFunctionKeys = 0;
int SIDTracker64ForIPadIsAmazing = 1;

float masterVolume = 1.0f;
float detuneCent = 0;
int displayingPanel = 0;
int displayStopped = 0;

char configbuf[MAX_PATHNAME];
char loadedsongfilename[MAX_PATHNAME]; // JP was MAX_FILENAME
char wavfilename[MAX_PATHNAME];
char videofilename[MAX_PATHNAME];
char songfilename[MAX_PATHNAME];	// JP was MAX_FILENAME
char wavfilter[MAX_FILENAME];
char videofilter[MAX_FILENAME];
char songfilter[MAX_FILENAME];
char ptmodfilter[MAX_FILENAME];
char ptmodfilename[MAX_PATHNAME];
char ptmodsamplefilter[MAX_FILENAME];
char ptmodsamplefilename[MAX_PATHNAME];
char songpath[MAX_PATHNAME];
char instrfilename[MAX_FILENAME];
char instrfilter[MAX_FILENAME];
char palettefilter[MAX_FILENAME];
char palettepath[MAX_FILENAME];
char paletteFileName[MAX_FILENAME] = { "palette_" };
char instrpath[MAX_PATHNAME];
char packedpath[MAX_PATHNAME];
char charsetFilename[MAX_PATHNAME];
char tempSngFilename[MAX_PATHNAME];
char backupSngFilename[MAX_PATHNAME];
char fkeysFilename[MAX_PATHNAME];

extern char* notename[];
char* programname = "$VER: GTUltraPro V2.0.0";
char specialnotenames[186];
char scalatuningfilepath[MAX_PATHNAME];
char tuningname[64];

char startPaletteName[MAX_PATHNAME];

static const char *ptmodFilenameFromPath(const char *path)
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

static int ptmodPatternContentScore(int pattern)
{
	int row;
	int channel;
	int score = 0;

	if (!ptmodState.valid || pattern < 0 || pattern >= ptmodState.patternCount)
		return 0;
	for (row = 0; row < PTMOD_ROWS; row++)
	{
		for (channel = 0; channel < PTMOD_CHANNELS; channel++)
		{
			PTMOD_CELL cell;

			if (ptmod_get_pattern_cell(pattern, row, channel, &cell) &&
				(cell.period || cell.sample))
				score++;
		}
	}
	return score;
}

static int ptmodInitialEditorOrder(void)
{
	int order;
	int fallbackOrder = 0;
	int fallbackScore = 0;

	if (!ptmodState.valid || ptmodState.songLength <= 0)
		return 0;
	for (order = 0; order < ptmodState.songLength; order++)
	{
		int pattern = ptmod_order_pattern(order);
		int score = ptmodPatternContentScore(pattern);

		if (score > fallbackScore)
		{
			fallbackScore = score;
			fallbackOrder = order;
		}
		if (score >= 8)
			return order;
	}
	return fallbackOrder;
}

static void ptmodResetEditorPosition(void)
{
	editorInfo.ptmodOrderIndex = ptmodInitialEditorOrder();
	editorInfo.ptmodStreamRow = 0;
	editorInfo.ptmodStreamView = 0;
	ptmodSetStreamSubColumn(PTMOD_STREAM_SUBCOLUMN_NOTE);
	editorInfo.ptmodStreamFollow = 0;
	editorInfo.ptmodEditPage = 0;
	editorInfo.ptmodBlockActive = 0;
	editorInfo.ptmodBlockOrder = 0;
	editorInfo.ptmodBlockPattern = 0;
	editorInfo.ptmodBlockRowStart = 0;
	editorInfo.ptmodBlockRowEnd = 0;
	editorInfo.ptmodBlockChannelStart = 0;
	editorInfo.ptmodBlockChannelEnd = 0;
	clampPtmodEditRow();
}

static int ptmodSaveAs(GTOBJECT *gt)
{
	char ptmodError[256];
	char ptmodPath[MAX_PATHNAME];
	const char *name;

	if (!ptmodState.valid)
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "No valid MOD is loaded");
		forceInfoLine = 1;
		return 0;
	}
	if (!ptmodfilter[0])
		strcpy(ptmodfilter, "*.mod");
	name = ptmodFilenameFromPath(ptmodState.path);
	if (name[0])
		strncpy(ptmodfilename, name, sizeof ptmodfilename - 1);
	else if (!ptmodfilename[0])
		strcpy(ptmodfilename, "untitled.mod");
	ptmodfilename[sizeof ptmodfilename - 1] = 0;

	if (!fileselector(ptmodfilename, songpath, ptmodfilter, "SAVE MOD FILE", 3, gt, CEDIT, 1))
		return 0;
	if (!makeSelectorPath(ptmodPath, sizeof ptmodPath, songpath, ptmodfilename))
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD path is too long");
		forceInfoLine = 1;
		return 0;
	}
	if (ptmod_save_as(ptmodPath, ptmodError, sizeof ptmodError))
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
	else
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmodError);
	forceInfoLine = 1;
	return !ptmod_is_dirty();
}

static int ptmodSaveCurrentOrAs(GTOBJECT *gt)
{
	char ptmodError[256];

	if (!ptmodState.valid)
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "No valid MOD is loaded");
		forceInfoLine = 1;
		return 0;
	}
	if (!ptmod_has_path())
		return ptmodSaveAs(gt);
	if (ptmod_save_current(ptmodError, sizeof ptmodError))
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
	else
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmodError);
	forceInfoLine = 1;
	return !ptmod_is_dirty();
}

static int ptmodConfirmDiscard(GTOBJECT *gt, const char *action)
{
	char prompt[96];

	if (!ptmod_is_dirty())
		return 1;

	snprintf(prompt, sizeof prompt, "Save MOD changes before %s? (y/n/esc)", action ? action : "continuing");
	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(CINFO_FOREGROUND, CGENERAL_BACKGROUND), prompt);
	waitkey(gt);
	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(CINFO_FOREGROUND, CGENERAL_BACKGROUND),
		"                                                ");
	if (key == 'y' || key == 'Y')
	{
		int ok = ptmodSaveCurrentOrAs(gt);
		key = 0;
		rawkey = 0;
		return ok;
	}
	if (key == 'n' || key == 'N')
	{
		key = 0;
		rawkey = 0;
		return 1;
	}
	snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD %s cancelled", action ? action : "operation");
	forceInfoLine = 1;
	key = 0;
	rawkey = 0;
	return 0;
}


char debugTextbuffer[MAX_PATHNAME];
char textbuffer[MAX_PATHNAME];
char infoTextBuffer[256];

char transportPolySIDEnabled[4];	// 0 = OFF 1 = ON (all OFF = mono)
char transportLoopPattern = 0;
char transportLoopPatternSelectArea = 0;
char transportRecord = 1;
char transportPlay = 1;
char transportShowKeyboard = 0;
char jdebugPlaying = 0;

char jpdebug = 0;

int selectedMIDIPort = 0;




unsigned char hexkeytbl[] = { '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

extern unsigned char datafile[];

int currentPalettePreset = 0;

unsigned char backupPaletteSong[MAX_CHN][MAX_SONGLEN + 2];

unsigned char paletteRGB[MAX_PALETTE_PRESETS][3][MAX_PALETTE_ENTRIES];
unsigned char paletteLoadRGB[MAX_PALETTE_PRESETS][3][MAX_PALETTE_LOAD_ENTRIES];

unsigned short tableBackgroundColors[MAX_TABLES][MAX_TABLELEN];
unsigned char paletteR[256];
unsigned char paletteG[256];
unsigned char paletteB[256];
int debugPalette = 0;
int debugPattern = 0;

static int paletteRGBIndex(int uiColor)
{
	return uiColor - FIRST_UI_COLOR;
}

static int paletteEntriesMatch(int palettePreset, int color1, int color2)
{
	int index1 = paletteRGBIndex(color1);
	int index2 = paletteRGBIndex(color2);

	if (palettePreset < 0 || palettePreset >= MAX_PALETTE_PRESETS)
		return 1;
	if (index1 < 0 || index1 >= MAX_PALETTE_ENTRIES)
		return 1;
	if (index2 < 0 || index2 >= MAX_PALETTE_ENTRIES)
		return 1;

	return paletteRGB[palettePreset][0][index1] == paletteRGB[palettePreset][0][index2] &&
		paletteRGB[palettePreset][1][index1] == paletteRGB[palettePreset][1][index2] &&
		paletteRGB[palettePreset][2][index1] == paletteRGB[palettePreset][2][index2];
}

static int isPatternNotePaletteReadable(int palettePreset)
{
	return !paletteEntriesMatch(palettePreset, CPATTERN_NOTE_FOREGROUND, CPATTERN_BACKGROUND1) &&
		!paletteEntriesMatch(palettePreset, CPATTERN_NOTE_FOREGROUND, CPATTERN_BACKGROUND2) &&
		!paletteEntriesMatch(palettePreset, CPATTERN_NOTE_FOREGROUND, CPATTERN_FIRST_BACKGROUND1) &&
		!paletteEntriesMatch(palettePreset, CPATTERN_NOTE_FOREGROUND, CPATTERN_FIRST_BACKGROUND2);
}

static void debugPrintPaletteEntry(const char* label, int uiColor)
{
	int paletteIndex = paletteRGBIndex(uiColor);

	if (!debugPalette)
		return;
	if (paletteIndex < 0 || paletteIndex >= MAX_PALETTE_ENTRIES)
	{
		fprintf(stdout, "[palette] %s color=%d out of palette range\n", label, uiColor);
		return;
	}

	fprintf(stdout, "[palette] %s color=%d index=%02X rgb=%02X,%02X,%02X\n",
		label,
		uiColor,
		paletteIndex,
		paletteR[uiColor],
		paletteG[uiColor],
		paletteB[uiColor]);
}

static void debugPrintActivePalette(void)
{
	if (!debugPalette)
		return;

	fprintf(stdout, "[palette] active preset=%d name='%s'\n",
		currentPalettePreset,
		paletteNames[currentPalettePreset] ? paletteNames[currentPalettePreset] : "(null)");
	debugPrintPaletteEntry("pattern background 1", CPATTERN_BACKGROUND1);
	debugPrintPaletteEntry("pattern foreground 1", CPATTERN_FOREGROUND1);
	debugPrintPaletteEntry("pattern background 2", CPATTERN_BACKGROUND2);
	debugPrintPaletteEntry("pattern foreground 2", CPATTERN_FOREGROUND2);
	debugPrintPaletteEntry("pattern note", CPATTERN_NOTE_FOREGROUND);
	debugPrintPaletteEntry("pattern command", CPATTERN_COMMAND_FOREGROUND);
	debugPrintPaletteEntry("pattern instrument", CPATTERN_INSTRUMENT_FOREGROUND);
	debugPrintPaletteEntry("pattern highlight background", CPATTERN_HIGHLIGHT_BACKGROUND);
	debugPrintPaletteEntry("pattern highlight foreground", CPATTERN_HIGHLIGHT_FOREGROUND);
	fflush(stdout);
}

//int editorInfo.maxSIDChannels = 3;	//12;
int gMIDINote = -1;

int loadedSongFlag = 0;

int jdebug[16];

WAVEFORM_INFO waveformDisplayInfo;

static int isvalidrelocatorsidaddress(int address)
{
	return (address >= 0xd400) && (address <= 0xe000) && (((address - 0xd400) & 0x1f) == 0);
}

static void decodelegacysidaddress(void)
{
	int lowaddress = sidaddress & 0xffff;
	int highaddress = (sidaddress >> 16) & 0xffff;

	if ((lowaddress == sidAddr1) && isvalidrelocatorsidaddress(highaddress))
		sidAddr2 = highaddress;
	else if ((highaddress == sidAddr1) && isvalidrelocatorsidaddress(lowaddress))
		sidAddr2 = lowaddress;
}

static void synclegacysidaddress(void)
{
	sidaddress = (unsigned int)sidAddr1 | ((unsigned int)sidAddr2 << 16);
}

static void validaterelocatorsettings(void)
{
	if ((fileformat < FORMAT_SID) || (fileformat > FORMAT_ASM))
		fileformat = FORMAT_PRG;

	if (!isvalidrelocatorsidaddress(sidAddr2)) sidAddr2 = 0xd420;
	if (!isvalidrelocatorsidaddress(sidAddr3)) sidAddr3 = 0xd440;
	if (!isvalidrelocatorsidaddress(sidAddr4)) sidAddr4 = 0xd460;

	synclegacysidaddress();
}


int main(int argc, char** argv)
{


	//	char palettename[MAX_PATHNAME];

	FILE* configfile;
	int c, d;
	int sidaddresssetfromcommandline = 0;

	debugPalette = isDebugEnvEnabled("GTULTRA_DEBUG_PALETTE");
	debugPattern = isDebugEnvEnabled("GTULTRA_DEBUG_PATTERN");
	if (debugPalette || debugPattern)
	{
		fprintf(stdout, "[debug] GTULTRA_DEBUG_PALETTE=%d GTULTRA_DEBUG_PATTERN=%d\n", debugPalette, debugPattern);
		fflush(stdout);
	}

	// JP: SDL2 produces no audio for Windows32 without explicitly setting this (otherwise, it's set to "dummy sound" as the output)
#ifdef __WIN32__
	SDL_setenv("SDL_AUDIODRIVER", "directsound", 1);
#endif


	editorInfo.multiplier = 1;
	editorInfo.finevibrato = 1;
	editorInfo.adparam = 0x0f00;

	programname += sizeof "$VER:";
	// Open datafile
	if (!io_openlinkeddatafile(datafile))
	{
		showStartupError("Could not open the linked GTUltraPro datafile.");
		return 1;
	}

	// Load configuration
#ifdef __WIN32__
	GetModuleFileName(NULL, appFileName, MAX_PATHNAME);
	appFileName[strlen(appFileName) - 3] = 'c';
	appFileName[strlen(appFileName) - 2] = 'f';
	appFileName[strlen(appFileName) - 1] = 'g';
#elif __amigaos__
	strcpy(appFileName, "PROGDIR:gtultra.cfg");
#else
	strcpy(appFileName, getenv("HOME"));
	strcat(appFileName, "/.goattrk/gtultra.cfg");
#endif

	createFilename(appFileName, charsetFilename, "charset.bin");
	createFilename(appFileName, backupSngFilename, "gtubackup.sng");
	createFilename(appFileName, fkeysFilename, "fkeys.cfg");


	// First, load the default palette and fill all 16 slots with it
	currentLoadedPresetIndex = 0;
	int maxPresetPalettes = 9;

	for (int i = 0;i < maxPresetPalettes;i++)
	{
		paletteNames[i] = NULL;
	}

	for (int i = 0;i < maxPresetPalettes;i++)
	{
		sprintf(textbuffer, "%ddefault.gtp", i);
		if (debugPalette)
			fprintf(stdout, "[palette] bundled open name='%s'\n", textbuffer);
		int handle = io_open(textbuffer);
		if (handle == -1)
		{
			snprintf(textbuffer, sizeof textbuffer, "Could not load startup palette %ddefault.gtp.", i);
			showStartupError(textbuffer);
			return 1;
		}

		int size = io_lseek(handle, 0, SEEK_END);
		io_lseek(handle, 0, SEEK_SET);
		char* paletteMem = malloc(size + 1);
		io_read(handle, paletteMem, size);
		io_close(handle);
		paletteMem[size] = 0;	// end marker
		if (debugPalette)
			fprintf(stdout, "[palette] bundled read name='%ddefault.gtp' bytes=%d startSlot=%d\n", i, size, currentLoadedPresetIndex);


		if (i == 0)
		{
			for (int j = 0;j < 16;j++)
			{
				int slot = currentLoadedPresetIndex;
				int loaded = readPaletteData(paletteMem, textbuffer);
				if (debugPalette)
					fprintf(stdout, "[palette] seed default slot=%d loaded=%d nextSlot=%d name='%s'\n", slot, loaded, currentLoadedPresetIndex, textbuffer);
			}
			currentLoadedPresetIndex = 1;
			if (debugPalette)
				fprintf(stdout, "[palette] reset next bundled slot to %d after default seeding\n", currentLoadedPresetIndex);
		}
		else
		{
			int slot = currentLoadedPresetIndex;
			int loaded = readPaletteData(paletteMem, textbuffer);
			if (debugPalette)
				fprintf(stdout, "[palette] bundled load slot=%d loaded=%d nextSlot=%d name='%s'\n", slot, loaded, currentLoadedPresetIndex, textbuffer);
		}

		free(paletteMem);
	}

	// Now load palettes from the gtpalettes folder to fill all 16 slots
	loadPalettes();
	if (debugPalette)
		fflush(stdout);

	configfile = fopen(appFileName, "rt");
	if (configfile)
	{
		getparam(configfile, &b);
		getparam(configfile, &mr);
		getparam(configfile, &hardsid);
		getparam(configfile, &editorInfo.sidmodel);
		getparam(configfile, &editorInfo.ntsc);
		getparam(configfile, (unsigned*)&fileformat);
		getparam(configfile, (unsigned*)&playeradr);
		getparam(configfile, (unsigned*)&zeropageadr);
		getparam(configfile, &playerversion);
		getparam(configfile, &keypreset);
		getparam(configfile, (unsigned*)&stepsize);

		getparam(configfile, &editorInfo.multiplier);
		getparam(configfile, &catweasel);
		getparam(configfile, &editorInfo.adparam);

		getparam(configfile, &interpolate);

		getparam(configfile, &patterndispmode);

		getparam(configfile, &sidaddress);
		decodelegacysidaddress();

		getparam(configfile, (unsigned int*)&editorInfo.finevibrato);
		getparam(configfile, &editorInfo.optimizepulse);
		getparam(configfile, &editorInfo.optimizerealtime);
		getparam(configfile, &residdelay);
		getparam(configfile, &customclockrate);
		getparam(configfile, &hardsidbufinteractive);
		getparam(configfile, &hardsidbufplayback);

		getfloatparam(configfile, &filterparams.distortionrate);
		getfloatparam(configfile, &filterparams.distortionpoint);
		getfloatparam(configfile, &filterparams.distortioncfthreshold);
		getfloatparam(configfile, &filterparams.type3baseresistance);
		getfloatparam(configfile, &filterparams.type3offset);
		getfloatparam(configfile, &filterparams.type3steepness);
		getfloatparam(configfile, &filterparams.type3minimumfetresistance);
		getfloatparam(configfile, &filterparams.type4k);
		getfloatparam(configfile, &filterparams.type4b);
		getfloatparam(configfile, &filterparams.voicenonlinearity);
		getparam(configfile, (unsigned int*)&win_fullscreen);

		getparam(configfile, &bigwindow);
		getfloatparam(configfile, &basepitch);
		getfloatparam(configfile, &equaldivisionsperoctave);
		getstringparam(configfile, specialnotenames);
		getstringparam(configfile, scalatuningfilepath);
		getparam(configfile, (unsigned int*)&editorInfo.maxSIDChannels);
		getstringparam(configfile, startPaletteName);
		//		getparam(configfile, &currentPalettePreset);
		getfloatparam(configfile, &masterVolume);
		getfloatparam(configfile, &detuneCent);
		getparam(configfile, &enablekeyrepeat);
		getparam(configfile, (unsigned int*)&selectedMIDIPort);
		getparam(configfile, (unsigned int*)&enableAntiAlias);
		getparam(configfile, (unsigned int*)&sidPanInts[0]);
		getparam(configfile, (unsigned int*)&sidPanInts[1]);
		getparam(configfile, (unsigned int*)&sidPanInts[2]);
		getparam(configfile, (unsigned int*)&sidPanInts[3]);
		getparam(configfile, (unsigned int*)&backupTimeSeconds);
		getparam(configfile, (unsigned int*)&autoNextPattern);
		getparam(configfile, (unsigned int*)&useRepeatsWhenCompressing);
		getparam(configfile, (unsigned int*)&SIDTracker64ForIPadIsAmazing);
		getparam(configfile, (unsigned int*)&debugEnabled);
		getparam(configfile, (unsigned int*)&sidAddr2);
		getparam(configfile, (unsigned int*)&sidAddr3);
		getparam(configfile, (unsigned int*)&sidAddr4);

		fclose(configfile);

	}

	setSIDTracker64KeyOnStyle();

	// Init pathnames
	initpaths();

	// Scan command line
	for (c = 1; c < argc; c++)
	{
#ifdef __WIN32__
		if ((argv[c][0] == '-') || (argv[c][0] == '/'))
#else
		if (argv[c][0] == '-')
#endif
		{
			int y = 0;
			switch (argv[c][1]) //switch (toupper(argv[c][1]))
			{
			case '?':
				if (!initscreen())
					return 1;
				if (argv[c][2] == '?') {
					onlinehelp(1, 0, &gtObject);
					return 0;
				}

				printtext(0, y++, getColor(15, 0), "Usage: GT2STEREO [songname] [options]");
				printtext(0, y++, getColor(15, 0), "Options:");
				printtext(0, y++, getColor(15, 0), "-Axx Set ADSR parameter for hardrestart in hex. DEFAULT=0F00");
				printtext(0, y++, getColor(15, 0), "-Bxx Set sound buffer length in milliseconds DEFAULT=100");
				printtext(0, y++, getColor(15, 0), "-Cxx Use CatWeasel MK3 PCI SID (0 = off, 1 = on)");
				printtext(0, y++, getColor(15, 0), "-Dxx Pattern row display (0 = decimal, 1 = hex, 2 = decimal w/dots, 3 = hex w/dots)");
				printtext(0, y++, getColor(15, 0), "-Exx Set emulated SID model (0 = 6581 1 = 8580) DEFAULT=6581");
				printtext(0, y++, getColor(15, 0), "-Fxx Set custom SID clock cycles per second (0 = use PAL/editorInfo.ntsc default)");
				printtext(0, y++, getColor(15, 0), "-Gxx Set pitch of A-4 in Hz (0 = use default frequencytable, close to 440Hz)");
				printtext(0, y++, getColor(15, 0), "-Hxx Use HardSID (0 = off, 1 = HardSID ID0 2 = HardSID ID1 etc.)");
				printtext(0, y++, getColor(15, 0), "     Use high nybble (it's hexadecimal) to specify right HardSID ID");
				printtext(0, y++, getColor(15, 0), "-Ixx Set reSID interpolation (0 = off, 1 = on, 2 = distortion, 3 = distortion & on) DEFAULT=off");
				printtext(0, y++, getColor(15, 0), "-Jxx Set special note names (2 chars for every note in an octave/cycle, e.g. C-DbD-EbE-F-GbG-AbA-BbB-)");
				printtext(0, y++, getColor(15, 0), "-Kxx Note-entry mode (0 = PROTRACKER 1 = DMC) DEFAULT=PROTRK.");
				printtext(0, y++, getColor(15, 0), "-Lxx SID memory locations in hex. DEFAULT=D500D400");
				printtext(0, y++, getColor(15, 0), "-Mxx Set sound mixing rate DEFAULT=44100");
				printtext(0, y++, getColor(15, 0), "-Oxx Set pulseoptimization/skipping (0 = off, 1 = on) DEFAULT=on");
				printtext(0, y++, getColor(15, 0), "-Qxx Set equal divisions per octave (12 = default, 8.2019143 = Bohlen-Pierce)");
				printtext(0, y++, getColor(15, 0), "-Rxx Set realtime-effect optimization/skipping (0 = off, 1 = on) DEFAULT=on");
				printtext(0, y++, getColor(15, 0), "-Sxx Set speed editorInfo.multiplier (0 for 25Hz, 1 for 1x, 2 for 2x etc.)");
				printtext(0, y++, getColor(15, 0), "-Txx Set HardSID interactive mode sound buffer length in milliseconds DEFAULT=20, max.buffering=0");
				printtext(0, y++, getColor(15, 0), "-Uxx Set HardSID playback mode sound buffer length in milliseconds DEFAULT=400, max.buffering=0");
				printtext(0, y++, getColor(15, 0), "-Vxx Set finevibrato conversion (0 = off, 1 = on) DEFAULT=on");
				printtext(0, y++, getColor(15, 0), "-Xxx Set window type (0 = window, 1 = fullscreen) DEFAULT=window");
				printtext(0, y++, getColor(15, 0), "-Yxx Path to a Scala tuning file .scl");
				printtext(0, y++, getColor(15, 0), "-Zxx Set random reSID write delay in cycles (0 = off) DEFAULT=off");
				printtext(0, y++, getColor(15, 0), "-wxx Set window scale factor (1 = no scaling, 2 to 4 = 2 to 4 times bigger window) DEFAULT=1");
				printtext(0, y++, getColor(15, 0), "-N   Use editorInfo.ntsc timing");
				printtext(0, y++, getColor(15, 0), "-P   Use PAL timing (DEFAULT)");
				printtext(0, y++, getColor(15, 0), "-W   Write sound output to a file SIDAUDIO.RAW");
				printtext(0, y++, getColor(15, 0), "-cxx SID channel count (3,6,9 or 12) DEFAULT=6");
				printtext(0, y++, getColor(15, 0), "-pxx set UI Skin (0-3) DEFAULT=0");
				printtext(0, y++, getColor(15, 0), "-vxx Master Volume (floating point) DEFAULT=1(large values may cause clipping / distortion)");
				printtext(0, y++, getColor(15, 0), "-dxxx Detune Pitchtable (-1 > 1 0 = no detune. -1 = -1 semitone 1 = +1 semitone");
				printtext(0, y++, getColor(15, 0), "-kx  Enable key repeat (0=only on selected keys. 1= on everything (DEFAULT 0)");
				printtext(0, y++, getColor(15, 0), "-mxx MIDI Port (DEFAULT 0.  9999 = disable all MIDI processing)");
				printtext(0, y++, getColor(15, 0), "-ax  enable antialiasing (0=off. 1 = on. DEFAULT=1)");
				printtext(0, y++, getColor(15, 0), "-bxxxx  Backup .sng every n seconds (0=off. DEFAULT=30");
				printtext(0, y++, getColor(15, 0), "-?   Show this info again");
				printtext(0, y++, getColor(15, 0), "-??  Standalone online help window");
				waitkeynoupdate();
				return 0;

			case 'Z':
				sscanf(&argv[c][2], "%u", &residdelay);
				break;

			case 'A':
				sscanf(&argv[c][2], "%x", &editorInfo.adparam);
				break;

			case 'S':
				sscanf(&argv[c][2], "%u", &editorInfo.multiplier);
				break;

			case 'B':
				sscanf(&argv[c][2], "%u", &b);
				break;

			case 'D':
				sscanf(&argv[c][2], "%u", &patterndispmode);
				break;

			case 'E':
				sscanf(&argv[c][2], "%u", &editorInfo.sidmodel);
				break;

			case 'I':
				sscanf(&argv[c][2], "%u", &interpolate);
				break;

			case 'K':
				sscanf(&argv[c][2], "%u", &keypreset);
				break;

			case 'L':
				sscanf(&argv[c][2], "%x", &sidaddress);
				sidaddresssetfromcommandline = 1;
				break;

			case 'N':
				editorInfo.ntsc = 1;
				customclockrate = 0;
				break;

			case 'P':
				editorInfo.ntsc = 0;
				customclockrate = 0;
				break;

			case 'F':
				sscanf(&argv[c][2], "%u", &customclockrate);
				break;

			case 'M':
				sscanf(&argv[c][2], "%u", &mr);
				break;

			case 'O':
				sscanf(&argv[c][2], "%u", &editorInfo.optimizepulse);
				break;

			case 'R':
				sscanf(&argv[c][2], "%u", &editorInfo.optimizerealtime);
				break;

			case 'H':
				sscanf(&argv[c][2], "%x", &hardsid);
				break;

			case 'V':
				sscanf(&argv[c][2], "%u", &editorInfo.finevibrato);
				break;

			case 'T':
				sscanf(&argv[c][2], "%u", &hardsidbufinteractive);
				break;

			case 'U':
				sscanf(&argv[c][2], "%u", &hardsidbufplayback);
				break;

			case 'W':
				writer = 1;
				break;

			case 'X':
				sscanf(&argv[c][2], "%u", &win_fullscreen);
				break;

			case 'C':
				sscanf(&argv[c][2], "%u", &catweasel);
				break;

			case 'G':
				sscanf(&argv[c][2], "%f", &basepitch);
				break;

			case 'Q':
				sscanf(&argv[c][2], "%f", &equaldivisionsperoctave);
				break;

			case 'J':
				sscanf(&argv[c][2], "%s", specialnotenames);
				break;

			case 'Y':
				sscanf(&argv[c][2], "%s", scalatuningfilepath);
				break;

			case 'w':
				sscanf(&argv[c][2], "%u", &bigwindow);
				break;

			case 'c':
				sscanf(&argv[c][2], "%d", &editorInfo.maxSIDChannels);

			case 'p':
				sscanf(&argv[c][2], "%d", &currentPalettePreset);

			case 'v':
				sscanf(&argv[c][2], "%f", &masterVolume);

			case 'd':
				sscanf(&argv[c][2], "%f", &detuneCent);

			case 'k':
				sscanf(&argv[c][2], "%d", &enablekeyrepeat);

			case 'm':
				sscanf(&argv[c][2], "%d", &selectedMIDIPort);

			case'a':
				sscanf(&argv[c][2], "%d", &enableAntiAlias);

			case'b':
				sscanf(&argv[c][2], "%d", &backupTimeSeconds);
			}
		}
		else
		{
			memset(specialnotenames, 0, 186);

			char startpath[MAX_PATHNAME];

			// JP - BUG in original GTStereo fix!
			// if argv[c] is > 60, only strcpy if the path contains no folders
			//strcpy(songfilename, argv[c]);

			int foundFileName = 0;	// JP Fix
			for (d = strlen(argv[c]) - 1; d >= 0; d--)
			{
				if ((argv[c][d] == '/') || (argv[c][d] == '\\'))
				{
					strcpy(startpath, argv[c]);
					startpath[d + 1] = 0;
					chdir(startpath);
					initpaths();
					strcpy(songfilename, &argv[c][d + 1]);
					foundFileName++;
					break;
				}
			}
			if (!foundFileName)
			{
				strcpy(songfilename, argv[c]);	// JP - Fix bug
			}
		}
	}

	if (sidaddresssetfromcommandline)
		decodelegacysidaddress();

	fkeys_loadCFG();	// Load fkeys.cfg file and process (user defined F1-F4)

	// Validate parameters

	if (selectedMIDIPort == 9999)	// GAHHHH!!! 1.4.1 fix. (just had the single = )
		midiEnabled = 0;	// No MIDI processing will take place if MIDIPort is set to 9999 within .cfg file or via -m commandline option
	else
		midiEnabled = 1;

	for (int i = 0;i < 4;i++)
	{
		convertInsToPans(i);
	}

	// Search to see if any of the palettes match the name in the cfg file. If so, use that one
	currentPalettePreset = 0;
	for (int i = 0;i < MAX_PALETTE_PRESETS;i++)
	{
		if (paletteNames[i] && !strcmp(startPaletteName, paletteNames[i]))
		{
			currentPalettePreset = i;
			break;
		}
	}
	if (debugPalette)
	{
		fprintf(stdout, "[palette] config startPaletteName='%s' selectedPreset=%d selectedName='%s'\n",
			startPaletteName,
			currentPalettePreset,
			paletteNames[currentPalettePreset] ? paletteNames[currentPalettePreset] : "(null)");
	}

	if (!isPatternNotePaletteReadable(currentPalettePreset))
	{
		if (debugPalette)
			fprintf(stdout, "[palette] selected preset %d note foreground is not readable against pattern backgrounds; falling back to preset 0\n", currentPalettePreset);
		currentPalettePreset = 0;
		if (isPatternNotePaletteReadable(currentPalettePreset))
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "Startup palette was unreadable; using default palette");
	}

	//	if (currentPalettePreset >= MAX_PALETTE_PRESETS)
	//		currentPalettePreset = 0;

	if (editorInfo.maxSIDChannels != 3 && editorInfo.maxSIDChannels != 6 && editorInfo.maxSIDChannels != 9 && editorInfo.maxSIDChannels != 12)
		editorInfo.maxSIDChannels = 6;

	validaterelocatorsettings();

	editorInfo.sidmodel &= 1;
	editorInfo.adparam &= 0xffff;
	zeropageadr &= 0xff;
	playeradr &= 0xff00;
	if (!stepsize) stepsize = 4;
	if (editorInfo.multiplier > 16) editorInfo.multiplier = 16;
	if (keypreset > 2) keypreset = 0;
	if ((editorInfo.finevibrato == 1) && (editorInfo.multiplier < 2)) editorInfo.usefinevib = 1;
	if (editorInfo.finevibrato > 1) editorInfo.usefinevib = 1;
	if (editorInfo.optimizepulse > 1) editorInfo.optimizepulse = 1;
	if (editorInfo.optimizerealtime > 1) editorInfo.optimizerealtime = 1;
	if (residdelay > 63) residdelay = 63;
	if (customclockrate < 100) customclockrate = 0;

	if ((detuneCent < -1) || (detuneCent > 1))
	{
		detuneCent = 0;
	}

	if (enablekeyrepeat > 1)
		enablekeyrepeat = 0;

	// Read Scala tuning file
	if (scalatuningfilepath[0] != '0' && scalatuningfilepath[1] != '\0')
	{
		readscalatuningfile();
	}

	// Calculate frequencytable if necessary
	if (basepitch < 0.0f)
		basepitch = 0.0f;
	if (basepitch > 0.0f || detuneCent != 1)
		calculatefreqtable();

	// Set special note names
	if (specialnotenames[1] != '\0')
	{
		setspecialnotenames();
	}

	// JP - Init MIDI (yes. MIDI)

	if (midiEnabled)
		selectedMIDIPort = initMidi(selectedMIDIPort);

	// Set screenmode
	if (!initscreen())
	{
		snprintf(textbuffer, sizeof textbuffer, "Could not initialize the GTUltraPro screen: %s", SDL_GetError());
		showStartupError(textbuffer);
		return 1;
	}


	waveformDisplayInfo.displayOnOff = 0;

	initPaletteDisplay();
	debugPrintActivePalette();
	setTableBackgroundColours(0);

	initPolyKeyboard();
	// Reset channels/song
	initchannels(&gtObject);
	clearsong(1, 1, 1, 1, 1, &gtObject);

	copyExpandedSongValidFlag = 0;

	gtObject.masterLoopSubSong = 0;
	gtObject.masterLoopChannel = 0;
	initAreaListFlag = 0;
	initUndoBufferFlag = 0;
	undoInitAllAreas(&gtObject);	// Must be called after clearSong. Creates undo buffers, containing duplicates of each GT area.


	// Init sound
	if (!sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate))
	{
		printtextc(MAX_ROWS / 2 - 1, getColor(15, 0), "Sound init failed. Press any key to run without sound (notice that song timer won't start)");
		waitkeynoupdate();
	}



	// JP - Init Editor info
	editorInfo.editmode = EDIT_PATTERN;
	editorInfo.epoctave = 2;
	editorInfo.epmarkchn = -1;
	editorInfo.esmarkchn = -1;
	editorInfo.esmarkchnend = -1;
	editorInfo.etlock = 0;		// was 1. changed to 0 for LMAN mode (tables unlocked)
	editorInfo.etmarknum = -1;

	editorInfo.einum = 1;	//jp
	disableEnterToReturnToLastPos = 1;

	// JP - Init GTObject
	gtObject.masterfader = 0xf;
	gtObject.controlEditor = 1;
	gtObject.noSIDWrites = 0;
	gtEditorObject.noSIDWrites = 1;
	gtLoopObject.noSIDWrites = 1;
	gtEditorLoopObject.noSIDWrites = 1;

	initSID(&gtObject);

	playUntilEnd(editorInfo.esnum);	// Get length of time of loaded or empty song

	initSngMemory();
	lastValidSongFileIndex = 0;
	currentSongFile = 0;	// V1.4.0
	allocateSngMemory(0);	// V1.4.0
	copyCurrentToSngBuffer(&gtObject, 0);
	allocateSngMemory(1);	// V1.4.0
	copyCurrentToSngBuffer(&gtObject, 1);

	allocateSngMemory(MAX_SONG_FILES);		// this is never overwritten. Use to create an empty sng
	copyCurrentToSngBuffer(&gtObject, MAX_SONG_FILES);




	//-------------------------------------------------------------
#if 0
	strcpy(songfilename, "ultestura.sng");
	editorInfo.maxSIDChannels = 3;
#endif
	// Load song if applicable
	if (strlen(songfilename))
	{
		int ok = loadsong(&gtObject, 0);
		if (ok)
		{
			loadedSongFlag = 1;
			undoInitAllAreas(&gtObject);	// recreate undo buffers, using the loaded song as the original info
			countInstruments();
			setTableBackgroundColours(editorInfo.einum);
		}

		playUntilEnd(editorInfo.esnum);	// Get length of time of loaded or empty song
		copyCurrentToSngBuffer(&gtObject, editorInfo.currentSongFile);
	}


#if 0
	initsong(editorInfo.esnum, PLAY_BEGINNING, &gtObject);
	followplay = shiftOrCtrlPressed;
	while (!exitprogram)
	{
		//waitkeymouse(&gtObject);
		if (key)
		{
			// Shutdown sound output now
			sound_uninit();
			return 0;
		}

	}

#endif

	// Start editor mainloop
	printmainscreen(&gtObject);

	//	SDL_Thread* threadID = SDL_CreateThread(doDisplay, "DisplayThread", (void*)&gtObject);


	while (!exitprogram)
	{

		if (doExportToWAV)
		{
			doExportToWAV = 0;
			ExportAsPCM(editorInfo.esnum, normalizeWAV, &gtObject);
		}
		//	int ch = checkFor3ChannelSong();

		waitkeymouse(&gtObject);
		docommand();


		//	sprintf(textbuffer, "jpdebug %d", jdebug[0]);	//, specialnotenames[0], specialnotenames[1]);
		//	printtext(70, 36, 0xe, textbuffer);
	}

	//SDL_WaitThread(threadID, NULL);

	// Shutdown sound output now
	sound_uninit();
	gt_video_close();

	/*
	#ifndef __WIN32__
	#ifdef __amigaos__
		strcpy(filename, "PROGDIR:gtskins.bin");
	#else
		strcpy(filename, getenv("HOME"));
		strcat(filename, "/.goattrk");
		mkdir(filename, S_IRUSR | S_IWUSR | S_IXUSR);
		strcat(filename, "/gtskins.bin");
	#endif
	#endif

	*/
	//	paletteChanged = 0;	// JP TEST TO REMOVE SAVE 
	//	if (paletteChanged)
	//	{
	//		configfile = fopen("gtskins.bin", "wb");		// wb write binary. wt = write text
	//		if (configfile)
	//		{
	//			fwrite(&paletteRGB, MAX_PALETTE_PRESETS * 3 * MAX_PALETTE_ENTRIES, 1, configfile);
	//			fclose(configfile);
	//		}
	//	}


		// Save configuration
#ifndef __WIN32__
#ifdef __amigaos__
	strcpy(appFileName, "PROGDIR:goattrk2.cfg");
#else
	strcpy(appFileName, getenv("HOME"));
	strcat(appFileName, "/.goattrk");
	mkdir(appFileName, S_IRUSR | S_IWUSR | S_IXUSR);
	strcat(appFileName, "/gtultra.cfg");
#endif
#endif
	synclegacysidaddress();
	configfile = fopen(appFileName, "wt");
	if (configfile)
	{
		fprintf(configfile, ";------------------------------------------------------------------------------\n"
			";GT2 config file. Rows starting with ; are comments. Hexadecimal parameters are\n"
			";to be preceded with $ and decimal parameters with nothing.                    \n"
			";------------------------------------------------------------------------------\n"
			"\n"
			";reSID buffer length (in milliseconds)\n%d\n\n"
			";reSID mixing rate (in Hz)\n%d\n\n"
			";Hardsid device number (0 = off)\n%d\n\n"
			";reSID model (0 = 6581, 1 = 8580)\n%d\n\n"
			";Timing mode (0 = PAL, 1 = editorInfo.ntsc)\n%d\n\n"
			";Packer/relocator fileformat (0 = SID, 1 = PRG, 2 = BIN, 3 = ASM)\n%d\n\n"
			";Packer/relocator player address\n$%04x\n\n"
			";Packer/relocator zeropage baseaddress\n$%02x\n\n"
			";Packer/relocator player type (0 = standard ... 3 = minimal)\n%d\n\n"
			";Key entry mode (0 = Protracker, 1 = DMC, 2 = Janko)\n%d\n\n"
			";Pattern highlight step size\n%d\n\n"
			";Speed editorInfo. (0 = 25Hz, 1 = 1X, 2 = 2X etc.)\n%d\n\n"
			";Use CatWeasel SID (0 = off, 1 = on)\n%d\n\n"
			";Hardrestart ADSR parameter\n$%04x\n\n"
			";reSID interpolation (0 = off, 1 = on, 2 = distortion, 3 = distortion & on)\n%d\n\n"
			";Pattern display mode (0 = decimal, 1 = hex, 2 = decimal w/dots, 3 = hex w/dots)\n%d\n\n"
			";SID baseaddresses\n$%08x\n\n"
			";Finevibrato mode (0 = off, 1 = on)\n%d\n\n"
			";Pulseskipping (0 = off, 1 = on)\n%d\n\n"
			";Realtime effect skipping (0 = off, 1 = on)\n%d\n\n"
			";Random reSID write delay in cycles (0 = off)\n%d\n\n"
			";Custom SID clock cycles per second (0 = use PAL/editorInfo.ntsc default)\n%d\n\n"
			";HardSID interactive mode buffer size (in milliseconds, 0 = maximum/no flush)\n%d\n\n"
			";HardSID playback mode buffer size (in milliseconds, 0 = maximum/no flush)\n%d\n\n"
			";reSID-fp distortion rate\n%f\n\n"
			";reSID-fp distortion point\n%f\n\n"
			";reSID-fp distortion CF threshold\n%f\n\n"
			";reSID-fp type 3 base resistance\n%f\n\n"
			";reSID-fp type 3 base offset\n%f\n\n"
			";reSID-fp type 3 base steepness\n%f\n\n"
			";reSID-fp type 3 minimum FET resistance\n%f\n\n"
			";reSID-fp type 4 k\n%f\n\n"
			";reSID-fp type 4 b\n%f\n\n"
			";reSID-fp voice nonlinearity\n%f\n\n"
			";Window type (0 = window, 1 = fullscreen)\n%d\n\n"
			";window scale factor (1 = no scaling, 2 to 4 = 2 to 4 times bigger window)\n%d\n\n"
			";Base pitch of A-4 in Hz (0 = use default frequencytable)\n%f\n\n"
			";Equal divisions per octave (12 = default, 8.2019143 = Bohlen-Pierce)\n%f\n\n"
			";Special note names (2 chars for every note in an octave/cycle)\n%s\n\n"
			";Path to a Scala tuning file .scl\n%s\n\n"
			";Default SID channel playback\n%d\n\n"
			";Default palette name\n%s\n\n"
			";Master Volume scaler (1 = normal volume. 2 = twice as loud 0.5 = half volume..)\n%f\n\n"
			";Detune Cent (0-2... 1 = no detune. 0 =-100 cents. 2=+100 cents)\n%f\n\n"
			";Enable Key repeat (0-1... 0=only on specific keys. 1=on all keys)\n%d\n\n"
			";MIDI Port\n%d\n\n"
			";Enable Antialias\n%d\n\n"
			";SID1 Pan\n$%04x\n\n"
			";SID2 Pan\n$%04x\n\n"
			";SID3 Pan\n$%04x\n\n"
			";SID4 Pan\n$%04x\n\n"
			";Backup sng every n seconds (0=OFF. Default = 30)\n%d\n\n"
			";AutoNextPattern Automatically move to next or previous pattern in order list when moving cursor in pattern view (0=OFF. 1=ON)\n%d\n\n"
			";Use repeats when compressing from expanded orderlist view (0=NO. 1=YES)\n%d\n\n"
			";SIDTracker64 style pattern editing (SIDTracker64 IS Amazing) (0=NO. 1=YES. WARNING. NOT COMPATIBLE WITH STANDARD GOATTRACKER EDITING!!)\n%d\n\n"
			";Perform MemoryChecks (Debug)\n%d\n\n"
			";Packer/relocator SID2 address\n$%04x\n\n"
			";Packer/relocator SID3 address\n$%04x\n\n"
			";Packer/relocator SID4 address\n$%04x\n\n",
			b,
			mr,
			hardsid,
			editorInfo.sidmodel,
			editorInfo.ntsc,
			fileformat,
			playeradr,
			zeropageadr,
			playerversion,
			keypreset,
			stepsize,
			editorInfo.multiplier,
			catweasel,
			editorInfo.adparam,
			interpolate,
			patterndispmode,
			sidaddress,
			editorInfo.finevibrato,
			editorInfo.optimizepulse,
			editorInfo.optimizerealtime,
			residdelay,
			customclockrate,
			hardsidbufinteractive,
			hardsidbufplayback,
			filterparams.distortionrate,
			filterparams.distortionpoint,
			filterparams.distortioncfthreshold,
			filterparams.type3baseresistance,
			filterparams.type3offset,
			filterparams.type3steepness,
			filterparams.type3minimumfetresistance,
			filterparams.type4k,
			filterparams.type4b,
			filterparams.voicenonlinearity,
			win_fullscreen,
			bigwindow,
			basepitch,
			equaldivisionsperoctave,
			specialnotenames,
			scalatuningfilepath,
			editorInfo.maxSIDChannels,
			paletteNames[currentPalettePreset],
			masterVolume,
			detuneCent,
			enablekeyrepeat,
			selectedMIDIPort,
			enableAntiAlias,
			sidPanInts[0],
			sidPanInts[1],
			sidPanInts[2],
			sidPanInts[3],
			backupTimeSeconds,
			autoNextPattern,
			useRepeatsWhenCompressing,
			SIDTracker64ForIPadIsAmazing,
			debugEnabled,
			sidAddr2,
			sidAddr3,
			sidAddr4
		);

		fclose(configfile);
	}

	// JP - ONLY THIS MODE IS CURRENTLY SUPPORTED FOR STEREO SID PANNING
	// 0 crashes things..
//	if (interpolate != 3)
//		interpolate = 3;

	// Exit
	return 0;
}

void waitkey(GTOBJECT* gt)
{
	for (;;)
	{
		if (!jdebugPlaying)
		{
			displayupdate(gt);
		}
		gt_video_tick(gt);
		getkey();
		if ((rawkey) || (key)) break;
		if (win_quitted) break;
	}

	converthex();
}

MIDI_MESSAGE midiMessage;

int refreshSongTime = 0;
int refreshSongInfoDeltaTime = 0;
int refreshCount = 0;

int jcnt2 = 0;
int forceKeys = 1;

int backupSongTimer = 0;

void waitkeymouse(GTOBJECT* gt)
{
	//int jc = 0;
	//int rk = 0;

	for (;;)
	{
		SDL_Delay(10);	// add this

		if (dropFileDir != NULL)
		{
			handleLoad(gt, dropFileDir);
			dropFileDir = NULL;
		}

		if (backupTimeSeconds > 0)
		{
			msDelta = SDL_GetTicks() - lastMS;

			backupSongTimer += msDelta;
			if (backupSongTimer > backupTimeSeconds * 1000)
			{
				if (gt->songinit == PLAY_STOPPED)
				{
					if (currentUndoPosition != lastUndoPosition)	// Only auto-save if something has changed..
					{
						lastUndoPosition = currentUndoPosition;
						int allowBackup = 1;
						if (editorInfo.expandOrderListView)
						{
							int maxSize = validateAllSongs();
							if (maxSize < 0xff)
								compressAllSongs();
							else
								allowBackup = 0;
						}
						if (allowBackup)
							saveBackupSong();
						backupSongTimer = 0;
					}
				}
			}
			lastMS = SDL_GetTicks();
		}



		if (!jdebugPlaying)
			displayupdate(gt);
		gt_video_tick(gt);

		getkey();
		if (mouseb)
		{
			break;
		}
		else if (prevmouseb)
		{
			break;		// Handle modifying values when hold / dragging. We've released the mouse
		}

		editorInfo.mouseTrack = 0;


#if 0
		// Debug - Force key presses
		if (debugCurrentUndoBufferSize < 1000)
		{
			if (forceKeys)
			{
				jcnt2++;
				jcnt2 %= 3;
				key = KEY_O + jcnt2;
				rawkey = key;
			}
		}
		else
			forceKeys = 0;
#endif

		if (win_mousewheel)
		{
			int keyUp = KEY_UP;
			int keyDown = KEY_DOWN;

			if (editorInfo.editmode == EDIT_ORDERLIST && editorInfo.expandOrderListView == 0)
			{
				keyUp = KEY_LEFT;
				keyDown = KEY_RIGHT;
			}

			if (win_mousewheel < 0)
				rawkey = keyDown;
			else
				rawkey = keyUp;

			win_mousewheel = 0;
		}
		// Debug end

		handlePolyphonicKeyboard(&gtObject);

		if ((rawkey) || (key))
		{
			break;
		}
		if (win_quitted) break;


		win_enableKeyRepeat();

		//	SDL_Delay(50);

		midiMessage.size = 0;

		/*
		Allow MIDI Jamming if not editing PATTERN (so also enable if we've got cursor on other areas)
		*/
		gMIDINote = -1;

		//		sprintf(textbuffer, "%x,%x,%x", jdebug[1], jdebug[2], jdebug[3]);
		//		printtext(70, 36, 0xe, textbuffer);

		//		sprintf(textbuffer, "hello: %s", paletteStringBuffer);	// paletteFolderEntry->d_name);
		//		printtext(5, 37, 0xe, textbuffer);

		if (!jdebugPlaying)
		{
			if (recordmode && editorInfo.editmode == EDIT_PATTERN && midiEnabled)
			{
				if (midiEnabled)
				{
					checkForMidiInput(&midiMessage, selectedMIDIPort);
					int i = 0;
					for (int c = 0;c < midiMessage.size / 3;c++)
					{
						unsigned char midiInstruction = midiMessage.message[i];
						unsigned char midiNote = midiMessage.message[i + 1];
						unsigned char midiVel = midiMessage.message[i + 2];
						i += 3;

						if (midiInstruction == 0x90 && midiVel > 0)	// key on
						{
							gMIDINote = midiNote + FIRSTNOTE;	// editing pattern data and have received keyon from MIDI device
							key = 0;
							rawkey = 0;
							handleMIDIPolykeyboard(&gtObject, midiMessage);
							return;
						}
						else
							handleMIDIPolykeyboard(&gtObject, midiMessage);
					}
				}
			}
			else  if ((!recordmode) || (editorInfo.epcolumn == 0 && editorInfo.editmode == EDIT_PATTERN))	//else if ((!recordmode) || (recordmode && editorInfo.editmode != EDIT_PATTERN))
			{
				if (midiEnabled)
				{
					do {

						checkForMidiInput(&midiMessage, selectedMIDIPort);
						handleMIDIPolykeyboard(&gtObject, midiMessage);

					} while (midiMessage.size);
				}

				handlePolyphonicKeyboard(&gtObject);	// update for QWERTY too


				// Need to change this so that it checks actual keyed on channels, rather than keys pressed


				if (!checkAnyPolyPlaying())
				{
					for (int i = 0;i < KEYBOARD_POLYPHONY;i++)
					{
						clearPolyChannel(i, gt);
					}
					if (clearInfoLine)
					{
						clearInfoLine = 0;
						if (editorInfo.editmode == EDIT_PATTERN)
						{
							lastInfoPatternCh = -1;	// force text
							displayPatternInfo(gt);
						}
						else
						{
							sprintf(&keyOffsetText[0], "                        ");
							sprintf(infoTextBuffer, "%s", keyOffsetText);
						}
					}
				}
				else
				{
					calculateNoteOffsets();
					sprintf(infoTextBuffer, "%s", keyOffsetText);
				}
			}
		}
	}
	converthex();

}

void waitkeymousenoupdate(void)
{
	for (;;)
	{
		fliptoscreen();
		getkey();
		if ((rawkey) || (key)) break;
		if (win_quitted) break;
		if (mouseb) break;
	}

	converthex();
}

void waitkeynoupdate(void)
{
	for (;;)
	{
		fliptoscreen();
		getkey();
		if ((rawkey) || (key)) break;
		if ((mouseb) && (!prevmouseb)) break;
		if (win_quitted) break;

	}
}

void converthex()
{
	int c;

	hexnybble = -1;
	for (c = 0; c < 16; c++)
	{
		if (tolower(key) == hexkeytbl[c])
		{
			if (c >= 10)
			{
				if (!shiftOrCtrlPressed) hexnybble = c;
			}
			else
			{
				hexnybble = c;
			}
		}
	}
}


void docommand(void)
{

	//int i = 0;
	//	for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i) {
	//		sprintf(textbuffer, "Audio driver %d: %s\n", i,  SDL_GetAudioDriver(0));
	//		printtext(70, 36, 0xe, textbuffer);
	//	}

	int c2;
	GTOBJECT* gt;

	gt = &gtObject;

	// "GUI" operation :)
	int m = mousebDoubleClick;
	mousecommands(gt);
	if (m)
		mousebDoubleClick = 0;

	GTUNDO_OBJECT* ed = undoCreateEditorInfo();

	// Mode-specific commands
	switch (editorInfo.editmode)
	{

	case EDIT_ORDERLIST:

		// We need to check all channels in order list incase user presses shift1-6 to swap them around
		// (we could just set this for the other channe in orderlistcommands - but this is just safer overall..)

		if (editorInfo.expandOrderListView == 0)
		{
			for (int i = 0;i < MAX_CHN;i++)
			{
				undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, i + (editorInfo.esnum * MAX_CHN), UNDO_AREA_DIRTY_CHECK);
			}

			undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
		}
		else
		{
			for (int i = 0;i < MAX_CHN;i++)
			{
				undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_PATTERN_EXPANDED, i + (editorInfo.esnum * MAX_CHN), UNDO_AREA_DIRTY_CHECK);
				undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_TRANSPOSE_EXPANDED, i + (editorInfo.esnum * MAX_CHN), UNDO_AREA_DIRTY_CHECK);
			}
			undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LENGTH_EXPANDED, 0, UNDO_AREA_DIRTY_CHECK);
		}

		c2 = getActualChannel(editorInfo.esnum, editorInfo.eschn);

		//	undoAreaSetCheckForChange(UNDO_AREA_CHANNEL_EDITOR_INFO, c2, UNDO_AREA_DIRTY_CHECK);

		orderlistcommands(gt);
		displayOrderTableInfo(gt);
		break;

	case EDIT_INSTRUMENT:



		if (mouseTrackModify(EDIT_INSTRUMENT))
		{
			undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, editorInfo.einum, UNDO_AREA_DIRTY_CHECK);
		}
		instrumentcommands(gt);
		displayInstrumentInfo(gt);
		break;

	case EDIT_TABLES:

		if (mouseTrackModify(EDIT_TABLES))
		{
			undoAreaSetCheckForChange(UNDO_AREA_INSTRUMENTS, editorInfo.einum, UNDO_AREA_DIRTY_CHECK);
			undoAreaSetCheckForChange(UNDO_AREA_TABLES + editorInfo.etnum, 0, UNDO_AREA_DIRTY_CHECK);	// left table
			undoAreaSetCheckForChange(UNDO_AREA_TABLES + editorInfo.etnum, 1, UNDO_AREA_DIRTY_CHECK);	// right table
		}

		tablecommands(gt);
		displayTableInfo(gt);
		break;

	case EDIT_PATTERN:

		c2 = getActualChannel(editorInfo.esnum, editorInfo.epchn);
		undoAreaSetCheckForChange(UNDO_AREA_PATTERN, gt->editorUndoInfo.editorInfo[c2].epnum, UNDO_AREA_DIRTY_CHECK);
		undoAreaSetCheckForChange(UNDO_AREA_PATTERN_LEN, 0, UNDO_AREA_DIRTY_CHECK);

		if (editorInfo.expandOrderListView == 0)
		{
			for (int i = 0;i < MAX_CHN;i++)
			{
				undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST, i + (editorInfo.esnum * MAX_CHN), UNDO_AREA_DIRTY_CHECK);
			}
			undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LEN, 0, UNDO_AREA_DIRTY_CHECK);
		}
		else
		{
			for (int i = 0;i < MAX_CHN;i++)
			{
				undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_PATTERN_EXPANDED, i + (editorInfo.esnum * MAX_CHN), UNDO_AREA_DIRTY_CHECK);
				undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_TRANSPOSE_EXPANDED, i + (editorInfo.esnum * MAX_CHN), UNDO_AREA_DIRTY_CHECK);
			}
			undoAreaSetCheckForChange(UNDO_AREA_ORDERLIST_LENGTH_EXPANDED, 0, UNDO_AREA_DIRTY_CHECK);
		}

		// JP REMOVED THIS. SEEMS TO CAUSE PROBLEMS..
	//	undoAreaSetCheckForChange(UNDO_AREA_CHANNEL_EDITOR_INFO, c2, UNDO_AREA_DIRTY_CHECK);

		// if gMIDINote!=-1, then use this as input instead of QWERTY note input
		// Also, if this is the case, set key and rawkey=0 so that only note input is recognised - just in case..
		patterncommands(gt, gMIDINote);

		displayPatternInfo(gt);
		countInstrumentsInPattern(gt->editorUndoInfo.editorInfo[c2].epnum);
		calculateTotalInstrumentsFromAllPatterns();
		break;

		case EDIT_NAMES:
			namecommands(gt);
			break;

		case EDIT_MOD:
			ptmodsettingscommands(gt);
			break;
		}



	if (!editPaletteMode)
	{
		if (undoValidateUndoAreas(ed) == 0)
		{
			undoFreeUndoObject((GTUNDO_OBJECT*)ed);
		}
	}
	else
		undoFreeUndoObject((GTUNDO_OBJECT*)ed);

	// General commands
	generalcommands(gt);
}

void mousecommands(GTOBJECT* gt)
{
	int c;

	if (!mouseb)
	{
	}
	else if (editorInfo.mouseTrack)
	{
		return;
	}

	if (mouseTransportBar(gt))
		return;

	if (ptmodMouseCommands(gt))
		return;

	/*
		if (editPaletteMode)
		{

			if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == 2) && (mousex >= 63 + 21) && (mousex <= 64 + 21))
			{
				if (mouseb & MOUSEB_LEFT) nextsong(gt);
				if (mouseb & MOUSEB_RIGHT) prevsong(gt);
			}

			// Song editpos & songnumber selection
			if ((mousey >= 3) && (mousey <= 5) && (mousex >= 40 + 21) && mouseb)
			{
				if (editorInfo.editmode != EDIT_ORDERLIST && prevmouseb)
					return;

				// editing palette, so don't allow user to click elsewhere.

				int newpos = editorInfo.esview + (mousex - 44 - 21) / 3;
				int newcolumn = (mousex - 44 - 21) % 3;
				int newchn = mousey - 3;
				if (newcolumn < 0) newcolumn = 0;
				if (newcolumn > 1) newcolumn = 1;
				if (newpos < 0)
				{
					newpos = 0;
					newcolumn = 0;
				}

				int maxPaletteText = getPaletteTextArraySize();

				if (newpos >= maxPaletteText / 2)
				{
					newpos = (maxPaletteText / 2) - 1;
					newcolumn = 1;
				}

				editorInfo.eschn = newchn;
				editorInfo.eseditpos = newpos;
				editorInfo.escolumn = newcolumn;

				editorInfo.editmode = EDIT_ORDERLIST;

			}
			return;
		}
	*/

	// V1.2.2 Fix - Ensure mouse clicking for 3 channel or 6 channel view is correct for mute + pattern change X positions
	int patternWidth = getPatternChannelWidth();
	int patternTextWidth = 7;
	int chTextWidth = 2;
	int chTextPos = 6;
	//int ok = 0;
	if (displayOriginal3Channel)
	{
		patternWidth = 14;
		patternTextWidth = 9;
		chTextWidth = 3;
		chTextPos = 11;
	}


	// Pattern editpos & pattern number selection
	for (c = 0; c < getVisibleChannelCount(); c++)
	{
		int patternActualChannel = getVisualChannelActualChannel(c);
		int patternLocalChannel = getVisualChannelLocalChannel(c);

		if (mousey == PATTERN_Y)
		{
			if ((mousex >= PATTERN_X + 5 + chTextPos + c * patternWidth) && (mousex <= PATTERN_X + 5 + chTextPos + 1 + c * patternWidth))
			{
				if ((!prevmouseb) || (mouseheld > HOLDDELAY))
				{
					if (mouseb & MOUSEB_LEFT)
					{
						setEditorVisualPatternChannel(c);
						nextpattern(gt);
					}
					if (mouseb & MOUSEB_RIGHT)
					{
						setEditorVisualPatternChannel(c);
						prevpattern(gt);
					}
				}
			}
			else if (mouseb && !prevmouseb)
			{

				if ((mousex >= PATTERN_X + 5 + c * patternWidth) && (mousex <= PATTERN_X + 5 + chTextWidth + c * patternWidth))
				{
					setEditorVisualPatternChannel(c);
					mutechannel(patternLocalChannel, gt);
				}
			}

		}
		else
		{
			if (!selectingInOrderList)
			{
				if ((mousey >= PATTERN_Y) && (mousey <= PATTERN_Y + VISIBLEPATTROWS + 0) && (mousex >= PATTERN_X + 5 + c * patternWidth) && (mousex <= PATTERN_X + 5 + patternTextWidth + c * patternWidth))
				{
					if (!mouseb)
						return;
					if (editorInfo.editmode != EDIT_PATTERN && prevmouseb)	// Don't allow hold/drag to select another panel
						return;

					int x = mousex - (PATTERN_X + 5) - c * patternWidth;
					int newpos = mousey - PATTERN_Y + 1 + 12 + editorInfo.epview - VISIBLEPATTROWS / 2;

					if (newpos < 0) newpos = 0;
					if (newpos > pattlen[gt->editorUndoInfo.editorInfo[patternActualChannel].epnum])
						newpos = pattlen[gt->editorUndoInfo.editorInfo[patternActualChannel].epnum];

					editorInfo.editmode = EDIT_PATTERN;

					if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb))
					{
						if ((editorInfo.epmarkchn != patternActualChannel) || (newpos != editorInfo.epmarkend))
						{
							editorInfo.epmarkchn = patternActualChannel;
							editorInfo.epmarkstart = editorInfo.epmarkend = newpos;
						}
					}

					if (mouseb & MOUSEB_LEFT)
					{
						setEditorVisualPatternChannel(c);
						if (x < 3) editorInfo.epcolumn = 0;
						if (x >= 3)
						{
							if (!displayOriginal3Channel)
								editorInfo.epcolumn = x - 2;
							else
							{
								//	sprintf(textbuffer, "%d", x);
								//	printtext(70, 36, 0xe, textbuffer);

								if (x >= 4 && x <= 5)
									editorInfo.epcolumn = 1 + (x - 4);	// instrument
								else if (x == 7)
									editorInfo.epcolumn = 3;	// instruction
								else if (x >= 8 && x <= 9)
									editorInfo.epcolumn = 4 + (x - 8);	// data
							}
						}

						setMasterLoopChannel(gt, "debug_7");
					}

					if (!prevmouseb)
					{
						if (mouseb & MOUSEB_LEFT)
							editorInfo.eppos = newpos;
					}

					if (editorInfo.eppos < 0) editorInfo.eppos = 0;
					if (editorInfo.eppos > pattlen[gt->editorUndoInfo.editorInfo[patternActualChannel].epnum])
						editorInfo.eppos = pattlen[gt->editorUndoInfo.editorInfo[patternActualChannel].epnum];

					if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) editorInfo.epmarkend = newpos;
				}
			}
		}
	}


	int maxCh = getVisibleChannelCount() - 1;

	if ((mousey == PANEL_ORDER_Y) && (mousex >= PANEL_ORDER_X + 5 && mousex <= PANEL_ORDER_X + 13) && !prevmouseb && mouseb)
	{
		int jc2 = getActualChannel(editorInfo.esnum, editorInfo.eschn);	// 0-12 for currently selected channel in orderlist

		int invalidCompressedDataLength = 0;
		if (editorInfo.expandOrderListView == 1)
		{
			if (validateAllSongs() > 0xff)
			{
				// at least one channel in expanded view is too large (over 0xff bytes when compressed...)	
				invalidCompressedDataLength++;
			}
		}
		if (!invalidCompressedDataLength)	// Only allow view to change to compressed view if expanded view isn't too large
		{
			stopsong(gt);
			resetSongInfo(gt, jc2);
			editorInfo.expandOrderListView = 1 - editorInfo.expandOrderListView;

			if (editorInfo.expandOrderListView == 1)
			{
				expandAllSongs();
				editorInfo.esnum = 1;
				songchange(gt, 1);
				editorInfo.esnum = 0;
				songchange(gt, 1);
				//	initEditorSongInfo(gt);
			}
			else
			{
				compressAllSongs();
				editorInfo.esnum = 1;
				songchange(gt, 1);
				editorInfo.esnum = 0;
				songchange(gt, 1);
				//	initEditorSongInfo(gt);
			}
		}
	}

	if (editorInfo.expandOrderListView == 0)
		checkForMouseInOrderList(gt, maxCh);
	else
		checkForMouseInExtendedOrderList(gt, maxCh);

	//	if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == 2) && (mousex >= 64 + 20) && (mousex <= 65 + 20))

	if (!prevmouseb && (mousey == PANEL_ORDER_Y) && (mousex >= PANEL_ORDER_X + 20) && (mousex <= PANEL_ORDER_X + 21))
	{
		if (mouseb & MOUSEB_LEFT) nextsong(gt);
		if (mouseb & MOUSEB_RIGHT) prevsong(gt);

	}



	// Instrument editpos & instrument number selection
	// Left instrument panel
	if ((mousey >= PANEL_INSTR_Y + 1) && (mousey <= PANEL_INSTR_Y + 5) && (mousex >= PANEL_INSTR_X + 16) && (mousex <= PANEL_INSTR_X + 17))
	{
		if (editorInfo.editmode != EDIT_INSTRUMENT && prevmouseb)	// Don't allow hold/drag to select another panel
			return;

		if (!mouseb)
			return;

		if (!prevmouseb)
		{
			editorInfo.editmode = EDIT_INSTRUMENT;
			editorInfo.eipos = mousey - (PANEL_INSTR_Y + 1);
			editorInfo.eicolumn = mousex - (PANEL_INSTR_X + 16);
			mouseTrack();	// MUST DO AFTER SETTING ABOVE VALUES
		}
	}

	// right instrument panel
	if ((mousey >= PANEL_INSTR_Y + 1) && (mousey <= PANEL_INSTR_Y + 5) && (mousex >= PANEL_INSTR_X + 36) && (mousex <= PANEL_INSTR_X + 37))
	{
		if (editorInfo.editmode != EDIT_INSTRUMENT && prevmouseb)	// Don't allow hold/drag to select another panel
			return;

		if (!mouseb)
			return;

		if (!prevmouseb)
		{
			editorInfo.editmode = EDIT_INSTRUMENT;
			editorInfo.eipos = mousey - (PANEL_INSTR_Y + 1) + 5;
			editorInfo.eicolumn = mousex - (PANEL_INSTR_X + 36);
			mouseTrack();	// MUST DO AFTER SETTING ABOVE VALUES
		}
	}
	if ((mousey == PANEL_INSTR_Y) && (mousex >= PANEL_INSTR_X))
	{
		if (editorInfo.editmode != EDIT_INSTRUMENT && prevmouseb)	// Don't allow hold/drag to select another panel
			return;

		if (!mouseb)
			return;

		if (!prevmouseb)
		{
			editorInfo.editmode = EDIT_INSTRUMENT;
			editorInfo.eipos = LAST_INST;	// 10 = edit instrument name
		}
	}

	if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == PANEL_INSTR_Y) && (mousex >= PANEL_INSTR_X + 16) && (mousex <= PANEL_INSTR_X + 17))
	{
		if (editorInfo.editmode != EDIT_INSTRUMENT && prevmouseb)	// Don't allow hold/drag to select another panel
			return;

		if (mouseb & MOUSEB_LEFT)
		{
			nextinstr();
		}
		if (mouseb & MOUSEB_RIGHT)
		{
			previnstr();
		}
	}

	if (((!prevmouseb) || (mouseheld > HOLDDELAY)) && (mousey == 1) && (mousex >= PANEL_ORDER_X + 5) && (mousex <= PANEL_ORDER_X + 5 + 2))
	{
		if (mouseb & MOUSEB_LEFT)
		{
			if (currentSongFile < lastValidSongFileIndex + 1)
			{
				stopsong(gt);
				undoCreateEditorInfoBackup();
				copyCurrentToSngBuffer(gt, currentSongFile);
				currentSongFile++;
				copySngBufferToCurrent(gt, currentSongFile);
				undoInvalidateUndoAreas();
				editorInfo.currentSongFile = currentSongFile;
				undoAddEditorSettingsToList();
			}
		}
		if (mouseb & MOUSEB_RIGHT)
		{
			if (currentSongFile > 0)
			{
				stopsong(gt);
				undoCreateEditorInfoBackup();
				copyCurrentToSngBuffer(gt, currentSongFile);
				currentSongFile--;
				copySngBufferToCurrent(gt, currentSongFile);
				editorInfo.currentSongFile = currentSongFile;
				undoInvalidateUndoAreas();
				undoAddEditorSettingsToList();
			}
		}
	}



	// Table editpos
	for (c = 0; c < MAX_TABLES - 1; c++)
	{
		if (mouseb && (mousey == PANEL_TABLES_Y && (mousex >= PANEL_TABLES_X + c * 10) && (mousex <= PANEL_TABLES_X + 7 + c * 10)))
		{
			// JP - I've no idea why I added this..
			if (editorInfo.editmode != EDIT_TABLE_WAVE && prevmouseb)	// Don't allow hold/drag to select another panel
				return;

			if (prevmouseb)
				return;

			if (editorInfo.editTableMode == EDIT_TABLE_WAVE + c)
				editorInfo.editTableMode = EDIT_TABLE_NONE;
			else if (editorInfo.editTableMode == EDIT_TABLE_NONE)
				editorInfo.editTableMode = EDIT_TABLE_WAVE + c;
			return;
		}
	}

	if (editorInfo.editTableMode == EDIT_TABLE_NONE)
	{
		for (c = 0; c < MAX_TABLES; c++)
		{
			checkForMouseInTable(c, PANEL_TABLES_X, PANEL_TABLES_Y);
		}
	}
	else if (editorInfo.editTableMode == EDIT_TABLE_WAVE)
	{
		checkForMouseInDetailedWaveTable(PANEL_TABLES_X, PANEL_TABLES_Y);
		checkForMouseInTable(EDIT_TABLE_SPEED - 1, PANEL_TABLES_X, PANEL_TABLES_Y);
	}
	else if (editorInfo.editTableMode == EDIT_TABLE_FILTER)
	{
		checkForMouseInDetailedFilterTable(PANEL_TABLES_X, PANEL_TABLES_Y);
	}
	else if (editorInfo.editTableMode == EDIT_TABLE_PULSE)
	{
		checkForMouseInDetailedPulseTable(PANEL_TABLES_X, PANEL_TABLES_Y);
	}


	// Name editpos (song name, author, (c) )
	if ((mousey >= (PANEL_NAMES_Y) && mousey < (PANEL_NAMES_Y + 3)) && (mousex >= PANEL_NAMES_X) && (mousex < PANEL_NAMES_X + 32))
	{
		if (!mouseb)
			return;

		editorInfo.nameIndex = mousey - PANEL_NAMES_Y;
		editorInfo.editmode = EDIT_NAMES;

	}


	//	if ((!prevmouseb) && (mousex <= 7) && (mousey == TRANSPORT_BAR_Y))
	//	{
	//		recordmode ^= 1;
	//	}
//	for (c = 0; c < MAX_CHN; c++)
//	{
//		if ((!prevmouseb) && (mousey >= 23 + 3 + 10) && (mousex >= 59 + 7 * c) && (mousex <= 64 + 7 * c))
//			mutechannel(c, gt);
//	}



	checkMouseInWaveformInfo();

	// Titlebar actions
	if (!menu)
	{
		if ((mousey == 0) && (!prevmouseb) && (mouseb == MOUSEB_LEFT))
		{
			if ((mousex >= 40 + 20) && (mousex <= 41 + 20))
			{
				// V1.3.9 change - handle fine vibrato correctly.
				//	editorInfo.usefinevib ^= 1;
				undoCreateEditorInfoBackup();
				editorInfo.finevibrato = 1 - editorInfo.finevibrato;

				// This is the same code as used when loading the .cfg file. Ensures finevib is only used in specific cases
				editorInfo.usefinevib = 0;
				if ((editorInfo.finevibrato == 1) && (editorInfo.multiplier < 2)) editorInfo.usefinevib = 1;
				if (editorInfo.finevibrato > 1) editorInfo.usefinevib = 1;

				undoAddEditorSettingsToList();
			}
			if ((mousex >= 43 + 20) && (mousex <= 44 + 20))
			{
				undoCreateEditorInfoBackup();
				editorInfo.optimizepulse ^= 1;
				undoAddEditorSettingsToList();
			}
			if ((mousex >= 46 + 20) && (mousex <= 47 + 20))
			{

				undoCreateEditorInfoBackup();
				editorInfo.optimizerealtime ^= 1;
				undoAddEditorSettingsToList();
			}
			if ((mousex >= 49 + 20) && (mousex <= 52 + 20))
			{
				undoCreateEditorInfoBackup();
				editorInfo.ntsc ^= 1;
				undoAddEditorSettingsToList();

				sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate);
			}
			if ((mousex >= 54 + 20) && (mousex <= 57 + 20))
			{
				undoCreateEditorInfoBackup();
				editorInfo.sidmodel ^= 1;
				undoAddEditorSettingsToList();
				sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate);
			}

			if ((mousex >= 59 + 20) && (mousex <= 60 + 20))
				editPan = 1 - editPan;
			if ((mousex >= 62 + 20) && (mousex <= 65 + 20))
			{
				if (!editPan)
				{
					//					undoCreateEditorInfoBackup();					
					editadsr(gt);
					//				undoAddEditorSettingsToList();
				}
				else
					editSIDPan(gt);
			}

			if ((mousex >= 67 + 20) && (mousex <= 68 + 20))
			{
				undoCreateEditorInfoBackup();
				prevmultiplier();
				undoAddEditorSettingsToList();
			}
			if ((mousex >= 69 + 20) && (mousex <= 70 + 20))
			{
				undoCreateEditorInfoBackup();
				nextmultiplier();
				undoAddEditorSettingsToList();
			}
		}
	}
	else
	{
		if ((!mousey) && (mouseb & MOUSEB_LEFT) && (!(prevmouseb & MOUSEB_LEFT)))
		{
			if ((mousex >= 0) && (mousex <= 5))
			{
				initsong(editorInfo.esnum, PLAY_BEGINNING, gt);
				followplay = shiftOrCtrlPressed;
			}
			if ((mousex >= 7) && (mousex <= 15))
			{
				initsong(editorInfo.esnum, PLAY_POS, gt);
				followplay = shiftOrCtrlPressed;
			}
			if ((mousex >= 17) && (mousex <= 26))
			{
				initsong(editorInfo.esnum, PLAY_PATTERN, gt);
				followplay = shiftOrCtrlPressed;
			}
			if ((mousex >= 28) && (mousex <= 33))
				stopsong(gt);
			if ((mousex >= 35) && (mousex <= 40))
			{
				handleLoad(gt, NULL);
			}
			if ((mousex >= 42) && (mousex <= 47))
				save(gt, 0);
			if ((mousex >= 49) && (mousex <= 57))
			{
				stopScreenDisplay();
				relocator(gt, 0, 0);
				restartScreenDisplay();
			}
			if ((mousex >= 59) && (mousex <= 64))
			{
				stopScreenDisplay();
				onlinehelp(0, 0, gt);
				restartScreenDisplay();
			}
			if ((mousex >= 66) && (mousex <= 72))
				clear(gt);
			if ((mousex >= 74) && (mousex <= 79))
				quit(gt);
		}
	}
}

void generalcommands(GTOBJECT* gt)
{
	int validSize = 1;
	//	int c;
	//	int songNum;
	//	int ac = getActualChannel(editorInfo.esnum, editorInfo.epchn);
	//	int ok = 0;

		//if (fkeys_check(gt, rawkey) == 1)
		//	return;

	switch (key)
	{
	case '?':
	case '-':
		if ((editorInfo.editmode != EDIT_NAMES) && (editorInfo.editmode != EDIT_ORDERLIST))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos == 9))) previnstr();
		}
		break;

	case '+':
	case '_':
		if ((editorInfo.editmode != EDIT_NAMES) && (editorInfo.editmode != EDIT_ORDERLIST))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9))) nextinstr();

		}
		break;

	case '*':
		if (editorInfo.editmode != EDIT_NAMES)
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave < 7) editorInfo.epoctave++;
			}
		}
		break;

	case '/':
	case '\'':
		if (editorInfo.editmode != EDIT_NAMES)
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave > 0) editorInfo.epoctave--;
			}
		}
		break;

	case '<':
		if (((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos != 9)) || (editorInfo.editmode == EDIT_TABLES))
			previnstr();
		break;

	case '>':
		if (((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos != 9)) || (editorInfo.editmode == EDIT_TABLES))
			nextinstr();
		break;

	case ';':
		previousSongPos(gt, 1);
		break;

	case ':':

		nextSongPos(gt);
		break;

	}
	if (win_quitted) exitprogram = 1;
	switch (rawkey)
	{
	case KEY_ESC:
		if (!shiftOrCtrlPressed)
			quit(gt);
		else
			clear(gt);
		break;

	case KEY_KPMULTIPLY:
		if ((editorInfo.editmode != EDIT_NAMES) && (!key))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave < 7) editorInfo.epoctave++;
			}
		}
		break;

	case KEY_KPDIVIDE:
		if ((editorInfo.editmode != EDIT_NAMES) && (!key))
		{
			if (!((editorInfo.editmode == EDIT_INSTRUMENT) && (editorInfo.eipos >= 9)))
			{
				if (editorInfo.epoctave > 0) editorInfo.epoctave--;
			}
		}
		break;


	case KEY_S:
		if (!ctrlpressed) break;

		if (editorInfo.expandOrderListView)
		{
			int maxSize = validateAllSongs();
			if (maxSize > 0xff)
				validSize = 0;
		}
		if (validSize)
		{
			int s = quickSave();	// compressAllSongs called from within savesong
			if (s)
				sprintf(infoTextBuffer, "quick save: %d", s);
			else
				save(gt, 0);
		}
		return;

	case KEY_Z:
		if (!ctrlpressed) break;

		if (!editPaletteMode)
		{
			if (editorInfo.editmode == EDIT_MOD)
			{
				char ptmodError[256];
				PTMOD_PREVIEW_STATS stats;

				if ((shiftpressed ? ptmod_redo(ptmodError, sizeof ptmodError) :
					ptmod_undo(ptmodError, sizeof ptmodError)))
				{
					ptmodplay_get_stats(&stats);
					clampPtmodEditRow();
					clampPtmodStreamCursor(&stats);
				}
				snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s",
					ptmodError[0] ? ptmodError :
					(shiftpressed ? "No MOD redo is available" : "No MOD undo is available"));
				forceInfoLine = 1;
			}
			else
			{
				undoPerform(gt);
			}
		}
		return;

	case KEY_Y:
		if (!ctrlpressed || editPaletteMode || editorInfo.editmode != EDIT_MOD) break;
		{
			char ptmodError[256];
			PTMOD_PREVIEW_STATS stats;

			if (ptmod_redo(ptmodError, sizeof ptmodError))
			{
				ptmodplay_get_stats(&stats);
				clampPtmodEditRow();
				clampPtmodStreamCursor(&stats);
			}
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s",
				ptmodError[0] ? ptmodError : "No MOD redo is available");
			forceInfoLine = 1;
		}
		return;

	case KEY_F12:
		if (shiftOrCtrlPressed)
		{
			SIDTracker64ForIPadIsAmazing = 1 - SIDTracker64ForIPadIsAmazing;
			setSIDTracker64KeyOnStyle();
			if (!SIDTracker64ForIPadIsAmazing)
				sprintf(infoTextBuffer, "SIDTracker64 Mode: Disabled");
			else
				sprintf(infoTextBuffer, "SIDTracker64 Mode: Enabled");
			forceInfoLine = 1;
			break;
		}
	case SDLK_HELP:
	{
		stopScreenDisplay();
		onlinehelp(0, shiftOrCtrlPressed, gt);
		restartScreenDisplay();
		break;
	}

		case KEY_TAB:
			if (!shiftOrCtrlPressed) editorInfo.editmode++;
			else editorInfo.editmode--;
			if (editorInfo.editmode > EDIT_MOD) editorInfo.editmode = EDIT_PATTERN;
			if (editorInfo.editmode < EDIT_PATTERN) editorInfo.editmode = EDIT_MOD;

			setMasterLoopChannel(gt, "debug_8");
			break;

	case KEY_F1:
		if (editPaletteMode)
			break;

		// JP - Shift_F1  changed to just turn looping on/off
		playUntilEnd(editorInfo.esnum);
		//		break;	// JP TEST

		if (useOriginalGTFunctionKeys)
		{
			transportLoopPattern = 0;
			if (shiftOrCtrlPressed)
				followplay = 1;
			else
				followplay = 0;
			orderPlayFromPosition(gt, 0, 0, 0, 1);
		}
		else
		{
			if (shiftpressed)
				orderPlayFromPosition(gt, 0, 0, 0, 1);
			else
				playFromCurrentPosition(gt, 0);
		}




		break;

		// PLAY FROM START OF SELECTED PATTERN
	case KEY_F2:

		if (editPaletteMode)
			break;
		if (editorInfo.editmode == EDIT_MOD && !shiftOrCtrlPressed)
		{
			ptmodPlayFromCursor(gt, 1);
			break;
		}

		// in SIDTracker mode, just use F2 for playing current position
		if (SIDTracker64ForIPadIsAmazing != 0)
		{
			if (shiftOrCtrlPressed)
				followplay = 1 - followplay;
			else
				playFromCurrentPosition(gt, 0);	// editorInfo.eppos);
		}
		else
		{
			if (useOriginalGTFunctionKeys)
			{
				playFromCurrentPosition(gt, 0);
				transportLoopPattern = 0;
				if (shiftOrCtrlPressed)
					followplay = 1;
				else
					followplay = 0;
			}
			else
			{

				if (shiftOrCtrlPressed)
					followplay = 1 - followplay;
				else
				{
					transportLoopPattern = 1 - transportLoopPattern;
					if (!transportLoopPattern)
					{
						editorInfo.highlightLoopChannel = 999;			// remove from display
						editorInfo.highlightLoopPatternNumber = -1;
						editorInfo.highlightLoopStart = editorInfo.highlightLoopEnd = 0;
					}
				}
			}
		}
		break;



	case KEY_F3:

		if (editPaletteMode)
			break;
		if (editorInfo.editmode == EDIT_MOD && !shiftOrCtrlPressed)
		{
			ptmodPlayFromCursor(gt, 0);
			break;
		}

		// ORIGINAL GT: LOOP PATTERN, PLAYING FROM SELECTED
		if (useOriginalGTFunctionKeys && SIDTracker64ForIPadIsAmazing == 0)
		{
			transportLoopPattern = 1;
			if (shiftOrCtrlPressed)
				followplay = 1;
			else
				followplay = 0;
			playFromCurrentPosition(gt, 0);
		}
		else
		{
			if (shiftOrCtrlPressed)
			{
				transportLoopPattern = 1 - transportLoopPattern;
			}
			else
			{
				if (editorInfo.editmode == EDIT_ORDERLIST)	// 1.1.7: Fast select / playback when in OrderList. Just press F3 to play from the cursor pos
				{
					orderSelectPatternsFromSelected(gt);
					orderPlayFromPosition(gt, 0, editorInfo.eseditpos, editorInfo.eschn, 1);
				}
				else
				{
					playFromCurrentPosition(gt, editorInfo.eppos);	//  F3 = plays from the current pattern pos
				}
			}
		}
		break;

	case KEY_F4:
		if (shiftOrCtrlPressed)
			mutechannel(editorInfo.epchn, gt);
		else
		{
			if (gt->songinit != PLAY_STOPPED)
			{
				stopsong(gt);
				setMasterLoopChannel(gt, "debug_9");
			}
		}

		break;

	case KEY_F5:
		if (!shiftOrCtrlPressed)
			editorInfo.editmode = EDIT_PATTERN;
		else prevmultiplier();
		break;

	case KEY_F6:
		if (!shiftOrCtrlPressed)
			editorInfo.editmode = EDIT_ORDERLIST;
		else nextmultiplier();
		break;

	case KEY_F7:
		if (!shiftOrCtrlPressed)
		{
			if (editorInfo.editmode == EDIT_INSTRUMENT)
				editorInfo.editmode = EDIT_TABLES;
			else
				editorInfo.editmode = EDIT_INSTRUMENT;
			disableEnterToReturnToLastPos = 1;
		}
		else
		{
			if (!editPan)
				editadsr(gt);
			else
				editSIDPan(gt);
		}
		break;

	case KEY_F8:
		if (shiftpressed && ctrlpressed)
		{
			PTMOD_PREVIEW_STATS stats;

			ptmodplay_get_stats(&stats);
			editorInfo.editmode = EDIT_MOD;
			if (stats.loaded && stats.active)
			{
				editorInfo.ptmodStreamFollow = 1;
				editorInfo.ptmodOrderIndex = stats.orderIndex;
				editorInfo.ptmodStreamRow = stats.row;
			}
			disableEnterToReturnToLastPos = 1;
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
			forceInfoLine = 1;
		}
		else if (!shiftOrCtrlPressed)
		{
			editorInfo.editmode = EDIT_TABLES;		// 'Cos JAMMAR SAID SO!
			disableEnterToReturnToLastPos = 1;
		}
		else
		{
			editorInfo.sidmodel ^= 1;
			sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate);
		}
		break;

	case KEY_F9:
		if (shiftpressed && ctrlpressed)
		{
			char ptmodError[256];
			char ptmodPath[MAX_PATHNAME];
			if (!ptmodConfirmDiscard(gt, "load"))
				break;
			if (!ptmodfilter[0])
				strcpy(ptmodfilter, "*.mod");
			if (fileselector(ptmodfilename, songpath, ptmodfilter, "LOAD MOD FILE", 0, gt, CEDIT, 0))
			{
				if (!makeSelectorPath(ptmodPath, sizeof ptmodPath, songpath, ptmodfilename))
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "MOD path is too long");
				else if (ptmod_load_source(ptmodPath, ptmodError, sizeof ptmodError))
				{
					ptmodResetEditorPosition();
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmod_status_text());
				}
				else
					snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s", ptmodError);
				forceInfoLine = 1;
			}
		}
		else if (!shiftOrCtrlPressed)
		{
			if (editorInfo.expandOrderListView)
			{
				int maxSize = validateAllSongs();
				if (maxSize > 0xff)
					validSize = 0;
				else
					compressAllSongs();

			}
			if (validSize)
			{
				stopScreenDisplay();
				relocator(gt, 0, 0);
				restartScreenDisplay();
				printmainscreen(gt);
				sprintf(infoTextBuffer, " ");

			}
		}
		else if (shiftpressed)
		{
			stereoMode++;
			stereoMode %= 3;
			validateStereoMode();
		}
		else if (ctrlpressed)
		{
			// Fast relocator - No menus. Use last export settings
			if (songExported)
				relocator(gt, 0, 1);
			if (songExported)
				sprintf(infoTextBuffer, "Song Exported:%s", packedsongname);
		}
		break;

	case KEY_F10:

		if (ctrlpressed)
		{
			if (shiftpressed)
			{
				gt_video_close();
				snprintf(infoTextBuffer, sizeof infoTextBuffer, "Video closed");
				forceInfoLine = 1;
			}
			else
				handleLoadVideo(gt);
		}
		else
			handleLoad(gt, NULL);
		break;

	case KEY_F11:
		if (editorInfo.editmode == EDIT_MOD && !ctrlpressed)
		{
			if (shiftpressed)
				ptmodSaveAs(gt);
			else
				ptmodSaveCurrentOrAs(gt);
			break;
		}
		if (shiftOrCtrlPressed)
			save(gt, 1);
		else
		{
			if (editorInfo.expandOrderListView)
			{
				int maxSize = validateAllSongs();
				if (maxSize > 0xff)
					validSize = 0;
			}
			if (validSize)
				save(gt, 0);		// compressAllSongs called from within savesong
		}
		break;

	case KEY_LEFT:
		if (ctrlpressed)
		{
			leftKeyTicksDelta = SDL_GetTicks() - leftKeyTicks;
			leftKeyTicks = SDL_GetTicks();
			if (leftKeyTicksDelta < 300)
			{
				handlePressRewind(1, gt);		// double click
			}
			else
			{
				handlePressRewind(0, gt);		// single click
			}
		}
		break;

	case KEY_RIGHT:
		if (ctrlpressed)
		{
			nextSongPos(&gtObject);
		}
		break;

	}
}

int load(GTOBJECT* gt, char* dragDropFileName)
{
	win_enableKeyRepeat();
	int ok = 0;
	if (((editorInfo.editmode != EDIT_INSTRUMENT) && (editorInfo.editmode != EDIT_TABLES)) || dragDropFileName != NULL)
	{
		if (dragDropFileName != NULL)
		{
			for (int i = 0;i < 256;i++)
			{
				songfilename[i] = dragDropFileName[i];
				if (dragDropFileName[i] == 0)
					break;
			}
			ok = loadsong(gt, 0);

			free(dragDropFileName);
			dragDropFileName = NULL;
		}
		else if (!shiftOrCtrlPressed)
		{
			if (fileselector(songfilename, songpath, songfilter, "LOAD SONG", 0, gt, CEDIT, 0))
				ok = loadsong(gt, 0);
		}
		else
		{
			if (fileselector(songfilename, songpath, songfilter, "MERGE SONG", 0, gt, CEDIT, 0))
				ok = mergesong(gt);
		}

		if (ok)
		{
			loadedSongFlag = 1;
			undoInitAllAreas(&gtObject);	// recreate undo buffers using the loaded song as the original info
			countInstruments();
			setTableBackgroundColours(editorInfo.einum);
			expandAllSongs();
		}
		return ok;
	}
	else
	{
		if (editorInfo.einum)
		{
			if (fileselector(instrfilename, instrpath, instrfilter, "LOAD INSTRUMENT", 0, gt, 15, 0))
				loadinstrument(gt);
		}
	}
	key = 0;
	rawkey = 0;
	return 0;
}

int quickSave()
{
	if (loadedSongFlag)	// set to 1 when song is loaded.set to 0 if song is cleared.
	{
		if (strlen(loadedsongfilename))
			strcpy(songfilename, loadedsongfilename);
		savesong();
	}
	return loadedSongFlag;
}

void save(GTOBJECT* gt, int exportWAVFlag)
{
	win_enableKeyRepeat();

	if ((editorInfo.editmode != EDIT_INSTRUMENT) && (editorInfo.editmode != EDIT_TABLES))
	{
		int done = 0;

		// Repeat until quit or save successful
		while (!done)
		{
			if (strlen(loadedsongfilename))
				strcpy(songfilename, loadedsongfilename);
			if (exportWAVFlag == 0)
			{
				if (fileselector(songfilename, songpath, songfilter, "SAVE SONG", 3, gt, 12, 1))
					done = savesong();
				else done = 1;
			}
			else
			{
				if (fileselector(wavfilename, songpath, wavfilter, "EXPORT AS WAV", 3, gt, 11, 2))
				{
					done = 1;
					doExportToWAV = 1;
				}
				else done = 1;
			}


		}
	}
	else
	{
		if (editorInfo.einum)
		{
			int done = 0;
			int useinstrname = 0;
			char tempfilename[MAX_FILENAME];

			// Repeat until quit or save successful
			while (!done)
			{
				if ((!strlen(instrfilename)) && (strlen(instr[editorInfo.einum].name)))
				{
					useinstrname = 1;
					strcpy(instrfilename, instr[editorInfo.einum].name);
					strcat(instrfilename, ".ins");
					strcpy(tempfilename, instrfilename);
				}

				if (fileselector(instrfilename, instrpath, instrfilter, "SAVE INSTRUMENT", 3, gt, 12, 0))
					done = saveinstrument();
				else done = 1;

				if (useinstrname)
				{
					if (!strcmp(tempfilename, instrfilename))
						memset(instrfilename, 0, sizeof instrfilename);
				}
			}
		}
	}
	key = 0;
	rawkey = 0;
}

void quit(GTOBJECT* gt)
{
	if ((!shiftOrCtrlPressed) || (mouseb))
	{
		//78,36
		printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(CINFO_FOREGROUND, CGENERAL_BACKGROUND), "Really Quit (y/n)?");
		waitkey(gt);

		printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(CINFO_FOREGROUND, CGENERAL_BACKGROUND), "                  ");
		if ((key == 'y') || (key == 'Y')) exitprogram = 1;
	}
	key = 0;
	rawkey = 0;
}

void clear(GTOBJECT* gt)
{
	int cs = 0;
	int cp = 0;
	int ci = 0;
	int ct = 0;
	int cn = 0;

	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), "Optimize everything (y/n)?");
	waitkey(gt);
	printbyterow(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), 32, 39);

	if ((key == 'y') || (key == 'Y'))
	{
		optimizeeverything(1, 1, &gtObject);
		key = 0;
		rawkey = 0;
		countpatternlengths();
		return;
	}

	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), "Clear orderlists (y/n)?");
	waitkey(gt);
	printbyterow(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), 32, 39);
	if ((key == 'y') || (key == 'Y')) cs = 1;

	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), "Clear patterns (y/n)?");
	waitkey(gt);
	printbyterow(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), 32, 39);
	if ((key == 'y') || (key == 'Y')) cp = 1;

	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), "Clear instruments (y/n)?");
	waitkey(gt);
	printbyterow(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), 32, 39);
	if ((key == 'y') || (key == 'Y')) ci = 1;

	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), "Clear tables (y/n)?");
	waitkey(gt);
	printbyterow(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), 32, 39);
	if ((key == 'y') || (key == 'Y')) ct = 1;

	printtext(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), "Clear songname (y/n)?");
	waitkey(gt);
	printbyterow(YES_NO_TEXT_X, YES_NO_TEXT_Y, getColor(15, CGENERAL_BACKGROUND), 32, 39);
	if ((key == 'y') || (key == 'Y')) cn = 1;

	if (cp == 1)
	{
		int selectdone = 0;
		int olddpl = defaultpatternlength;

		printtext(60, 36, getColor(15, CGENERAL_BACKGROUND), "Pattern length:");
		while (!selectdone)
		{
			sprintf(textbuffer, "%02d ", defaultpatternlength);
			printtext(60 + 15, 36, getColor(15, CGENERAL_BACKGROUND), textbuffer);

			waitkey(gt);
			switch (rawkey)
			{
			case KEY_LEFT:
				defaultpatternlength -= 7;
			case KEY_DOWN:
				defaultpatternlength--;
				if (defaultpatternlength < 1) defaultpatternlength = 1;
				break;

			case KEY_RIGHT:
				defaultpatternlength += 7;
			case KEY_UP:
				defaultpatternlength++;
				if (defaultpatternlength > MAX_PATTROWS) defaultpatternlength = MAX_PATTROWS;
				break;

			case KEY_ESC:
				defaultpatternlength = olddpl;
				selectdone = 1;
				break;

			case KEY_ENTER:
				selectdone = 1;
				break;
			}
		}
		printbyterow(60, 36, getColor(15, CGENERAL_BACKGROUND), 32, 39);
	}

	if (cs | cp | ci | ct | cn)
	{
		loadedSongFlag = 0;
		memset(songfilename, 0, sizeof songfilename);
	}
	clearsong(cs, cp, ci, ct, cn, &gtObject);

	key = 0;
	rawkey = 0;
}


void convertPansToInts(int sidChips)
{
	int intVal = 0;
	for (int i = 0;i < sidChips;i++)
	{
		intVal <<= 4;
		intVal += SID_StereoPanPositions[sidChips - 1][i];
	}
	sidPanInts[sidChips - 1] = intVal;	// stored in .cfg file
}

void convertInsToPans(int sidChips)
{
	for (int i = 0;i < 4;i++)
	{
		SID_StereoPanPositions[sidChips][i] = (sidPanInts[sidChips] >> (4 * i)) & 0xf;
	}
}


void editSIDPan(GTOBJECT* gt)
{
	int sidChips = editorInfo.maxSIDChannels / 3;

	//	int	v = SID_StereoPanPositions[sidChips - 1][i];


	eamode = 1;
	editorInfo.eacolumn = 0;

	for (;;)
	{
		waitkeymouse(gt);

		if (win_quitted)
		{
			exitprogram = 1;
			key = 0;
			rawkey = 0;
			return;
		}

		if (hexnybble >= 0 && hexnybble < 0xf)
		{
			SID_StereoPanPositions[sidChips - 1][editorInfo.eacolumn] = hexnybble;
			convertPansToInts(sidChips);

			editorInfo.eacolumn++;
		}

		switch (rawkey)
		{

		case KEY_F7:
			if (!shiftOrCtrlPressed) break;

		case KEY_ESC:
		case KEY_ENTER:
		case KEY_TAB:
			eamode = 0;
			key = 0;
			rawkey = 0;
			return;

		case KEY_BACKSPACE:
			if (!editorInfo.eacolumn) break;
		case KEY_LEFT:
			editorInfo.eacolumn--;
			break;

		case KEY_RIGHT:
			editorInfo.eacolumn++;
		}
		if (editorInfo.eacolumn < 0)
			editorInfo.eacolumn = sidChips - 1;
		editorInfo.eacolumn %= sidChips;

		if ((mouseb) && (!prevmouseb))
		{
			eamode = 0;
			return;
		}
	}


}


void editadsr(GTOBJECT* gt)
{
	eamode = 1;
	editorInfo.eacolumn = 0;

	for (;;)
	{
		waitkeymouse(gt);

		if (win_quitted)
		{
			exitprogram = 1;
			key = 0;
			rawkey = 0;
			return;
		}

		if (hexnybble >= 0)
		{
			undoCreateEditorInfoBackup();

			switch (editorInfo.eacolumn)
			{

			case 0:
				editorInfo.adparam &= 0x0fff;
				editorInfo.adparam |= hexnybble << 12;
				break;

			case 1:
				editorInfo.adparam &= 0xf0ff;
				editorInfo.adparam |= hexnybble << 8;
				break;

			case 2:
				editorInfo.adparam &= 0xff0f;
				editorInfo.adparam |= hexnybble << 4;
				break;

			case 3:
				editorInfo.adparam &= 0xfff0;
				editorInfo.adparam |= hexnybble;
				break;
			}
			editorInfo.eacolumn++;

			undoAddEditorSettingsToList();
		}

		switch (rawkey)
		{

		case KEY_Z:
			if (!ctrlpressed) break;
			if (editorInfo.editmode == EDIT_MOD)
			{
				char ptmodError[256];
				PTMOD_PREVIEW_STATS stats;

				if ((shiftpressed ? ptmod_redo(ptmodError, sizeof ptmodError) :
					ptmod_undo(ptmodError, sizeof ptmodError)))
				{
					ptmodplay_get_stats(&stats);
					clampPtmodEditRow();
					clampPtmodStreamCursor(&stats);
				}
				snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s",
					ptmodError[0] ? ptmodError :
					(shiftpressed ? "No MOD redo is available" : "No MOD undo is available"));
				forceInfoLine = 1;
			}
			else
			{
				undoPerform(gt);
			}
			break;

		case KEY_Y:
			if (!ctrlpressed || editorInfo.editmode != EDIT_MOD) break;
			{
				char ptmodError[256];
				PTMOD_PREVIEW_STATS stats;

				if (ptmod_redo(ptmodError, sizeof ptmodError))
				{
					ptmodplay_get_stats(&stats);
					clampPtmodEditRow();
					clampPtmodStreamCursor(&stats);
				}
				snprintf(infoTextBuffer, sizeof infoTextBuffer, "%s",
					ptmodError[0] ? ptmodError : "No MOD redo is available");
				forceInfoLine = 1;
			}
			break;

		case KEY_F7:
			if (!shiftOrCtrlPressed) break;

		case KEY_ESC:
		case KEY_ENTER:
		case KEY_TAB:
			eamode = 0;
			key = 0;
			rawkey = 0;
			return;

		case KEY_BACKSPACE:
			if (!editorInfo.eacolumn) break;
		case KEY_LEFT:
			editorInfo.eacolumn--;
			break;

		case KEY_RIGHT:
			editorInfo.eacolumn++;
		}

		editorInfo.eacolumn &= 3;

		if ((mouseb) && (!prevmouseb))
		{
			eamode = 0;
			return;
		}
	}
}

void getparam(FILE* handle, unsigned int* value)
{
	char* configptr;

	for (;;)
	{
		if (feof(handle)) return;
		fgets(configbuf, MAX_PATHNAME, handle);
		if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
	}

	configptr = configbuf;
	if (*configptr == '$')
	{
		*value = 0;
		configptr++;
		for (;;)
		{
			char c = tolower(*configptr++);
			int h = -1;

			if ((c >= 'a') && (c <= 'f')) h = c - 'a' + 10;
			if ((c >= '0') && (c <= '9')) h = c - '0';

			if (h >= 0)
			{
				*value *= 16;
				*value += h;
			}
			else break;
		}
	}
	else
	{
		*value = 0;
		for (;;)
		{
			char c = tolower(*configptr++);
			int d = -1;

			if ((c >= '0') && (c <= '9')) d = c - '0';

			if (d >= 0)
			{
				*value *= 10;
				*value += d;
			}
			else break;
		}
	}
}

void getfloatparam(FILE* handle, float* value)
{
	char* configptr;

	for (;;)
	{
		if (feof(handle)) return;
		fgets(configbuf, MAX_PATHNAME, handle);
		if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
	}

	configptr = configbuf;
	*value = 0.0f;
	sscanf(configptr, "%f", value);
}

void getstringparam(FILE* handle, char* value)
{
	char* configptr;

	int foundSemi = 0;

	for (;;)
	{

		if (feof(handle)) return;

		int currentoffset = ftell(handle);


		fgets(configbuf, MAX_PATHNAME, handle);
		if (configbuf[0] == ';')	// Found comment (should be the comment for this string param)
		{
			if (foundSemi)	// Already found a comment? Means that there was no string inbetween...
			{
				fseek(handle, currentoffset, SEEK_SET);	// seek back to this comment.
				return;
			}
			foundSemi++;
		}

		if ((configbuf[0]) && (configbuf[0] != ';') && (configbuf[0] != ' ') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
	}

	configptr = configbuf;

	sscanf(configptr, "%s", value);
}

int prevmultiplier(void)
{
	if (editorInfo.multiplier > 0)
	{
		editorInfo.multiplier--;
		reInitSID();
		playUntilEnd(editorInfo.esnum);
		return 1;
	}
	return 0;
}

int nextmultiplier(void)
{
	if (editorInfo.multiplier < 16)
	{
		editorInfo.multiplier++;
		reInitSID();
		playUntilEnd(editorInfo.esnum);
		return 1;
	}
	return 0;
}

void reInitSID()
{
	sound_init(b, mr, writer, hardsid, editorInfo.sidmodel, editorInfo.ntsc, editorInfo.multiplier, catweasel, interpolate, customclockrate);
}

void calculatefreqtable()
{
	float bp = basepitch;
	if (!bp)
		bp = 440.0f;

	double basefreq = (double)bp * (16777216.0 / 985248.0) * pow(2.0, 0.25) / 32.0;
	double cyclebasefreq = basefreq;
	double freq = basefreq;
	int c;
	int i;


	if (tuningcount)
	{
		c = 0;
		while (c < 96)
		{
			for (i = 0; i < tuningcount; i++)
			{
				if (c < 96)
				{
					int intfreq = freq + 0.5;
					if (intfreq > 0xffff)
						intfreq = 0xffff;
					freqtbllo[c] = intfreq & 0xff;
					freqtblhi[c] = intfreq >> 8;
					freq = cyclebasefreq * tuning[i];
					c++;
				}
			}
			cyclebasefreq = freq;
		}
	}
	else
	{
		for (c = 0; c < 8 * 12; c++)
		{
			double note = c * 100;			// * 100 so we can handle detune by +/- 100 cents
			note += (double)detuneCent * 100;
			double freq = basefreq * pow(2.0, note / (double)(equaldivisionsperoctave * 100));
			int intfreq = freq + 0.5;
			if (intfreq > 0xffff)
				intfreq = 0xffff;
			freqtbllo[c] = intfreq & 0xff;
			freqtblhi[c] = intfreq >> 8;
		}
	}
}

void setspecialnotenames()
{
	int i;
	int j;
	int oct;
	char* name;
	char octave[11];

	i = 0;
	oct = 0;
	while (i < 93)
	{
		for (j = 0; j < 186; j += 2)
		{
			if (specialnotenames[j] == '\0')
				break;
			if (i < 93)
			{
				sprintf(octave, "%d", oct);
				name = malloc(2 + strlen(octave) + 1);
				if (!name)
					return;
				memcpy(name, specialnotenames + j, 2);
				strcpy(name + 2, octave);
				notename[i] = name;
				i++;
			}
		}
		oct++;
	}
}

void readscalatuningfile()
{
	FILE* scalatuningfile;
	char* configptr;
	char strbuf[64];
	char name[3];
	int i;
	double numerator;
	double denominator;
	double centvalue;

	scalatuningfile = fopen(scalatuningfilepath, "rt");
	if (scalatuningfile)
	{
		// Tuning name
		for (;;)
		{
			if (feof(scalatuningfile)) return;
			fgets(configbuf, MAX_PATHNAME, scalatuningfile);
			if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
		}
		configptr = configbuf;
		sscanf(configptr, "%63[^\t\n]", tuningname);

		// Tuning count
		for (;;)
		{
			if (feof(scalatuningfile)) return;
			fgets(configbuf, MAX_PATHNAME, scalatuningfile);
			if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
		}
		configptr = configbuf;
		sscanf(configptr, "%d", &tuningcount);

		// Tunings 
		for (i = 0; i < tuningcount; i++)
		{
			for (;;)
			{
				if (feof(scalatuningfile)) return;
				fgets(configbuf, MAX_PATHNAME, scalatuningfile);
				if ((configbuf[0]) && (configbuf[0] != '!') && (configbuf[0] != 13) && (configbuf[0] != 10)) break;
			}
			configptr = configbuf;
			name[0] = '\0';
			sscanf(configptr, "%63s %2s", strbuf, name);
			if (!i)
			{
				strcpy(specialnotenames, name);
			}
			else
			{
				if (i == tuningcount - 1)
				{
					char* tmp = strdup(specialnotenames);
					strcpy(specialnotenames, name);
					strcat(specialnotenames, tmp);
					free(tmp);
				}
				else
				{
					strcat(specialnotenames, name);
				}
			}
			if (!strchr(strbuf, '.'))
			{
				sscanf(strbuf, "%lf", &numerator);
				if (strchr(strbuf, '/'))
				{
					sscanf(strchr(strbuf, '/') + 1, "%lf", &denominator);
					tuning[i] = numerator / denominator;
				}
			}
			else
			{
				sscanf(configptr, "%lf", &centvalue);
				tuning[i] = pow(2.0, centvalue / 1200.0);
			}
		}
		fclose(scalatuningfile);
	}
}

/*
Foreground / Background for each column display for editing palette RGB
For each entry, the RGB for index 0-n will set the paletteRGB entry (likey with an offset).
So, modifying RGB for index 0 will set the CPATTERN_BACKGROUND1. modifying for index 1 will set CPATTERN_FOREGROUND1
*/

void initPaletteDisplay()
{
	setSkin(currentPalettePreset);	// set the actual gfx_ palette colours (using the loaded gtskin.bin file)
}

void setSkin(int palettePreset)
{
	for (int i = 0;i < MAX_PALETTE_ENTRIES;i++)
	{
		setGFXPaletteRGBFromPaletteRGB(palettePreset, i);
	}
}

void setPaletteRGB(int presetIndex, int paletteIndex, int r, int g, int b)
{
	paletteRGB[presetIndex][0][paletteIndex] = r;
	paletteRGB[presetIndex][1][paletteIndex] = g;
	paletteRGB[presetIndex][2][paletteIndex] = b;

	setGFXPaletteRGBFromPaletteRGB(presetIndex, paletteIndex);
}

int isMatchingRGB(int presetIndex, int color)
{
	int c1 = color & 0xff;
	int c2 = (color >> 8) & 0xff;

	for (int i = 0;i < 3;i++)
	{
		if ((paletteR[c1] != paletteR[c2]) || (paletteG[c1] != paletteG[c2]) || (paletteB[c1] != paletteB[c2]))
		{
			return 0;
		}
	}
	return 1;
}

void swapPalettes(int p1, int p2)
{
	for (int i = 0;i < MAX_PALETTE_ENTRIES;i++)
	{
		for (int j = 0;j < 3;j++)
		{
			int t = paletteRGB[p1][j][i];
			paletteRGB[p1][j][i] = paletteRGB[p2][j][i];
			paletteRGB[p2][j][i] = t;
		}
	}
}

void setGFXPaletteRGBFromPaletteRGB(int presetIndex, int paletteIndex)
{
	int r = paletteRGB[presetIndex][0][paletteIndex];
	int g = paletteRGB[presetIndex][1][paletteIndex];
	int b = paletteRGB[presetIndex][2][paletteIndex];

	gfx_setPaletteRGB(FIRST_UI_COLOR + paletteIndex, r, g, b);
	paletteR[FIRST_UI_COLOR + paletteIndex] = r;
	paletteG[FIRST_UI_COLOR + paletteIndex] = g;
	paletteB[FIRST_UI_COLOR + paletteIndex] = b;

	//	int r1 = r & 0xf0;
	//	int g1 = g & 0xf0;
	//	int b1 = b & 0xf0;
	//	int r2 = (r << 4) & 0xf0;
	//	int g2 = (g << 4) & 0xf0;
	//	int b2 = (b << 4) & 0xf0;

//	gfx_setPaletteRGB(FIRST_UI_COLOR + (paletteIndex * 2), r1, g1, b1);
//	gfx_setPaletteRGB(FIRST_UI_COLOR + (paletteIndex * 2) + 1, r2, g2, b2);

//	paletteR[FIRST_UI_COLOR + (paletteIndex * 2)] = r1;
//	paletteG[FIRST_UI_COLOR + (paletteIndex * 2)] = g1;
//	paletteB[FIRST_UI_COLOR + (paletteIndex * 2)] = b1;

//	paletteR[FIRST_UI_COLOR + (paletteIndex * 2) + 1] = r2;
//	paletteG[FIRST_UI_COLOR + (paletteIndex * 2) + 1] = g2;
//	paletteB[FIRST_UI_COLOR + (paletteIndex * 2) + 1] = b2;
}


void handlePaletteDisplay(GTOBJECT* gt, int palettePreset)
{
	if (gt->songinit != PLAY_STOPPED)
	{
		stopsong(gt);
	}
	SDL_Delay(50);


	// backup song 0
	for (int c = 0;c < MAX_CHN;c++)
	{
		for (int p = 0;p < MAX_SONGLEN;p++)
		{
			if (editPaletteMode)
				backupPaletteSong[c][p] = songorder[0][c][p];
			else
			{
				songorder[0][c][p] = backupPaletteSong[c][p];
			}
		}
	}


	if (editPaletteMode)
	{
		copyPaletteToOrderList(palettePreset);

	}

	editorInfo.eseditpos = 0;
	editorInfo.eppos = 0;
	editorInfo.esnum = 0;
	editorInfo.eschn = 0;
	editorInfo.editmode = EDIT_ORDERLIST;
	songchange(gt, 1);

}

void copyPaletteToOrderList(int palettePreset)
{
	for (int c = 0;c < MAX_CHN;c++)
	{
		for (int p = 0;p < MAX_PALETTE_ENTRIES;p++)
		{
			songorder[0][0][p] = paletteRGB[palettePreset][0][p];
			songorder[0][1][p] = paletteRGB[palettePreset][1][p];
			songorder[0][2][p] = paletteRGB[palettePreset][2][p];
		}
	}
}

int highlightTableBuffer[MAX_TABLELEN];


void setTableBackgroundColours(int currentInstrument)
{

	for (int t = 0;t < MAX_TABLES;t++)
	{
		int alternateTableColor = 0;
		int needNewTable = 1;
		int startTableOffset;
		int instrumentTablePtr = instr[currentInstrument].ptr[t];
		instrumentTablePtr--;

		highlightInstrument(t, instrumentTablePtr);

		for (int i = 0;i < MAX_TABLELEN;i++)
		{

			if (needNewTable)
			{
				startTableOffset = i;
				needNewTable = 0;
			}

			//		int foundEnd = 0;
			if (t != 3)		// not the speed table?
			{

				if (ltable[t][i] == 0xff)	// end marker?
				{
					setTableColour(instrumentTablePtr, t, startTableOffset, i, getColor(CTABLE_FOREGROUND1 + (alternateTableColor * 2), CTABLE_BACKGROUND1 + (alternateTableColor * 2)));
					alternateTableColor = 1 - alternateTableColor;
					needNewTable = 1;
				}
			}
			else
			{
				if (ltable[t][i] != 0 || rtable[t][i] != 0)	// end marker?
				{
					setTableColour(instrumentTablePtr, t, startTableOffset, i, getColor(CTABLE_FOREGROUND1 + (alternateTableColor * 2), CTABLE_BACKGROUND1 + (alternateTableColor * 2)));
					needNewTable = 1;
				}
			}

		}

		setTableColour(instrumentTablePtr, t, startTableOffset, MAX_TABLELEN - 1, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND));

		if (instrumentTablePtr < startTableOffset)	// Valid table data
			highlightInstrument(t, instrumentTablePtr);
	}
}



void highlightInstrument(int t, int instrumentTablePtr)
{
	for (int i = 0;i < MAX_TABLELEN;i++)
	{
		highlightTableBuffer[i] = 0;
	}

	if (instrumentTablePtr < 0)
		return;

	if (t == 3)
	{
		highlightTableBuffer[instrumentTablePtr] = 1;
		return;
	}
	for (int i = 0;i < MAX_TABLELEN;i++)
	{
		if (highlightTableBuffer[instrumentTablePtr] == 1)
			return;	// we've looped to a previously played table slot

		highlightTableBuffer[instrumentTablePtr] = 1;
		if (ltable[t][instrumentTablePtr] == 0xff)
		{
			if (rtable[t][instrumentTablePtr] == 0)
				break;
			else
				instrumentTablePtr = rtable[t][instrumentTablePtr] - 1;
		}
		else
			instrumentTablePtr++;
	}

}

void setTableColour(int instrumentTablePtr, int t, int startTableOffset, int endTableOffset, int color)
{
	for (int j = startTableOffset;j <= endTableOffset;j++)
	{
		if (highlightTableBuffer[j])
		{
			//			color &= 0xff;
			color = CTABLE_SELECTED_INSTRUMENT_FOREGROUND;
			color |= (CTABLE_SELECTED_INSTRUMENT_BACKGROUND << 8);
		}
		tableBackgroundColors[t][j] = color;
	}
}

// Used to get time of overall length of song
// Either when first channel hits an END SONG or when last channel has looped

int patternOrderArray[256];
int patternOrderList[256];
int patternRemapOrderIndex;

void initRemapArrays()
{
	for (int i = 0;i < 256;i++)
	{
		patternOrderArray[i] = -1;
		patternOrderList[i] = -1;
	}
}



// JUST NEED TO TEST THIS NOW..

void ExportAsPCM(int songNumber, int doNormalize, GTOBJECT* gt)
{


	// Stop playback & then stop SID processing
	if (gt->songinit != PLAY_STOPPED)
	{
		stopsong(gt);
		setMasterLoopChannel(gt, "debug_9");
	}

	playUntilEnd(songNumber);

	bypassPlayRoutine = 1;	// Stop interrupt from updating play routine. We're going to do it manually
	SDL_Delay(50);

	GenerateExportFileName();
	OpenExportFileNameForWriting();


	int sng = getActualSongNumber(songNumber, 0);	// editorInfo.esnum
	int currentLoopEnabledFlag = gt->loopEnabledFlag;
	int currentFollowFlag = followplay;


	initsong(sng, PLAY_BEGINNING, gt);
	gt->loopEnabledFlag = 0;
	followplay = 1;

	int samplesToExport = (mr * 2) / 100;	// For 44100, IT APPEARS TO GENERATE 882 SAMPLES FOR 1x speed. 441 for 2x.. 220 for 4x...
	if (editorInfo.multiplier == 0)
		samplesToExport *= 2;	// Handle 1/2 speed
	else
		samplesToExport /= editorInfo.multiplier;
	largestExportValue = 0;	// Used for normalizing PCM
	int writeCounter = 0;

	int allDone;
	do {

		if (writeCounter == 0)
		{
			SDL_Delay(10);
			getkey();
			displayupdate(gt);
		}
		writeCounter++;
		writeCounter %= 100;


		playroutine(gt);
		ExportSIDToPCMFile(samplesToExport, doNormalize);

		if (gt->songinit == PLAY_STOPPED)	// Error in song data
		{
			break;
		}

		allDone = 1;
		for (int i = 0;i < editorInfo.maxSIDChannels;i++)
		{
			if (gt->chn[i].loopCount == 0)	// wait until all channels have looped (or song ends)
			{
				allDone = 0;	// hasn't looped
				break;
			}
		}
	} while (allDone == 0);

	ExportCloseFileHandle();

	convertRAWToWAV(doNormalize);


	gt->loopEnabledFlag = currentLoopEnabledFlag;
	followplay = currentFollowFlag;

	bypassPlayRoutine = 0;

	if (gt->songinit != PLAY_STOPPED)
	{
		stopsong(gt);
		setMasterLoopChannel(gt, "debug_9");
	}
}



void playUntilEnd(int songNumber)
{
	patternRemapOrderIndex = 0;
	initRemapArrays();
	playUntilEnd2(songNumber);
	setSongLengthTime(&gtEditorObject);
}

void playUntilEnd2(int songNumber)
{
	int sng = getActualSongNumber(songNumber, 0);	// editorInfo.esnum
	GTOBJECT* gte = &gtEditorObject;

	initsong(sng, PLAY_BEGINNING, gte);	// JP FEB
	gte->loopEnabledFlag = 0;

	//printf("---- SubSong %x ----\n", songNumber);

	int allDone;
	do {
		playroutine(gte);

		// Create arrays that are used to remap exported SID patterns in playing order
		for (int i = 0;i < editorInfo.maxSIDChannels;i++)
		{
			int pat = gte->chn[i].pattnum;
			if (patternOrderArray[pat] == -1)
			{
				//		printf("Pattern %x\n", pat);
				patternOrderList[patternRemapOrderIndex] = pat;	// contains list of patterns in order of playing
				patternOrderArray[pat] = patternRemapOrderIndex;
				patternRemapOrderIndex++;
			}
		}
		if (gte->songinit == PLAY_STOPPED)	// Error in song data
		{
			break;
		}

		allDone = 1;
		for (int i = 0;i < editorInfo.maxSIDChannels;i++)
		{
			if (gte->chn[i].loopCount == 0)	// wait until all channels have looped (or song ends)
			{
				allDone = 0;	// hasn't looped
				break;
			}
		}
	} while (allDone == 0);


}

int mouseTransportBar(GTOBJECT* gt)
{
	if (!mouseb)
		return 0;



	if (checkMouseRange(TRANSPORT_BAR_X + 37, TRANSPORT_BAR_Y, 3, 2))
	{
		if (mouseb == MOUSEB_RIGHT)
		{
			detuneCent -= (msDelta / 2000.0f);
			if (detuneCent < -1)
				detuneCent = -1;
		}
		else
		{
			detuneCent += (msDelta / 2000.0f);
			if (detuneCent > 1)
				detuneCent = 1;
		}

		calculatefreqtable();
		return 0;


	}


	if (checkMouseRange(TRANSPORT_BAR_X + 12, TRANSPORT_BAR_Y, 3, 2))
	{
		if (mouseheld > HOLDDELAY)
		{
			setSongToBeginning(&gtObject);
			return 1;
		}
	}

	if (checkMouseRange(8, TRANSPORT_BAR_Y, 3, 2))
	{
		if (mouseb == MOUSEB_RIGHT)
		{
			masterVolume -= (msDelta / 500.0f);
			if (masterVolume < 0)
				masterVolume = 0;
		}
		else if (mouseb)
		{
			masterVolume += (msDelta / 500.0f);	// half a second to change int
			if (masterVolume > 6)
				masterVolume = 6;
		}
	}

	if (prevmouseb)
		return 0;

	int change = 1;
	if (mouseb == MOUSEB_RIGHT)
		change = -change;

	if (checkMouseRange(0, TRANSPORT_BAR_Y, 3, 2))
	{
		if (ctrlpressed)
		{
			if (bothShiftAndCtrlPressed)
			{
				if (gt->songinit != PLAY_STOPPED)
				{
					stopsong(gt);
				}

				stopScreenDisplay();
				displayCharWindow();
				restartScreenDisplay();
				return 1;
			}
			else
			{
				//		editPaletteMode = 1 - editPaletteMode;
				stopScreenDisplay();
				displayPaletteEditorWindow(gt);
				restartScreenDisplay();
				return 1;
				//		handlePaletteDisplay(gt, currentPalettePreset);
				//		if (editPaletteMode)
				//		{
				//			stopsong(gt);
				//		}
			}
		}
		else
		{
			currentPalettePreset += change;
			if (currentPalettePreset >= MAX_PALETTE_PRESETS)
				currentPalettePreset = MAX_PALETTE_PRESETS - 1;
			else if (currentPalettePreset < 0)
				currentPalettePreset = 0;

			setSkin(currentPalettePreset);
		}

		return 1;
	}

	//return 0;

	if (checkMouseRange(4, TRANSPORT_BAR_Y, 3, 2))
	{
		if (editPaletteMode)
			return 1;

		int newCh = editorInfo.maxSIDChannels + change * 3;
		if (newCh < 3)
			newCh = 3;
		else if (newCh > 12)
			newCh = 12;

		if (newCh != editorInfo.maxSIDChannels)
		{
			undoCreateEditorInfoBackup();
			editorInfo.maxSIDChannels = newCh;
			undoAddEditorSettingsToList();
			handleSIDChannelCountChange(&gtObject);
		}
		return 1;
	}

	if (checkMouseRange(TRANSPORT_BAR_X - 2, TRANSPORT_BAR_Y, 3, 2))
	{
		editorInfo.epoctave += change;
		if (editorInfo.epoctave < 0)
			editorInfo.epoctave = 0;
		else if (editorInfo.epoctave > 6)
			editorInfo.epoctave = 6;

		return 1;
	}

	if (checkMouseRange(TRANSPORT_BAR_X + 4 - 1, TRANSPORT_BAR_Y, 3, 2))
	{
		if (editPaletteMode)
			return 1;

		if (shiftOrCtrlPressed)
		{
			autoNextPattern = 1 - autoNextPattern;
			if (autoNextPattern)
				sprintf(infoTextBuffer, "Auto move to previous/next pattern: Enabled");
			else
				sprintf(infoTextBuffer, "Auto move to previous/next pattern: Disabled");
			forceInfoLine = 1;
		}
		else
		{
			followplay = 1 - followplay;
			if (followplay && gt->songinit != PLAY_STOPPED)
				resetOrderView(&gtObject);
		}
		return 1;
	}

	if (checkMouseRange(TRANSPORT_BAR_X + 8 - 1, TRANSPORT_BAR_Y, 3, 2))
	{
		if (shiftOrCtrlPressed)
		{
			if (editorInfo.editmode == EDIT_MOD)
				return ptmodToggleSelectedLoopFromMod(gt);
			transportLoopPatternSelectArea = 1 - transportLoopPatternSelectArea;
			if (!transportLoopPatternSelectArea)
				ptmodplay_set_loop_range(0, 0, 0, 0, 0);
			if (transportLoopPatternSelectArea)
				sprintf(infoTextBuffer, "Selected pattern area looping: Enabled");
			else
				sprintf(infoTextBuffer, "Selected pattern area looping: Disabled");
			forceInfoLine = 1;
			// Enable / Disable select area looping

		}
		else
		{
			transportLoopPattern = 1 - transportLoopPattern;
		}
		return 1;
	}



	if (checkMouseRange(TRANSPORT_BAR_X + 12 - 1, TRANSPORT_BAR_Y, 3, 2))
	{
		if (editPaletteMode)
			return 1;

		handlePressRewind(mousebDoubleClick, gt);
		return 1;
	}


	if (checkMouseRange(TRANSPORT_BAR_X + 16 - 1, TRANSPORT_BAR_Y, 3, 2))
	{
		if (shiftOrCtrlPressed)
		{

			useOriginalGTFunctionKeys = 1 - useOriginalGTFunctionKeys;
			if (useOriginalGTFunctionKeys)
				sprintf(infoTextBuffer, "Use Original GT F1, F2 and F3 keys");
			else
				sprintf(infoTextBuffer, "Use GTUltraPro F1, F2 and F3 keys");
			forceInfoLine = 1;
		}
		else
			recordmode = 1 - recordmode;

#ifdef DISPLAY_FREE_MEM
		getFreeMem = 1;
#endif
		return 1;
	}

	if (checkMouseRange(TRANSPORT_BAR_X + 20 - 1, TRANSPORT_BAR_Y, 3, 2))
	{
		if (editPaletteMode)
			return 1;
		if (gt->songinit == PLAY_STOPPED)
			playFromCurrentPosition(gt, editorInfo.eppos);
		else
		{
			if (gt->songinit != PLAY_STOPPED)
			{
				stopsong(gt);
			}
		}
		return 1;
	}



	if (checkMouseRange(TRANSPORT_BAR_X + 24 - 1, TRANSPORT_BAR_Y, 3, 2))
	{
		if (editPaletteMode)
			return 1;
		if (editorInfo.editmode == EDIT_MOD)
		{
			PTMOD_PREVIEW_STATS stats;

			ptmodplay_get_stats(&stats);
			ptmodToggleFollowNow(&stats);
			return 1;
		}

		nextSongPos(&gtObject);
		return 1;
	}

	if (checkMouseRange(TRANSPORT_BAR_X + 28, TRANSPORT_BAR_Y, 4, 2))
	{
		int index = 0;
		if (mousey >= TRANSPORT_BAR_Y + 1)
			index += 2;
		if (mousex >= TRANSPORT_BAR_X + 28 + 2)
			index += 1;

		transportPolySIDEnabled[index] = 1 - transportPolySIDEnabled[index];
		return 1;
	}

	if (checkMouseRange(TRANSPORT_BAR_X + 33, TRANSPORT_BAR_Y, 3, 2))
	{
		if (shiftOrCtrlPressed)
		{
			if (midiEnabled)
				displayMIDISelectWindow();
		}
		else
		{
			lastDisplayChanCount = 0;
			transportShowKeyboard = 1 - transportShowKeyboard;
		}
	}


	if (checkMouseRange(TRANSPORT_BAR_X + 41, TRANSPORT_BAR_Y, 3, 2))
	{
		stereoMode++;
		stereoMode %= 3;
		if (stereoMode == 1 && editorInfo.maxSIDChannels == 3)
			stereoMode++;
		if (stereoMode == 0)
			monomode = 1;
		else
			monomode = 0;

		//monomode ^= 1;
	}

	return 0;

}

void handlePressRewind(int doubleClick, GTOBJECT* gt)
{
	if (doubleClick)
		previousSongPos(&gtObject, 1);
	else
	{
		if (gt->songinit == PLAY_STOPPED)
			previousSongPos(&gtObject, 1);		// move to start of previous pattern
		else if (editorInfo.eppos)
			previousSongPos(&gtObject, 0);		// playing. Not at start of pattern. move to start of current pattern
		else
			previousSongPos(&gtObject, 1);		// playing. At start of pattern. move to start of previous pattern
	}
}


int checkMouseRange(int x, int y, int w, int h)
{
	if (mousex >= x && mousex < x + w && mousey >= y && mousey < y + h)
		return 1;
	return 0;
}

static int orderChannelHasPattern(int songNum, int songCh)
{
	for (int pos = 0; pos < MAX_SONGLEN; pos++)
	{
		int value = songorder[songNum][songCh][pos];
		if (value >= LOOPSONG)
			break;
		if (value < REPEAT)
			return 1;
	}
	return 0;
}

static void initialiseOrderChannelPattern(GTOBJECT* gt, int songNum, int songCh, int actualChannel, int patternNumber)
{
	memset(&songorder[songNum][songCh][0], 0, MAX_SONGLEN + 2);
	songorder[songNum][songCh][0] = patternNumber;
	songorder[songNum][songCh][1] = LOOPSONG;
	songlen[songNum][songCh] = 1;

	clearExpandedSongChannel(songNum, songCh);
	generateExpandedSongChannel(songNum, songCh);
	songCompressedSize[songNum][songCh] = generateCompressedSongChannel(songNum, songCh, 1);

	if (actualChannel >= 0 && actualChannel < MAX_PLAY_CH)
	{
		gt->editorUndoInfo.editorInfo[actualChannel].epnum = patternNumber;
		gt->editorUndoInfo.editorInfo[actualChannel].espos = 0;
		gt->editorUndoInfo.editorInfo[actualChannel].esend = 0;
	}
}

static void ensureVisibleSIDChannelPatterns(GTOBJECT* gt)
{
	int baseSong = editorInfo.esnum;

	if (editorInfo.maxSIDChannels > MAX_CHN)
		baseSong &= 0xfffffffe;

	for (int visualChannel = 0; visualChannel < editorInfo.maxSIDChannels && visualChannel < MAX_PLAY_CH; visualChannel++)
	{
		int songNum = baseSong;
		int songCh = visualChannel % MAX_CHN;
		int patternNumber = visualChannel;

		if (editorInfo.maxSIDChannels > MAX_CHN && visualChannel >= MAX_CHN)
			songNum++;
		if (songNum >= MAX_SONGS || patternNumber >= MAX_PATT)
			continue;
		if (orderChannelHasPattern(songNum, songCh))
			continue;

		initialiseOrderChannelPattern(gt, songNum, songCh, visualChannel, patternNumber);
	}
}

static void normalizeSIDChannelEditorState(GTOBJECT* gt)
{
	int visibleChannels = getVisibleChannelCount();
	int patternVisualChannel;
	int orderVisualChannel;

	if (visibleChannels < 1)
		visibleChannels = 1;

	if (editorInfo.maxSIDChannels <= MAX_CHN)
		editorInfo.esnum &= 0xfffffffe;

	patternVisualChannel = getEditorVisualPatternChannel();
	orderVisualChannel = getEditorVisualOrderChannel();

	if (patternVisualChannel >= visibleChannels)
		patternVisualChannel = visibleChannels - 1;
	if (orderVisualChannel >= visibleChannels)
		orderVisualChannel = visibleChannels - 1;
	if (patternVisualChannel < 0)
		patternVisualChannel = 0;
	if (orderVisualChannel < 0)
		orderVisualChannel = 0;

	if (editorInfo.editmode == EDIT_ORDERLIST)
	{
		setEditorVisualPatternChannel(patternVisualChannel);
		setEditorVisualOrderChannel(orderVisualChannel);
	}
	else
	{
		setEditorVisualOrderChannel(orderVisualChannel);
		setEditorVisualPatternChannel(patternVisualChannel);
	}

	editorInfo.epmarkchn = -1;
	editorInfo.esmarkchn = -1;
	editorInfo.esmarkchnend = -1;

	if (gt->masterLoopChannel < 0 || gt->masterLoopChannel >= editorInfo.maxSIDChannels)
		setMasterLoopChannel(gt, "sid_count_normalize");
}

void handleSIDChannelCountChange(GTOBJECT* gt)
{
	if (gt->songinit != PLAY_STOPPED)
	{
		stopsong(gt);
	}
	SDL_Delay(100);	// ensure that GT player has done an update, so that playing channels are now silent prior to setting new channel count

	//	if (gt->masterLoopChannel >= editorInfo.maxSIDChannels)
	//		gt->masterLoopChannel = 0;


	normalizeSIDChannelEditorState(gt);

	if ((editorInfo.eseditpos == songlen[editorInfo.esnum][editorInfo.eschn]) || (editorInfo.eseditpos > songlen[editorInfo.esnum][editorInfo.eschn] + 1))
	{
		editorInfo.eseditpos = songlen[editorInfo.esnum][editorInfo.eschn] + 1;
		editorInfo.escolumn = 0;
	}
	setMasterLoopChannel(gt, "debug_a");

	ensureVisibleSIDChannelPatterns(gt);
	orderSelectPatternsFromSelected(gt);
	return;

	int resetSong = 0;

	for (int i = 0;i < MAX_PLAY_CH;i++)
	{
		int c2 = getActualChannel(editorInfo.esnum, i);
		int sng = getActualSongNumber(editorInfo.esnum, i);

		int ep = gt->editorUndoInfo.editorInfo[c2].espos;
		int ep2 = ep;

		if (editorInfo.expandOrderListView == 0)
		{
			if (songlen[sng][c2 % 6] > 0)
			{
				do
				{
					ep2 = ep;
					if ((songorder[sng][c2 % 6][ep] >= REPEAT) && (songorder[sng][c2 % 6][ep] < TRANSDOWN))
						ep++;
					if ((songorder[sng][c2 % 6][ep] >= TRANSDOWN) && (songorder[sng][c2 % 6][ep] < LOOPSONG))
						ep++;
				} while (ep != ep2);
				gt->editorUndoInfo.editorInfo[c2].epnum = songorder[sng][c2 % 6][ep];
				gt->editorUndoInfo.editorInfo[c2].espos = ep;	// set current channel pos
			}
			else
			{
				resetSong = 1;
				gt->editorUndoInfo.editorInfo[c2].epnum = 0;
				gt->editorUndoInfo.editorInfo[c2].espos = 0;	// reset current channel pos
			}
		}
		else
		{
			if (songOrderLength[sng][c2 % 6] > 0)
			{
				gt->editorUndoInfo.editorInfo[c2].epnum = songOrderPatterns[sng][c2 % 6][ep];
				gt->editorUndoInfo.editorInfo[c2].espos = ep;	// set current channel pos
			}
			else
			{
				resetSong = 1;
				gt->editorUndoInfo.editorInfo[c2].epnum = 0;
				gt->editorUndoInfo.editorInfo[c2].espos = 0;	// reset current channel pos
			}
		}
	}

	// overkill??
	if (resetSong)
	{
		editorInfo.esnum = 1;
		songchange(gt, 1);
		editorInfo.esnum = 0;
		songchange(gt, 1);
	}


}


int backupPatternPos[MAX_PLAY_CH];
int oldepViewValue;
int oldepPosValue;



void backupPatternDisplayInfo(GTOBJECT* gt)
{
	// JP - orderSelectPatternsFromSelected resets the pattern step position. We need to preserve this when changing subsong
	oldepViewValue = editorInfo.epview;
	oldepPosValue = editorInfo.eppos;

	for (int c = 0; c < editorInfo.maxSIDChannels; c++)	// V1.2.2
	{
		int c2 = getVisualChannelActualChannel(c);	// 0-11
		backupPatternPos[c] = gt->chn[c2].pattptr;
	}
}

void restorePatternDisplayInfo(GTOBJECT* gt)
{
	editorInfo.epview = oldepViewValue;
	editorInfo.eppos = oldepPosValue;

	for (int c = 0; c < editorInfo.maxSIDChannels; c++)	//V1.2.2 restore pattern play position when selecting another pattern in orderlist
	{
		int c2 = getVisualChannelActualChannel(c);	// 0-11
		gt->chn[c2].pattptr = backupPatternPos[c];
		// check if cursor > patlen. And reset to 0 if it is
		if (c == getEditorVisualPatternChannel())
		{
			if (editorInfo.eppos > pattlen[gt->editorUndoInfo.editorInfo[c2].epnum])
			{
				editorInfo.eppos = 0;
				editorInfo.epview = -VISIBLEPATTROWS / 2;
				//				jdebug[10]++;
				//				sprintf(textbuffer, "out of range: %d", jdebug[10]);
				//				printtext(70, 36, 0xe, textbuffer);
			}
		}

	}
}

int jcc = 0;

void nextSongPos(GTOBJECT* gt)
{
	int songNum = getActualSongNumber(editorInfo.esnum, gt->masterLoopChannel);	//editorInfo.epchn);
	int ac = getActualChannel(editorInfo.esnum, gt->masterLoopChannel);	//editorInfo.epchn);	// 0-12
	int c3 = ac % 6;

	int len = songlen[songNum][c3];
	if (editorInfo.expandOrderListView)
		len = songOrderLength[songNum][c3];

	if (gt->songinit == PLAY_STOPPED)
	{


		if (gt->editorUndoInfo.editorInfo[ac].espos < len - 1)
		{
			//			sprintf(textbuffer, "%d ac %d c3 %d esp %d sn %d sl %d", jcc++, ac, c3, gt->editorUndoInfo.editorInfo[ac].espos, songNum, songlen[songNum][c3]);
			//			printtext(60, 36, 0xe, textbuffer);

			backupPatternDisplayInfo(gt);	// V1.2.2 - keep pattern editing position when selecting a new song pos

			editorInfo.eseditpos = gt->editorUndoInfo.editorInfo[ac].espos + 1;
			orderSelectPatternsFromSelected(gt);
			if (gt->editorUndoInfo.editorInfo[ac].espos - editorInfo.esview >= VISIBLEORDERLIST)
			{
				editorInfo.esview = gt->editorUndoInfo.editorInfo[ac].espos - VISIBLEORDERLIST + 1;
				editorInfo.eseditpos = gt->editorUndoInfo.editorInfo[ac].espos;
			}

			restorePatternDisplayInfo(gt);	// V1.2.2 - keep pattern editing position when selecting a new song pos

			updateviewtopos(gt);
		}
	}
	else
	{
		if (gt->chn[gt->masterLoopChannel].songptr < len)
		{
			orderPlayFromPosition(gt, 0, gt->chn[gt->masterLoopChannel].songptr, gt->masterLoopChannel, 0);
		}
	}
}


void previousSongPos(GTOBJECT* gt, int songDffset)
{
	int songNum = getActualSongNumber(editorInfo.esnum, gt->masterLoopChannel);	//editorInfo.epchn);
	int ac = getActualChannel(editorInfo.esnum, gt->masterLoopChannel);	//editorInfo.epchn);	// 0-12
	int c3 = ac % 6;

	if (gt->songinit == PLAY_STOPPED)
	{

		editorInfo.eseditpos = gt->editorUndoInfo.editorInfo[ac].espos - songDffset;	// move back n positions (0 if just moving to top of pattern. 1 otherwise)
		if (editorInfo.eseditpos < 0)
			editorInfo.eseditpos = 0;
		else if (songDffset)
		{
			/*
			Check if we're on a transpose or repeat. If so, keep moving backwards to a valid pattern
			*/
			if (editorInfo.expandOrderListView == 0)
			{
				while ((songorder[songNum][c3][editorInfo.eseditpos] >= REPEAT) && (songorder[songNum][c3][editorInfo.eseditpos] < LOOPSONG))
				{
					editorInfo.eseditpos--;
					if (editorInfo.eseditpos < 0)
					{
						editorInfo.eseditpos = 0;
						break;
					}
				}
			}
		}

		backupPatternDisplayInfo(gt);	// V1.2.2 - keep pattern editing position when selecting a new song pos
		orderSelectPatternsFromSelected(gt);
		restorePatternDisplayInfo(gt);	// V1.2.2 - keep pattern editing position when selecting a new song pos

		if (gt->editorUndoInfo.editorInfo[ac].espos < editorInfo.esview)
		{
			editorInfo.esview = gt->editorUndoInfo.editorInfo[ac].espos;
			editorInfo.eseditpos = gt->editorUndoInfo.editorInfo[ac].espos;
		}
		updateviewtopos(gt);
	}
	else
	{
		if (gt->chn[gt->masterLoopChannel].songptr)
		{
			int so = gt->chn[gt->masterLoopChannel].songptr - 1 - songDffset;
			if (so < 0)
				so = 0;

			if (songDffset)
			{
				/*
				Check if we're on a transpose or repeat. If so, keep moving backwards to a valid pattern
				*/

				if (editorInfo.expandOrderListView == 0)
				{
					while ((songorder[songNum][c3][so] >= REPEAT) && (songorder[songNum][c3][so] < LOOPSONG))
					{
						so--;
						if (so < 0)
						{
							so = 0;
							break;
						}
					}
				}
			}

			orderPlayFromPosition(gt, 0, so, gt->masterLoopChannel, 0);
		}
	}
}

void setSongToBeginning(GTOBJECT* gt)
{
	if (editPaletteMode)
		return;

	editorInfo.eseditpos = 0;
	editorInfo.eschn = editorInfo.epchn;
	if (gt->songinit == PLAY_STOPPED)
	{
		backupPatternDisplayInfo(gt);	// V1.2.2 Preserve pattern editing position if possible
		orderSelectPatternsFromSelected(gt);
		restorePatternDisplayInfo(gt);	// V1.2.2 Preserve pattern editing position if possible
	}
	else
	{
		orderPlayFromPosition(gt, 0, 0, 0, 0);
		editorInfo.esview = 0;
		editorInfo.eseditpos = 0;
	}

	updateviewtopos(gt);
}

void playFromCurrentPosition(GTOBJECT* gt, int currentPos)
{

	if (editPaletteMode)
		return;

	int t1 = followplay;
	int t2 = gt->interPatternLoopEnabledFlag;
	int t3 = transportLoopPattern;
	gt->loopEnabledFlag = 0;
	gt->interPatternLoopEnabledFlag = 0;
	int c2 = getActualChannel(editorInfo.esnum, editorInfo.epchn);
	handleShiftSpace(gt, c2, currentPos * 4, 0, 1);
	if (gt == &gtObject)
		ptmodplay_start_at(editorInfo.eseditpos, currentPos);

	gt->loopEnabledFlag = t3;	//transportLoopPattern;
	gt->interPatternLoopEnabledFlag = t2;
	followplay = t1;
}

void mouseTrack()
{
	editorInfo.mouseTrack = 1;
	editorInfo.mouseTrackX = mousex;
	editorInfo.mouseTrackY = mousey;
	ModifyTrackGetOriginalValue();
}


void ModifyTrackGetOriginalValue()
{
	if (editorInfo.editmode == EDIT_TABLES)
	{
		if (editorInfo.etcolumn < 2)	// columns 0+1 = lefttable value
			editorInfo.mouseTrackOriginalValue = ltable[editorInfo.etnum][editorInfo.etpos];
		else
			editorInfo.mouseTrackOriginalValue = rtable[editorInfo.etnum][editorInfo.etpos];
	}
	if (editorInfo.editmode == EDIT_INSTRUMENT)
	{
		unsigned char* ptr = &instr[editorInfo.einum].ad;
		ptr += editorInfo.eipos;
		editorInfo.mouseTrackOriginalValue = *ptr;
	}

}

// Hold left mouse button + move mouse = modify value under cursor
int mouseTrackModify(int editorWindow)
{


	if (!mouseb || (editorWindow == EDIT_TABLES && editorInfo.editTableMode != EDIT_TABLE_NONE))
	{
		editorInfo.mouseTrackDoUndo = 0;
		editorInfo.mouseTrack = 0;
		return 1;
	}

	if (editorInfo.mouseTrack == 0)	// This ensures that we only track hold/move when we start to hold on the same value that the cursor is on
		return 1;


	int xdiff = mousex - editorInfo.mouseTrackX;
	//int ydiff = mousey - editorInfo.mouseTrackY;

	if (editorWindow == EDIT_TABLES)
	{

		char* dptr = (char*)&ltable[editorInfo.etnum][editorInfo.etpos];
		if (editorInfo.etcolumn > 1)	// columns 0+1 = lefttable value
			dptr = (char*)&rtable[editorInfo.etnum][editorInfo.etpos];

		int v = editorInfo.mouseTrackOriginalValue << 1;
		v += xdiff;
		v >>= 1;

		*dptr = v;

		if (*dptr != editorInfo.mouseTrackOriginalValue)
		{
			editorInfo.mouseTrackDoUndo = 1;
			return 0;	// 0 = stop recording change in UNDO. We only record the last value when releasing button for a hold/drag
		}
		return 1;
	}
	if (editorWindow == EDIT_INSTRUMENT)
	{
		if (editorInfo.eicolumn < 2 && editorInfo.eipos < 2)	// Editing ADSR. Only hold/drag on nybbles
		{
			int v = editorInfo.mouseTrackOriginalValue;

			char* dptr = (char*)&instr[editorInfo.einum].ad;
			if (editorInfo.eipos == 1)
				dptr = (char*)&instr[editorInfo.einum].sr;

			if (editorInfo.eicolumn == 0)	// high nybble			
				v >>= 4;
			else
				v &= 0xf;

			//		v <<= 1;
			v += xdiff;
			//		v >>= 1;

			if (v < 0)
				v = 0;
			else if (v > 0xf)
				v = 0xf;
			if (editorInfo.eicolumn == 0)	// high nybble
			{
				v <<= 4;
				v |= (*dptr & 0xf);
			}
			else
				v |= (*dptr & 0xf0);

			*dptr = v;
			if (*dptr != editorInfo.mouseTrackOriginalValue)
			{
				editorInfo.mouseTrackDoUndo = 1;
				return 0;	// 0 = stop recording change in UNDO. We only record the last value when releasing button for a hold/drag
			}
			return 1;
		}
		else
		{
			unsigned char* dptr = &instr[editorInfo.einum].ad;
			dptr += editorInfo.eipos;

			int v = editorInfo.mouseTrackOriginalValue << 1;
			v += xdiff;
			*dptr = v >> 1;

			setTableBackgroundColours(editorInfo.einum);

			if (*dptr != editorInfo.mouseTrackOriginalValue)
			{
				editorInfo.mouseTrackDoUndo = 1;
				return 0;	// 0 = stop recording change in UNDO. We only record the last value when releasing button for a hold/drag
			}
			return 1;

		}
	}
	return 0;
}

int checkForMouseInTable(int c, int OX, int OY)
{
	if ((mousey > OY) && (mousey <= OY + 6 + 9) && (mousex >= OX + 3 + c * 10) && (mousex <= OX + 7 + c * 10))
	{
		if (editorInfo.editmode != EDIT_TABLES && prevmouseb)	// Don't allow hold/drag to select another panel
			return 0;

		if (!mouseb)
			return 0;

		int newpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
		if (newpos < 0) newpos = 0;
		if (newpos >= MAX_TABLELEN) newpos = MAX_TABLELEN - 1;

		editorInfo.editmode = EDIT_TABLES;
		disableEnterToReturnToLastPos = 1;

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb))
		{
			if ((editorInfo.etmarknum != editorInfo.etnum) || (newpos != editorInfo.etmarkend))
			{
				editorInfo.etmarknum = c;
				editorInfo.etmarkstart = editorInfo.etmarkend = newpos;
			}
		}
		if (mouseb & MOUSEB_LEFT && (!prevmouseb))
		{
			editorInfo.etnum = c;
			editorInfo.etpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
			editorInfo.etcolumn = mousex - (OX + 3) - c * 10;
			if (editorInfo.etcolumn > 2) editorInfo.etcolumn--;
			mouseTrack();	// MUST DO AFTER SETTING ABOVE VALUES

		}

		if (editorInfo.etpos < 0) editorInfo.etpos = 0;
		if (editorInfo.etpos > MAX_TABLELEN - 1) editorInfo.etpos = MAX_TABLELEN - 1;

		if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) editorInfo.etmarkend = newpos;

		return 1;
	}
	return 0;
}


int checkForMouseInDetailedFilterTable(int OX, int OY)
{

	if ((mousey > OY) && (mousey <= OY + 6 + 9))
	{
		int newpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
		if (newpos < 0) newpos = 0;
		if (newpos >= MAX_TABLELEN) newpos = MAX_TABLELEN - 1;

		if (mousex >= OX + 4 && mousex <= OX + 7 && mouseb && !prevmouseb)
		{
			detailedFilterTableChangeCommand(mousex - (OX + 4), newpos);	// select Filter Cutoff, modify,filterinfo, jump/end option
			return 1;
		}
		else if (mousex >= OX + 20 && mousex <= OX + 22 && mouseb && !prevmouseb)
		{
			detailedFilterTableChangeSign(mousex - (OX + 20), newpos);
		}
		else if (mousex >= OX + 24 && mousex <= OX + 29 && mouseb && !prevmouseb)
		{
			detailedFilterTableChangeFilterType((mousex - (OX + 24)) / 2, newpos);
		}

		int col = -1;
		if (mousex > OX + 8 && mousex < OX + 17)
		{
			col = 0;
		}
		else if (mousex >= OX + 17 && mousex <= OX + 18)
			col = mousex - (OX + 17);
		else if (mousex >= OX + 21 && mousex <= OX + 22)
			col = 2 + (mousex - (OX + 21));
		if (col == -1)
			return 0;

		if (editorInfo.editmode != EDIT_TABLES && prevmouseb)	// Don't allow hold/drag to select another panel
			return 0;

		if (!mouseb)
			return 0;

		editorInfo.editmode = EDIT_TABLES;
		disableEnterToReturnToLastPos = 1;

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb))
		{
			if ((editorInfo.etmarknum != editorInfo.etnum) || (newpos != editorInfo.etmarkend))
			{
				editorInfo.etmarknum = 0;
				editorInfo.etmarkstart = editorInfo.etmarkend = newpos;
			}
		}
		if (mouseb & MOUSEB_LEFT && (!prevmouseb))
		{

			editorInfo.etnum = FTBL;
			editorInfo.etpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
			editorInfo.etcolumn = col;
			mouseTrack();	// MUST DO AFTER SETTING ABOVE VALUES
		}
		if (editorInfo.etpos < 0) editorInfo.etpos = 0;
		if (editorInfo.etpos > MAX_TABLELEN - 1) editorInfo.etpos = MAX_TABLELEN - 1;

		if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) editorInfo.etmarkend = newpos;

		return 1;
	}
	return 0;
}



int checkForMouseInDetailedPulseTable(int OX, int OY)
{

	if ((mousey > OY) && (mousey <= OY + 6 + 9))
	{
		int newpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
		if (newpos < 0) newpos = 0;
		if (newpos >= MAX_TABLELEN) newpos = MAX_TABLELEN - 1;

		if (mousex >= OX + 4 && mousex <= OX + 6 && mouseb && !prevmouseb)
		{
			detailedPulseTableChangeCommand(mousex - (OX + 4), newpos);
			return 1;
		}
		else if (mousex == OX + 21 && mouseb && !prevmouseb)
		{
			detailedPulseTableChangeSign(mousex - (OX + 21), newpos);
		}


		int col = -1;
		if (mousex > OX + 8 && mousex < OX + 17)
		{
			col = 0;
		}
		else if (mousex >= OX + 17 && mousex <= OX + 19)	// 0-2 = left column (2 only used for setting pulse width)
			col = mousex - (OX + 17);
		else if (mousex >= OX + 22 && mousex <= OX + 23)	// 3-4 = right column
			col = 3 + (mousex - (OX + 22));
		if (col == -1)
			return 0;

		if (editorInfo.editmode != EDIT_TABLES && prevmouseb)	// Don't allow hold/drag to select another panel
			return 0;

		if (!mouseb)
			return 0;

		editorInfo.editmode = EDIT_TABLES;
		disableEnterToReturnToLastPos = 1;

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb))
		{
			if ((editorInfo.etmarknum != editorInfo.etnum) || (newpos != editorInfo.etmarkend))
			{
				editorInfo.etmarknum = 0;
				editorInfo.etmarkstart = editorInfo.etmarkend = newpos;
			}
		}
		if (mouseb & MOUSEB_LEFT && (!prevmouseb))
		{
			editorInfo.etnum = PTBL;
			editorInfo.etpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
			editorInfo.etcolumn = col;
			mouseTrack();	// MUST DO AFTER SETTING ABOVE VALUES
		}
		if (editorInfo.etpos < 0) editorInfo.etpos = 0;
		if (editorInfo.etpos > MAX_TABLELEN - 1) editorInfo.etpos = MAX_TABLELEN - 1;

		if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) editorInfo.etmarkend = newpos;

		return 1;
	}
	return 0;
}



/*
Mouse clicking on the various -WDCJ / AB / + options in the detailed wavetable view?
If so, initialise data accordingly.
*/
int checkForMouseInDetailedWaveTable(int OX, int OY)
{
	if ((mousey > OY) && (mousey <= OY + 6 + 9))
	{
		int newpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
		if (newpos < 0) newpos = 0;
		if (newpos >= MAX_TABLELEN) newpos = MAX_TABLELEN - 1;

		if (mousex >= OX + 4 && mousex <= OX + 8 && mouseb && !prevmouseb)
		{
			detailedWaveTableChangeCommand(mousex - (OX + 4), newpos);	// Clicked on either -WDCJ
			return 1;
		}
		else if (mousex >= OX + 10 && mousex <= OX + 11 && mouseb && !prevmouseb)
		{
			detailedWaveTableChangeData(mousex - (OX + 10), newpos);		// Clicked on either R or A (relative / Absolute)
			return 1;
		}
		else if (mousex == 81 && mouseb && !prevmouseb)
		{
			detailedWaveTableChangeRelativeNote(mousex - (OX + 10), newpos);	// Clicked on +/- to change sign of relative note
			return 1;
		}
		int col = -1;
		if (mousex > OX + 12 && mousex < OX + 18)
		{
			col = 0;
		}
		else if (mousex >= OX + 18 && mousex <= OX + 19)
			col = mousex - (OX + 18);
		else if (mousex >= OX + 22 && mousex <= OX + 23)
			col = 2 + (mousex - (OX + 22));
		if (col == -1)
			return 0;

		if (editorInfo.editmode != EDIT_TABLES && prevmouseb)	// Don't allow hold/drag to select another panel
			return 0;

		if (!mouseb)
			return 0;



		editorInfo.editmode = EDIT_TABLES;
		disableEnterToReturnToLastPos = 1;

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb))
		{
			if ((editorInfo.etmarknum != editorInfo.etnum) || (newpos != editorInfo.etmarkend))
			{
				editorInfo.etmarknum = 0;
				editorInfo.etmarkstart = editorInfo.etmarkend = newpos;
			}
		}
		if (mouseb & MOUSEB_LEFT && (!prevmouseb))
		{
			editorInfo.etnum = 0;
			editorInfo.etpos = mousey - (OY + 1) + editorInfo.etview[editorInfo.etnum];
			editorInfo.etcolumn = col;
			mouseTrack();	// MUST DO AFTER SETTING ABOVE VALUES
		}
		if (editorInfo.etpos < 0) editorInfo.etpos = 0;
		if (editorInfo.etpos > MAX_TABLELEN - 1) editorInfo.etpos = MAX_TABLELEN - 1;

		if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) editorInfo.etmarkend = newpos;

		return 1;
	}
	return 0;
}




/*
Wavetable right side:  00-5F Relative notes
			   60-7F Negative relative notes (lower pitch)
			   80    Keep frequency unchanged
			   81-DF Absolute notes C#0 - B-7

*/

/*
User has clicked on +/- to change relative note sign
*/
void detailedWaveTableChangeRelativeNote(int x, int y)
{
	unsigned char v = ltable[0][y];
	if (v < 1 || v >= 0xf0)	// Does right table contain abs or relative note?
		return;	// No..

	int vr = rtable[0][y];
	if (vr >= 0x80 || vr == 0)	// Does right table contain relative note (or a non-zero relative note)?
		return;	// No..

	if (vr <= 0x5f)	// positive relative?
	{
		vr = 0x80 - vr;
		if (vr < 0x60)
			vr = 0x60;
		rtable[0][y] = vr;
	}
	else
	{
		vr -= 0x60;
		vr = 0x20 - vr;
		rtable[0][y] = vr;
	}

}

/*
User has clicked on R/A to change wavetable relative / absolute
*/
void detailedWaveTableChangeData(int x, int y)
{
	unsigned char v = ltable[0][y];
	if (v < 1 || v >= 0xf0)	// Does right table contain abs or relative note?
		return;	// No..

	v = rtable[0][y];


	if (v >= 0x81)	// was abs note?
	{
		if (x == 1)
			rtable[0][y] = 0x80;	// Was absolute already. So turn off (make "no change")
		else
			rtable[0][y] = 0;	// set to Relative
	}
	else if (v < 0x80)	// relative note?
	{
		if (x == 0)
			rtable[0][y] = 0x80;	// Was Relative already. So turn off (make "no change")
		else
			rtable[0][y] = 0x81;	// Set to Absolute
	}
	else if (v == 0x80)	// Was "no change"
	{
		if (x == 0)
			rtable[0][y] = 0;	// Set to Relative
		else
			rtable[0][y] = 0x81;	// Set to Absolute
	}

}


/*
Wavetable left side:   00    Leave waveform unchanged
					   01-0F Delay this step by 1-15 frames
					   10-DF Waveform values
					   E0-EF Inaudible waveform values $00-$0F
					   F0-FE Execute command 0XY-EXY. Right side is parameter.
					   FF    Jump. Right side tells position ($00 = stop)

*/

/*
If user changes the command type, we need to reset the data within the table so that it's initialised in range for that specific command
*/
void detailedWaveTableChangeCommand(int x, int y)
{

	unsigned char v = ltable[WTBL][y];

	int currentCommand = 0;
	if (v >= 1 && v <= 0xf)
		currentCommand = 2;
	else if (v >= 0x10 && v <= 0xef)
		currentCommand = 1;
	else if (v >= 0xf0 && v <= 0xfe)
		currentCommand = 3;
	else if (v == 0xff)
		currentCommand = 4;

	if (x == currentCommand)	// selecting the same command?
		return;

	if (x == 0)
		ltable[WTBL][y] = 0;
	else if (x == 2)
		ltable[WTBL][y] = 1;
	else if (x == 1)
	{
		ltable[WTBL][y] = 0x41;
	}
	else if (x == 3)
		ltable[WTBL][y] = 0xf0;
	else if (x == 4)
	{
		ltable[WTBL][y] = 0xff;
		rtable[WTBL][y] = 0;
	}
}



/*
Filtertable left side: 00    Set cutoff, indicated by right side
					   01-7F Filter modulation step. Left side indicates time
							 and right side the speed (signed 8-bit value)
					   80-F0 Set filter parameters. Left side high nybble
							 tells the passband ($90 = lowpass, $A0 = bandpass
							 etc.) and right side tells resonance/channel
							 bitmask, as in command BXY.
					   FF    Jump. Right side tells position ($00 = stop)
*/

/*
If user changes the command type, we need to reset the data within the table so that it's initialised in range for that specific command
*/

void detailedFilterTableChangeFilterType(int x, int y)
{

	unsigned char v = ltable[FTBL][y];

	if (v >= 0x80 && v <= 0xfe)
	{
		v ^= (0x10 << x);
		ltable[FTBL][y] = v;
	}
}

void detailedFilterTableChangeSign(int x, int y)
{
	unsigned char v = ltable[FTBL][y];
	if (v >= 0x1 && v <= 0x7f)
	{
		if (x == 0)// filter modulation
		{
			v = rtable[FTBL][y];
			if (v >= 0x80)
			{
				v = 0x100 - v;
				rtable[FTBL][y] = v;
			}
			else
			{
				if (v == 0)
					v = 1;
				v = 0x100 - v;
				rtable[FTBL][y] = v;
			}
		}
	}
	else if (v >= 0x80)
	{
		int rv = rtable[FTBL][y];
		rv ^= 1 << x;
		rtable[FTBL][y] = rv;
	}
}



void detailedPulseTableChangeSign(int x, int y)
{
	unsigned char v = ltable[PTBL][y];
	if (v >= 0x1 && v <= 0x7f)
	{
		if (x == 0)// filter modulation
		{
			v = rtable[PTBL][y];
			if (v >= 0x80)
			{
				v = 0x100 - v;
				rtable[PTBL][y] = v;
			}
			else
			{
				if (v == 0)
					v = 1;
				v = 0x100 - v;
				rtable[PTBL][y] = v;
			}
		}
	}
}


void detailedFilterTableChangeCommand(int x, int y)
{

	unsigned char v = ltable[FTBL][y];

	// CMFJ
	int currentCommand = 0;
	if (v >= 1 && v <= 0x7f)
		currentCommand = 1;
	else if (v >= 0x80 && v <= 0xfe)
		currentCommand = 2;
	else if (v == 0xff)
		currentCommand = 3;

	if (x == currentCommand)	// selecting the same command?
		return;

	if (x == 0)
	{
		ltable[FTBL][y] = 0;
		rtable[FTBL][y] = 0;
	}
	else if (x == 1)
	{
		ltable[FTBL][y] = 0x1;
		rtable[FTBL][y] = 0x0;
	}
	else if (x == 2)
	{
		ltable[FTBL][y] = 0x90;		// low pass
		rtable[FTBL][y] = 0x7;		// all channels
	}
	else if (x == 3)
	{
		ltable[FTBL][y] = 0xff;
		rtable[FTBL][y] = 0;
	}

}


void detailedPulseTableChangeCommand(int x, int y)
{

	unsigned char v = ltable[PTBL][y];

	// CMFJ
	int currentCommand = 0;
	if (v >= 1 && v <= 0x7f)
		currentCommand = 1;
	else if (v >= 0x80 && v <= 0xfe)
		currentCommand = 0;
	else if (v == 0xff)
		currentCommand = 2;

	if (x == currentCommand)	// selecting the same command?
		return;

	if (x == 1)	// Modify
	{
		ltable[PTBL][y] = 0x1;
		rtable[PTBL][y] = 0x0;
	}
	else if (x == 0) //Set
	{
		ltable[PTBL][y] = 0x88;		// Set pulse to 0x800 as default
		rtable[PTBL][y] = 0x0;		//
	}
	else if (x == 2)
	{
		ltable[PTBL][y] = 0xff;		// stop
		rtable[PTBL][y] = 0;
	}

}

int checkMouseInWaveformInfo()
{
	if (!mouseb)
		return 0;
	if (prevmouseb)
		return 0;
	if (mousey != TRANSPORT_BAR_Y - 1)
		return 0;
	if (waveformDisplayInfo.displayOnOff == 0)
		return 0;

	int x = mousex - (TRANSPORT_BAR_X - 5);
	if (x < 0)
		return 0;
	if (x % 5 == 0)
		return 0;

	x /= 5;
	if (x > 7)
		return 0;

	waveformDisplayInfo.value ^= (0x80 >> x);

	if (editorInfo.editmode == EDIT_TABLES)
	{
		if (ltable[WTBL][editorInfo.etpos] < 0xf0)
		{
			int data = waveformDisplayInfo.value;	// wavetable value in left table
			if (data < 0x10)
				data += 0xe0;
			*waveformDisplayInfo.destAddress = (unsigned char)data;
		}
		else
		{
			*waveformDisplayInfo.destAddress = (unsigned char)waveformDisplayInfo.value;
		}
	}
	else if (editorInfo.editmode == EDIT_INSTRUMENT)
	{
		*waveformDisplayInfo.destAddress = (unsigned char)waveformDisplayInfo.value;
	}
	else if (editorInfo.editmode == EDIT_PATTERN)
	{
		*waveformDisplayInfo.destAddress = (unsigned char)waveformDisplayInfo.value;

	}

	return 1;
}


// Wrote all this, then realised I could just easily modify the existing calculatefreqtable 
// Will leave it here anyway. May use it again one day...
float noteToHz(int note)
{
	note *= 100;
	return centToHz(note);
}

// 1200 per octave (100 per semitone)
float centToHz(int cent)
{
	float a = 440.0f; //frequency of A (coomon value is 440Hz)
	return (a / 32) * pow(2, ((cent - 900) / 1200.0));
}


int HzToSIDFreq(float hz)
{
	float phi = 985248;	// PAL

	//	phi = 1022727;	//editorInfo.ntsc

	float freqCons = (256 * 256 * 256) / phi;
	float sidFreq = freqCons * hz;
	return sidFreq;
}

void detunePitchTable()
{
	for (int i = 0;i < 0x60;i++)
	{
		int cent = (12 + i) * 100;
		cent += detuneCent - 100;	// -99 > + 99

		float hz = centToHz(cent);
		int SIDFreq = HzToSIDFreq(hz);
		freqtbllo[i] = SIDFreq & 0xff;
		freqtblhi[i] = (SIDFreq >> 8) & 0xff;
	}

}


void createFilename(char* filePath, char* newfileName, char* filename)
{
	int d = 0;
	for (d = strlen(filePath) - 1; d >= 0; d--)
	{
		if ((filePath[d] == '/') || (filePath[d] == '\\'))
		{
			strcpy(newfileName, filePath);
			break;
		}
	}
	strcpy(&newfileName[d + 1], filename);
}


void checkForMouseInOrderList(GTOBJECT* gt, int maxCh)
{
	// Song editpos & songnumber selection
	if ((mousey >= PANEL_ORDER_Y + 1) && (mousey <= PANEL_ORDER_Y + 1 + maxCh) && (mousex >= PANEL_ORDER_X + 5))
	{
		if (editorInfo.editmode != EDIT_ORDERLIST && prevmouseb)	// Don't allow hold/drag to select another panel
			return;

		if (!mouseb)
			return;

		int newpos = editorInfo.esview + (mousex - (PANEL_ORDER_X + 5)) / 3;
		int newcolumn = (mousex - (PANEL_ORDER_X + 5)) % 3;
		int newchn = mousey - (PANEL_ORDER_Y + 1);
		int songNum = getVisualChannelSongNumber(newchn);
		int songCh = getVisualChannelLocalChannel(newchn);
		if (newcolumn < 0) newcolumn = 0;
		if (newcolumn > 1) newcolumn = 1;
		if (newpos < 0)
		{
			newpos = 0;
			newcolumn = 0;
		}
		if (newpos == songlen[songNum][songCh])
		{
			newpos++;
			newcolumn = 0;
		}
		if (newpos > songlen[songNum][songCh] + 1)
		{
			newpos = songlen[songNum][songCh] + 1;
			newcolumn = 1;
		}

		editorInfo.editmode = EDIT_ORDERLIST;

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb) && (newpos < songlen[songNum][songCh]))
		{
			setEditorVisualOrderChannel(newchn);

			if ((editorInfo.esmarkchn != editorInfo.eschn) || (newpos != editorInfo.esmarkend))
			{
				editorInfo.esmarkchn = editorInfo.eschn;
				editorInfo.esmarkstart = editorInfo.esmarkend = newpos;
			}

		}

		if (mouseb & MOUSEB_LEFT)
		{
			int m = mousebDoubleClick;
			int s = shiftOrCtrlPressed;

			if (((mouseheld > HOLDDELAY) || (s != 0)) && !editPaletteMode)
			{
				setEditorVisualOrderChannel(newchn);
				editorInfo.eseditpos = newpos;
				editorInfo.escolumn = newcolumn;
				setMasterLoopChannel(gt, "debug_b");
				backupPatternDisplayInfo(gt);	//V1.2.2 - Preserve pattern edit position
				orderSelectPatternsFromSelected(gt);
				restorePatternDisplayInfo(gt);	//V1.2.2
			}
			else if (m && !editPaletteMode)	// double click?
			{

				setEditorVisualOrderChannel(newchn);
				editorInfo.eseditpos = newpos;
				editorInfo.escolumn = newcolumn;
				setMasterLoopChannel(gt, "debug_f");
				orderPlayFromPosition(gt, 0, editorInfo.eseditpos, editorInfo.eschn, 1);
			}
			else
			{
				setEditorVisualOrderChannel(newchn);
				editorInfo.eseditpos = newpos;
				editorInfo.escolumn = newcolumn;

				setMasterLoopChannel(gt, "debug_c");
			}
		}

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (newpos < songlen[songNum][songCh]))
			editorInfo.esmarkend = newpos;
	}
}

void validateStereoMode()
{
	if (stereoMode == 1 && editorInfo.maxSIDChannels == 3)
		stereoMode++;
	if (stereoMode == 0)
		monomode = 1;
	else
		monomode = 0;
}



char backupFolderName[MAX_PATHNAME];

void saveBackupSong()
{
	if (!createBackupFolder())
		return;

	time_t mytime = time(NULL);
	char* time_str = ctime(&mytime);
	if (time_str == NULL)
		return;
	time_str[strlen(time_str) - 1] = '\0';
	replacechar(time_str, ':', '_');

	int result = snprintf(backupSngFilename, sizeof backupSngFilename, "%s/GTUltraPro_%s.sng", backupFolderName, time_str);
	if ((result < 0) || (result >= (int)sizeof backupSngFilename))
	{
		SDL_Log("Backup filename is too long; skipping backup save.\n");
		return;
	}

	strcpy(tempSngFilename, songfilename);
	strcpy(songfilename, backupSngFilename);
	int tempforce3chan = forceSave3ChannelSng;
	forceSave3ChannelSng = 0;
	savesong();
	forceSave3ChannelSng = tempforce3chan;
	strcpy(songfilename, tempSngFilename);
	strcpy(loadedsongfilename, tempSngFilename);
}

int replacechar(char* str, char orig, char rep)
{
	char* ix = str;
	int n = 0;
	while ((ix = strchr(ix, orig)) != NULL) {
		*ix++ = rep;
		n++;
	}
	return n;
}


int createBackupFolder()
{

	DIR* folder;
	int result;

	memset(backupFolderName, '\0', sizeof(backupFolderName));

	//JP - NOT TESTED ! SETTING DIFFERENT PATH FOR LINUX FOR READING PALETTES FROM .EXE LOCATION INSTEAD OF .CFG
#ifdef __WIN32__
	createFilename(appFileName, backupFolderName, "gtbackup");
#else
	const char* home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
	{
		SDL_Log("HOME is not set; skipping backup save.\n");
		return 0;
	}
	result = snprintf(backupFolderName, sizeof backupFolderName, "%s/.goattrk/gtbackup", home);
	if ((result < 0) || (result >= (int)sizeof backupFolderName))
	{
		SDL_Log("Backup folder path is too long; skipping backup save.\n");
		backupFolderName[0] = '\0';
		return 0;
	}
#endif

	folder = opendir(backupFolderName);
	if (folder == NULL)
	{
#ifdef __WIN32__
		result = mkdir(backupFolderName);	// default backup folder didn't exist in config file location. It does now..
#else
		result = mkdir(backupFolderName, 0777);
#endif
		return result == 0;
	}
	closedir(folder);
	return 1;

}




void checkForMouseInExtendedOrderList(GTOBJECT* gt, int maxCh)
{

	// Song editpos & songnumber selection
	if ((mousey >= PANEL_ORDER_Y + 2) && (mousey <= 3 + EXTENDEDVISIBLEORDERLIST) && (mousex >= PANEL_ORDER_X + 4) && (mousex <= PANEL_ORDER_X + 4 + 4 + (maxCh * 6)))
	{
		if (editorInfo.editmode != EDIT_ORDERLIST && prevmouseb)	// Don't allow hold/drag to select another panel
			return;

		if (!mouseb)
			return;

		int newpos = editorInfo.esview + (mousey - (PANEL_ORDER_Y + 2));	// -EXTENDEDVISIBLEORDERLIST);
		int newcolumn = (mousex - (PANEL_ORDER_X + 4)) % 6;
		if (newcolumn > 4)
			return;
		//			newcolumn--;

		int newchn = (mousex - (PANEL_ORDER_X + 4)) / 6;
		int songNum = getVisualChannelSongNumber(newchn);
		int songCh = getVisualChannelLocalChannel(newchn);

		if (newpos < 0)
		{
			newpos = 0;
		}
		if (newpos >= 0x7ff)
			newpos = 0x7ff;

		// JP - Left mouse button + either middle or right button to cancel selection
		if ((mouseb & MOUSEB_LEFT) && (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)))
		{
			editorInfo.esmarkchn = -1;
			editorInfo.esmarkchnend = -1;	// Cancel selection
			selectingInOrderList = 0;
			return;
		}

		if (songOrderPatterns[songNum][songCh][newpos] < 0xff)
		{
			if (newcolumn == 2)	// is cursor in the gap between pattern and transpose? if so, move it left
			{
				if (mouseb & MOUSEB_LEFT)
					return;
			}
		}

		editorInfo.editmode = EDIT_ORDERLIST;

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (!prevmouseb) && (newpos < MAX_SONGLEN_EXPANDED))	//< songOrderLength[editorInfo.esnum][editorInfo.eschn]))
		{
			setEditorVisualOrderChannel(newchn);

			if ((editorInfo.esmarkchn != editorInfo.eschn) || (newpos != editorInfo.esmarkend))
			{
				editorInfo.esmarkchn = editorInfo.eschn;
				editorInfo.esmarkstart = editorInfo.esmarkend = newpos;
				selectingInOrderList = 1;
				selectingInOrderListDeltaTime = 0;
			}

		}

		if (mouseb & MOUSEB_LEFT)
		{
			int m = mousebDoubleClick;
			int s = shiftOrCtrlPressed;

			if (((mouseheld > HOLDDELAY) || (s != 0)) && !editPaletteMode)
			{
				if (editorInfo.eseditpos == newpos)
				{
					setEditorVisualOrderChannel(newchn);
					editorInfo.eseditpos = newpos;
					editorInfo.escolumn = newcolumn;
					setMasterLoopChannel(gt, "debug_d");
					backupPatternDisplayInfo(gt);	//V1.2.2 - Preserve pattern edit position
					orderSelectPatternsFromSelected(gt);
					restorePatternDisplayInfo(gt);	//V1.2.2
				}
				else
				{
					mouseheld = 0;
					setEditorVisualOrderChannel(newchn);
					editorInfo.eseditpos = newpos;
					editorInfo.escolumn = newcolumn;
				}
			}
			else if (m && !editPaletteMode)	// double click?
			{

				setEditorVisualOrderChannel(newchn);
				editorInfo.eseditpos = newpos;
				editorInfo.escolumn = newcolumn;
				setMasterLoopChannel(gt, "debug_g");
				orderPlayFromPosition(gt, 0, editorInfo.eseditpos, editorInfo.eschn, 1);

			}
			else
			{
				if (editorInfo.eseditpos != newpos)
					mouseheld = 0;
				setEditorVisualOrderChannel(newchn);
				editorInfo.eseditpos = newpos;
				editorInfo.escolumn = newcolumn;

				setMasterLoopChannel(gt, "debug_e");
			}
		}

		if ((mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE)) && (newpos < MAX_SONGLEN_EXPANDED))
		{
			if (editorInfo.esmarkchn != -1)	// are we currently selecting?
			{
				editorInfo.esmarkend = newpos;
				editorInfo.esmarkchnend = songCh;
			}
		}
	}
	else if (selectingInOrderList)
	{
		if (mouseb & (MOUSEB_RIGHT | MOUSEB_MIDDLE))
		{
			int deltaTicks = SDL_GetTicks();
			selectingInOrderListDeltaTime += deltaTicks - selectingInOrderListDeltaTicks;
			selectingInOrderListDeltaTicks = deltaTicks;

			if (selectingInOrderListDeltaTime > 20)	//50)
			{
				selectingInOrderListDeltaTime = 0;
				if (mousey > PANEL_ORDER_Y + 1 + EXTENDEDVISIBLEORDERLIST)
				{
					if (editorInfo.esmarkend < 0x7ff)
					{
						editorInfo.esmarkend = editorInfo.esview + EXTENDEDVISIBLEORDERLIST;
						if (editorInfo.esmarkend >= 0x7ff)
							editorInfo.esmarkend = 0x7fe;
						editorInfo.eseditpos = editorInfo.esmarkend;
					}
				}
				else if (mousey < PANEL_ORDER_Y + 2)
				{
					if (editorInfo.esmarkend > 0)
					{
						editorInfo.esmarkend = editorInfo.esview - 1;
						if (editorInfo.esmarkend < 0)
							editorInfo.esmarkend = 0;
						editorInfo.eseditpos = editorInfo.esmarkend;
					}
				}
			}
		}
		else
			selectingInOrderList = 0;
	}

}

void stopScreenDisplay()
{
	displayingPanel = 1;
	return;

	while (displayStopped == 0)
	{
		SDL_Delay(1000 / 60);
	}
}

void restartScreenDisplay()
{
	displayingPanel = 0;
}


void handleLoad(GTOBJECT* gt, char* dragdropfile)
{
	stopScreenDisplay();

	int ok = load(gt, dragdropfile);
	if (ok)
	{
		songExported = 0;
		forceSave3ChannelSng = 0;

		// Set up song 1 and then 0... This allows editor pattern numbers to be complete, so that F3 works from the very start.
		// (Bit of a nasty hack..Meh. Never mind)
		editorInfo.esnum = 1;
		songchange(gt, 1);
		editorInfo.esnum = 0;
		songchange(gt, 1);

		playUntilEnd(editorInfo.esnum);

		copyCurrentToSngBuffer(gt, currentSongFile);	// V1.4.0
	}

	restartScreenDisplay();


}

void handleLoadVideo(GTOBJECT* gt)
{
	if (!gt_video_enabled())
	{
		snprintf(infoTextBuffer, sizeof infoTextBuffer, "Video support not enabled in this build");
		forceInfoLine = 1;
		return;
	}

	stopScreenDisplay();

	if (fileselector(videofilename, songpath, videofilter, "LOAD MP4 VIDEO", 0, gt, CEDIT, 0))
	{
		if (gt_video_load(videofilename))
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "Video Loaded:%.*s", (int)(sizeof infoTextBuffer - sizeof("Video Loaded:")), videofilename);
		else
			snprintf(infoTextBuffer, sizeof infoTextBuffer, "Video Load Failed:%.*s", (int)(sizeof infoTextBuffer - sizeof("Video Load Failed:")), videofilename);
		forceInfoLine = 1;
	}

	restartScreenDisplay();
	key = 0;
	rawkey = 0;
}
