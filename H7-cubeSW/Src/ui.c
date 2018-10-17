/* USER CODE BEGIN Includes */
#include "oled.h"
#include "audiostream.h"
#include "gfx.h"
#include "ui.h"

GFX theGFX;
uint16_t* adcVals;
float knobVals[NUM_KNOBS];

uint8_t buttonsHeld[NUM_BUTTONS];

char* modeNames[FullModeCount];
char* shortModeNames[ModeCount];
char* chainLockString = " }  }  }  ";
static void initModeNames();

static void buttonWasPressed(VocodecButton button);
static void upButtonWasPressed(void);
static void downButtonWasPressed(void);
static void aButtonWasPressed(void);
static void bButtonWasPressed(void);
static void buttonWasReleased(VocodecButton button);
static void writeModeToLCD(VocodecMode in, UpDownMode ud);

uint8_t xPos;
uint8_t yPos;
uint8_t penWeight;
uint8_t penColor = 1;

tRamp* knobRamps[NUM_KNOBS];
float lastval[NUM_KNOBS];

UpDownMode upDownMode = ModeChange;
VocodecMode displayMode = VocoderMode;
VocodecMode modeChain[CHAIN_LENGTH];
uint8_t chainLock[CHAIN_LENGTH];
uint8_t chainIndex = 0;
uint8_t modeAvail[ModeCount];

void UIInit(uint16_t* myADCArray)
{
	float val;

	initModeNames();

	adcVals = myADCArray;

	for (int i = 0; i < CHAIN_LENGTH; i++)
	{
		modeChain[i] = ModeNil;
		chainLock[i] = 0;
	}
	modeChain[0] = displayMode;

	for (int i = 0; i < ModeCount; i++)
	{
		modeAvail[i] = 1;
	}

	writeModeToLCD(displayMode, upDownMode);
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
	}
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

#define ASCII_NUM_OFFSET 48
static void writeModeToLCD(VocodecMode in, UpDownMode ud)
{
	if (in == DrawMode) return;
	OLEDwriteLine(modeNames[in], 10, FirstLine);
	if (in == ChainEditMode)
	{

		return;
	}
	if (knobLock[in] > 0) OLEDwriteString("}", 1, 112, FirstLine);
	if (in == AutotuneNearestMode)
	{
		if (autotuneLock > 0) OLEDwriteLine("LOCK", 4, SecondLine);
	}
	else if (in == AutotuneAbsoluteMode || in == VocoderMode || in == SynthMode)
	{
		OLEDwriteIntLine(numActiveVoices[in], 2, SecondLine);
		if (ud == ParameterChange)
		{
			OLEDwriteString("<", 1, 28, SecondLine);
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

	writeModeToLCD(displayMode, upDownMode);
}

static void upButtonWasPressed()
{
	if (upDownMode == ModeChange)
	{
		knobLock[displayMode] = 1;
		for (int i = 0; i < ModeCount; i++)
		{
			if (displayMode < ModeCount - 1) displayMode++;
			else displayMode = 0;
			if (modeAvail[displayMode] > 0) break;
		}
		if (chainLock[chainIndex] == 0) modeChain[chainIndex] = displayMode;
		OLEDclear();
	}
	else if (upDownMode == ParameterChange)
	{
		if (displayMode == AutotuneAbsoluteMode)
		{
			if (numActiveVoices[displayMode] < NUM_SHIFTERS) numActiveVoices[displayMode]++;
			else numActiveVoices[displayMode] = 1;
			//else numActiveVoices[mode] = NUM_SHIFTERS;
		}
		else if (displayMode == VocoderMode || displayMode == SynthMode)
		{
			if (numActiveVoices[displayMode] < NUM_VOICES) numActiveVoices[displayMode]++;
			else numActiveVoices[displayMode] = 1;
			//else numActiveVoices[mode] = NUM_VOICES;
		}
	}
}

static void downButtonWasPressed()
{
	if (upDownMode == ModeChange)
	{
		knobLock[displayMode] = 1;
		for (int i = 0; i < ModeCount; i++)
		{
			if (displayMode > 0) displayMode--;
			else displayMode = ModeCount - 1;
			if (modeAvail[displayMode] > 0) break;
		}
		if (chainLock[chainIndex] == 0) modeChain[chainIndex] = displayMode;
		OLEDclear();
	}
	else if (upDownMode == ParameterChange)
	{
		if (displayMode == AutotuneAbsoluteMode)
		{
			if (numActiveVoices[displayMode] > 1) numActiveVoices[displayMode]--;
			else numActiveVoices[displayMode] = NUM_SHIFTERS;
			//else activeShifters = 1;
		}
		else if (displayMode == VocoderMode || displayMode == SynthMode)
		{
			if (numActiveVoices[displayMode] > 1) numActiveVoices[displayMode]--;
			else numActiveVoices[displayMode] = NUM_VOICES;
			//else activeVoices = 1;
		}
	}
}

static void aButtonWasPressed()
{

	grabBuffer();


	if (buttonsHeld[ButtonB] > 0)
	{
		if (displayMode == ChainEditMode)
		{
			displayMode = modeChain[0];
			upDownMode = ModeChange;
		}
		else
		{
			displayMode = ChainEditMode;
			upDownMode = ParameterChange;
		}
	}
	else if (knobLock[displayMode] > 0) knobLock[displayMode] = 0;
	else
	{
		if (displayMode == PitchShiftMode || displayMode == AutotuneNearestMode || displayMode == AutotuneAbsoluteMode)
		{
			formantCorrect[displayMode] = (formantCorrect[displayMode] > 0) ? 0 : 1;
		}
		else if (displayMode == DrawMode)
		{
			OLEDclear();
		}
	}
}

static void bButtonWasPressed()
{
	if (upDownMode == ModeChange)
	{
		if (displayMode == AutotuneNearestMode)
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

			autotuneLock = (autotuneLock > 0) ? 0 : 1;
		}
		else if (displayMode == AutotuneAbsoluteMode)
		{
			upDownMode = ParameterChange;
		}
		else if (displayMode == VocoderMode || displayMode == SynthMode)
		{
			upDownMode = ParameterChange;
		}
		else if (displayMode == DrawMode)
		{
			penColor = (penColor > 0) ? 0 : 1;
		}
	}
	else if (upDownMode == ParameterChange)
	{
		if (displayMode == ChainEditMode)
		{

		}
		else upDownMode = ModeChange;
	}
}

static void buttonWasReleased(VocodecButton button)
{
	buttonsHeld[button] = 0;
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
	modeNames[DrawMode] = "          ";
	shortModeNames[DrawMode] = "DW";
	modeNames[LevelMode] = "LEVEL     ";
	shortModeNames[LevelMode] = "LV";

	modeNames[ChainEditMode] = ">VC -- -- ";
}
