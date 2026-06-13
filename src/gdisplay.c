//
// GTUltra screen display routines
//

#define GDISPLAY_C

#include "goattrk2.h"
#include "gmodplay.h"

char *notename[] =
{ "C-0", "C#0", "D-0", "D#0", "E-0", "F-0", "F#0", "G-0", "G#0", "A-0", "A#0", "B-0",
 "C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
 "C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
 "C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3",
 "C-4", "C#4", "D-4", "D#4", "E-4", "F-4", "F#4", "G-4", "G#4", "A-4", "A#4", "B-4",
 "C-5", "C#5", "D-5", "D#5", "E-5", "F-5", "F#5", "G-5", "G#5", "A-5", "A#5", "B-5",
 "C-6", "C#6", "D-6", "D#6", "E-6", "F-6", "F#6", "G-6", "G#6", "A-6", "A#6", "B-6",
 "C-7", "C#7", "D-7", "D#7", "E-7", "F-7", "F#7", "G-7", "G#7", "...", "---", "+++" };


char *notenameTableView[] =
{ "C-0", "C#0", "D-0", "D#0", "E-0", "F-0", "F#0", "G-0", "G#0", "A-0", "A#0", "B-0",
 "C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
 "C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
 "C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3",
 "C-4", "C#4", "D-4", "D#4", "E-4", "F-4", "F#4", "G-4", "G#4", "A-4", "A#4", "B-4",
 "C-5", "C#5", "D-5", "D#5", "E-5", "F-5", "F#5", "G-5", "G#5", "A-5", "A#5", "B-5",
 "C-6", "C#6", "D-6", "D#6", "E-6", "F-6", "F#6", "G-6", "G#6", "A-6", "A#6", "B-6",
 "C-7", "C#7", "D-7", "D#7", "E-7", "F-7", "F#7", "G-7", "G#7", "A-7", "A#7", "B-7",
 "???", "???", "???", "???", "???", "???", "???", "???", "???", "???", "???", "???",
 "???", "???", "???", "???", "???", "???", "???", "???", "???", "???", "???", "???",
 "???", "???", "???", "???", "???", "???", "???", "???" };



char timechar[] = { ':', ' ' };

static void displayPtmodSettings(int cc, int OX, int OY);
static void displayPtmodPatternView(GTOBJECT *gt);

static int colorWithForeground(int color, int foreground)
{
	return (color & 0xff00) | (foreground & 0xff);
}

static int colorWithBackground(int color, int background)
{
	return (color & 0xff) | ((background & 0xff) << 8);
}

static int colorDistanceSquared(int color1, int color2)
{
	int dr = (int)paletteR[color1 & 0xff] - (int)paletteR[color2 & 0xff];
	int dg = (int)paletteG[color1 & 0xff] - (int)paletteG[color2 & 0xff];
	int db = (int)paletteB[color1 & 0xff] - (int)paletteB[color2 & 0xff];

	return dr * dr + dg * dg + db * db;
}

static int readableForegroundForBackground(int foreground, int background)
{
	static const int fallbackColors[] = {
		CPATTERN_NOTE_FOREGROUND,
		CPATTERN_COMMAND_FOREGROUND,
		CPATTERN_INSTRUMENT_FOREGROUND,
		CPATTERN_HIGHLIGHT_FOREGROUND,
		CPATTERN_INDEX_HIGHLIGHT,
		CTITLES_FOREGROUND,
		CINFO_FOREGROUND,
		15,
		0
	};
	enum { MIN_PATTERN_TEXT_DISTANCE = 48 * 48 };
	int bestForeground = foreground & 0xff;
	int bestDistance = colorDistanceSquared(bestForeground, background);

	if (bestDistance >= MIN_PATTERN_TEXT_DISTANCE)
		return bestForeground;

	for (size_t i = 0; i < sizeof fallbackColors / sizeof fallbackColors[0]; i++)
	{
		int candidate = fallbackColors[i] & 0xff;
		int distance = colorDistanceSquared(candidate, background);

		if (distance > bestDistance)
		{
			bestForeground = candidate;
			bestDistance = distance;
		}
	}

	return bestForeground;
}

static int colorWithReadableForeground(int color, int foreground)
{
	int background = (color >> 8) & 0xff;
	int readableForeground = readableForegroundForBackground(foreground, background);

	return colorWithForeground(color, readableForeground);
}

static void debugPrintPatternRow(const char* renderer, int c, int c2, int p, int patternNumber, unsigned lastpattptr, int colorNoChange, int color2, int color3, int color4, int color5, const char* text)
{
	static int rowsPrinted = 0;

	if (!debugPattern || rowsPrinted >= 48)
		return;

	fprintf(stdout,
		"[pattern] %s c=%d actual=%d row=%d pat=%02X lastpattptr=%08X colorNoChange=%d note=%04X inst=%04X cmd=%04X data=%04X text='%.13s'\n",
		renderer,
		c,
		c2,
		p,
		patternNumber & 0xff,
		lastpattptr,
		colorNoChange,
		color2 & 0xffff,
		color3 & 0xffff,
		color4 & 0xffff,
		color5 & 0xffff,
		text);
	rowsPrinted++;
	if (rowsPrinted == 48)
	{
		fprintf(stdout, "[pattern] renderer debug limit reached\n");
		fflush(stdout);
	}
}

int UIUnderline = 0;

int initForST64 = 0;

char debugtext[256];

static int isValidPatternNumber(int patternNumber)
{
	return patternNumber >= 0 && patternNumber < MAX_PATT;
}

static int getDisplayPatternNumber(GTOBJECT *gt, int channel)
{
	if (channel < 0 || channel >= MAX_PLAY_CH)
		return -1;
	return gt->editorUndoInfo.editorInfo[channel].epnum;
}

static int getDisplayPatternLength(GTOBJECT *gt, int channel)
{
	int patternNumber = getDisplayPatternNumber(gt, channel);
	if (!isValidPatternNumber(patternNumber))
		return -1;
	return pattlen[patternNumber];
}

void setSIDTracker64KeyOnStyle()
{
	if (SIDTracker64ForIPadIsAmazing != 0)
	{
		notename[(12 * 8) - 1] = " | ";	// Makes it look more like ST64 view instead of +++
	}
	else
	{
		notename[(12 * 8) - 1] = "+++";
	}
}

void printmainscreen(GTOBJECT *gt)
{
	updatescreenlayout();
	clearscreen(getColor(7, CGENERAL_BACKGROUND));
	printstatus(gt);
	fliptoscreen();
}

void displayupdate(GTOBJECT *gt)
{
	updatescreenlayout();
	if (cursorflashdelay >= 6)
	{
		cursorflashdelay %= 6;
		cursorflash++;
		cursorflash &= 3;
	}

	doDisplay((void*)gt);
}

int doDisplay(void *gt)
{
	GTOBJECT *gto = (GTOBJECT*)gt;
	//	while (!exitprogram)
		//{
		//	if (!displayingPanel)
		//	{
	printstatus(gto);

	if (transportShowKeyboard)
	{
		resetKeyboardDisplay();
		displayNotes(&gtObject);
		displayKeyboard();
	}

	fliptoscreen();
	//		displayStopped = 0;
	//	}
	//	else
	//		displayStopped = 1;

	//	SDL_Delay(1000 / 100);
	//}
	return 0;
}

//int expandOrderListView = 0;
int lastExpandOrderListView = -1;

void printstatus(GTOBJECT *gt)
{
	//	int c, d, color, color2;
	int cc = cursorcolortable[cursorflash];
	menu = 0;

	if ((mouseb > MOUSEB_LEFT) && (mousey == 0) && (!eamode))
		menu = 1;

	displayTopBar(menu, cc);
	displayTransportBar(gt);
	updateDisplayWhenFollowingAndPlaying(gt);
	displayPattern(gt);
	//	printtext(4, 4, getColor(CORDER_INST_TABLE_EDITING, 0), debugtext);

	if (editorInfo.expandOrderListView != lastExpandOrderListView)
	{
		lastExpandOrderListView = editorInfo.expandOrderListView;
		fillArea(PANEL_ORDER_X, PANEL_ORDER_Y, getSidePanelWidth(), EXTENDEDVISIBLEORDERLIST + 2, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), 32);	//65);
	}


	int lockPatternColor = getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);	//0xe;
	sprintf(textbuffer, "FILE %d/%d", currentSongFile, lastValidSongFileIndex + 1);
	printtext(PANEL_ORDER_X, PANEL_ORDER_Y - 1, lockPatternColor, textbuffer);


	if (editorInfo.editmode == EDIT_MOD)
	{
		displayPtmodSettings(cc, PANEL_ORDER_X, PANEL_ORDER_Y);
	}
	else
	{
		if (editorInfo.expandOrderListView == 0)	// display original orderlist + instrument information
		{
			displayOrderList(gt, cc, PANEL_ORDER_X, PANEL_ORDER_Y);
		}
		else
		{
			displayExpandedOrderList(gt, cc, PANEL_ORDER_X, PANEL_ORDER_Y);
		}

		displayInstrument(gt, cc, PANEL_INSTR_X, PANEL_INSTR_Y);

		displayTables(PANEL_TABLES_X, PANEL_TABLES_Y);
		displaySongInfo(cc, PANEL_NAMES_X, PANEL_NAMES_Y);
	}

	/*
		switch (autoadvance)
		{
		case 0:
			color = getColor(CINFO_FOREGROUND, CGENERAL_BACKGROUND);	// 10
			break;

		case 1:
			color = getColor(14, CGENERAL_BACKGROUND);	//14;
			break;

		case 2:
			color = getColor(12, CGENERAL_BACKGROUND);	//12;
			break;
		}
	*/
}

void resettime(GTOBJECT *gt)
{
	gt->timemin = 0;
	gt->timesec = 0;
	gt->timeframe = 0;
}

void setSongLengthTime(GTOBJECT *gt)
{
	gt->totalFrame = gt->timeframe;
	gt->totalSec = gt->timesec;
	gt->totalMin = gt->timemin;
}

void incrementtime(GTOBJECT *gt)
{
	{
		gt->timeframe++;
		if (!editorInfo.ntsc)
		{
			if (((editorInfo.multiplier) && (gt->timeframe >= PALFRAMERATE * editorInfo.multiplier))
				|| ((!editorInfo.multiplier) && (gt->timeframe >= PALFRAMERATE / 2)))
			{
				gt->timeframe = 0;
				gt->timesec++;
			}
		}
		else
		{
			if (((editorInfo.multiplier) && (gt->timeframe >= NTSCFRAMERATE * editorInfo.multiplier))
				|| ((!editorInfo.multiplier) && (gt->timeframe >= NTSCFRAMERATE / 2)))
			{
				gt->timeframe = 0;
				gt->timesec++;
			}
		}
		if (gt->timesec == 60)
		{
			gt->timesec = 0;
			gt->timemin++;
			gt->timemin %= 60;
		}
	}
}


void clearOrderListDisplay()
{

	sprintf(textbuffer, "RST");
	for (int c = 0; c < MAX_CHN; c++)
	{
		for (int d = 0; d < VISIBLEORDERLIST; d++)
		{
			printtext(44 + 20 + d * 3, 3 + c, getColor(1, 0), textbuffer);
		}
	}
}


