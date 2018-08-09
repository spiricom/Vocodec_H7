/* USER CODE BEGIN Includes */
#include "oled.h"
#include "audiostream.h"
#include "gfx.h"
#include "ui.h"

GFX theGFX;
uint16_t* adcVals;
float knobVals[NUM_KNOBS];
uint8_t knobActive[NUM_KNOBS];
float knobValsOnModeChange[NUM_KNOBS];
float knobValsPerMode[ModeCount][NUM_KNOBS];

uint8_t buttonsHeld[NUM_BUTTONS];

char* modeNames[ModeCount];
char* shortModeNames[ModeCount];
char homeScreenString[LINE_LENGTH];
static void initModeNames();

static void buttonWasPressed(VocodecButton button);
static void upButtonWasPressed(void);
static void downButtonWasPressed(void);
static void aButtonWasPressed(void);
static void bButtonWasPressed(void);
static void buttonWasReleased(VocodecButton button);
static void writeScreen(void);
static void setAvailableModes(void);
static void resetKnobs(void);

uint8_t xPos;
uint8_t yPos;
uint8_t penWeight;
uint8_t penColor = 1;

tRamp* knobRamps[NUM_KNOBS];
float lastval[NUM_KNOBS];

VocodecScreen screen = HomeScreen;
VocodecMode modeChain[CHAIN_LENGTH];
LockState chainLock = Locked;
uint8_t chainIndex = 0;
uint8_t modeAvail[CHAIN_LENGTH][ModeCount];
VocodecButton lastButtonPressed = ButtonNil;

void UIInit(uint16_t* myADCArray)
{
	float val;

	initModeNames();

	adcVals = myADCArray;

	for (int i = 0; i < CHAIN_LENGTH; i++)
	{
		modeChain[i] = ModeNil;
	}
	modeChain[0] = VocoderMode;
	setAvailableModes();

	writeScreen();

	for(int i = 0; i < NUM_KNOBS; i++)
	{
		knobRamps[i] = tRampInit(100.0f, 1);

		val = (int) (adcVals[(NUM_KNOBS-1)-i] * 0.00390625f);
		lastval[i] = val;
		val /= 255.0f;
		if (val < 0.025f) val = 0.025;
		else if (val > 0.975f) val = 0.975f;
		val = (val - 0.025f) / 0.95f;
		tRampSetDest(knobRamps[i], val);
		tRampSetVal(knobRamps[i], val);
		knobVals[i] = val;
	}
	resetKnobs();
}

void UIDrawFrame(void)
{
	xPos = (int) (knobVals[2] * 128);
	yPos = (int) (32 - (knobVals[3] * 32));
	penWeight = (int) (knobVals[1] * 10);
	OLEDdrawCircle(xPos, yPos, penWeight, penColor);
}

#define BUTTON_HYSTERESIS 4
void buttonCheck(void)
{
	buttonValues[0] = !HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1);
	buttonValues[1] = !HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9);
	buttonValues[2] = !HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14);
	buttonValues[3] = !HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12);

	for (int i = 0; i < 4; i++)
	{
		if (buttonValues[i] != buttonValuesPrev[i])
		{
			if (buttonCounters[i] < BUTTON_HYSTERESIS)
			{
				buttonCounters[i]++;
			}
			else
			{
				if (buttonValues[i] == 1)
				{
					buttonPressed[i] = 1;
					buttonWasPressed(i);
				}
				else
				{
					buttonPressed[i] = 0;
					buttonWasReleased(i);
				}
				buttonValuesPrev[i] = buttonValues[i];
				buttonCounters[i] = 0;
			}
		}
	}
}

void processKnobs(void)
{
	float val;
	for(int i = 0; i < NUM_KNOBS; ++i)
	{
		val = (int) (adcVals[(NUM_KNOBS-1)-i] * 0.00390625f);
		if (fabsf(lastval[i] - val) >= 2.0f)
		{
			lastval[i] = val;
			val /= 255.0f;
			if (val < 0.025f) val = 0.025;
			else if (val > 0.975f) val = 0.975f;
			val = (val - 0.025f) / 0.95f;
			tRampSetDest(knobRamps[i], val);
		}
	}
}