void displayOrderList(GTOBJECT *gt, int cc, int OX, int OY)
{
	int color;
	int maxChan = getVisibleChannelCount();
	int cursorVisualChannel = getEditorVisualOrderChannel();
	int markVisualChannel = editorInfo.esmarkchn;
	int lockPatternColor = getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);	//0xe;

	if ((editorInfo.maxSIDChannels > 6) && (markVisualChannel >= 0))
		markVisualChannel += (editorInfo.esnum & 1) * MAX_CHN;

	sprintf(textbuffer, "CHN  ");
	printtext(OX, OY, lockPatternColor, textbuffer);

	lockPatternColor = getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);	//0xe;

	sprintf(textbuffer, "ORDERLIST ");
	printtext(OX + 5, OY, lockPatternColor, textbuffer);

	sprintf(textbuffer, "(SUBTUNE %02X, POS %02X)   ", editorInfo.esnum, editorInfo.eseditpos);
	printtext(OX + 15, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);

	for (int c = 0; c < maxChan; c++)
	{
		int c2 = getVisualChannelActualChannel(c);	// 0-11
		int songNum = getVisualChannelSongNumber(c);
		int songCh = getVisualChannelLocalChannel(c);
		int playingSong = getActualSongNumber(editorInfo.esnum, c2);	// JP added this. Only highlight playing row if showing the right song

		printtext(OX + 3, OY + 1 + c, getColor(15, CORDER_INST_BACKGROUND), "  ");

		sprintf(textbuffer, "%X  ", c2);	// c + 1);
		printtext(OX, OY + 1 + c, getColor(15, CORDER_INST_BACKGROUND), textbuffer);

		if (c2 == gt->masterLoopChannel)
		{
			printbyte(OX + 3, OY + 1 + c, getColor(14, CORDER_INST_BACKGROUND), 19);
			printbyte(OX + 3 + 1, OY + 1 + c, getColor(14, CORDER_INST_BACKGROUND), 20);
		}
		else if (c2 == gt->masterLoopChannel + MAX_CHN)
		{
			sprintf(textbuffer, "%X", gt->masterLoopSubSong);	// c + 1);
			printtext(OX + 2, OY + 1 + c, getColor(11, CORDER_INST_BACKGROUND), textbuffer);

			printbyte(OX + 3, OY + 1 + c, getColor(11, CORDER_INST_BACKGROUND), 19);
			printbyte(OX + 3 + 1, OY + 1 + c, getColor(11, CORDER_INST_BACKGROUND), 20);
		}
		else if (c2 == gt->masterLoopChannel - MAX_CHN)
		{
			sprintf(textbuffer, "%X", gt->masterLoopSubSong);	// c + 1);
			printtext(OX + 2, OY + 1 + c, getColor(11, CORDER_INST_BACKGROUND), textbuffer);

			printbyte(OX + 3, OY + 1 + c, getColor(11, CORDER_INST_BACKGROUND), 19);
			printbyte(OX + 3 + 1, OY + 1 + c, getColor(11, CORDER_INST_BACKGROUND), 20);
		}

		int foundLoopMarker = 0;
		for (int d = 0; d < VISIBLEORDERLIST; d++)
		{
			int p = editorInfo.esview + d;
			color = CORDER_INST_FOREGROUND;
			if (isplaying(gt))
			{
				int chnpos = gt->chn[c2].songptr;
				chnpos--;
				if (chnpos < 0)
					chnpos = 0;

				if ((p == chnpos) && (gt->chn[c2].advance))
				{
					if (songNum == playingSong)
						color = CPLAYING;
				}

			}
			if (p == gt->editorUndoInfo.editorInfo[c2].espos)
				color = CORDER_INST_TABLE_EDITING;

			if ((gt->editorUndoInfo.editorInfo[c2].esend) && (p == gt->editorUndoInfo.editorInfo[c2].esend))
				color = CORDER_INST_TABLE_EDITING;

			if (c2 >= editorInfo.maxSIDChannels)
				color = CORDER_INST_BACKGROUND;	// Hide channels 3-6 if SID set to 1 or 3


			if ((p < 0) || (p > (songlen[songNum][songCh] + 1)) || (p > MAX_SONGLEN + 1))
			{
				sprintf(textbuffer, "   ");
			}
			else
			{
				if (songorder[songNum][songCh][p] < LOOPSONG)
				{
					if ((songorder[songNum][songCh][p] < REPEAT) || (p >= songlen[songNum][songCh]))
					{
						if (songorder[songNum][songCh][p] >= 0xd0 && foundLoopMarker == 0)
						{
							sprintf(textbuffer, "Error! p=%x song %x, songlen=%x c=%x, val %x", p, songNum, songlen[songNum][songCh], songCh, songorder[songNum][songCh][p]);
							printtext(60, 12, 0xe, textbuffer);
							printf("JP Error\n");
						}
						sprintf(textbuffer, "%02X ", songorder[songNum][songCh][p]);
						if ((p >= songlen[songNum][songCh]) && (color == CORDER_INST_FOREGROUND)) color = CORDER_TRANS_REPEAT;
					}
					else
					{
						if (songorder[songNum][songCh][p] >= TRANSUP)
						{
							sprintf(textbuffer, "+%01X ", songorder[songNum][songCh][p] & 0xf);
							if (color == CORDER_INST_FOREGROUND) color = CORDER_TRANS_REPEAT;
						}
						else
						{
							if (songorder[songNum][songCh][p] >= TRANSDOWN)
							{
								sprintf(textbuffer, "-%01X ", 16 - (songorder[songNum][songCh][p] & 0x0f));
								if (color == CORDER_INST_FOREGROUND) color = CORDER_TRANS_REPEAT;
							}
							else
							{
								sprintf(textbuffer, "R%01X ", (songorder[songNum][songCh][p] + 1) & 0x0f);
								if (color == CORDER_INST_FOREGROUND) color = CORDER_TRANS_REPEAT;
							}
						}
					}
				}
				if (songorder[songNum][songCh][p] == LOOPSONG)
				{
					sprintf(textbuffer, "RST");
					foundLoopMarker = 1;
					if (color == CORDER_INST_FOREGROUND) color = CORDER_TRANS_REPEAT;
				}
			}

			printtext(OX + 5 + d * 3, OY + 1 + c, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

			if (c == markVisualChannel)
			{

				if (editorInfo.esmarkstart <= editorInfo.esmarkend)
				{
					if ((p >= editorInfo.esmarkstart) && (p <= editorInfo.esmarkend))
					{
						if (p != editorInfo.esmarkend)
							printbg(OX + 5 + d * 3, OY + 1 + c, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 3);
						else
							printbg(OX + 5 + d * 3, OY + 1 + c, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
					}
				}
				else
				{
					if ((p <= editorInfo.esmarkstart) && (p >= editorInfo.esmarkend))
					{
						if (p != editorInfo.esmarkstart)
							printbg(OX + 5 + d * 3, OY + 1 + c, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 3);
						else
							printbg(OX + 5 + d * 3, OY + 1 + c, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
					}
				}
			}

			// Flash cursor
			if ((p == editorInfo.eseditpos) && (editorInfo.editmode == EDIT_ORDERLIST) && (cursorVisualChannel == c))
			{
				if (!eamode) printbg(OX + 5 + d * 3 + editorInfo.escolumn, OY + 1 + c, cc << 8, 1);
			}
		}
	}

	// orderlist border
	printbyterow(OX, OY + 1 + maxChan, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f6, 38);
	printbytecol(OX + 38, OY + 1, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f5, maxChan);
}


void displayPaletteInfo(int cc)
{
	int color;

	for (int c = 0; c < MAX_CHN; c++)
	{
		int c2 = getActualChannel(editorInfo.esnum, c);	// 0-12
		//int playingSong = getActualSongNumber(editorInfo.esnum, c2);	// JP added this. Only highlight playing row if showing the right song
		sprintf(textbuffer, " %d ", c2);	// c + 1);
		printtext(40 + 20, 3 + c, getColor(15, 0), textbuffer);


		for (int d = 0; d < VISIBLEORDERLIST; d++)
		{
			int p = editorInfo.esview + d;
			color = CNORMAL;


			if ((p < 0) || (p >= MAX_PALETTE_ENTRIES) || c > 2)
			{
				sprintf(textbuffer, "   ");
			}
			else
			{
				sprintf(textbuffer, "%02X ", songorder[editorInfo.esnum][c][p]);
			}
			printtext(45 + 20 + d * 3, 3 + c, getColor(color, 0), textbuffer);

			// draw cursor
			if ((p == editorInfo.eseditpos) && (editorInfo.editmode == EDIT_ORDERLIST) && (editorInfo.eschn == c))
			{
				if (!eamode) printbg(45 + 20 + d * 3 + editorInfo.escolumn, 3 + c, cc < 8, 1);
			}
		}
	}
}


int displayOriginal3Channel = 0;

int lastDisplayChanCount = 0;
int maxDebugTicks;

int getVisibleChannelCount(void)
{
	if (editorInfo.maxSIDChannels <= 3)
		return 3;
	if (editorInfo.maxSIDChannels <= 6)
		return 6;
	if (editorInfo.maxSIDChannels <= 9)
		return 9;
	return 12;
}

static int getPtmodPatternChannelCount(void)
{
	PTMOD_PREVIEW_STATS stats;

	ptmodplay_get_stats(&stats);
	if (!stats.loaded || stats.channels <= 0)
		return 0;
	if (stats.channels > PTMOD_MAX_PREVIEW_CHANNELS)
		return PTMOD_MAX_PREVIEW_CHANNELS;
	return stats.channels;
}

static int isPtmodPatternView(void)
{
	return editorInfo.editmode == EDIT_MOD;
}

static int getPtmodPatternViewChannelCount(void)
{
	int channels = getPtmodPatternChannelCount();

	return channels > 0 ? channels : 4;
}

static int getPtmodPatternViewChannelWidth(void)
{
	return 21;
}

static int getPtmodPatternViewAreaWidth(void)
{
	return 5 + getPtmodPatternViewChannelCount() * getPtmodPatternViewChannelWidth();
}

static int getPatternDisplayChannelCount(void)
{
	return getVisibleChannelCount() + getPtmodPatternChannelCount();
}

int getEditorVisualPatternChannel(void)
{
	if (editorInfo.maxSIDChannels > 6)
		return (editorInfo.esnum & 1) * MAX_CHN + editorInfo.epchn;
	return editorInfo.epchn;
}

int getEditorVisualOrderChannel(void)
{
	if (editorInfo.maxSIDChannels > 6)
		return (editorInfo.esnum & 1) * MAX_CHN + editorInfo.eschn;
	return editorInfo.eschn;
}

int getVisualChannelSongNumber(int visualChannel)
{
	if (editorInfo.maxSIDChannels > 6)
		return (editorInfo.esnum & 0xfffffffe) + (visualChannel >= MAX_CHN);
	return editorInfo.esnum;
}

int getVisualChannelLocalChannel(int visualChannel)
{
	if (visualChannel < 0)
		visualChannel = 0;
	return visualChannel % MAX_CHN;
}

int getVisualChannelActualChannel(int visualChannel)
{
	return visualChannel;
}

void setEditorVisualPatternChannel(int visualChannel)
{
	if (visualChannel < 0)
		visualChannel = 0;
	if (visualChannel >= getVisibleChannelCount())
		visualChannel = getVisibleChannelCount() - 1;
	editorInfo.esnum = getVisualChannelSongNumber(visualChannel);
	editorInfo.epchn = getVisualChannelLocalChannel(visualChannel);
}

void setEditorVisualOrderChannel(int visualChannel)
{
	if (visualChannel < 0)
		visualChannel = 0;
	if (visualChannel >= getVisibleChannelCount())
		visualChannel = getVisibleChannelCount() - 1;
	editorInfo.esnum = getVisualChannelSongNumber(visualChannel);
	editorInfo.eschn = getVisualChannelLocalChannel(visualChannel);
}

int getPatternChannelWidth(void)
{
	if (isPtmodPatternView())
		return getPtmodPatternViewChannelWidth();
	return getPatternDisplayChannelCount() == 3 ? 14 : 9;
}

int getPatternAreaWidth(void)
{
	if (isPtmodPatternView())
		return getPtmodPatternViewAreaWidth();
	return 5 + getPatternDisplayChannelCount() * getPatternChannelWidth();
}

int getSidePanelWidth(void)
{
	if (editorInfo.expandOrderListView)
	{
		int expandedOrderWidth = 5 + getVisibleChannelCount() * 6;
		return expandedOrderWidth > 40 ? expandedOrderWidth : 40;
	}
	return 40;
}

int getSidePanelX(void)
{
	int sideX = getPatternAreaWidth() + 2;
	return sideX > 60 ? sideX : 60;
}

int getLayoutColumns(void)
{
	int columns = getSidePanelX() + getSidePanelWidth();
	if (columns < DEFAULT_COLUMNS)
		columns = DEFAULT_COLUMNS;
	if (columns > MAX_COLUMNS)
		columns = MAX_COLUMNS;
	return columns;
}

void displayPattern(GTOBJECT *gt)
{
	if (debugTicks > maxDebugTicks)
		maxDebugTicks = debugTicks;

	//	sprintf(textbuffer, "ticks:%2d/%2d ", debugTicks, maxDebugTicks);
	//	printtext(61, 1, 0xe, textbuffer);

	if (isPtmodPatternView())
	{
		displayPtmodPatternView(gt);
		displayOriginal3Channel = 0;
		return;
	}

	int maxChan = getVisibleChannelCount();

	if (maxChan != lastDisplayChanCount)	// clear pattern display area if swapping between 3/6 channel views
	{
		lastDisplayChanCount = maxChan;

		int color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);

		fillArea(PATTERN_X, PATTERN_Y, getPatternAreaWidth(), 30, color, ' ');
	}


	if (maxChan != 3)
	{
		displayPattern6Chn(gt);
		displayOriginal3Channel = 0;
	}
	else
	{
		displayPattern3Chn(gt);
		displayOriginal3Channel = 1;
	}
}

static int getPtmodPatternRowColor(int p)
{
	int color = getColor(CPATTERN_FOREGROUND1, CPATTERN_BACKGROUND1);

	if ((p % (stepsize * 2)) < stepsize)
	{
		if ((p % stepsize) == 0)
			color = getColor(CPATTERN_FIRST_FOREGROUND2, CPATTERN_FIRST_BACKGROUND2);
		else
			color = getColor(CPATTERN_FOREGROUND2, CPATTERN_BACKGROUND2);
	}
	else if ((p % stepsize) == 0)
	{
		color = getColor(CPATTERN_FIRST_FOREGROUND1, CPATTERN_FIRST_BACKGROUND1);
	}

	return color;
}

static void formatPtmodPeriodNote(int period, char *dest, size_t destSize)
{
	static const char *protrackerNotes[] = {
		"C-1", "C#1", "D-1", "D#1", "E-1", "F-1", "F#1", "G-1", "G#1", "A-1", "A#1", "B-1",
		"C-2", "C#2", "D-2", "D#2", "E-2", "F-2", "F#2", "G-2", "G#2", "A-2", "A#2", "B-2",
		"C-3", "C#3", "D-3", "D#3", "E-3", "F-3", "F#3", "G-3", "G#3", "A-3", "A#3", "B-3"
	};
	static const int protrackerPeriods[] = {
		856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
		428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
		214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
	};
	int bestIndex = 0;
	int bestDistance = 0x7fffffff;

	if (!dest || destSize == 0)
		return;

	if (period <= 0)
	{
		snprintf(dest, destSize, "---");
		return;
	}

	for (size_t i = 0; i < sizeof protrackerPeriods / sizeof protrackerPeriods[0]; i++)
	{
		int distance = period > protrackerPeriods[i] ? period - protrackerPeriods[i] : protrackerPeriods[i] - period;
		if (distance < bestDistance)
		{
			bestDistance = distance;
			bestIndex = (int)i;
		}
	}

	if (bestDistance)
		snprintf(dest, destSize, "???");
	else
		snprintf(dest, destSize, "%s", protrackerNotes[bestIndex]);
}

static void formatPtmodCell(const PTMOD_CELL *cell, char *dest, size_t destSize)
{
	char noteText[8];
	char sampleText[4];
	char effectText[4];

	if (!dest || destSize == 0)
		return;
	if (!cell)
	{
		snprintf(dest, destSize, "%10s", "");
		return;
	}

	formatPtmodPeriodNote(cell->period, noteText, sizeof noteText);
	if (cell->sample)
		snprintf(sampleText, sizeof sampleText, "%02X", cell->sample & 0xff);
	else
		snprintf(sampleText, sizeof sampleText, "..");
	if (cell->effect || cell->param)
		snprintf(effectText, sizeof effectText, "%01X%02X", cell->effect & 0x0f, cell->param & 0xff);
	else
		snprintf(effectText, sizeof effectText, "...");

	snprintf(dest, destSize, "%3s %2s %3s", noteText, sampleText, effectText);
}

static int clampPtmodOrderForDisplay(const PTMOD_PREVIEW_STATS *stats, int orderIndex)
{
	int maxOrder = 0;

	(void)stats;
	if (ptmodState.valid && ptmodState.songLength > 0)
		maxOrder = ptmodState.songLength - 1;
	if (orderIndex < 0)
		orderIndex = 0;
	if (orderIndex > maxOrder)
		orderIndex = maxOrder;
	return orderIndex;
}

static int getPtmodDisplayOrder(const PTMOD_PREVIEW_STATS *stats)
{
	if (stats && stats->loaded && stats->active && editorInfo.ptmodStreamFollow)
		return clampPtmodOrderForDisplay(stats, stats->orderIndex);
	return clampPtmodOrderForDisplay(stats, editorInfo.ptmodOrderIndex);
}

static int getPtmodDisplayRow(const PTMOD_PREVIEW_STATS *stats)
{
	int row;

	if (stats && stats->loaded && stats->active && editorInfo.ptmodStreamFollow)
		row = stats->row;
	else
		row = editorInfo.ptmodStreamRow;
	if (row < 0)
		row = 0;
	if (row >= PTMOD_ROWS)
		row = PTMOD_ROWS - 1;
	return row;
}

static void displayPtmodPatternView(GTOBJECT *gt)
{
	PTMOD_PREVIEW_STATS stats;
	PTMOD_RUNTIME_SETTINGS runtimeSettings;
	int channels;
	int chnWidth = getPtmodPatternViewChannelWidth();
	int patternWidth = getPtmodPatternViewAreaWidth();
	int clearWidth = getSidePanelX() - PATTERN_X - 1;
	int mutedColor = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
	int titleColor = getColor(CTITLES_FOREGROUND, CTRANSPORT_FOREGROUND);
	int dividerColor = getColor(CPATTERN_DIVIDER_LINE, CUNUSED_MUTED_BACKGROUND);
	int rowStart;
	int cursorRow;
	int maxRowStart;
	int orderIndex;
	int pattern;
	char title[160];

	(void)gt;
	ptmodplay_get_stats(&stats);
	ptmodplay_get_runtime_settings(&runtimeSettings);
	channels = getPtmodPatternViewChannelCount();

	if (clearWidth < patternWidth)
		clearWidth = patternWidth;
	if (PATTERN_X + clearWidth > MAX_COLUMNS)
		clearWidth = MAX_COLUMNS - PATTERN_X;

	fillArea(PATTERN_X, PATTERN_Y - 1, clearWidth, VISIBLEPATTROWS + 2, mutedColor, ' ');
	printbyterow(PATTERN_X, PATTERN_Y - 1, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_FOREGROUND), 0x20, clearWidth);
	printbytecol(PATTERN_X + patternWidth - 1, PATTERN_Y - 1, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f5, VISIBLEPATTROWS + 2);

	if (!stats.loaded)
	{
		printtext(PATTERN_X, PATTERN_Y, titleColor, "MOD PATTERN");
		printtext(PATTERN_X + 1, PATTERN_Y + 2, mutedColor, "No ProTracker MOD loaded");
		lastDisplayChanCount = -1;
		return;
	}

	orderIndex = getPtmodDisplayOrder(&stats);
	pattern = ptmod_order_pattern(orderIndex);
	cursorRow = getPtmodDisplayRow(&stats);
	maxRowStart = PTMOD_ROWS > VISIBLEPATTROWS ? PTMOD_ROWS - VISIBLEPATTROWS : 0;
	if (cursorRow < 0)
		cursorRow = 0;
	if (cursorRow >= PTMOD_ROWS)
		cursorRow = PTMOD_ROWS - 1;

	if (editorInfo.ptmodStreamFollow && stats.active)
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

	snprintf(title, sizeof title, "MOD PATTERN%s  ORD:%02d/%02d PAT:%02X ROW:%02X  PLAY:%02d:%02X DEL:%d FOLLOW:%s",
		editorInfo.ptmodEditPage == 0 ? "*" : " ",
		orderIndex,
		stats.songLength > 0 ? stats.songLength - 1 : 0,
		pattern < 0 ? 0 : pattern,
		cursorRow,
		stats.orderIndex,
		stats.row,
		stats.delayFramesRemaining,
		editorInfo.ptmodStreamFollow ? "ON" : "OFF");
	snprintf(textbuffer, sizeof textbuffer, "%-*.*s", clearWidth, clearWidth, title);
	printtext(PATTERN_X, PATTERN_Y - 1, getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND), textbuffer);

	printtext(PATTERN_X, PATTERN_Y, titleColor, " ROW");
	for (int c = 0; c < channels; c++)
	{
		int x = PATTERN_X + 4 + c * chnWidth;
		int headerColor = runtimeSettings.channelMute[c] ? getColor(CUNUSED_MUTED_FOREGROUND, CTRANSPORT_FOREGROUND) : titleColor;

		printbyte(x, PATTERN_Y, dividerColor, 0x1ff);
		snprintf(textbuffer, sizeof textbuffer, "CH%d NOTE SM FX", c + 1);
		printtext(x + 1, PATTERN_Y, headerColor, textbuffer);
	}

	for (int d = 0; d < VISIBLEPATTROWS; d++)
	{
		int p = rowStart + d;
		int rowColor = getPtmodPatternRowColor(p);
		int indexColor = rowColor & 0xff00;

		if (p < 0 || p >= PTMOD_ROWS)
		{
			indexColor = mutedColor;
			sprintf(textbuffer, "   ");
		}
		else
		{
			indexColor = colorWithReadableForeground(indexColor, (p % stepsize) == 0 ? CPATTERN_INDEX_HIGHLIGHT : CPATTERN_NOTE_FOREGROUND);
			if (!(patterndispmode & 1))
				sprintf(textbuffer, " %02d", p);
			else
				sprintf(textbuffer, " %02X", p & 0xff);
		}

		printbyte(PATTERN_X, PATTERN_Y + 1 + d, indexColor, 0x20);
		printtext(PATTERN_X + 1, PATTERN_Y + 1 + d, indexColor, textbuffer);

		for (int c = 0; c < channels; c++)
		{
			PTMOD_CELL cell;
			int x = PATTERN_X + 4 + c * chnWidth;
			int cellColor = rowColor;
			int muted = !runtimeSettings.enabled || runtimeSettings.channelMute[c];
			int hasData = 0;

			printbyte(x, PATTERN_Y + 1 + d, (rowColor & 0xff00) | CPATTERN_DIVIDER_LINE, 0x1ff);

			if (p < 0 || p >= PTMOD_ROWS || pattern < 0 || !ptmod_get_pattern_cell(pattern, p, c, &cell))
			{
				snprintf(textbuffer, sizeof textbuffer, "%10s", "");
				cellColor = mutedColor;
			}
			else
			{
				hasData = cell.sample || cell.period || cell.effect || cell.param;
				formatPtmodCell(&cell, textbuffer, sizeof textbuffer);

				if (muted)
					cellColor = mutedColor;
				else if (stats.active && stats.orderIndex == orderIndex && stats.row == p)
					cellColor = getColor(CPATTERN_HIGHLIGHT_PLAYING_LINE_FOREGROUND, CPATTERN_HIGHLIGHT_PLAYING_LINE_BACKGROUND);
				else if (editorInfo.ptmodBlockActive && editorInfo.ptmodBlockPattern == pattern)
				{
					int rowStart = editorInfo.ptmodBlockRowStart;
					int rowEnd = editorInfo.ptmodBlockRowEnd;
					int channelStart = editorInfo.ptmodBlockChannelStart;
					int channelEnd = editorInfo.ptmodBlockChannelEnd;

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
					if (p >= rowStart && p <= rowEnd && c >= channelStart && c <= channelEnd)
						cellColor = getColor(CORDER_INST_TABLE_EDITING, CORDER_INST_BACKGROUND);
					else
						cellColor = colorWithReadableForeground(cellColor, hasData ? CPATTERN_NOTE_FOREGROUND : CPATTERN_COMMAND_FOREGROUND);
				}
				else
					cellColor = colorWithReadableForeground(cellColor, hasData ? CPATTERN_NOTE_FOREGROUND : CPATTERN_COMMAND_FOREGROUND);
			}

				printtext(x + 1, PATTERN_Y + 1 + d, cellColor, textbuffer);
				if (editorInfo.ptmodEditPage == 0 && p == cursorRow && c == editorInfo.ptmodStreamChannel && !eamode)
				{
					static const int subColumnOffset[] = { 0, 4, 5, 7, 8, 9 };
					static const int subColumnWidth[] = { 3, 1, 1, 1, 1, 1 };
					int subColumn = editorInfo.ptmodStreamSubColumn;

					if (subColumn < 0 || subColumn >= (int)(sizeof subColumnOffset / sizeof subColumnOffset[0]))
						subColumn = 0;
				printbg(x + 1 + subColumnOffset[subColumn], PATTERN_Y + 1 + d, cursorcolortable[cursorflash] << 8, subColumnWidth[subColumn]);
			}
		}
	}

	lastDisplayChanCount = -1;
}

void displayPattern6Chn(GTOBJECT *gt)
{
	int sidChan = getVisibleChannelCount();
	int ptmodChan = getPtmodPatternChannelCount();
	int maxChan = sidChan + ptmodChan;
	int chnWidth = getPatternChannelWidth();
	int patternWidth = getPatternAreaWidth();
	int cursorVisualChannel = getEditorVisualPatternChannel();
	PTMOD_PREVIEW_STATS ptmodStats;

	ptmodplay_get_stats(&ptmodStats);

	printbytecol(PATTERN_X + patternWidth - 1, PATTERN_Y - 1, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f5, VISIBLEPATTROWS + 2);

	printbyterow(PATTERN_X, PATTERN_Y - 1, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_FOREGROUND), 0x20, patternWidth - 1);

	int cc = cursorcolortable[cursorflash];
	int maxpattlen = 0;
	UIUnderline = UNDERLINE_MASK;

	//	int highlightPatternLoop = 0;

	for (int c = 0; c < maxChan; c++)
	{
		int patternLen;

		if (c >= sidChan)
		{
			patternLen = ptmodStats.frames ? (int)ptmodStats.frames - 1 : 0;
		}
		else
		{
			int c2 = getVisualChannelActualChannel(c);	// 0-11
			patternLen = getDisplayPatternLength(gt, c2);
		}

		if (patternLen > maxpattlen)
			maxpattlen = patternLen;
	}

//	if (gt->editorUndoInfo.editorInfo[editorInfo.highlightLoopChannel].epnum == editorInfo.highlightLoopPatternNumber)
//		highlightPatternLoop = 1;

	printtext(PATTERN_X + 0, PATTERN_Y, getColor(CTITLES_FOREGROUND, CTRANSPORT_FOREGROUND), " POS");
	for (int d = 0; d < VISIBLEPATTROWS; d++)
	{
		int p = editorInfo.epview + d;
		int color = getColor(CPATTERN_FOREGROUND1, CPATTERN_BACKGROUND1);

		if ((p % (stepsize * 2)) < stepsize)
		{
			if ((p% stepsize) == 0)
				color = getColor(CPATTERN_FIRST_FOREGROUND2, CPATTERN_FIRST_BACKGROUND2);
			else
				color = getColor(CPATTERN_FOREGROUND2, CPATTERN_BACKGROUND2);
		}
		else if ((p% stepsize) == 0)
			color = getColor(CPATTERN_FIRST_FOREGROUND1, CPATTERN_FIRST_BACKGROUND1);

		color &= 0xff00;
		color |= CPATTERN_DIVIDER_LINE;

		int colort = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
		printbyte(PATTERN_X + patternWidth - 1, PATTERN_Y + 1 + d, colort, 0x20);	//0xff);

		color &= 0xff00;
		//		if ((p% stepsize) == 0)
		//			color |= CPATTERN_INDEX_HIGHLIGHT;
		//		else
		color = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);

		if ((p < 0) || (p > maxpattlen))
		{
			color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
			sprintf(textbuffer, "   ");
			printbyte(PATTERN_X, PATTERN_Y + 1 + d, color, 0x20);	//ff);
//			printbyte(PATTERN_X + (maxChan * 9) + 4, PATTERN_Y + 1 + d, color, 0xff);
		}
		else
		{
			printbyte(PATTERN_X, PATTERN_Y + 1 + d, color, 0x20);	//ff);
//			printbyte(PATTERN_X + (maxChan * 9) + 4, PATTERN_Y + 1 + d, color, 0xff);


			color &= 0xff00;
			if ((p% stepsize) == 0)
				color = colorWithReadableForeground(color, CPATTERN_INDEX_HIGHLIGHT);
			else
				color = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);

			if (!(patterndispmode & 1))
			{
				if (p < 100)
					sprintf(textbuffer, " %02d", p);
				else
					sprintf(textbuffer, "%03d", p);
			}
			else
				sprintf(textbuffer, " %02X", p);
		}

		printtext(PATTERN_X + 1, PATTERN_Y + 1 + d, color, textbuffer);


		if (d == 0)
		{
			color = CPATTERN_DIVIDER_LINE | (CUNUSED_MUTED_BACKGROUND << 8);
			//		printbyte(PATTERN_X, PATTERN_Y, color, 0xff);
			//		printbyte(PATTERN_X + (9 * (MAX_CHN)) + 4, PATTERN_Y, color, 0xff);	// header sepearator 
		}
	}

	for (int c = 0; c < maxChan; c++)
	{
		for (int d = 0; d < VISIBLEPATTROWS; d++)
		{
			int c2 = c < sidChan ? getVisualChannelActualChannel(c) : -1;	// 0-11
			int patternLen = c < sidChan ? getDisplayPatternLength(gt, c2) : (ptmodStats.frames ? (int)ptmodStats.frames - 1 : 0);
			int p = editorInfo.epview + d;

			int color = getColor(CPATTERN_FOREGROUND1, CPATTERN_BACKGROUND1);

			if ((p % (stepsize * 2)) < stepsize)
			{
				if ((p% stepsize) == 0)
					color = getColor(CPATTERN_FIRST_FOREGROUND2, CPATTERN_FIRST_BACKGROUND2);
				else
					color = getColor(CPATTERN_FOREGROUND2, CPATTERN_BACKGROUND2);
			}
			else if ((p% stepsize) == 0)
				color = getColor(CPATTERN_FIRST_FOREGROUND1, CPATTERN_FIRST_BACKGROUND1);

			if ((p < 0) || (p > patternLen) || (c < sidChan && c2 >= editorInfo.maxSIDChannels))
			{
				color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
			}

			int cl = color & 0xff00;
			cl |= CPATTERN_DIVIDER_LINE;

			printbyte(PATTERN_X + 4 + (c * chnWidth), PATTERN_Y + 1 + d, cl, 0x1ff);
			if (d == 0)
				printbyte(PATTERN_X + 4 + (c * chnWidth), PATTERN_Y, getColor(CPATTERN_DIVIDER_LINE, CUNUSED_MUTED_BACKGROUND), 0x1ff);
		}
	}

	for (int c = 0; c < maxChan; c++)
	{
		int c2 = c < sidChan ? getVisualChannelActualChannel(c) : -1;	// 0-11
		int ptmodChannel = c - sidChan;
		//int playingSong = getActualSongNumber(editorInfo.esnum, c2);	// JP added this. Only highlight playing row if showing the right song


		int patternNumber = c < sidChan ? getDisplayPatternNumber(gt, c2) : -1;
		int patternLen = c < sidChan ? getDisplayPatternLength(gt, c2) : (ptmodStats.frames ? (int)ptmodStats.frames - 1 : 0);
		int invalidColumn = c < sidChan ? (c2 >= editorInfo.maxSIDChannels || !isValidPatternNumber(patternNumber)) : (ptmodChannel >= ptmodStats.channels);

		if (c >= sidChan)
			sprintf(textbuffer, " M%01X %02X/%02X", ptmodChannel + 1, ptmodStats.orderIndex, ptmodStats.row);
		else if (invalidColumn)
			sprintf(textbuffer, " CH%01X   -- ", c2);
		else
			sprintf(textbuffer, " CH%01X   %02X ", c2, patternNumber);

		int headerColor = invalidColumn ? getColor(CUNUSED_MUTED_FOREGROUND, CTRANSPORT_FOREGROUND) : getColor(CTITLES_FOREGROUND, CTRANSPORT_FOREGROUND);

		printtext(PATTERN_X + 4 + c * chnWidth, PATTERN_Y, headerColor, textbuffer);

		if (c < sidChan && getFilterOnOff(gt, c2))
			headerColor = getColor(CCOLOR_RED, CGENERAL_BACKGROUND);
		if (c < sidChan)
			printbyte(PATTERN_X + 9 + c * chnWidth, PATTERN_Y, headerColor, 0x1f3);	// Filter on/off marker


		headerColor = getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND);
		if (c < sidChan && !(c % 3))
		{
			int t = UIUnderline;
			UIUnderline = 0;

			sprintf(textbuffer, "LO BN HI  CUT:%02X  RES:%01X", getFilterCutoff(gt, c2), getFilterResonance(gt, c2));
			printtext(PATTERN_X + 6 + c * chnWidth, PATTERN_Y - 1, headerColor, textbuffer);

			for (int i = 0;i < 2;i++)
			{
				printbyte(PATTERN_X + 6 + i + c * chnWidth, PATTERN_Y - 1, headerColor, 0x1e0 + i);
				printbyte(PATTERN_X + 9 + i + c * chnWidth, PATTERN_Y - 1, headerColor, 0x1e2 + i);
				printbyte(PATTERN_X + 12 + i + c * chnWidth, PATTERN_Y - 1, headerColor, 0x1e4 + i);
			}

			UIUnderline = t;

			int filterTypeOffColor = getColor(CUNUSED_MUTED_FOREGROUND, CTRANSPORT_FOREGROUND);
			int filterEnabledType = getFilterType(gt, c2);

			for (int i = 0;i < 2;i++)
			{
				for (int j = 0;j < 3;j++)
				{
					if (!(filterEnabledType&(1 << j)))
					{
						printbg(PATTERN_X + 6 + i + (j * 3) + c * chnWidth, PATTERN_Y - 1, filterTypeOffColor, 2);
					}
				}
			}

		}


		int colorNoChange = 0;


		for (int d = 0; d < VISIBLEPATTROWS; d++)
		{
			int notEmpty = 0;
			colorNoChange = 0;

			int p = editorInfo.epview + d;

			int color = getColor(CPATTERN_FOREGROUND1, CPATTERN_BACKGROUND1);


				if (c < sidChan && c2 == editorInfo.epmarkchn)
				{

				if (editorInfo.epmarkstart <= editorInfo.epmarkend)
				{
					if ((p >= editorInfo.epmarkstart) && (p <= editorInfo.epmarkend))
					{
						color = getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND);
						colorNoChange++;
					}
				}
				else if ((p <= editorInfo.epmarkstart) && (p >= editorInfo.epmarkend))
				{
					color = getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND);
					colorNoChange++;
				}
			}


			if (!colorNoChange)
			{
				if ((p % (stepsize * 2)) < stepsize)
				{
					if ((p% stepsize) == 0)
						color = getColor(CPATTERN_FIRST_FOREGROUND2, CPATTERN_FIRST_BACKGROUND2);
					else
						color = getColor(CPATTERN_FOREGROUND2, CPATTERN_BACKGROUND2);
				}
				else if ((p% stepsize) == 0)
					color = getColor(CPATTERN_FIRST_FOREGROUND1, CPATTERN_FIRST_BACKGROUND1);

			}

					if (c < sidChan && isValidPatternNumber(gt->chn[c2].lastpattnum) && patternNumber == gt->chn[c2].lastpattnum && isplaying(gt))
					{
					int chnrow = gt->chn[c2].lastpattptr / 4;

					if (chnrow > pattlen[gt->chn[c2].lastpattnum])
						chnrow = pattlen[gt->chn[c2].lastpattnum];

					if (chnrow == p - 0 && !invalidColumn)
					{
						color = getColor(CPATTERN_HIGHLIGHT_PLAYING_LINE_FOREGROUND, CPATTERN_HIGHLIGHT_PLAYING_LINE_BACKGROUND);
					}
			}

				if (c < sidChan && gt->chn[c2].mute)
				{
				color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
				colorNoChange = 1;
			}


				if ((p < 0) || (p > patternLen) || (c < sidChan && c2 >= editorInfo.maxSIDChannels) || invalidColumn)
				{
					color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
					colorNoChange = 1;
					sprintf(textbuffer, "        ");
				}
				else if (c >= sidChan)
				{
					PTMOD_PREVIEW_ROW row;

					if (ptmodplay_get_row(ptmodChannel, (size_t)p, &row))
					{
						if (row.sample)
							sprintf(textbuffer, "%02X %03X %1X%02X", row.sample & 0xff, row.period & 0xfff, row.effect & 0x0f, row.param & 0xff);
						else
							sprintf(textbuffer, ".. %03X %1X%02X", row.period & 0xfff, row.effect & 0x0f, row.param & 0xff);
						notEmpty = row.sample || row.period || row.effect || row.param;
						colorNoChange = 0;
					}
					else
					{
						sprintf(textbuffer, "        ");
						colorNoChange = 1;
					}
				}
				else
				{
				if (pattern[patternNumber][p * 4] == ENDPATT)
				{
					sprintf(textbuffer, "PATT.END");
					if (colorNoChange == 0)
					{
						color &= 0xff00;	// keep background (stripes)
						color = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);
						colorNoChange++;
						//				notEmpty++;
					}
				}
				else
				{
					sprintf(textbuffer, "%s%02X%01X%02X",
						notename[pattern[patternNumber][p * 4] - FIRSTNOTE],
						pattern[patternNumber][p * 4 + 1],
						pattern[patternNumber][p * 4 + 2],
						pattern[patternNumber][p * 4 + 3]);

					if (patterndispmode & 2)
					{
						if (!pattern[patternNumber][p * 4 + 1])
							memset(&textbuffer[3], '.', 2);
						if (!pattern[patternNumber][p * 4 + 2])
							memset(&textbuffer[5], '.', 3);
					}
				}
			}

			int displayCursor = 0;
				if (c < sidChan && p == editorInfo.eppos && !invalidColumn)
				{
				displayCursor++;
				color = colorWithReadableForeground(color, CPATTERN_HIGHLIGHT_FOREGROUND);
			}

			int color3 = color;
			int color2 = color;
			int color4 = color;
			int color5 = color;


				if (c < sidChan && colorNoChange == 0 && isValidPatternNumber(patternNumber))
				{
				int n = pattern[patternNumber][p * 4] - FIRSTNOTE;

				int noteColor = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);
				int commandColor = colorWithReadableForeground(color, CPATTERN_COMMAND_FOREGROUND);
				int instrumentColor = colorWithReadableForeground(color, CPATTERN_INSTRUMENT_FOREGROUND);

				color2 = noteColor;
				color3 = instrumentColor;
				color4 = commandColor;
				color5 = noteColor;

				if (n != 93)
				{
					notEmpty++;
				}

				if (pattern[patternNumber][p * 4 + 1] != 0)
				{
					notEmpty++;
				}
				if (pattern[patternNumber][p * 4 + 2] != 0)
				{
					notEmpty++;
				}
			}

			int dispCursorLine = 1;

			// Display highlight line where cursor is
				if (c >= sidChan && ptmodStats.active && (size_t)p == ptmodStats.frameIndex)
				{
					color2 = getColor(CPATTERN_HIGHLIGHT_PLAYING_LINE_FOREGROUND, CPATTERN_HIGHLIGHT_PLAYING_LINE_BACKGROUND);
					color3 = color2;
					color4 = color2;
					color5 = color2;
				}
				else if (c < sidChan && p == editorInfo.eppos && dispCursorLine && !invalidColumn && !followplay)
				{
				color2 = colorWithBackground(color2, CPATTERN_HIGHLIGHT_BACKGROUND);
				color3 = colorWithBackground(color3, CPATTERN_HIGHLIGHT_BACKGROUND);
				color4 = colorWithBackground(color4, CPATTERN_HIGHLIGHT_BACKGROUND);
				color5 = colorWithBackground(color5, CPATTERN_HIGHLIGHT_BACKGROUND);
			}
			else
			{
				if (!notEmpty)
				{
					if (isMatchingRGB(currentPalettePreset, color))
					{
						sprintf(textbuffer, "        ");
					}
				}
			}

				debugPrintPatternRow("multi", c, c2, p, patternNumber, c < sidChan ? gt->chn[c2].lastpattptr : 0, colorNoChange, color2, color3, color4, color5, textbuffer);
				printtext(PATTERN_X + 5 + c * chnWidth, PATTERN_Y + 1 + d, color2, textbuffer);
				if (c < sidChan)
				{
					printtext(PATTERN_X + 8 + c * chnWidth, PATTERN_Y + 1 + d, color3, &textbuffer[3]);
					printtext(PATTERN_X + 10 + c * chnWidth, PATTERN_Y + 1 + d, color4, &textbuffer[5]);
					printtext(PATTERN_X + 11 + c * chnWidth, PATTERN_Y + 1 + d, color5, &textbuffer[6]);
				}


			//			sprintf(textbuffer, "%d mark chan c2 %x c %x chn:%x", jdebug[14]++, c2, c, editorInfo.epmarkchn);
			//			printtext(70, 13, 0xe, textbuffer);

			/*
			if (c2 == editorInfo.epmarkchn)
			{
				if (editorInfo.epmarkstart <= editorInfo.epmarkend)
				{
					if ((p >= editorInfo.epmarkstart) && (p <= editorInfo.epmarkend))
					{
						printbg(PATTERN_X + 5 + c * 9, PATTERN_Y + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 8);

						if (transportLoopPattern && transportLoopPatternSelectArea)
						{
							printbg(PATTERN_X + 4 + c * 9, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
							printbg(PATTERN_X + 4 + 9 + c * 9, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
						}
					}

				}
				else
				{
					if ((p <= editorInfo.epmarkstart) && (p >= editorInfo.epmarkend))
					{
						printbg(PATTERN_X + 5 + c * 9, PATTERN_Y + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 8);

						if (transportLoopPattern && transportLoopPatternSelectArea)
						{
							printbg(PATTERN_X + 4 + c * 9, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
							printbg(PATTERN_X + 4 + 9 + c * 9, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
						}
					}

				}
			}
			*/

				if (c < sidChan && (displayCursor) && (editorInfo.editmode == EDIT_PATTERN) && (cursorVisualChannel == c))
			{
				switch (editorInfo.epcolumn)
				{
				case 0:
					if (!eamode) printbg(PATTERN_X + 5 + c * chnWidth, PATTERN_Y + 1 + d, cc << 8, 3);
					break;

				default:
					if (!eamode) printbg(PATTERN_X + 5 + c * chnWidth + 2 + editorInfo.epcolumn, PATTERN_Y + 1 + d, cc << 8, 1);
					break;
				}
			}
		}
	}

	UIUnderline = 0;

}

void displayPattern3Chn(GTOBJECT *gt)
{
	printbytecol(PATTERN_X + 59, PATTERN_Y - 1, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f5, VISIBLEPATTROWS + 2);

	printbyterow(PATTERN_X, PATTERN_Y - 1, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_FOREGROUND), 0x20, 59);

	int cc = cursorcolortable[cursorflash];
	int maxpattlen = 0;
	UIUnderline = UNDERLINE_MASK;

	//int highlightPatternLoop = 0;

	int maxChan = MAX_CHN;
	if ((editorInfo.esnum & 1 && editorInfo.maxSIDChannels == 9) || editorInfo.maxSIDChannels == 3)
		maxChan = 3;

	int chnWidth = 14;

	for (int c = 0; c < maxChan; c++)
	{
		int c2 = getActualChannel(editorInfo.esnum, c);	// 0-12
		int patternLen = getDisplayPatternLength(gt, c2);

		if (patternLen > maxpattlen)
			maxpattlen = patternLen;
	}