void knobCheck(void)
{
	float diff;
	for(int i = 0; i < NUM_KNOBS; ++i)
	{
		diff = fabsf(knobVals[i] - knobValsOnModeChange[i]);
		if (diff > HYSTERESIS) knobActive[i] = 1;
	}
}

#define ASCII_NUM_OFFSET 48
static void writeScreen(void)
{
	if (screen == HomeScreen)
	{
		for (int i = 0; i < LINE_LENGTH; i++)
		{
			homeScreenString[i] = ' ';
		}
		homeScreenString[chainIndex*3] = '>';
		for (int i = 0; i < CHAIN_LENGTH; i++)
		{
			homeScreenString[(i*3)+1] = shortModeNames[modeChain[i]][0];
			homeScreenString[(i*3)+2] = shortModeNames[modeChain[i]][1];
		}
		if (chainLock == Locked)
		{
			homeScreenString[9] = '}';
			if (modeChain[chainIndex] == AutotuneNearestMode)
			{
				if (autotuneLock > 0) OLEDwriteLine("LOCK", 4, SecondLine);
			}
			else if (modeChain[chainIndex] == AutotuneAbsoluteMode || modeChain[chainIndex] == VocoderMode || modeChain[chainIndex] == SynthMode)
			{
				OLEDwriteIntLine(numActiveVoices[modeChain[chainIndex]], 2, SecondLine);
			}
			else OLEDclearLine(SecondLine);
		}
		else
		{
			homeScreenString[9] = '{';
			OLEDwriteLine(modeNames[modeChain[chainIndex]], 10, SecondLine);
		}
		OLEDwriteLine(homeScreenString, 10, FirstLine);
	}
	else if (screen == EditScreen)
	{
		if (modeChain[chainIndex] == DrawMode) return;
		OLEDwriteLine(modeNames[modeChain[chainIndex]], 10, FirstLine);
		if (chainLock > 0) OLEDwriteString("}", 1, 112, FirstLine);
		else OLEDwriteString("{", 1, 112, FirstLine);
		OLEDclearLine(SecondLine);
		if (modeChain[chainIndex] == AutotuneNearestMode)
		{
			if (autotuneLock > 0) OLEDwriteLine("LOCK", 4, SecondLine);
		}
		else if (modeChain[chainIndex] == AutotuneAbsoluteMode || modeChain[chainIndex] == VocoderMode || modeChain[chainIndex] == SynthMode)
		{
			OLEDwriteIntLine(numActiveVoices[modeChain[chainIndex]], 2, SecondLine);
		}
	}
}

static void buttonWasPressed(VocodecButton button)
{
	buttonsHeld[button] = 1;
	if (button == ButtonUp) upButtonWasPressed();
	else if (button == ButtonDown) downButtonWasPressed();
	else if (button == ButtonA) aButtonWasPressed();
	else if (button == ButtonB) bButtonWasPressed();
	lastButtonPressed = button;
}

static void upButtonWasPressed()
{
	if (screen == HomeScreen)
	{
		if (buttonsHeld[ButtonA] > 0) return;
	}
	else if (screen == EditScreen)
	{
		if (buttonsHeld[ButtonA] > 0)
		{
			screen = HomeScreen;
			modeAvail[0][ModeNil] = 1;
			modeAvail[1][ModeNil] = 1;
			modeAvail[2][ModeNil] = 1;
			writeScreen();
			return;
		}
	}
	if (chainLock == Locked)
	{
		if (modeChain[chainIndex] == AutotuneNearestMode)
		{
			int notesHeld = 0;
			for (int i = 0; i < 12; ++i)
			{
				if (chordArray[i] > 0) { notesHeld = 1; }
			}

			if (notesHeld)
			{
				for (int i = 0; i < 12; ++i)
				{
					lockArray[i] = chordArray[i];
				}
			}

			autotuneLock = 1;
			writeScreen();
		}
		if (modeChain[chainIndex] == AutotuneAbsoluteMode)
		{
			if (numActiveVoices[modeChain[chainIndex]] < NUM_SHIFTERS) numActiveVoices[modeChain[chainIndex]]++;
			else numActiveVoices[modeChain[chainIndex]] = 1;
			//else numActiveVoices[mode] = NUM_SHIFTERS;
			writeScreen();
		}
		else if (modeChain[chainIndex] == VocoderMode || modeChain[chainIndex] == SynthMode)
		{
			if (numActiveVoices[modeChain[chainIndex]] < NUM_VOICES) numActiveVoices[modeChain[chainIndex]]++;
			else numActiveVoices[modeChain[chainIndex]] = 1;
			//else numActiveVoices[mode] = NUM_VOICES;
			writeScreen();
		}
	}
	else
	{
		resetKnobs();
		for (int i = 0; i < ModeCount; i++)
		{
			if (modeChain[chainIndex] < ModeNil) modeChain[chainIndex]++;
			else modeChain[chainIndex] = 0;
			if (modeAvail[chainIndex][modeChain[chainIndex]] > 0) break;
		}
		setAvailableModes();
		writeScreen();
	}
}

static void downButtonWasPressed()
{
	if (screen == HomeScreen)
	{
		if (buttonsHeld[ButtonA] > 0)
		{
			if (modeChain[chainIndex] != ModeNil)
			{
				screen = EditScreen;
				modeAvail[0][ModeNil] = 0;
				modeAvail[1][ModeNil] = 0;
				modeAvail[2][ModeNil] = 0;
				writeScreen();
			}
			return;
		}
	}
	else if (screen == EditScreen)
	{
		if (buttonsHeld[ButtonA] > 0) return;
	}
	if (chainLock == Locked)
	{
		if (modeChain[chainIndex] == AutotuneNearestMode)
		{
			int notesHeld = 0;
			for (int i = 0; i < 12; ++i)
			{
				if (chordArray[i] > 0) { notesHeld = 1; }
			}

			if (notesHeld)
			{
				for (int i = 0; i < 12; ++i)
				{
					lockArray[i] = chordArray[i];
				}
			}

			autotuneLock = 0;
			writeScreen();
		}
		else if (modeChain[chainIndex] == AutotuneAbsoluteMode)
		{
			if (numActiveVoices[modeChain[chainIndex]] > 1) numActiveVoices[modeChain[chainIndex]]--;
			else numActiveVoices[modeChain[chainIndex]] = NUM_SHIFTERS;
			//else activeShifters = 1;
			writeScreen();
		}
		else if (modeChain[chainIndex] == VocoderMode || modeChain[chainIndex] == SynthMode)
		{
			if (numActiveVoices[modeChain[chainIndex]] > 1) numActiveVoices[modeChain[chainIndex]]--;
			else numActiveVoices[modeChain[chainIndex]] = NUM_VOICES;
			//else activeVoices = 1;
			writeScreen();
		}
	}
	else
	{
		resetKnobs();
		for (int i = 0; i < ModeCount; i++)
		{
			if (modeChain[chainIndex] > 0) modeChain[chainIndex]--;
			else modeChain[chainIndex] = ModeNil;
			if (modeAvail[chainIndex][modeChain[chainIndex]] > 0) break;
		}
		setAvailableModes();
		writeScreen();
	}
}

static void aButtonWasPressed()
{
	// does everything on release
}

static void bButtonWasPressed()
{
	if (screen == HomeScreen)
	{
		resetKnobs();
		if (chainIndex < (CHAIN_LENGTH - 1)) chainIndex++;
		else chainIndex = 0;
		writeScreen();
	}
}

static void buttonWasReleased(VocodecButton button)
{
	buttonsHeld[button] = 0;
	if ((button == ButtonA) && (lastButtonPressed == ButtonA))
	{
		chainLock = (chainLock == Locked) ? Unlocked : Locked;
		writeScreen();
	}
}

void OLEDdrawPoint(int16_t x, int16_t y, uint16_t color)
{
	GFXwritePixel(&theGFX, x, y, color);
	ssd1306_display_full_buffer();
}

void OLEDdrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
	GFXwriteLine(&theGFX, x0, y0, x1, y1, color);
	ssd1306_display_full_buffer();
}

void OLEDdrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
	GFXfillCircle(&theGFX, x0, y0, r, color);
	ssd1306_display_full_buffer();
}


void OLEDclear()
{
	GFXfillRect(&theGFX, 0, 0, 128, 32, 0);
	ssd1306_display_full_buffer();
}

void OLEDclearLine(OLEDLine line)
{
	GFXfillRect(&theGFX, 0, (line%2)*16, 128, 16*((line/2)+1), 0);
	ssd1306_display_full_buffer();
}

void OLEDwriteString(char* myCharArray, uint8_t arrayLength, uint8_t startCursor, OLEDLine line)
{
	uint8_t cursorX = startCursor;
	uint8_t cursorY = 15 + (16 * (line%2));
	GFXsetCursor(&theGFX, cursorX, cursorY);

	GFXfillRect(&theGFX, startCursor, line*16, arrayLength*12, (line*16)+16, 0);
	for (int i = 0; i < arrayLength; ++i)
	{
		GFXwrite(&theGFX, myCharArray[i]);
	}
	ssd1306_display_full_buffer();
}

void OLEDwriteLine(char* myCharArray, uint8_t arrayLength, OLEDLine line)
{
	if (line == FirstLine)
	{
		GFXfillRect(&theGFX, 0, 0, 128, 16, 0);
		GFXsetCursor(&theGFX, 4, 15);
	}
	else if (line == SecondLine)
	{
		GFXfillRect(&theGFX, 0, 16, 128, 16, 0);
		GFXsetCursor(&theGFX, 4, 31);
	}
	else if (line == BothLines)
	{
		GFXfillRect(&theGFX, 0, 0, 128, 32, 0);
		GFXsetCursor(&theGFX, 4, 15);
	}
	for (int i = 0; i < arrayLength; ++i)
	{
		GFXwrite(&theGFX, myCharArray[i]);
	}
	ssd1306_display_full_buffer();
}

void OLEDwriteInt(uint32_t myNumber, uint8_t numDigits, uint8_t startCursor, OLEDLine line)
{
	int len = OLEDparseInt(oled_buffer, myNumber, numDigits);

	OLEDwriteString(oled_buffer, len, startCursor, line);
}

void OLEDwriteIntLine(uint32_t myNumber, uint8_t numDigits, OLEDLine line)
{
	int len = OLEDparseInt(oled_buffer, myNumber, numDigits);

	OLEDwriteLine(oled_buffer, len, line);
}

void OLEDwritePitch(float midi, uint8_t startCursor, OLEDLine line)
{
	int len = OLEDparsePitch(oled_buffer, midi);

	OLEDwriteString(oled_buffer, len, startCursor, line);
}

void OLEDwritePitchLine(float midi, OLEDLine line)
{
	int len = OLEDparsePitch(oled_buffer, midi);

	OLEDwriteLine(oled_buffer, len, line);
}

void OLEDwriteFixedFloat(float input, uint8_t numDigits, uint8_t numDecimal, uint8_t startCursor, OLEDLine line)
{
	int len = OLEDparseFixedFloat(oled_buffer, input, numDigits, numDecimal);

	OLEDwriteString(oled_buffer, len, startCursor, line);
}

void OLEDwriteFixedFloatLine(float input, uint8_t numDigits, uint8_t numDecimal, OLEDLine line)
{
	int len = OLEDparseFixedFloat(oled_buffer, input, numDigits, numDecimal);

	OLEDwriteLine(oled_buffer, len, line);
}