//	if (gt->editorUndoInfo.editorInfo[editorInfo.highlightLoopChannel].epnum == editorInfo.highlightLoopPatternNumber)
//		highlightPatternLoop = 1;


	printtext(PATTERN_X + 0, PATTERN_Y, getColor(CTITLES_FOREGROUND, CTRANSPORT_FOREGROUND), " POS");
	for (int d = 0; d < VISIBLEPATTROWS; d++)
	{
		int p = editorInfo.epview + d;
		int color = getColor(CPATTERN_FOREGROUND1, CPATTERN_BACKGROUND1);
		if ((p % (stepsize * 2)) < stepsize)
		{
			if ((p% stepsize) == 0)
				color = getColor(CPATTERN_FIRST_FOREGROUND2, CPATTERN_FIRST_BACKGROUND2);
			else
				color = getColor(CPATTERN_FOREGROUND2, CPATTERN_BACKGROUND2);
		}
		else if ((p% stepsize) == 0)
			color = getColor(CPATTERN_FIRST_FOREGROUND1, CPATTERN_FIRST_BACKGROUND1);

		color &= 0xff00;
		color |= CPATTERN_DIVIDER_LINE;

		//	int colort = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
			//		printbyte(PATTERN_X + (MAX_CHN * chnWidth) + 4, PATTERN_Y + 1 + d, colort, 0x20);	//0xff);

		color &= 0xff00;
		//		if ((p% stepsize) == 0)
		//			color |= CPATTERN_INDEX_HIGHLIGHT;
		//		else
		color = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);

		if ((p < 0) || (p > maxpattlen))
		{
			color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
			sprintf(textbuffer, "   ");
			printbyte(PATTERN_X, PATTERN_Y + 1 + d, color, 0x20);	//ff);
//			printbyte(PATTERN_X + (maxChan * 9) + 4, PATTERN_Y + 1 + d, color, 0xff);
		}
		else
		{
			printbyte(PATTERN_X, PATTERN_Y + 1 + d, color, 0x20);	//ff);
//			printbyte(PATTERN_X + (maxChan * 9) + 4, PATTERN_Y + 1 + d, color, 0xff);


			color &= 0xff00;
			if ((p% stepsize) == 0)
				color = colorWithReadableForeground(color, CPATTERN_INDEX_HIGHLIGHT);
			else
				color = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);

			if (!(patterndispmode & 1))
			{
				if (p < 100)
					sprintf(textbuffer, " %02d", p);
				else
					sprintf(textbuffer, "%03d", p);
			}
			else
				sprintf(textbuffer, " %02X", p);
		}

		printtext(PATTERN_X + 1, PATTERN_Y + 1 + d, color, textbuffer);


		if (d == 0)
		{
			color = CPATTERN_DIVIDER_LINE | (CUNUSED_MUTED_BACKGROUND << 8);
			//		printbyte(PATTERN_X, PATTERN_Y, color, 0xff);
			//		printbyte(PATTERN_X + (9 * (MAX_CHN)) + 4, PATTERN_Y, color, 0xff);	// header sepearator 
		}
	}

	//return;

	for (int c = 0; c < 3; c++)
	{
		for (int d = 0; d < VISIBLEPATTROWS; d++)
			{
				int c2 = getActualChannel(editorInfo.esnum, c);	// 0-12
				int patternLen = getDisplayPatternLength(gt, c2);
				int p = editorInfo.epview + d;

				int color = getColor(CPATTERN_FOREGROUND1, CPATTERN_BACKGROUND1);
				if ((p % (stepsize * 2)) < stepsize)
				{
					if ((p% stepsize) == 0)
						color = getColor(CPATTERN_FIRST_FOREGROUND2, CPATTERN_FIRST_BACKGROUND2);
					else
						color = getColor(CPATTERN_FOREGROUND2, CPATTERN_BACKGROUND2);
				}
				else if ((p% stepsize) == 0)
					color = getColor(CPATTERN_FIRST_FOREGROUND1, CPATTERN_FIRST_BACKGROUND1);

				if ((p < 0) || (p > patternLen) || c2 >= editorInfo.maxSIDChannels || c >= maxChan)
				{
					color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
				}

			int cl = color & 0xff00;
			cl |= CPATTERN_DIVIDER_LINE;

			printbyte(PATTERN_X + 4 + (c * chnWidth), PATTERN_Y + 1 + d, cl, 0x1ff);
			if (d == 0)
				printbyte(PATTERN_X + 4 + (c * chnWidth), PATTERN_Y, getColor(CPATTERN_DIVIDER_LINE, CUNUSED_MUTED_BACKGROUND), 0x1ff);
		}
	}

	for (int c = 0; c < 3;c++)	//MAX_CHN; c++)
	{
		//9
		int xpos = PATTERN_X + 4 + c * chnWidth;

			int c2 = getActualChannel(editorInfo.esnum, c);	// 0-12
			//int playingSong = getActualSongNumber(editorInfo.esnum, c2);	// JP added this. Only highlight playing row if showing the right song


			int patternNumber = getDisplayPatternNumber(gt, c2);
			int patternLen = getDisplayPatternLength(gt, c2);
			int invalidColumn = c >= maxChan || c2 >= editorInfo.maxSIDChannels || !isValidPatternNumber(patternNumber);

			if (invalidColumn)
				sprintf(textbuffer, " CHN%01X   PAT --", c2 + 1);
			else
				sprintf(textbuffer, " CHN%01X   PAT %02X", c2 + 1, patternNumber);

		int filterInfoXOffset = 6;

		int headerColor = getColor(CTITLES_FOREGROUND, CTRANSPORT_FOREGROUND);
		if (c >= maxChan)
			headerColor = getColor(CUNUSED_MUTED_FOREGROUND, CTRANSPORT_FOREGROUND);

		printtext(xpos, PATTERN_Y, headerColor, textbuffer);
		if (c == 2)
			printbyterow(xpos + chnWidth, PATTERN_Y, headerColor, ' ', 59 - (xpos + chnWidth));


		if (getFilterOnOff(gt, c2))
			headerColor = getColor(CCOLOR_RED, CGENERAL_BACKGROUND);
		printbyte(xpos + filterInfoXOffset, PATTERN_Y, headerColor, 0x1f3);	// Filter on/off marker


		headerColor = getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND);
		if (!(c % 3))
		{
			int t = UIUnderline;
			UIUnderline = 0;

			sprintf(textbuffer, "LO BN HI  CUT:%02X  RES:%01X", getFilterCutoff(gt, c2), getFilterResonance(gt, c2));
			printtext(PATTERN_X + filterInfoXOffset + 6 + c * chnWidth, PATTERN_Y - 1, headerColor, textbuffer);

			for (int i = 0;i < 2;i++)
			{
				printbyte(PATTERN_X + filterInfoXOffset + 6 + i + c * chnWidth, PATTERN_Y - 1, headerColor, 0x1e0 + i);
				printbyte(PATTERN_X + filterInfoXOffset + 9 + i + c * chnWidth, PATTERN_Y - 1, headerColor, 0x1e2 + i);
				printbyte(PATTERN_X + filterInfoXOffset + 12 + i + c * chnWidth, PATTERN_Y - 1, headerColor, 0x1e4 + i);
			}

			UIUnderline = t;

			int filterTypeOffColor = getColor(CUNUSED_MUTED_FOREGROUND, CTRANSPORT_FOREGROUND);
			int filterEnabledType = getFilterType(gt, c2);

			for (int i = 0;i < 2;i++)
			{
				for (int j = 0;j < 3;j++)
				{
					if (!(filterEnabledType&(1 << j)))
					{
						printbg(PATTERN_X + filterInfoXOffset + 6 + i + (j * 3) + c * chnWidth, PATTERN_Y - 1, filterTypeOffColor, 2);
					}
				}
			}
		}


		int colorNoChange = 0;



		for (int d = 0; d < VISIBLEPATTROWS; d++)
		{
			int notEmpty = 0;
			colorNoChange = 0;

			int p = editorInfo.epview + d;

			int color = getColor(CPATTERN_FOREGROUND1, CPATTERN_BACKGROUND1);

			if (c2 == editorInfo.epmarkchn)
			{

				if (editorInfo.epmarkstart <= editorInfo.epmarkend)
				{
					if ((p >= editorInfo.epmarkstart) && (p <= editorInfo.epmarkend))
					{
						color = getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND);
						colorNoChange++;
					}
				}
				else if ((p <= editorInfo.epmarkstart) && (p >= editorInfo.epmarkend))
				{
					color = getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND);
					colorNoChange++;
				}
			}


			if (!colorNoChange)
			{

				if ((p % (stepsize * 2)) < stepsize)
				{
					if ((p% stepsize) == 0)
						color = getColor(CPATTERN_FIRST_FOREGROUND2, CPATTERN_FIRST_BACKGROUND2);
					else
						color = getColor(CPATTERN_FOREGROUND2, CPATTERN_BACKGROUND2);
				}
				else if ((p% stepsize) == 0)
					color = getColor(CPATTERN_FIRST_FOREGROUND1, CPATTERN_FIRST_BACKGROUND1);

			}

			if (isValidPatternNumber(gt->chn[c2].lastpattnum) && patternNumber == gt->chn[c2].lastpattnum && isplaying(gt))
			{
				int chnrow = gt->chn[c2].lastpattptr / 4;
				if (chnrow == 0x7ffffff)
					chnrow = 0;
				if (chnrow > pattlen[gt->chn[c2].lastpattnum])
					chnrow = pattlen[gt->chn[c2].lastpattnum];

				if (chnrow == p - 0 && !invalidColumn)
				{
					color = getColor(CPATTERN_HIGHLIGHT_PLAYING_LINE_FOREGROUND, CPATTERN_HIGHLIGHT_PLAYING_LINE_BACKGROUND);
				}
			}

			if (gt->chn[c2].mute)
			{
				color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
				colorNoChange = 1;
			}


			if ((p < 0) || (p > patternLen) || c2 >= editorInfo.maxSIDChannels || invalidColumn)
			{
				color = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
				colorNoChange = 1;
				sprintf(textbuffer, "             ");
			}
			else
			{
				if (pattern[patternNumber][p * 4] == ENDPATT)
				{
					sprintf(textbuffer, "PATTERN.END  ");
					if (colorNoChange == 0)
					{
						color &= 0xff00;	// keep background (stripes)
						color = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);
						colorNoChange++;
						//				notEmpty++;
					}
				}
				else
				{
					sprintf(textbuffer, "%s %02X%01X%02X    ",
						notename[pattern[patternNumber][p * 4] - FIRSTNOTE],
						pattern[patternNumber][p * 4 + 1],
						pattern[patternNumber][p * 4 + 2],
						pattern[patternNumber][p * 4 + 3]);

					if (patterndispmode & 2)
					{
						if (!pattern[patternNumber][p * 4 + 1])		// No instrument
							memset(&textbuffer[4], '.', 2);
						if (!pattern[patternNumber][p * 4 + 2])		// No command
						{
							memset(&textbuffer[6], '.', 1);	// clear command
							memset(&textbuffer[7], '.', 2);	// clear data
						}
					}

				}
			}

			int displayCursor = 0;
			if (p == editorInfo.eppos && !invalidColumn)
			{
				displayCursor++;
				color = colorWithReadableForeground(color, CPATTERN_HIGHLIGHT_FOREGROUND);
			}


			int color3 = color;
			int color2 = color;
			int color4 = color;
			int color5 = color;


			if (colorNoChange == 0 && isValidPatternNumber(patternNumber))
			{
				int n = pattern[patternNumber][p * 4] - FIRSTNOTE;

				int noteColor = colorWithReadableForeground(color, CPATTERN_NOTE_FOREGROUND);
				int commandColor = colorWithReadableForeground(color, CPATTERN_COMMAND_FOREGROUND);
				int instrumentColor = colorWithReadableForeground(color, CPATTERN_INSTRUMENT_FOREGROUND);

				color2 = noteColor;
				color3 = instrumentColor;
				color4 = commandColor;
				color5 = noteColor;

				if (n != 93)
				{
					notEmpty++;
				}

				if (pattern[patternNumber][p * 4 + 1] != 0)
				{
					notEmpty++;
				}
				if (pattern[patternNumber][p * 4 + 2] != 0)
				{
					notEmpty++;
				}
			}

			int dispCursorLine = 1;

			// Display highlight line where cursor is
			if (p == editorInfo.eppos && dispCursorLine && !invalidColumn && !followplay)
			{
				color2 = colorWithBackground(color2, CPATTERN_HIGHLIGHT_BACKGROUND);
				color3 = colorWithBackground(color3, CPATTERN_HIGHLIGHT_BACKGROUND);
				color4 = colorWithBackground(color4, CPATTERN_HIGHLIGHT_BACKGROUND);
				color5 = colorWithBackground(color5, CPATTERN_HIGHLIGHT_BACKGROUND);
			}
			else
			{
				if (!notEmpty)
				{
					if (isMatchingRGB(currentPalettePreset, color))
					{
						sprintf(textbuffer, "             ");
					}
				}
			}


			xpos = PATTERN_X + 5;
			debugPrintPatternRow("3ch", c, c2, p, patternNumber, gt->chn[c2].lastpattptr, colorNoChange, color2, color3, color4, color5, textbuffer);
			printtext(xpos + (c * chnWidth), PATTERN_Y + 1 + d, color2, textbuffer);		// 4 chars: C-3<space>
			printtext(xpos + 4 + (c* chnWidth), PATTERN_Y + 1 + d, color3, &textbuffer[4]);	// 2 chars: 00
			printtext(xpos + 4 + 2 + (c * chnWidth), PATTERN_Y + 1 + d, color4, &textbuffer[4 + 2]);	// 1 char: 1
			printtext(xpos + 4 + 2 + 1 + (c * chnWidth), PATTERN_Y + 1 + d, color5, &textbuffer[4 + 2 + 1]);	// 2 chars: 00

			if (c2 == editorInfo.epmarkchn)
			{
				if (editorInfo.epmarkstart <= editorInfo.epmarkend)
				{
					if ((p >= editorInfo.epmarkstart) && (p <= editorInfo.epmarkend))
					{
						//		printbg(PATTERN_X + 5 + c * chnWidth, PATTERN_Y + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), chnWidth - 1);

						if (transportLoopPattern && transportLoopPatternSelectArea)
						{
							printbg(PATTERN_X + 4 + c * chnWidth, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
							printbg(PATTERN_X + 4 + chnWidth + c * chnWidth, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
						}
					}

				}
				else
				{
					if ((p <= editorInfo.epmarkstart) && (p >= editorInfo.epmarkend))
					{
						//		printbg(PATTERN_X + 5 + c * chnWidth, PATTERN_Y + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), chnWidth - 1);

						if (transportLoopPattern && transportLoopPatternSelectArea)
						{
							printbg(PATTERN_X + 4 + c * chnWidth, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
							printbg(PATTERN_X + 4 + chnWidth + c * chnWidth, PATTERN_Y + 1 + d, getColor(CPATTERN_LOOP_MARKER_FOREGROUND, CPATTERN_LOOP_MARKER_BACKGROUND), 1);
						}
					}

				}
			}

			// Display cursor
			if ((displayCursor) && (editorInfo.editmode == EDIT_PATTERN) && (editorInfo.epchn == c))
			{
				int displayColumn = 0;
				int displayCursorOffset = 0;
				switch (editorInfo.epcolumn)
				{
				case 0:
					if (!eamode) printbg(PATTERN_X + 5 + c * chnWidth, PATTERN_Y + 1 + d, cc << 8, 3);
					break;

				default:
					if (editorInfo.epcolumn == 1 || editorInfo.epcolumn == 2)	// instrument 00
					{
						displayColumn = 4;
						displayCursorOffset = editorInfo.epcolumn - 1;
					}
					else if (editorInfo.epcolumn == 3)	// command 0
					{
						displayColumn = 7;
						displayCursorOffset = editorInfo.epcolumn - 3;
					}
					else
					{
						displayColumn = 8;	// data 00
						displayCursorOffset = editorInfo.epcolumn - 4;
					}

					if (!eamode) printbg(PATTERN_X + 5 + c * chnWidth + displayColumn + displayCursorOffset, PATTERN_Y + 1 + d, cc << 8, 1);
					break;
				}
			}
		}
	}

	UIUnderline = 0;

}


void displayTransportBar(GTOBJECT *gt)
{
	for (int i = 0;i < 59;i++)
	{
		printbyte(i, TRANSPORT_BAR_Y - 1, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_FOREGROUND), 0x20);
		printbyte(i, TRANSPORT_BAR_Y, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_FOREGROUND), 0x20);
		printbyte(i, TRANSPORT_BAR_Y + 1, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_FOREGROUND), 0x20);
		printbyte(i, TRANSPORT_BAR_Y + 2, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_FOREGROUND), 0x20);
	}

	for (int i = 0;i < 59;i++)
	{
		printbyte(i, TRANSPORT_BAR_Y, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_BETWEEN_BUTTONS), 0x1bd);
		printbyte(i, TRANSPORT_BAR_Y + 1, getColor(CTRANSPORT_FOREGROUND, CTRANSPORT_BETWEEN_BUTTONS), 0x1dd);

	}

	printbyterow(0, TRANSPORT_BAR_Y + 3, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f6, getPatternAreaWidth() - 1);

	displayTransportBarSkinning(0, TRANSPORT_BAR_Y);
	displayTransportBarSIDCount(4, TRANSPORT_BAR_Y);
	displayTransportBarMasterVolume(8, TRANSPORT_BAR_Y);

	int x = TRANSPORT_BAR_X - 2;	// +1;
	displayTransportBarOctave(x, TRANSPORT_BAR_Y);
	x += 5;
	displayTransportBarFollow(x, TRANSPORT_BAR_Y);
	x += 4;
	displayTransportBarLoopPattern(x, TRANSPORT_BAR_Y);
	x += 4;
	displayTransportBarRewind(x, TRANSPORT_BAR_Y);
	x += 4;
	displayTransportBarRecord(x, TRANSPORT_BAR_Y);
	x += 4;
	displayTransportBarPlaying(gt, x, TRANSPORT_BAR_Y);
	x += 4;
	displayTransportBarFastForward(x, TRANSPORT_BAR_Y);
	x += 5;
	displayTransportBarPolyChannels(x, TRANSPORT_BAR_Y);
	x += 5;
	displayTransportBarKeyboard(x, TRANSPORT_BAR_Y);
	x += 4;
	displayTransportBarDetune(x, TRANSPORT_BAR_Y);
	x += 4;
	displayTransportBarMonoStereo(x, TRANSPORT_BAR_Y);



	int tdiv = 25 * editorInfo.multiplier;

	if (editorInfo.multiplier && editorInfo.ntsc)
		tdiv = 30 * editorInfo.multiplier;
	else if (!editorInfo.multiplier && !editorInfo.ntsc)
		tdiv = gt->timeframe / 13;
	else if (!editorInfo.multiplier && editorInfo.ntsc)
		tdiv = gt->timeframe / 15;

	int v = 0;
	if (tdiv)
		v = gt->timeframe / tdiv & 1;

	sprintf(textbuffer, " %02d%c%02d:%02d ", gt->timemin, timechar[v], gt->timesec, gt->timeframe);
	printtext(TRANSPORT_BAR_X + 5, TRANSPORT_BAR_Y - 1, getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND), textbuffer);
	sprintf(textbuffer, "/ %02d:%02d:%02d ", gtEditorObject.totalMin, gtEditorObject.totalSec, gtEditorObject.totalFrame);
	printtext(TRANSPORT_BAR_X + 5 + 10, TRANSPORT_BAR_Y - 1, getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND), textbuffer);

	displayWaveformInfo(TRANSPORT_BAR_X - 5, TRANSPORT_BAR_Y - 1);


	{
		int infoColor = getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND);
		int infoWidth = getactivescreencolumns() - 6;

		printtext(0, TRANSPORT_BAR_Y + 2, infoColor, "INFO:");
		if (infoWidth > 0)
			printblankc(6, TRANSPORT_BAR_Y + 2, infoColor, infoWidth);
	}
	if (editPaletteMode)
	{
		int maxPaletteText = getPaletteTextArraySize();
		int index = editorInfo.eseditpos * 2 + editorInfo.escolumn;
		jcounter = maxPaletteText;

		if (index < maxPaletteText)
		{
			sprintf(textbuffer, "%d %s", FIRST_UI_COLOR + index, paletteText[index]);
			printtext(6, TRANSPORT_BAR_Y + 2, getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND), textbuffer);
		}
	}
	else
	{
		PTMOD_PREVIEW_STATS ptmodStats;
		ptmodplay_get_stats(&ptmodStats);
		if (ptmodStats.loaded && isplaying(gt))
		{
			snprintf(textbuffer, sizeof textbuffer, "MOD mix %llu pos %d row %d dly %d peak %d",
				ptmodStats.mixedSamples,
				ptmodStats.orderIndex,
				ptmodStats.row,
				ptmodStats.delayFramesRemaining,
				ptmodStats.peakSinceReset);
			printtext(6, TRANSPORT_BAR_Y + 2, getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND), textbuffer);
		}
		else
		{
			printtext(6, TRANSPORT_BAR_Y + 2, getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND), infoTextBuffer);
		}
	}

}

void displayTransportBarFastForward(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	if (editorInfo.editmode == EDIT_MOD && editorInfo.ptmodStreamFollow)
		color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_ENABLED);

	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 0x1af + i);
		printbyte(x + i, y + 1, color, 0x1cf + i);
	}
}

void displayTransportBarRewind(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);

	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 0x1a6 + i);
		printbyte(x + i, y + 1, color, 0x1c6 + i);
	}
}

void displayTransportBarPolyChannels(int x, int y)
{
	for (int p = 0;p < 4;p++)
	{
		int imageOffset = 0x12;
		int yoffset = p / 2;
		int xoffset = (p & 1) * 2;

		int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
		if (transportPolySIDEnabled[p])
		{
			imageOffset = 0x19;
			color = getColor(CTRANSPORT_ENABLED, CTRANSPORT_FOREGROUND);
		}

		for (int i = 0;i < 02;i++)
		{
			printbyte(x + i + xoffset, y + yoffset, color, 0x1a0 + (32 * yoffset) + i + imageOffset + xoffset);
		}
	}
}

void displayTransportBarKeyboard(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_BACKGROUND, CTRANSPORT_BUTTON_FOREGROUND);
	if (transportShowKeyboard)
		color = getColor(CTRANSPORT_ENABLED, CTRANSPORT_FOREGROUND);

	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, i + 2);
		printbyte(x + i, y + 1, color, 1);
	}
}



void displayTransportBarPlaying(GTOBJECT *gt, int x, int y)
{
	int imageOffset = 12;
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	if (gt->songinit == PLAY_PLAYING)
		imageOffset = 22;

	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 0x1a0 + i + imageOffset);
		printbyte(x + i, y + 1, color, 0x1c0 + i + imageOffset);
	}
}


void displayTransportBarRecord(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	if (recordmode)
		color = getColor(CCOLOR_RED, CTRANSPORT_BUTTON_BACKGROUND);

	int firstChar = 0x1a9;
	if (useOriginalGTFunctionKeys)
		firstChar = 0x101;

	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, firstChar + i);
		printbyte(x + i, y + 1, color, firstChar + 0x20 + i);
	}
}


void displayTransportBarMasterVolume(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 21 + i);
		printbyte(x + i, y + 1, color, 0x1e9 + i);
	}

	sprintf(textbuffer, "%.1f", masterVolume);
	printtext(x, y + 1, color, textbuffer);

}

void displayTransportBarOctave(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 0x1e6 + i);
		printbyte(x + i, y + 1, color, 0x1e9 + i);
	}
	sprintf(textbuffer, "%1d", editorInfo.epoctave);
	printtext(x + 1, y + 1, color, textbuffer);

}

void displayTransportBarDetune(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 0x19d + i);
		printbyte(x + i, y + 1, color, 0x1e9 + i);
	}
	sprintf(textbuffer, "%.1f", detuneCent + 1);
	printtext(x + 0, y + 1, color, textbuffer);

}

void displayTransportBarMonoStereo(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);

	int b = 0x107;	//195;
	if (monomode || (editorInfo.maxSIDChannels == 3 && stereoMode == 1))	// either in mono mode, or 3 channels and forced stereo
	{
		printbyte(x, y + 1, color, 0x20);
		printbyte(x + 1, y + 1, color, 0x19c);
		printbyte(x + 2, y + 1, color, 0x20);

	}
	else if (stereoMode == 1)	// SID Stereo (0+2 = left, 1+3 = right)
	{
		b = 0x10a;	//198;
		printbyte(x, y + 1, color, 0x19b);
		printbyte(x + 1, y + 1, color, 0x20);
		printbyte(x + 2, y + 1, color, 0x19c);
	}
	else
	{
		b = 0x10d;	//19b;	// Stereo Panning mode
		printbyte(x, y + 1, color, 0x19b);
		printbyte(x + 1, y + 1, color, 0x20);
		printbyte(x + 2, y + 1, color, 0x19c);
	}


	// Display MON, STE or PAN text
	for (int i = 0;i < 3;i++)
	{
		printbyte(x + i, y, color, b);
		b++;
	}

	//	sprintf(textbuffer, "%.1f", detuneCent + 1);
	//	printtext(x + 0, y + 1, color, textbuffer);

}

void displayTransportBarSIDCount(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	printbyte(x, y, color, 0x1be);
	printbyte(x + 1, y, color, 0x1bf);
	printbyte(x + 2, y, color, 0x1de);
	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y + 1, color, 0x1e9 + i);
	}
	sprintf(textbuffer, "%1X", editorInfo.maxSIDChannels / 3);
	printtext(x + 1, y + 1, color, textbuffer);

}

void displayTransportBarSkinning(int x, int y)
{

	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	if (editPaletteMode)
		color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_ENABLED);
	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 0x1ed + i);
		printbyte(x + i, y + 1, color, 0x1f0 + i);
	}

	sprintf(textbuffer, "%1X", currentPalettePreset);
	printtext(x + 2, y + 1, color, textbuffer);

}

void displayTransportBarFollow(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	if (followplay)
		color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_ENABLED);

	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, 0x1a0 + i);
		printbyte(x + i, y + 1, color, 0x1c0 + i);
	}
}

void displayTransportBarLoopPattern(int x, int y)
{
	int color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_BUTTON_BACKGROUND);
	if (transportLoopPattern)
		color = getColor(CTRANSPORT_BUTTON_FOREGROUND, CTRANSPORT_ENABLED);

	int imageOffset = 0x1a3;
	if (transportLoopPatternSelectArea)
		imageOffset = 0x104;

	for (int i = 0;i < 0x3;i++)
	{
		printbyte(x + i, y, color, imageOffset + i);
		printbyte(x + i, y + 1, color, imageOffset + 0x20 + i);
	}
}


struct NOTEDISPLAYPLAYING
{
	int note;
	int charIndex;
	int bitmask;

};


// Will do this for each octave,
// note = note number 0-11 (one octave)
// charIndex = charIndex array offset
// bitmask is offset to "or" in char indexArray offset
// We then look up noteChars[noteNumber][indexArray] to get the right image


struct NOTEDISPLAYPLAYING displayKeyboardTopRowOctaveInfo[] = {
 {.note = 0,.charIndex = 0,.bitmask = 1},		// c
 {.note = 1,.charIndex = 0,.bitmask = 2},		// c#
{.note = 1,.charIndex = 1,.bitmask = 1},		// c#
{.note = 2,.charIndex = 1,.bitmask = 2},		// d
{.note = 3,.charIndex = 1,.bitmask = 4},		// d#
{.note = 3,.charIndex = 2,.bitmask = 1},		// d#
{.note = 4,.charIndex = 2,.bitmask = 2},		// e
{.note = 5,.charIndex = 3,.bitmask = 1},		// f
{.note = 6,.charIndex = 3,.bitmask = 2},		// f#
{.note = 6,.charIndex = 4,.bitmask = 1},		// f#
{.note = 7,.charIndex = 4,.bitmask = 2},		// g
{.note = 8,.charIndex = 4,.bitmask = 4},		// g#
{.note = 8,.charIndex = 5,.bitmask = 1},		// g#
{.note = 9,.charIndex = 5,.bitmask = 2},		// a
{.note = 10,.charIndex = 5,.bitmask = 4},		// a#
{.note = 10,.charIndex = 6,.bitmask = 1},		// a#
{.note = 11,.charIndex = 6,.bitmask = 2},		// b
{.note = -1,.charIndex = -1,.bitmask = 0}		// END
};

// Top Row char numbers, based on bitpattern for keyon in charIndexArrayTopRow
char keyboardTopRow_Char0[] = { 2,6,7,8 };	// c + c#
char keyboardTopRow_Char1[] = { 3,9,10,11,12,13,14,15 };		// c#,d,d#
char keyboardTopRow_Char2[] = { 4,16,17,18 };	// d#,e
char keyboardTopRow_Char3[] = { 2,6,7,8 };	// f + f#
char keyboardTopRow_Char4[] = { 3,9,10,11,12,13,14,15 };		// f#,g,g#
char keyboardTopRow_Char5[] = { 3,9,10,11,12,13,14,15 };		// g#,a,a#
char keyboardTopRow_Char6[] = { 4,16,17,18 };	// a#,b

char *keyboardTopRowPtrs[] = {
	(char*)keyboardTopRow_Char0,
	(char*)keyboardTopRow_Char1,
	(char*)keyboardTopRow_Char2,
	(char*)keyboardTopRow_Char3,
	(char*)keyboardTopRow_Char4,
	(char*)keyboardTopRow_Char5,
	(char*)keyboardTopRow_Char6 };

char keyboardBottomRow_Char0[] = { 1,5 };

int keyboardDisplayNoteIndexArray[92];

int charIndexArrayTopRow[59];	// 8 octave. 7 different char arrays (see .charIndex above)
int charOnOffBottomRow[59];	// 8 octave. 7 keys in major scale

char majorKeyOn[12] = { 0,-1,1,-1,2,3,-1,4,-1,5,-1,6 };

// Call this once per frame. Prior to doing the setNote
void resetKeyboardDisplay()
{
	for (int i = 0;i < 59;i++)
	{
		charIndexArrayTopRow[i] = 0;
		charOnOffBottomRow[i] = 0;
	}
}


void displayNotes(GTOBJECT *gt)
{
	for (int i = 0;i < MAX_PLAY_CH;i++)
	{
		if (!gt->chn[i].mute)
		{
			if (gt->chn[i].instr)	// not 0.. So playing
			{
				if (gt->chn[i].releaseTime)
				{
					int closestNote = getClosestNote(gt->chn[i].freq);
					//					closestNote = gt->chn[i].lastnote;
					setNote(closestNote);
				}
			}
		}
	}
}

void setNote(int noteNumber)
{
	int octave = noteNumber / 12;
	int note = noteNumber % 12;

	int index = 0;
	while (displayKeyboardTopRowOctaveInfo[index].note != -1)
	{
		if (displayKeyboardTopRowOctaveInfo[index].note == note)
		{
			int noteCharIndex = displayKeyboardTopRowOctaveInfo[index].charIndex;	// 0-6
			noteCharIndex += octave * 7;
			charIndexArrayTopRow[noteCharIndex] |= displayKeyboardTopRowOctaveInfo[index].bitmask;
		}
		index++;
	};
	if (majorKeyOn[note] >= 0)
	{
		int majorNote = majorKeyOn[note] + (octave * 7);
		charOnOffBottomRow[majorNote] = 1;
	}
}



void displayKeyboard()
{
	int x = 0;
	int y = 3;
	for (int i = 0;i < 59;i++)
	{
		char* cp = keyboardTopRowPtrs[i % 7];
		char charToDisplay = cp[charIndexArrayTopRow[i]];
		printbyte(x + i, y, getColor(15, 0), charToDisplay);
		printbyte(x + i, y + 1, getColor(15, 0), charToDisplay);

		charToDisplay = keyboardBottomRow_Char0[charOnOffBottomRow[i]];
		printbyte(x + i, y + 2, getColor(15, 0), charToDisplay);
	}
}


int getTableTitleColour(int c)
{
	if (editorInfo.editTableMode == EDIT_TABLE_NONE)
		return getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);

	if (editorInfo.editTableMode == c)
		return getColor(CCOLOR_EXCLAMATION, CGENERAL_BACKGROUND);

	sprintf(textbuffer, "        ");
	return getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);
}


void displaySongInfo(int cc, int OX, int OY)
{

	printtext(OX, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), "NAME   ");

	//	sprintf(textbuffer, "%d", strlen(backupSngFilename));
	sprintf(textbuffer, "%-31s", songname);
	printtext(OX + 7, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);

	printtext(OX, OY + 1, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), "AUTHOR ");
	//	sprintf(textbuffer, "%-31s", destBackupFolderName);
	sprintf(textbuffer, "%-32s", authorname);
	printtext(OX + 7, OY + 1, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);

	printtext(OX, OY + 2, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), "COPYR. ");
	sprintf(textbuffer, "%-32s", copyrightname);
	printtext(OX + 7, OY + 2, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);

	if ((editorInfo.editmode == EDIT_NAMES) && (!eamode))
	{
		switch (editorInfo.nameIndex)
		{
		case 0:
			printbg(OX + 7 + strlen(songname), OY, cc << 8, 1);
			break;
		case 1:
			printbg(OX + 7 + strlen(authorname), OY + 1, cc << 8, 1);
			break;
		case 2:
			printbg(OX + 7 + strlen(copyrightname), OY + 2, cc << 8, 1);
			break;
		}
	}
}

static const char *displayBasename(const char *path)
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

static void printPtmodSettingLine(int OX, int y, int width, int row, int color, const char *text)
{
	char line[MAX_COLUMNS + 1];
	int lineColor = color;

	if (row >= 0 && editorInfo.ptmodEditPage == 1 && row == editorInfo.ptmodEditRow)
		lineColor = getColor(CORDER_INST_TABLE_EDITING, CORDER_INST_BACKGROUND);

	if (width < 0)
		width = 0;
	if (width > MAX_COLUMNS)
		width = MAX_COLUMNS;
	snprintf(line, sizeof line, "%-*.*s", width, width, text ? text : "");
	printtext(OX, y, lineColor, line);
}

#define PTMOD_SIDE_ROW_TITLE 0
#define PTMOD_SIDE_ROW_LENGTH 1
#define PTMOD_SIDE_ROW_RESTART 2
#define PTMOD_SIDE_ROW_FOLLOW 3
#define PTMOD_SIDE_ROW_ORDER_FIRST 4