static void initModeNames()
{
	modeNames[VocoderMode] = "VOCODER   ";
	shortModeNames[VocoderMode] = "VC";
	modeNames[FormantShiftMode] = "FORMANT   ";
	shortModeNames[FormantShiftMode] = "FS";
	modeNames[PitchShiftMode] = "PITCHSHIFT";
	shortModeNames[PitchShiftMode] = "PS";
	modeNames[AutotuneNearestMode] = "AUTOTUNE1 ";
	shortModeNames[AutotuneNearestMode] = "A1";
	modeNames[AutotuneAbsoluteMode] = "AUTOTUNE2 ";
	shortModeNames[AutotuneAbsoluteMode] = "A2";
	modeNames[DelayMode] = "DELAY     ";
	shortModeNames[DelayMode] = "DL";
	modeNames[ReverbMode] = "REVERB    ";
	shortModeNames[ReverbMode] = "RV";
	modeNames[BitcrusherMode] = "BITCRUSHER ";
	shortModeNames[BitcrusherMode] = "BC";
	modeNames[DrumboxMode] = "DRUMBOX   ";
	shortModeNames[DrumboxMode] = "DB";
	modeNames[SynthMode] = "SYNTH     ";
	shortModeNames[SynthMode] = "SY";
	modeNames[DrawMode] = "DRAW      ";
	shortModeNames[DrawMode] = "DW";
	modeNames[LevelMode] = "LEVEL     ";
	shortModeNames[LevelMode] = "LV";
	modeNames[ModeNil] = "          ";
	shortModeNames[ModeNil] = "--";
}

static void setAvailableModes(void)
{
	for (int i = 0; i < CHAIN_LENGTH; i++)
	{
		for (int j = 0; j < ModeCount; j++)
		{
			modeAvail[i][j] = 1;
		}
	}
	for (int i = 0; i < CHAIN_LENGTH; i++)
	{
		if (modeChain[i] == VocoderMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][VocoderMode] = 0;
				modeAvail[(i+j)%3][AutotuneNearestMode] = 0;
				modeAvail[(i+j)%3][AutotuneAbsoluteMode] = 0;
				modeAvail[(i+j)%3][SynthMode] = 0;
			}
		}
		else if (modeChain[i] == FormantShiftMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][FormantShiftMode] = 0;
			}
		}
		else if (modeChain[i] == PitchShiftMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][PitchShiftMode] = 0;
				modeAvail[(i+j)%3][AutotuneNearestMode] = 0;
				modeAvail[(i+j)%3][AutotuneAbsoluteMode] = 0;
			}
		}
		else if (modeChain[i] == AutotuneNearestMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][VocoderMode] = 0;
				modeAvail[(i+j)%3][PitchShiftMode] = 0;
				modeAvail[(i+j)%3][AutotuneNearestMode] = 0;
				modeAvail[(i+j)%3][AutotuneAbsoluteMode] = 0;
				modeAvail[(i+j)%3][SynthMode] = 0;
			}
		}
		else if (modeChain[i] == AutotuneAbsoluteMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][VocoderMode] = 0;
				modeAvail[(i+j)%3][PitchShiftMode] = 0;
				modeAvail[(i+j)%3][AutotuneNearestMode] = 0;
				modeAvail[(i+j)%3][AutotuneAbsoluteMode] = 0;
				modeAvail[(i+j)%3][SynthMode] = 0;
			}
		}
		else if (modeChain[i] == DelayMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][DelayMode] = 0;
				modeAvail[(i+j)%3][DrumboxMode] = 0;
			}
		}
		else if (modeChain[i] == ReverbMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][ReverbMode] = 0;
			}
		}
		else if (modeChain[i] == BitcrusherMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][BitcrusherMode] = 0;
			}
		}
		else if (modeChain[i] == DrumboxMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][DelayMode] = 0;
				modeAvail[(i+j)%3][DrumboxMode] = 0;
			}
		}
		else if (modeChain[i] == SynthMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][VocoderMode] = 0;
				modeAvail[(i+j)%3][AutotuneNearestMode] = 0;
				modeAvail[(i+j)%3][AutotuneAbsoluteMode] = 0;
				modeAvail[(i+j)%3][SynthMode] = 0;
			}
		}
		else if (modeChain[i] == DrawMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][DrawMode] = 0;
			}
		}
		else if (modeChain[i] == LevelMode)
		{
			for (int j = 1; j < CHAIN_LENGTH; j++)
			{
				modeAvail[(i+j)%3][LevelMode] = 0;
			}
		}
	}
}

static void resetKnobs(void)
{
	for (int i = 0; i < NUM_KNOBS; i++)
	{
		knobValsOnModeChange[i] = knobVals[i];
		if (knobActive[i] > 0) knobValsPerMode[modeChain[chainIndex]][i] = knobVals[i];
		knobActive[i] = 0;
	}
}