static int displayPtmodOrderListStart(const PTMOD_PREVIEW_STATS *stats)
{
	int maxOrder = 0;
	int selected;
	int start;
	int maxStart;

	(void)stats;
	if (ptmodState.valid && ptmodState.songLength > 0)
		maxOrder = ptmodState.songLength - 1;
	selected = getPtmodDisplayOrder(stats);
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

static int displayPtmodRuntimeBase(void)
{
	return PTMOD_SIDE_ROW_ORDER_FIRST + PTMOD_ORDER_VISIBLE_ROWS;
}

static int displayPtmodChannelBase(void)
{
	return displayPtmodRuntimeBase() + 4;
}

static int displayPtmodSampleBase(const PTMOD_PREVIEW_STATS *stats)
{
	int channels = stats && stats->loaded ? stats->channels : PTMOD_MAX_PREVIEW_CHANNELS;

	if (channels < 0)
		channels = 0;
	if (channels > PTMOD_MAX_PREVIEW_CHANNELS)
		channels = PTMOD_MAX_PREVIEW_CHANNELS;
	return displayPtmodChannelBase() + channels;
}

static void formatPtmodWaveform(const PTMOD_SAMPLE *sample, char *dest, size_t destSize)
{
	static const char levels[] = ".:-=+*#@";
	size_t waveWidth;
	size_t i;

	if (!dest || destSize == 0)
		return;
	dest[0] = 0;
	if (!sample || !sample->data || sample->length == 0)
	{
		snprintf(dest, destSize, "(empty)");
		return;
	}

	waveWidth = destSize > 1 ? destSize - 1 : 0;
	if (waveWidth > 28)
		waveWidth = 28;
	for (i = 0; i < waveWidth; i++)
	{
		size_t start = (sample->length * i) / waveWidth;
		size_t end = (sample->length * (i + 1)) / waveWidth;
		unsigned peak = 0;
		size_t p;

		if (end <= start)
			end = start + 1;
		if (end > sample->length)
			end = sample->length;
		for (p = start; p < end; p++)
		{
			int value = (signed char)sample->data[p];
			unsigned absValue = value < 0 ? (unsigned)-value : (unsigned)value;

			if (absValue > peak)
				peak = absValue;
		}
		if (peak > 127)
			peak = 127;
		dest[i] = levels[(peak * (sizeof levels - 2)) / 127];
	}
	dest[waveWidth] = 0;
}

static void formatPtmodCurrentEffectHelp(const PTMOD_PREVIEW_STATS *stats, char *dest, size_t destSize)
{
	PTMOD_CELL cell;
	int orderIndex;
	int pattern;
	int row;

	if (!dest || destSize == 0)
		return;
	dest[0] = 0;
	orderIndex = getPtmodDisplayOrder(stats);
	pattern = ptmod_order_pattern(orderIndex);
	row = getPtmodDisplayRow(stats);
	if (pattern < 0 || !ptmod_get_pattern_cell(pattern, row, editorInfo.ptmodStreamChannel, &cell))
	{
		snprintf(dest, destSize, "--");
		return;
	}
	ptmod_format_effect_help(cell.effect, cell.param, dest, destSize);
}

static void formatPtmodMeterBar(int level, char *dest, size_t destSize)
{
	int width;
	int filled;
	int i;

	if (!dest || destSize == 0)
		return;
	width = (int)destSize - 1;
	if (width > 24)
		width = 24;
	if (width < 0)
		width = 0;
	if (level < 0)
		level = 0;
	if (level > 64)
		level = 64;
	filled = width ? (level * width + 32) / 64 : 0;
	for (i = 0; i < width; i++)
		dest[i] = i < filled ? '#' : '.';
	dest[width] = 0;
}

static const char *formatPtmodPanLabel(int pan)
{
	if (pan <= 0x40)
		return "L";
	if (pan >= 0xc0)
		return "R";
	return "C";
}

static void displayPtmodScopePanel(int OX, int *y, int width,
	const PTMOD_PREVIEW_STATS *stats, const PTMOD_RUNTIME_SETTINGS *runtimeSettings,
	int color, int mutedColor, int titleColor)
{
	int c;

	printtext(OX, (*y)++, titleColor, "SCOPES / METERS");
	printPtmodSettingLine(OX, (*y)++, width, -1, color, "Ctrl+M hides  MOD mix remains mono");
	for (c = 0; c < PTMOD_CHANNELS; c++)
	{
		char meter[28];
		int rowColor = runtimeSettings && runtimeSettings->channelMute[c] ? mutedColor : color;
		int pan = stats ? stats->channelPan[c] : 0x80;
		int level = stats ? stats->channelLevel[c] : 0;
		int sample = stats ? stats->channelSample[c] : 0;
		int period = stats ? stats->channelPeriod[c] : 0;
		int position = stats ? stats->channelPosition[c] : 0;

		formatPtmodMeterBar(level, meter, sizeof meter);
		snprintf(textbuffer, sizeof textbuffer, "Ch%d pan:%s vol:%02d [%s]",
			c + 1, formatPtmodPanLabel(pan), level, meter);
		printPtmodSettingLine(OX, (*y)++, width, -1, rowColor, textbuffer);
		snprintf(textbuffer, sizeof textbuffer, "    smp:%02d per:%03X pos:%06X mute:%s",
			sample, period & 0xfff, position & 0xffffff,
			runtimeSettings && runtimeSettings->channelMute[c] ? "ON " : "OFF");
		printPtmodSettingLine(OX, (*y)++, width, -1, rowColor, textbuffer);
	}
}

static void displayPtmodSettings(int cc, int OX, int OY)
{
	PTMOD_PREVIEW_STATS stats;
	PTMOD_RUNTIME_SETTINGS runtimeSettings;
	PTMOD_SAMPLE sample;
	int width = getSidePanelWidth();
	int color = getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);
	int titleColor = getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);
	int mutedColor = getColor(CUNUSED_MUTED_FOREGROUND, CUNUSED_MUTED_BACKGROUND);
	int y = OY;
	int c;
	int sampleIndex;
	int orderIndex;
	int pattern;
	int orderStart;
	int runtimeBase;
	int channelBase;
	int sampleBase;
	char wave[32];
	char effectHelp[96];

	(void)cc;
	if (width < 40)
		width = 40;
	if (OX + width > MAX_COLUMNS)
		width = MAX_COLUMNS - OX;

	fillArea(OX, OY - 1, width, TRANSPORT_BAR_Y - OY + 3, getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND), ' ');
	ptmodplay_get_stats(&stats);
	ptmodplay_get_runtime_settings(&runtimeSettings);

	snprintf(textbuffer, sizeof textbuffer, "MOD EDITOR%s", editorInfo.ptmodEditPage == 1 ? "*" : "");
	printtext(OX, y++, titleColor, textbuffer);

	if (!ptmodState.valid)
	{
		printPtmodSettingLine(OX, y++, width, -1, mutedColor, "No ProTracker MOD loaded");
		return;
	}

	snprintf(textbuffer, sizeof textbuffer, "Source: %.247s", displayBasename(ptmodState.path));
	printPtmodSettingLine(OX, y++, width, -1, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Orders:%d Patterns:%d Save:F11 %s",
		ptmodState.songLength, ptmodState.patternCount, ptmodState.dirty ? "DIRTY" : "clean");
	printPtmodSettingLine(OX, y++, width, -1, color, textbuffer);
	orderIndex = getPtmodDisplayOrder(&stats);
	pattern = ptmod_order_pattern(orderIndex);
	snprintf(textbuffer, sizeof textbuffer, "Ord:%02d Pat:%02X Row:%02X Ch:%d",
		orderIndex, pattern < 0 ? 0 : pattern, getPtmodDisplayRow(&stats),
		editorInfo.ptmodStreamChannel + 1);
	printPtmodSettingLine(OX, y++, width, -1, color, textbuffer);

	snprintf(textbuffer, sizeof textbuffer, "Title: %.28s", ptmodState.title[0] ? ptmodState.title : "(untitled)");
	printPtmodSettingLine(OX, y++, width, PTMOD_SIDE_ROW_TITLE, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Length:%3d", ptmodState.songLength);
	printPtmodSettingLine(OX, y++, width, PTMOD_SIDE_ROW_LENGTH, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Restart:%02d", ptmodState.restartPosition);
	printPtmodSettingLine(OX, y++, width, PTMOD_SIDE_ROW_RESTART, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Follow:[%s] Ctrl+F", editorInfo.ptmodStreamFollow ? "ON " : "OFF");
	printPtmodSettingLine(OX, y++, width, PTMOD_SIDE_ROW_FOLLOW, color, textbuffer);

	orderStart = displayPtmodOrderListStart(&stats);
	for (c = 0; c < PTMOD_ORDER_VISIBLE_ROWS; c++)
	{
		int order = orderStart + c;
		int rowId = PTMOD_SIDE_ROW_ORDER_FIRST + c;
		int rowColor = color;
		char playMarker = ' ';
		char cursorMarker = ' ';

		if (order >= ptmodState.songLength)
		{
			printPtmodSettingLine(OX, y++, width, rowId, mutedColor, "");
			continue;
		}
		pattern = ptmod_order_pattern(order);
		if (stats.active && stats.orderIndex == order)
		{
			playMarker = '>';
			rowColor = getColor(CPATTERN_HIGHLIGHT_PLAYING_LINE_FOREGROUND, CPATTERN_HIGHLIGHT_PLAYING_LINE_BACKGROUND);
		}
		if (order == orderIndex)
			cursorMarker = '*';
		snprintf(textbuffer, sizeof textbuffer, "%c Order %02d  Pattern %02X %c",
			playMarker, order, pattern < 0 ? 0 : pattern, cursorMarker);
		printPtmodSettingLine(OX, y++, width, rowId, rowColor, textbuffer);
	}

	formatPtmodCurrentEffectHelp(&stats, effectHelp, sizeof effectHelp);
	snprintf(textbuffer, sizeof textbuffer, "Effect: %.60s", effectHelp);
	printPtmodSettingLine(OX, y++, width, -1, color, textbuffer);

	runtimeBase = displayPtmodRuntimeBase();
	channelBase = displayPtmodChannelBase();
	sampleBase = displayPtmodSampleBase(&stats);

	y++;
	snprintf(textbuffer, sizeof textbuffer, "Preview:%s", runtimeSettings.enabled ? "ON " : "OFF");
	printPtmodSettingLine(OX, y++, width, runtimeBase, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Replay:%s", ptmodplay_replay_mode_name(runtimeSettings.replayMode));
	printPtmodSettingLine(OX, y++, width, runtimeBase + 1, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Mix volume:%3d%%", runtimeSettings.masterVolume);
	printPtmodSettingLine(OX, y++, width, runtimeBase + 2, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Start delay:%3d frames", runtimeSettings.startDelayFrames);
	printPtmodSettingLine(OX, y++, width, runtimeBase + 3, color, textbuffer);
	if (runtimeSettings.replayMode == PTMOD_REPLAY_THC_WAVEFORM)
	{
		snprintf(textbuffer, sizeof textbuffer, "THCMOD: waveform voice 3 setup%s",
			stats.voice3Conflict ? " CONFLICT" : "");
		printPtmodSettingLine(OX, y++, width, -1,
			stats.voice3Conflict ? titleColor : color,
			textbuffer);
	}
	snprintf(textbuffer, sizeof textbuffer, "Scopes:%s Ctrl+M", editorInfo.ptmodScopeView ? "ON " : "OFF");
	printPtmodSettingLine(OX, y++, width, -1, color, textbuffer);

	for (c = 0; c < stats.channels && c < PTMOD_MAX_PREVIEW_CHANNELS; c++)
	{
		snprintf(textbuffer, sizeof textbuffer, "Ch%d vol:%3d%% mute:%s", c + 1,
			runtimeSettings.channelVolume[c],
			runtimeSettings.channelMute[c] ? "ON " : "OFF");
		printPtmodSettingLine(OX, y++, width, channelBase + c, runtimeSettings.channelMute[c] ? mutedColor : color, textbuffer);
	}

	y++;
	if (editorInfo.ptmodScopeView)
	{
		displayPtmodScopePanel(OX, &y, width, &stats, &runtimeSettings, color, mutedColor, titleColor);
		snprintf(textbuffer, sizeof textbuffer, "Pos:%d Pattern:%d Row:%d Speed:%d BPM:%d",
			stats.orderIndex, stats.pattern, stats.row, stats.speed, stats.bpm);
		printPtmodSettingLine(OX, y++, width, -1, color, textbuffer);
		return;
	}
	printtext(OX, y++, titleColor, "SAMPLE");
	sampleIndex = editorInfo.ptmodSampleIndex;
	if (sampleIndex < 0)
		sampleIndex = 0;
	if (sampleIndex >= PTMOD_MAX_SAMPLES)
		sampleIndex = PTMOD_MAX_SAMPLES - 1;
	if (!ptmod_get_sample(sampleIndex, &sample))
		memset(&sample, 0, sizeof sample);
	snprintf(textbuffer, sizeof textbuffer, "Sample:%02d", sampleIndex + 1);
	printPtmodSettingLine(OX, y++, width, sampleBase, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Name: %.30s", sample.name[0] ? sample.name : "(empty)");
	printPtmodSettingLine(OX, y++, width, sampleBase + 1, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Finetune:%2d", sample.finetune);
	printPtmodSettingLine(OX, y++, width, sampleBase + 2, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Volume:%2d Length:%u", sample.volume, sample.length);
	printPtmodSettingLine(OX, y++, width, sampleBase + 3, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Loop start:%u", sample.loopStart);
	printPtmodSettingLine(OX, y++, width, sampleBase + 4, color, textbuffer);
	snprintf(textbuffer, sizeof textbuffer, "Loop length:%u", sample.loopLength);
	printPtmodSettingLine(OX, y++, width, sampleBase + 5, color, textbuffer);
	formatPtmodWaveform(&sample, wave, sizeof wave);
	snprintf(textbuffer, sizeof textbuffer, "Wave:[%s]", wave);
	printPtmodSettingLine(OX, y++, width, sampleBase + 6, color, textbuffer);
	printPtmodSettingLine(OX, y++, width, sampleBase + 7, color, "Enter editor  A audition  Ctrl+I/E/D");

	snprintf(textbuffer, sizeof textbuffer, "Pos:%d Pattern:%d Row:%d Speed:%d BPM:%d",
		stats.orderIndex, stats.pattern, stats.row, stats.speed, stats.bpm);
	printPtmodSettingLine(OX, y++, width, -1, color, textbuffer);
}

void updateDisplayWhenFollowingAndPlaying(GTOBJECT *gt)
{
	if (editorInfo.expandOrderListView == 1)
		updateDisplayWhenFollowingAndPlaying_Expanded(gt);
	else
		updateDisplayWhenFollowingAndPlaying_Compressed(gt);
}

void updateDisplayWhenFollowingAndPlaying_Compressed(GTOBJECT *gt)
{
	//	if ((followplay) && (!transportLoopPattern) && (isplaying(gt)))	// 1.1.7 FIX (added !transportLoopPattern) 3/5/2022
	if ((followplay) && (isplaying(gt)))	// 1.2.1 - removed (!transportLoopPattern) as it stopped follow+loop..gotta look into why I added that!
	{
		for (int c = 0; c < editorInfo.maxSIDChannels; c++)
		{
			int c2 = getVisualChannelActualChannel(c);	// 0-11
			int playingSong = getVisualChannelSongNumber(c);
			int songCh = getVisualChannelLocalChannel(c);

			int newpos = gt->chn[c2].lastpattptr / 4;	//.pattptr / 4;
			if (gt->chn[c2].advance)
				gt->editorUndoInfo.editorInfo[c2].epnum = gt->chn[c2].pattnum;

			if (newpos > pattlen[gt->editorUndoInfo.editorInfo[c2].epnum])
				newpos = pattlen[gt->editorUndoInfo.editorInfo[c2].epnum];


			int follow = 0;
			//	if (c == editorInfo.eschn)
			//		follow = 1;

			if (c == gt->masterLoopChannel)
				follow = 1;

			if (follow == 1)
			{
				editorInfo.eppos = newpos;
				editorInfo.epview = newpos - VISIBLEPATTROWS / 2;
			}

			newpos = gt->chn[c2].songptr;
			newpos--;
			if (newpos < 0)
				newpos = 0;
			if (newpos > songlen[playingSong][songCh])
				newpos = songlen[playingSong][songCh];


			gt->editorUndoInfo.editorInfo[c2].espos = gt->chn[c2].songptr - 1;

			/*
			if ((c2 == gt->masterLoopChannel) && (gt->chn[c2].advance))	// && (playingSong == editorInfo.esnum))
			{
				editorInfo.eseditpos = newpos;
				if (newpos - editorInfo.esview < 0)
				{
					editorInfo.esview = newpos;
				}

				int endPos = VISIBLEORDERLIST - 1;	// V1.2.2 change: Scroll order list 1 pattern before the end of the display, so we can see the next pattern that's going to play
				if (newpos - editorInfo.esview >= endPos)
				{
					editorInfo.esview = newpos - (endPos - 1);
				}
			}
			*/


		}
	}
}

void updateDisplayWhenFollowingAndPlaying_Expanded(GTOBJECT *gt)
{
	//	if ((followplay) && (!transportLoopPattern) && (isplaying(gt)))	// 1.1.7 FIX (added !transportLoopPattern) 3/5/2022
	if ((followplay) && (isplaying(gt)))	// 1.2.1 - removed (!transportLoopPattern) as it stopped follow+loop..gotta look into why I added that!
	{
		for (int c = 0; c < editorInfo.maxSIDChannels; c++)
		{
			int c2 = getVisualChannelActualChannel(c);	// 0-11
			int playingSong = getVisualChannelSongNumber(c);
			int songCh = getVisualChannelLocalChannel(c);

			int newpos = gt->chn[c2].lastpattptr / 4;	//stpattptr / pattptr / 4;
			if (gt->chn[c2].advance)
				gt->editorUndoInfo.editorInfo[c2].epnum = gt->chn[c2].pattnum;

			if (newpos > pattlen[gt->editorUndoInfo.editorInfo[c2].epnum])
				newpos = pattlen[gt->editorUndoInfo.editorInfo[c2].epnum];


			int follow = 0;
			//			if (c == editorInfo.eschn)
			//				follow = 1;

			if (c == gt->masterLoopChannel)
				follow = 1;

			if (follow == 1)
			{
				editorInfo.eppos = newpos;
				editorInfo.epview = newpos - VISIBLEPATTROWS / 2;
			}

			newpos = gt->chn[c2].songptr;
			newpos--;
			if (newpos < 0)
				newpos = 0;
			if (newpos > songOrderLength[playingSong][songCh])
				newpos = songOrderLength[playingSong][songCh];



			gt->editorUndoInfo.editorInfo[c2].espos = gt->chn[c2].songptr - 1;

			/*
			if ((c2 == gt->masterLoopChannel) && (gt->chn[c2].advance))	// && (playingSong == editorInfo.esnum))
			{
				editorInfo.eseditpos = newpos;
				if (newpos - editorInfo.esview < 0)
				{
					editorInfo.esview = newpos;
				}

				int endPos = VISIBLEORDERLIST - 1;	// V1.2.2 change: Scroll order list 1 pattern before the end of the display, so we can see the next pattern that's going to play
				if (newpos - editorInfo.esview >= endPos)
				{
					editorInfo.esview = newpos - (endPos - 1);
				}
			}
			*/



		}
	}
}

static void appendbounded(char *dest, size_t destSize, const char *src)
{
	size_t used;

	if (!dest || !src || destSize == 0)
		return;

	used = strlen(dest);
	if (used >= destSize - 1)
		return;

	strncat(dest, src, destSize - used - 1);
}

void displayTopBar(int menu, int cc)
{
	printblankc(0, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), MAX_COLUMNS);

	int menuInfoXOffset = 38;
	if (!menu)
	{
		textbuffer[0] = 0;
		appendbounded(textbuffer, sizeof textbuffer, programname);
		if (strlen(loadedsongfilename))
		{
			//	int lockPatternColor = getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);	//0xe;
			appendbounded(textbuffer, sizeof textbuffer, ": ");
			appendbounded(textbuffer, sizeof textbuffer, loadedsongfilename);
			//	printtext(PANEL_ORDER_X, PANEL_ORDER_Y - 1, lockPatternColor, textbuffer);
			//	sprintf(textbuffer, "%s - %s", programname, loadedsongfilename);
		}
		textbuffer[57] = 0;
		printtext(0, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), textbuffer);

		menuInfoXOffset += 2;

		if (editorInfo.usefinevib)
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "FV");
		else
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND_OFF, TOPBAR_BACKGROUND), "FV");

		menuInfoXOffset += 3;
		if (editorInfo.optimizepulse)
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "PO");
		else
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND_OFF, TOPBAR_BACKGROUND), "PO");
		menuInfoXOffset += 3;

		if (editorInfo.optimizerealtime)
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "RO");
		else
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND_OFF, TOPBAR_BACKGROUND), "RO");

		menuInfoXOffset += 3;
		if (editorInfo.ntsc)
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "NTSC");
		else
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), " PAL");

		menuInfoXOffset += 5;
		if (!editorInfo.sidmodel)
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "6581");
		else
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "8580");

		menuInfoXOffset += 5;

		if (!editPan)
		{
			sprintf(textbuffer, "HR:%04X", editorInfo.adparam);
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), textbuffer);
		}
		else
		{
			unsigned int v = 0;
			int sidChips = editorInfo.maxSIDChannels / 3;
			for (int i = 0;i < sidChips;i++)
			{
				v <<= 4;
				v |= SID_StereoPanPositions[sidChips - 1][i];
			}
			if (sidChips == 1)
				sprintf(textbuffer, "P%01X:%01X---", sidChips, v);
			else if (sidChips == 2)
				sprintf(textbuffer, "P%01X:%02X--", sidChips, v);
			else if (sidChips == 3)
				sprintf(textbuffer, "P%01X:%03X-", sidChips, v);
			else
				sprintf(textbuffer, "P%01X:%04X", sidChips, v);
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), textbuffer);
		}

		menuInfoXOffset += 3;
		if (eamode)
			printbg(menuInfoXOffset + 20 + editorInfo.eacolumn, getColor(0, 0), cc << 8, 1);

		menuInfoXOffset += 5;


		if (editorInfo.multiplier)
		{
			sprintf(textbuffer, "%2dX", editorInfo.multiplier);
			printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), textbuffer);
		}
		else printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "25Hz");

		menuInfoXOffset += 5;
		printtext(menuInfoXOffset + 20, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), "F12=HELP");
	}
	else
	{
		printtext(0, 0, getColor(TOPBAR_FOREGROUND, TOPBAR_BACKGROUND), " PLAY | PLAYPOS | PLAYPATT | STOP | LOAD | SAVE | PACK/RL | HELP | CLEAR | QUIT |");
	}
}

//#define DISPLAY_FREE_MEM

#ifdef DISPLAY_FREE_MEM

/* Definition of a structure that is 1024 bytes (1 kilobyte) in size.) */

struct kilo {/*  w  ww .  j  ava 2s .  c  o m*/
	struct kilo *next;
	//	char dummy[1022];
};


#define FREEMEMALLOCSIZE 512*1024		// 1/2 meg packets
int FreeMem(void)
{
	/*Returns the number of kilobytes (1024 bytes) of free memory. */

	long counter;
	struct kilo *head, *current, *nextone;

	unsigned int memSize = FREEMEMALLOCSIZE;
	current = head = (struct kilo*) malloc(FREEMEMALLOCSIZE);

	if (head == NULL)
		return 0;      /*No memory available.*/

	counter = memSize;

	int allocCount = 1;
	//	do
	//	{
	int doAgain = 1;
	do {
		current->next = (struct kilo*) malloc(memSize);

		if (current->next != NULL)
		{
			allocCount++;
			current = current->next;
			counter += memSize;
		}
		else
			doAgain = 0;
	} while (doAgain != 0);
	//		memSize /= 2;
	//	} while (memSize >= 64 * 1024);

	current = head;
	do {
		nextone = current->next;
		free(current);
		current = nextone;
		allocCount--;
	} while (nextone != NULL);

	return counter;
}

int getFreeMem = 1;
int freememsize = 0;

#endif

void displayInstrument(GTOBJECT *gt, int cc, int OX, int OY)
{
	int color;



#ifdef DISPLAY_FREE_MEM
	if (getFreeMem)
	{
		freememsize = FreeMem();	// *FREEMEMALLOCSIZE;
		getFreeMem = 0;
	}
	sprintf(textbuffer, "free mem: 0x%x", freememsize);
	printtext(60, 12, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);
#endif

	//	UIUnderline = UNDERLINE_FOREGROUND_MASK;
	sprintf(textbuffer, "INSTRUMENT NUM. %02X  %-18s", editorInfo.einum, instr[editorInfo.einum].name);
	printtext(OX, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);

	//	UIUnderline = 0;

	sprintf(textbuffer, "          Pan   %02X", instr[editorInfo.einum].pan);
	if (editorInfo.eipos == 9)
		color = CORDER_INST_TABLE_EDITING;
	else
		color = CORDER_INST_FOREGROUND;
	printtext(OX + 20, OY + 5, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	//	sprintf(textbuffer, "                  ");
	//	printtext(OX + 20, OY + 5, getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND), textbuffer);
	sprintf(textbuffer, "IC:%d", instrumentCount[editorInfo.einum]);
	printtext(OX + 20, OY + 5, getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "Attack/Decay    %02X  ", instr[editorInfo.einum].ad);
	if (editorInfo.eipos == 0)
		color = CORDER_INST_TABLE_EDITING;
	else
		color = CORDER_INST_FOREGROUND;
	printtext(OX, OY + 1, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "Sustain/Release %02X  ", instr[editorInfo.einum].sr);
	if (editorInfo.eipos == 1) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX, OY + 2, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "Wavetable Pos   %02X  ", instr[editorInfo.einum].ptr[WTBL]);
	if (editorInfo.eipos == 2) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX, OY + 3, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "Pulsetable Pos  %02X  ", instr[editorInfo.einum].ptr[PTBL]);
	if (editorInfo.eipos == 3) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX, OY + 4, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "Filtertable Pos %02X  ", instr[editorInfo.einum].ptr[FTBL]);
	if (editorInfo.eipos == 4) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX, OY + 5, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "Vibrato Param   %02X", instr[editorInfo.einum].ptr[STBL]);
	if (editorInfo.eipos == 5) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX + 20, OY + 1, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "Vibrato Delay   %02X", instr[editorInfo.einum].vibdelay);
	if (editorInfo.eipos == 6) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX + 20, OY + 2, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "HR/Gate Timer   %02X", instr[editorInfo.einum].gatetimer);
	if (editorInfo.eipos == 7) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX + 20, OY + 3, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	sprintf(textbuffer, "1stFrame Wave   %02X", instr[editorInfo.einum].firstwave);
	if (editorInfo.eipos == 8) color = CORDER_INST_TABLE_EDITING; else color = CORDER_INST_FOREGROUND;
	printtext(OX + 20, OY + 4, getColor(color, CORDER_INST_BACKGROUND), textbuffer);

	// cursor
	if (editorInfo.editmode == EDIT_INSTRUMENT)
	{
		if (editorInfo.eipos < LAST_INST)
		{
			if (!eamode) printbg(OX - 4 + 20 + editorInfo.eicolumn + 20 * (editorInfo.eipos / 5), OY + 1 + (editorInfo.eipos % 5), cc << 8, 1);
		}
		else
		{
			if (!eamode) printbg(OX + 20 + strlen(instr[editorInfo.einum].name), OY, cc << 8, 1);
		}
	}

	// Instrument border
	printbyterow(OX, OY + 6, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f6, 38);
	printbytecol(OX + 38, OY + 1, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f5, 5);
}


int lastTableType = EDIT_TABLE_NONE;
void displayTables(int OX, int OY)
{
	if (editorInfo.editTableMode != lastTableType)
	{
		lastTableType = editorInfo.editTableMode;

		// Clear table panel
		for (int y = 0;y < VISIBLETABLEROWS;y++)
		{
			printbyterow(OX, OY + 1 + y, getColor(CTABLE_FOREGROUND1, CTABLE_BACKGROUND1), 32, 38);
		}

		if (editorInfo.editTableMode == EDIT_TABLE_WAVE)
		{
			editorInfo.etDetailedWaveTableColumn = 15;	// left table column offset
			if (editorInfo.etnum != EDIT_TABLE_SPEED - 1)
			{
				editorInfo.etnum = WTBL;
				if (editorInfo.etcolumn > 4)	// Not currently editing WAVE table? Move cursor to first column
					editorInfo.etcolumn = 0;
			}
		}
		else if (editorInfo.editTableMode == EDIT_TABLE_FILTER)
		{
			editorInfo.etDetailedWaveTableColumn = 8;	// left table column offset
			editorInfo.etnum = FTBL;

			if (editorInfo.etcolumn > 4)	// Not currently editing WAVE table? Move cursor to first column
				editorInfo.etcolumn = 0;
		}
		else if (editorInfo.editTableMode == EDIT_TABLE_PULSE)
		{
			editorInfo.etDetailedWaveTableColumn = 8;	// left table column offset
			editorInfo.etnum = PTBL;

			if (editorInfo.etcolumn > 4)	// Not currently editing WAVE table? Move cursor to first column
				editorInfo.etcolumn = 0;
		}
	}

	int cc = cursorcolortable[cursorflash];

	//	UIUnderline = UNDERLINE_FOREGROUND_MASK;
	sprintf(textbuffer, "WAVE TBL");
	printtext(OX, OY, getTableTitleColour(EDIT_TABLE_WAVE), textbuffer);

	sprintf(textbuffer, "PULSETBL");
	printtext(OX + 10, OY, getTableTitleColour(EDIT_TABLE_PULSE), textbuffer);

	sprintf(textbuffer, "FILT.TBL");
	printtext(OX + 20, OY, getTableTitleColour(EDIT_TABLE_FILTER), textbuffer);

	if (editorInfo.editTableMode == EDIT_TABLE_WAVE || editorInfo.editTableMode == EDIT_TABLE_NONE)
	{
		sprintf(textbuffer, "SPEEDTBL");
		printtext(OX + 30, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);
	}
	else
	{
		sprintf(textbuffer, "        ");
		printtext(OX + 30, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);
	}

	printtext(OX + 8, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), "  ");
	printtext(OX + 10 + 8, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), "  ");
	printtext(OX + 20 + 8, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), "  ");
	//	UIUnderline = 0;

	if (editorInfo.editTableMode == EDIT_TABLE_NONE)
	{
		displayOriginalTableView(cc, OX, OY);
	}
	else if (editorInfo.editTableMode == EDIT_TABLE_WAVE)
	{
		displayDetailedWaveTable(cc, OX, OY);
	}
	else if (editorInfo.editTableMode == EDIT_TABLE_FILTER)
	{
		displayDetailedFilterTable(cc, OX, OY);
	}
	else if (editorInfo.editTableMode == EDIT_TABLE_PULSE)
	{
		displayDetailedPulseTable(cc, OX, OY);
	}


	// Table bottom border
	printbyterow(PANEL_TABLES_X, PANEL_TABLES_Y + 15, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f6, 38);
	printbytecol(PANEL_TABLES_X + 38, PANEL_TABLES_Y + 1, getColor(CGENERAL_HIGHLIGHT, CGENERAL_BACKGROUND), 0x1f5, 0xe);
}

/*
Wavetable left side:   00    Leave waveform unchanged
					   01-0F Delay this step by 1-15 frames
					   10-DF Waveform values
					   E0-EF Inaudible waveform values $00-$0F
					   F0-FE Execute command 0XY-EXY. Right side is parameter.
					   FF    Jump. Right side tells position ($00 = stop)

*/
void displayDetailedWaveTable(int cc, int OX, int OY)
{
	for (int d = 0; d < VISIBLETABLEROWS; d++)
	{
		int p = editorInfo.etview[0] + d;
		int color = tableBackgroundColors[0][p];
		UIUnderline = 0;

		int v = ltable[0][p];
		int v2 = v;
		if (v == 0xff)
			UIUnderline = UNDERLINE_FOREGROUND_MASK;

		int r = rtable[0][p];
		int r2 = r;

		sprintf(textbuffer, "%02X:", p + 1);
		printtext(OX, OY + 1 + d, color, textbuffer);

		printtext(OX + 4, OY + 1 + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), "-WDCJ");
		int highlightOffset = 0;
		int displayRightTable = 0;

		if (v == 0)	// skip
		{
			detailedTableBaseLValue[p] = -1;
			printtext(OX + 13, OY + 1 + d, color, "---- ");
			displayRightTable = 1;
		}
		else if (v >= 1 && v <= 0x0f)
		{
			detailedTableLValue[p] = v;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xf;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 1;
			detailedTableBaseLValue[p] = 0;		// base value to add to current to get new tableR[p] value

			printtext(OX + 13, OY + 1 + d, color, "DELY:");
			highlightOffset = 2;	// delay
			displayRightTable = 1;
		}
		else if (v >= 0x10 && v <= 0x0ef)
		{
			highlightOffset = 1;	// wave
			printtext(OX + 13, OY + 1 + d, color, "WAVE:");
			if (v >= 0xe0)
				v2 -= 0xe0;	// waveforms 0-0xf

			// Will need to check ltable[p] to know how to set final value 
			detailedTableLValue[p] = v2;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xdf;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0x0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)

			displayRightTable = 1;
		}
		else if (v >= 0xf0 && v <= 0x0fe)
		{
			highlightOffset = 3;	// command
			printtext(OX + 13, OY + 1 + d, color, "CMND:");
			v2 -= 0xf0;

			detailedTableLValue[p] = v2;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xe;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0xf0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)

			detailedTableRValue[p] = r;			// current L value. Modify this with HexInput
			detailedTableMaxRValue[p] = 0xff;		// maximum possible value (min is always 0)
			detailedTableMinRValue[p] = 0;
			detailedTableBaseRValue[p] = 0x0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)


			displayRightTable = 2;
		}
		else
		{
			detailedTableBaseRValue[p] = -1;

			highlightOffset = 4;	// jump
			if (r)
				printtext(OX + 13, OY + 1 + d, color, "JUMP:");
			else
				printtext(OX + 13, OY + 1 + d, color, "STOP:");
			displayRightTable = 0;

			detailedTableLValue[p] = r;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xff;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0x0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)

			v2 = r;
		}

		if (v != 0)
		{
			sprintf(textbuffer, "%02X", v2);
			printtext(OX + 18, OY + 1 + d, color, textbuffer);
		}
		else
			printtext(OX + 18, OY + 1 + d, color, "--");


		/*
		Wavetable right side:  00-5F Relative notes
					   60-7F Negative relative notes (lower pitch)
					   80    Keep frequency unchanged
					   81-DF Absolute notes C#0 - B-7

		*/


		printtext(OX + 25, OY + 1 + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), "   ");
		if (displayRightTable == 1)	// 1 = display Relative/Absolute / Note name (eg C#4)
		{

			int rhighlight = -1;
			printtext(OX + 10, OY + 1 + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), "RA");
			if (r != 0x80)
			{
				if (r >= 0x81)	// && r <= 0xdf)
				{
					r2 -= 0x80;
					detailedTableRValue[p] = r2;
					detailedTableMaxRValue[p] = 0x5f;
					detailedTableBaseRValue[p] = 0x80;
					detailedTableMinRValue[p] = 1;

					rhighlight = 1;
					printtext(OX + 25, OY + 1 + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), notenameTableView[r2]);
					sprintf(textbuffer, " %02X", r2);
					printtext(OX + 21, OY + 1 + d, color, textbuffer);
				}
				else
				{
					rhighlight = 0;
					if (r >= 0x60 && r <= 0x7f)	// negative relative notes
					{
						r2 += 0x80;	// change negative values to 0xe0>0xff
						int r3 = -r2;
						r3 &= 0xff;

						detailedTableRValue[p] = r3;			// current R value. Modify this with HexInput
						detailedTableMaxRValue[p] = 0x20;		// maximum possible value (min is always 0)
						detailedTableBaseRValue[p] = 0x60;		// base value to add to current to get new tableR[p] value
						detailedTableMinRValue[p] = 1;

						sprintf(textbuffer, "-%02X", r3);
						printtext(OX + 21, OY + 1 + d, color, textbuffer);
					}
					else
					{
						detailedTableRValue[p] = r2;
						detailedTableMaxRValue[p] = 0x5f;
						detailedTableBaseRValue[p] = 0x0;
						detailedTableMinRValue[p] = 0;

						sprintf(textbuffer, "+%02X", r2);
						printtext(OX + 21, OY + 1 + d, color, textbuffer);
					}
					printbg(OX + 21, OY + 1 + d, getColor(CCOLOR_EXCLAMATION, CTABLE_UNUSED_BACKGROUND), 1);
				}
				//				if (rhighlight >= 0)
				printbg(OX + 10 + rhighlight, OY + 1 + d, getColor(CCOLOR_EXCLAMATION, CTABLE_UNUSED_BACKGROUND), 1);
			}
			else
			{
				detailedTableBaseRValue[p] = -1;	// -1 = User can't edit value
				printtext(OX + 21, OY + 1 + d, color, " --");
			}
		}
		else if (displayRightTable == 2)	// Display command
		{
			sprintf(textbuffer, " %02X", r2);
			printtext(OX + 21, OY + 1 + d, color, textbuffer);
		}
		else
			printtext(OX + 21, OY + 1 + d, color, " --");

		UIUnderline = 0;

		printbg(OX + 4 + highlightOffset, OY + 1 + d, getColor(CCOLOR_EXCLAMATION, CTABLE_UNUSED_BACKGROUND), 1);


		if (editorInfo.etmarknum == 0)
		{
			if (editorInfo.etmarkstart <= editorInfo.etmarkend)
			{
				if ((p >= editorInfo.etmarkstart) && (p <= editorInfo.etmarkend))
				{
					printbg(OX + 18, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
					printbg(OX + 18 - 5, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 5);
					printbg(OX + 22, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
				}
			}
			else
			{
				if ((p <= editorInfo.etmarkstart) && (p >= editorInfo.etmarkend))
				{
					printbg(OX + 18, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
					printbg(OX + 18 - 5, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 5);
					printbg(OX + 22, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
				}
			}
		}
	}

	displayTable(EDIT_TABLE_SPEED - 1, OX, OY);
	if (editorInfo.editmode == EDIT_TABLES)
	{
		if (!eamode)
		{
			if (editorInfo.etnum < EDIT_TABLE_SPEED - 1)
			{
				editorInfo.cursorX = OX + 3 + (editorInfo.etcolumn & 1) + editorInfo.etDetailedWaveTableColumn + (editorInfo.etcolumn / 2) * 4;
				printbg(editorInfo.cursorX, OY + 1 + editorInfo.etpos - editorInfo.etview[editorInfo.etnum], cc << 8, 1);
			}
			else
			{
				editorInfo.cursorX = OX + 3 + editorInfo.etnum * 10 + (editorInfo.etcolumn & 1) + (editorInfo.etcolumn / 2) * 3;
				printbg(editorInfo.cursorX, OY + 1 + editorInfo.etpos - editorInfo.etview[editorInfo.etnum], cc << 8, 1);
			}
		}
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

void displayDetailedFilterTable(int cc, int OX, int OY)
{
	//int filterEnabledColor[3] = { 0,0,0 };
	int xpos = OX + 9;
	int ypos = OY + 1;



	for (int d = 0; d < VISIBLETABLEROWS; d++)
	{
		int p = editorInfo.etview[FTBL] + d;
		int color = tableBackgroundColors[FTBL][p];
		UIUnderline = 0;


		printtext(xpos + 15, ypos + d, getColor(CTABLE_FOREGROUND1, CTABLE_BACKGROUND1), "      ");


		int v = ltable[FTBL][p];
		int v2 = v;
		if (v == 0xff)
			UIUnderline = UNDERLINE_FOREGROUND_MASK;

		int r = rtable[FTBL][p];
		int r2 = r;

		sprintf(textbuffer, "%02X:", p + 1);
		printtext(OX, ypos + d, color, textbuffer);

		printtext(OX + 4, ypos + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), "CMFJ");
		int highlightOffset = 0;
		int displayRightTable = 0;


		if (v == 0)	// cutoff
		{
			highlightOffset = 0;

			detailedTableBaseRValue[p] = -1;	// No right value to display
			displayRightTable = 0;

			printtext(xpos, ypos + d, color, "CUTOFF ");

			v++;
			v2 = r;	// display right value in left column
			detailedTableLValue[p] = r;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xff;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0;		// base value to add to current to get new tableR[p] value

		}
		else if (v >= 1 && v <= 0x7f)	// modify filter cutoff
		{
			highlightOffset = 1;
			detailedTableLValue[p] = v;			// Left value = time
			detailedTableMaxLValue[p] = 0x7f;		// maximum possible value
			detailedTableMinLValue[p] = 1;
			detailedTableBaseLValue[p] = 0;		// base value to add to current to get new tableR[p] value

			printtext(xpos, ypos + d, color, "FLT MOD");
			displayRightTable = 1;
		}
		else if (v >= 0x80 && v <= 0x0f0)
		{
			// Could display all of this within LEFT display code
			displayRightTable = 2;
			detailedTableRValue[p] = -1;

			highlightOffset = 2;	// Resonance, filter On/Off and filter type.
			printtext(xpos, ypos + d, color, "FLT SET");


			// Left side low nybble is not used.
			int filterType = (v >> 4) & 0xf;
			int filterResonance = (r >> 4) & 0xf;
			int filterOnOff = r & 0x7;

			v2 = filterResonance;

			// Will need to check ltable[p] to know how to set final value 
			detailedTableLValue[p] = v2;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xf;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0x0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)

			//jp123
			for (int i = 0;i < 3;i++)
			{
				int headerColor = getColor(CTABLE_UNUSED_FOREGROUND, 0);
				if (filterOnOff &(1 << i))
					headerColor = getColor(CCOLOR_RED, 0);
				printbyte(xpos + 11 + i, ypos + d, headerColor, 0x1f3);	// Filter on/off marker

//				filterEnabledColor[i] = headerColor;
			}

			for (int j = 0;j < 3;j++)
			{
				int headerColor = getColor(CTABLE_UNUSED_FOREGROUND, 0);
				if (filterType &(1 << j))
					headerColor = getColor(CINFO_FOREGROUND, 0);
				for (int i = 0;i < 2;i++)
				{
					int x = j * 2 + i;
					printbyte(xpos + 15 + x, ypos + d, headerColor, 0x1e0 + (j * 2) + i);
				}
			}

		}
		else if (v >= 0xf1 && v <= 0xfe)
		{
			printtext(xpos, ypos + d, color, "??? ???");	// invalid command range. 
			detailedTableBaseLValue[p] = -1;
			detailedTableBaseRValue[p] = -1;
			displayRightTable = 0;
		}
		else
		{
			highlightOffset = 3;	// jump
			if (r)
				printtext(xpos, ypos + d, color, "JUMP:  ");
			else
				printtext(xpos, ypos + d, color, "STOP:  ");

			displayRightTable = 0;

			detailedTableBaseRValue[p] = -1;

			detailedTableLValue[p] = r;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xff;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0x0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)

			v2 = r;
		}

		if (v != 0)
		{
			sprintf(textbuffer, "%02X", v2);
			printtext(xpos + 8, ypos + d, color, textbuffer);
		}
		else
			printtext(xpos + 8, ypos + d, color, "--");


		printbg(OX + 4 + highlightOffset, ypos + d, getColor(CCOLOR_EXCLAMATION, CTABLE_UNUSED_BACKGROUND), 1);

		if (displayRightTable == 1)	// 1 = display Modulation speed (signed 8bit)
		{
			if (r <= 0x7f)
			{
				detailedTableRValue[p] = r;
				detailedTableMaxRValue[p] = 0x7f;
				detailedTableBaseRValue[p] = 0x00;
				detailedTableMinRValue[p] = 0;

				sprintf(textbuffer, "+%02X", r);
				printtext(xpos + 11, ypos + d, color, textbuffer);
			}
			else
			{
				r2 = 0x100 - r;
				detailedTableRValue[p] = r2;
				detailedTableMaxRValue[p] = 0x7f;
				detailedTableBaseRValue[p] = 0x00;
				detailedTableMinRValue[p] = 1;

				sprintf(textbuffer, "-%02X", r2);
				printtext(xpos + 11, ypos + d, color, textbuffer);
			}
			printbg(xpos + 11, ypos + d, getColor(CCOLOR_EXCLAMATION, CTABLE_UNUSED_BACKGROUND), 1);
		}
		else if (displayRightTable == 2)
		{

		}
		else
			printtext(xpos + 11, ypos + d, color, " --");

		UIUnderline = 0;


		if (editorInfo.etmarknum == FTBL)
		{
			if (editorInfo.etmarkstart <= editorInfo.etmarkend)
			{
				if ((p >= editorInfo.etmarkstart) && (p <= editorInfo.etmarkend))
				{
					printbg(OX + 17, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
					printbg(OX + 17 - 8, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 7);
					if (displayRightTable != 2)
						printbg(OX + 21, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
				}
			}
			else
			{
				if ((p <= editorInfo.etmarkstart) && (p >= editorInfo.etmarkend))
				{
					printbg(OX + 17, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
					printbg(OX + 17 - 8, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 7);
					if (displayRightTable != 2)
						printbg(OX + 21, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
				}
			}
		}

	}

	if (editorInfo.editmode == EDIT_TABLES)
	{
		if (!eamode)
		{
			editorInfo.cursorX = xpos + (editorInfo.etcolumn & 1) + editorInfo.etDetailedWaveTableColumn + (editorInfo.etcolumn / 2) * 4;
			int ctype = 0;
			if (ltable[FTBL][editorInfo.etpos] < 0x80 || ltable[FTBL][editorInfo.etpos] == 0xff)
				ctype = 1;
			else if (ltable[FTBL][editorInfo.etpos] >= 0x80 && ltable[FTBL][editorInfo.etpos] <= 0xfe && editorInfo.etcolumn < 2)
				ctype = 1;

			if (ctype == 1)
			{
				printbg(editorInfo.cursorX, ypos + editorInfo.etpos - editorInfo.etview[editorInfo.etnum], cc << 8, 1);
			}
			else
			{
				int f = editorInfo.cursorX - (xpos + 11);

				int filterOnOff = rtable[FTBL][editorInfo.etpos];
				filterOnOff &= 0x7;

				cc <<= 8;
				cc &= 0xff00;
				if (filterOnOff&(1 << f))
					cc |= CCOLOR_RED;
			}
		}
	}
}

/*
Pulsetable left side:  01-7F Pulse modulation step. Left side indicates time
							 and right side the speed (signed 8-bit value).
					   8X-FX Set pulse width. X is the high 4 bits, right
							 side tells the 8 low bits.
					   FF    Jump. Right side tells position ($00 = stop)

*/
void displayDetailedPulseTable(int cc, int OX, int OY)
{

	int xpos = OX + 9;
	int ypos = OY + 1;


	for (int d = 0; d < VISIBLETABLEROWS; d++)
	{
		int p = editorInfo.etview[PTBL] + d;
		int color = tableBackgroundColors[PTBL][p];
		UIUnderline = 0;


		printtext(xpos + 15, ypos + d, getColor(CTABLE_FOREGROUND1, CTABLE_BACKGROUND1), "      ");


		int v = ltable[PTBL][p];
		int v2 = v;
		if (v == 0xff)
			UIUnderline = UNDERLINE_FOREGROUND_MASK;

		int r = rtable[PTBL][p];
		int r2 = r;

		sprintf(textbuffer, "%02X:", p + 1);
		printtext(OX, ypos + d, color, textbuffer);

		printtext(OX + 4, ypos + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), "SMJ");
		int highlightOffset = 0;
		int displayRightTable = 0;


		if (v >= 1 && v <= 0x7f)	// modify pulse width
		{
			highlightOffset = 1;
			detailedTableLValue[p] = v;			// Left value = time
			detailedTableMaxLValue[p] = 0x7f;		// maximum possible value
			detailedTableMinLValue[p] = 1;
			detailedTableBaseLValue[p] = 0;		// base value to add to current to get new tableR[p] value
			displayRightTable = 1;
			printtext(xpos, ypos + d, color, "PLS MOD");
		}
		else if (v >= 0x80 && v <= 0x0fe)
		{
			displayRightTable = 0;
			detailedTableRValue[p] = -1;

			highlightOffset = 0;	// Resonance, filter On/Off and filter type.
			printtext(xpos, ypos + d, color, "PLS SET");

			v2 &= 0xf;
			v2 <<= 8;
			v2 |= r;
			sprintf(textbuffer, "%03X", v2);
			printtext(xpos + 8, ypos + d, color, textbuffer);
			v = -1;	// don't display anything else for left column


			// Will need to check ltable[p] to know how to set final value 
			detailedTableLValue[p] = v2;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xff;		// 0x3ff -- ignored anyway. maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0x0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)


		}
		else if (v == 0xff)
		{
			highlightOffset = 2;	// jump
			if (r)
				printtext(xpos, ypos + d, color, "JUMP:  ");
			else
				printtext(xpos, ypos + d, color, "STOP:  ");

			displayRightTable = 0;

			detailedTableBaseRValue[p] = -1;

			detailedTableLValue[p] = r;			// current L value. Modify this with HexInput
			detailedTableMaxLValue[p] = 0xff;		// maximum possible value (min is always 0)
			detailedTableMinLValue[p] = 0;
			detailedTableBaseLValue[p] = 0x0;		// base value to add to current to get new tableL[p] value (will need to check ltable[p] initially to see if we need to massage data further)

			v2 = r;
		}
		else
		{
			printtext(xpos, ypos + d, color, "??? ???");	// invalid command range. 
			detailedTableBaseLValue[p] = -1;
			detailedTableBaseRValue[p] = -1;
			displayRightTable = 0;
			v = 0;
		}


		if (v > 0)
		{
			sprintf(textbuffer, " %02X", v2);
			printtext(xpos + 8, ypos + d, color, textbuffer);
		}
		else if (v == 0)
			printtext(xpos + 8, ypos + d, color, " --");


		printbg(OX + 4 + highlightOffset, ypos + d, getColor(CCOLOR_EXCLAMATION, CTABLE_UNUSED_BACKGROUND), 1);


		if (displayRightTable == 1)	// 1 = display Modulation speed (signed 8bit)
		{
			if (r <= 0x7f)
			{
				detailedTableRValue[p] = r;
				detailedTableMaxRValue[p] = 0x7f;
				detailedTableBaseRValue[p] = 0x00;
				detailedTableMinRValue[p] = 0;

				sprintf(textbuffer, "+%02X", r);
				printtext(xpos + 12, ypos + d, color, textbuffer);
			}
			else
			{
				r2 = 0x100 - r;
				detailedTableRValue[p] = r2;
				detailedTableMaxRValue[p] = 0x7f;
				detailedTableBaseRValue[p] = 0x00;
				detailedTableMinRValue[p] = 1;

				sprintf(textbuffer, "-%02X", r2);
				printtext(xpos + 12, ypos + d, color, textbuffer);
			}

		}
		else //if (v >= 0)
		{
			int ut = UIUnderline;
			UIUnderline = 0;
			printtext(xpos + 12, ypos + d, color, " --");
			UIUnderline = ut;
		}
		printbg(xpos + 12, ypos + d, getColor(CCOLOR_EXCLAMATION, 0), 1);

		UIUnderline = 0;


		if (editorInfo.etmarknum == PTBL)
		{
			if (editorInfo.etmarkstart <= editorInfo.etmarkend)
			{
				if ((p >= editorInfo.etmarkstart) && (p <= editorInfo.etmarkend))
				{
					printbg(OX + 17, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 3);

					printbg(OX + 22, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);

					printbg(OX + 17 - 8, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 7);
				}
			}
			else
			{
				if ((p <= editorInfo.etmarkstart) && (p >= editorInfo.etmarkend))
				{
					printbg(OX + 17, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 3);

					printbg(OX + 22, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 2);
					printbg(OX + 17 - 8, ypos + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 7);
				}
			}
		}

	}

	if (editorInfo.editmode == EDIT_TABLES)
	{
		if (!eamode)
		{
			if (editorInfo.etcolumn <= 2)
			{
				if (ltable[PTBL][editorInfo.etpos] < 0x80 || ltable[PTBL][editorInfo.etpos] >0xfe)
				{
					if (editorInfo.etcolumn == 0)
						editorInfo.etcolumn = 1;
				}
				editorInfo.cursorX = xpos + (editorInfo.etcolumn % 3) + editorInfo.etDetailedWaveTableColumn;
			}
			else
				editorInfo.cursorX = xpos + ((1 + editorInfo.etcolumn) & 1) + editorInfo.etDetailedWaveTableColumn + (editorInfo.etcolumn / 3) * 5;
			int ctype = 1;	// was 0

			if (ctype == 1)
			{
				printbg(editorInfo.cursorX, ypos + editorInfo.etpos - editorInfo.etview[editorInfo.etnum], cc << 8, 1);
			}
		}
	}
}




void displayOriginalTableView(int cc, int OX, int OY)
{
	for (int c = 0; c < MAX_TABLES; c++)
	{
		displayTable(c, OX, OY);
	}

	if (editorInfo.editmode == EDIT_TABLES)
	{
		if (!eamode)
			printbg(OX + 3 + editorInfo.etnum * 10 + (editorInfo.etcolumn & 1) + (editorInfo.etcolumn / 2) * 3, OY + 1 + editorInfo.etpos - editorInfo.etview[editorInfo.etnum], cc << 8, 1);
	}
}


void displayTable(int c, int OX, int OY)
{
	for (int d = 0; d < VISIBLETABLEROWS; d++)
	{
		int p = editorInfo.etview[c] + d;

		int color = tableBackgroundColors[c][p];

		UIUnderline = 0;
		if ((c != 3 && ltable[c][p] == 0xff))
			UIUnderline = UNDERLINE_FOREGROUND_MASK;


		if ((p == editorInfo.etpos) && (editorInfo.etnum == c))
		{
			color &= 0xff00;
			color |= CORDER_INST_TABLE_EDITING;
		}


		sprintf(textbuffer, "%02X:%02X %02X", p + 1, ltable[c][p], rtable[c][p]);
		printtext(OX + 10 * c, OY + 1 + d, color, textbuffer);

		UIUnderline = 0;
		if (c < 3)
		{
			printtext(OX + (10 * (c + 1)) - 2, OY + 1 + d, getColor(0, CTABLE_UNUSED_BACKGROUND), "  ");
		}

		if (editorInfo.etmarknum == c)
		{
			if (editorInfo.etmarkstart <= editorInfo.etmarkend)
			{
				if ((p >= editorInfo.etmarkstart) && (p <= editorInfo.etmarkend))
					printbg(OX + 10 * c + 3, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 5);
			}
			else
			{
				if ((p <= editorInfo.etmarkstart) && (p >= editorInfo.etmarkend))
					printbg(OX + 10 * c + 3, OY + 1 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 5);
			}
		}
	}
}



int getWaveforumColour(int bit, int value)
{
	int cOFF = getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND);
	int cON = getColor(CCOLOR_EXCLAMATION, CTABLE_UNUSED_BACKGROUND);

	if (value & bit)
		return cON;
	return cOFF;
}

void displayWaveformInfo(int x, int y)
{

	if (!waveformDisplayInfo.displayOnOff)
		return;

	int cdivider = getColor(CINFO_FOREGROUND, CTRANSPORT_FOREGROUND);

	int xs = 0;
	for (int xp = 0;xp < 9;xp++)
	{
		xs = x + (xp * 5);
		printbyte(xs, y, cdivider, 0x1ff);
	}


	for (int xp = 0;xp < 4;xp++)
	{
		int c = getWaveforumColour(0x80 >> xp, waveformDisplayInfo.value);

		xs = x + (xp * 5);
		printbyterow(xs + 1, y, c, 0x18 + xp, 4);
	}
	xs += 6;


	int c = getWaveforumColour(0x8, waveformDisplayInfo.value);
	printtext(xs, y, c, "TEST");
	xs += 5;
	c = getWaveforumColour(0x4, waveformDisplayInfo.value);
	printtext(xs, y, c, "RING");
	xs += 5;
	c = getWaveforumColour(0x2, waveformDisplayInfo.value);
	printtext(xs, y, c, "SYNC");
	xs += 5;
	c = getWaveforumColour(0x1, waveformDisplayInfo.value);
	printtext(xs, y, c, "GATE");
}



void displayExpandedOrderList(GTOBJECT *gt, int cc, int OX, int OY)
{
	int maxChan = getVisibleChannelCount();
	int cursorVisualChannel = getEditorVisualOrderChannel();
	int markVisualStart = editorInfo.esmarkchn;
	int markVisualEnd = editorInfo.esmarkchnend;

	if (editorInfo.maxSIDChannels > 6)
	{
		if (markVisualStart >= 0)
			markVisualStart += (editorInfo.esnum & 1) * MAX_CHN;
		if (markVisualEnd >= 0)
			markVisualEnd += (editorInfo.esnum & 1) * MAX_CHN;
	}

	sprintf(textbuffer, "(SUBTUNE %02X, POS %03X)   ", editorInfo.esnum, editorInfo.eseditpos);

	printtext(OX + 15, OY, getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND), textbuffer);
	int lockPatternColor = getColor(CTITLES_FOREGROUND, CGENERAL_BACKGROUND);	//0xe;
	sprintf(textbuffer, "ORDERLIST ");
	printtext(OX + 5, OY, lockPatternColor, textbuffer);

	int color;

	for (int d = 0; d < EXTENDEDVISIBLEORDERLIST; d++)
	{
		color = getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);


		int p = editorInfo.esview + d;
		sprintf(textbuffer, "%03X", p);
		printtext(OX, OY + 2 + d, color, textbuffer);
		printbyte(OX + 3, OY + 2 + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), 0x1ff);
		printtext(OX, OY + 1, color, "   ");
		printbyte(OX + 3, OY + 1, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), 0x1ff);

		for (int c = 0; c < maxChan; c++)
		{
			int c2 = getVisualChannelActualChannel(c);	// 0-11
			int songNum = getVisualChannelSongNumber(c);
			int songCh = getVisualChannelLocalChannel(c);
			int playingSong = getActualSongNumber(editorInfo.esnum, c2);	// JP added this. Only highlight playing row if showing the right song


			if (d == 0)
			{
				UIUnderline = UNDERLINE_MASK;
				sprintf(textbuffer, "%02X ", c2);	// display channel number

				printtext(OX + 4 + (c * 6), OY + 1, getColor(15, CORDER_INST_BACKGROUND), textbuffer);
				int compressedSize = songCompressedSize[songNum][songCh];
				if (compressedSize <= 0xff)
					sprintf(textbuffer, "%02X", compressedSize);	// display channel compressed size
				else
					sprintf(textbuffer, "**");

				int cl = getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND);
				if (compressedSize >= 0xf0)
					cl = getColor(CCOLOR_RED, CORDER_INST_BACKGROUND);
				printtext(OX + 4 + 3 + (c * 6), OY + 1, cl, textbuffer);
				printbyte(OX + 4 + (c * 6) + 5, OY + 1, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), 0x1ff);	// Divider

				if (c2 == gt->masterLoopChannel)
				{
					printbyte(OX + 4 + 2 + (c * 6), OY + 1, getColor(14, CORDER_INST_BACKGROUND), 0x100);	// Master loop channel
				}
				UIUnderline = 0;
			}

			int pattern = songOrderPatterns[songNum][songCh][p];
			int transpose = songOrderTranspose[songNum][songCh][p];


			if (p < songOrderLength[songNum][songCh])
				color = getColor(CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);
			else
				color = getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND);
			//				color = getColor(0, 0);	//CORDER_INST_FOREGROUND, CORDER_INST_BACKGROUND);

			if (pattern != 0xff)
			{
				if (isplaying(gt))
				{
					int chnpos = gt->chn[c2].songptr;
					chnpos--;
					if (chnpos < 0)
						chnpos = 0;

					if ((p == chnpos) && (gt->chn[c2].advance))
					{
						if (songNum == playingSong)
							color = CPLAYING;
					}

				}

				if (p == gt->editorUndoInfo.editorInfo[c2].espos)
					color = CORDER_INST_TABLE_EDITING;

				int transValue = transpose & 0x7f;
				sprintf(textbuffer, "%02X ", pattern);	//, transValue);
				printtext(OX + 4 + (c * 6), OY + 2 + d, color, textbuffer);

				if (transpose & 0x80)
					sprintf(textbuffer, "-%01X", transValue);
				else
					sprintf(textbuffer, "+%01X", transValue);

				if (transValue != 0)
				{
					printtext(OX + 4 + (c * 6) + 3, OY + 2 + d, getColor(CORDER_TRANS_REPEAT, CORDER_INST_BACKGROUND), textbuffer);
				}
				else
					printtext(OX + 4 + (c * 6) + 3, OY + 2 + d, color, textbuffer);
				printbyte(OX + 4 + (c * 6) + 5, OY + 2 + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), 0x1ff);
			}
			else
			{
				sprintf(textbuffer, "%02X", pattern);
				printtext(OX + 4 + (c * 6), OY + 2 + d, getColor(CORDER_TRANS_REPEAT, CORDER_INST_BACKGROUND), textbuffer);
				sprintf(textbuffer, "%03X", transpose);
				printtext(OX + 4 + (c * 6) + 2, OY + 2 + d, getColor(14, CORDER_INST_BACKGROUND), textbuffer);
				printbyte(OX + 4 + (c * 6) + 5, OY + 2 + d, getColor(CTABLE_UNUSED_FOREGROUND, CTABLE_UNUSED_BACKGROUND), 0x1ff);
			}

			int ms = markVisualStart;
			int me = markVisualEnd;
			if ((ms >= 0) && (me >= 0) && (me < ms))
			{
				ms = me;
				me = markVisualStart;
			}

			if ((ms >= 0) && (me >= 0) && (c >= ms) && (c <= me))
			{

				if (editorInfo.esmarkstart <= editorInfo.esmarkend)
				{
					if ((p >= editorInfo.esmarkstart) && (p <= editorInfo.esmarkend))
					{
						printbg(OX + 4 + (c * 6), OY + 2 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 5);
					}
				}
				else
				{
					if ((p <= editorInfo.esmarkstart) && (p >= editorInfo.esmarkend))
					{
						printbg(OX + 4 + (c * 6), OY + 2 + d, getColor(CSELECT_TO_COPY_FOREGROUND, CSELECT_TO_COPY_BACKGROUND), 5);
					}
				}
			}


			// Flash cursor
			if ((p == editorInfo.eseditpos) && (editorInfo.editmode == EDIT_ORDERLIST) && (cursorVisualChannel == c))
			{
				if (!eamode)
				{
					if (editorInfo.escolumn < 2)	// pattern value
						printbg(OX + 4 + (c * 6) + (editorInfo.escolumn & 1), OY + 2 + d, cc << 8, 1);
					else
					{
						printbg(OX + 4 + (c * 6) + (editorInfo.escolumn - 2) + 2, OY + 2 + d, cc << 8, 1);
					}
				}
			}
		}
	}
}
