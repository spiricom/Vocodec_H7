/* USER CODE BEGIN Includes */
#include "oled.h"
#include "audiostream.h"
#include "gfx.h"
#include "ui.h"

GFX theGFX;
uint16_t* adcVals;
float knobVals[NUM_KNOBS];

char* modeNames[ModeCount*2];
static void initModeNames();

static void buttonWasPressed(VocodecButton button);
static void buttonWasReleased(VocodecButton button);
static void writeModeToLCD(VocodecMode in, UpDownMode ud);

uint8_t xPos;
uint8_t yPos;
uint8_t penWeight;
uint8_t penColor = 1;

tRamp* knobRamps[NUM_KNOBS];
float lastval[NUM_KNOBS];

UpDownMode upDownMode = ModeChange;
VocodecMode mode = DrawMode;

void UIInit(uint16_t* myADCArray)
{
	initModeNames();

	adcVals = myADCArray;

	writeModeToLCD(mode, upDownMode);
	for(int i = 0; i < NUM_KNOBS; i++)
	{
		knobRamps[i] = tRampInit(0.05f, 1);
		lastval[i] = 0.0f;
	}

}

void UIDrawFrame(void)
{
	xPos = (int) (adcVals[1] * INV_TWO_TO_16 * 128);
	yPos = (int) (32 - (adcVals[0] * INV_TWO_TO_16 * 32));
	penWeight = (int) (adcVals[2] * INV_TWO_TO_16 * 10);
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
		val = (int)(adcVals[(NUM_KNOBS-1)-i] * 0.00390625);
		if((int)val % 10 < 5)
		{
			val = (int)(val/10.0f);
			val = val/25.5f;
			tRampSetDest(knobRamps[i], val);
		}
		else
		{
			val = (int)((val+10)/10.0f);
			val = val/25.5f;
			tRampSetDest(knobRamps[i], val);
		}
		val = tRampTick(knobRamps[i]);
		knobVals[i] = val;
	}
}

#define ASCII_NUM_OFFSET 48
static void writeModeToLCD(VocodecMode in, UpDownMode ud)
{
	if (in == DrawMode) return;
	int i = in;
	if ((formantCorrect > 0) && (in != LevelMode)) i += ModeCount;
	else if (levelLock && (in == LevelMode)) i += ModeCount;
	OLEDwriteLine(modeNames[i], 10, FirstLine);
	OLEDclearLine(SecondLine);
	if (in == AutotuneNearestMode)
	{
		if (autotuneLock > 0) OLEDwriteLine("LOCK", 4, SecondLine);
	}
	else if (in == AutotuneAbsoluteMode)
	{
		OLEDwriteIntLine(activeShifters, 2, SecondLine);
	}
	else if (in == VocoderMode || in == SynthMode)
	{
		OLEDwriteIntLine(activeVoices, 2, SecondLine);
	}
	else if (in == BitcrusherMode)
	{
		OLEDwriteInt(bitDepth, 2, 76, SecondLine);
	}
	if (ud == ParameterChange)
	{
		OLEDwriteString("<", 1, 112, SecondLine);
	}
}

static void buttonWasPressed(VocodecButton button)
{
	int modex = (int) mode;

	if (button == ButtonUp)
	{
		if (upDownMode == ModeChange)
		{
			if (modex < ModeCount - 1) modex++;
			else modex = 0;
			OLEDclear();
		}
		else if (upDownMode == ParameterChange)
		{
			if (mode == AutotuneNearestMode)
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
			}
			else if (mode == AutotuneAbsoluteMode)
			{
				if (activeShifters < NUM_SHIFTERS) activeShifters++;
				else activeShifters = 1;
				//else activeShifters = NUM_SHIFTERS;
			}
			else if (mode == VocoderMode || mode == SynthMode)
			{
				if (activeVoices < NUM_VOICES) activeVoices++;
				else activeVoices = 1;
				//else activeVoices = NUM_VOICES;
			}
		}
	}
	else if (button == ButtonDown)
	{
		if (upDownMode == ModeChange)
		{
			if (modex > 0) modex--;
			else modex = ModeCount - 1;
			OLEDclear();
		}
		else if (upDownMode == ParameterChange)
		{
			if (mode == AutotuneAbsoluteMode)
			{
				if (activeShifters > 1) activeShifters--;
				else activeShifters = NUM_SHIFTERS;
				//else activeShifters = 1;
			}
			else if (mode == VocoderMode || mode == SynthMode)
			{
				if (activeVoices > 1) activeVoices--;
				else activeVoices = NUM_VOICES;
				//else activeVoices = 1;
			}
		}
	}
	else if (button == ButtonA)
	{
		if (mode == FormantShiftMode)
		{
			formantCorrect = (formantCorrect > 0) ? 0 : 1;
		}
		else if (mode == PitchShiftMode)
		{
			formantCorrect = (formantCorrect > 0) ? 0 : 1;
		}
		else if (mode == AutotuneAbsoluteMode)
		{
			formantCorrect = (formantCorrect > 0) ? 0 : 1;
		}
		else if (mode == DrawMode)
		{
			OLEDclear();
		}
		else if (mode == LevelMode)
		{
			levelLock = (levelLock == 0) ? 1 : 0;
		}
	}
	else if (button == ButtonB)
	{
		if (upDownMode == ModeChange)
		{
			if (mode == FormantShiftMode)
			{
				formantCorrect = (formantCorrect > 0) ? 0 : 1;
			}
			else if (mode == PitchShiftMode)
			{
				formantCorrect = (formantCorrect > 0) ? 0 : 1;
			}
			else if (mode == AutotuneNearestMode)
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
			else if (mode == AutotuneAbsoluteMode)
			{
				upDownMode = ParameterChange;
			}
			else if (mode == VocoderMode || mode == SynthMode)
			{
				upDownMode = ParameterChange;
			}
			else if (mode == DrawMode)
			{
				penColor = (penColor > 0) ? 0 : 1;
			}
		}
		else upDownMode = ModeChange;
	}

	mode = (VocodecMode) modex;

	writeModeToLCD(mode, upDownMode);
}

static void buttonWasReleased(VocodecButton button)
{

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
	modeNames[VocoderMode+ModeCount] = "V0CODER   ";

	modeNames[FormantShiftMode] = "FORMANT   ";
	modeNames[FormantShiftMode+ModeCount] = "F0RMANT   ";

	modeNames[PitchShiftMode] = "PITCHSHIFT";
	modeNames[PitchShiftMode+ModeCount] = "P0RCHSHIFT";

	modeNames[AutotuneNearestMode] = "AUTOTUNE1 ";
	modeNames[AutotuneNearestMode+ModeCount] = "AUTOTUNE1 ";

	modeNames[AutotuneAbsoluteMode] = "AUTOTUNE2 ";
	modeNames[AutotuneAbsoluteMode+ModeCount] = "AUTOTUNE2 ";

	modeNames[DelayMode] = "DELAY     ";
	modeNames[DelayMode+ModeCount] = "DELAY     ";

	modeNames[BitcrusherMode] = "BITCRUSHER ";
	modeNames[BitcrusherMode+ModeCount] = "BITCRUSHER ";

	modeNames[DrumboxMode] = "DRUMBIES  ";
	modeNames[DrumboxMode+ModeCount] = "DRUMB0X   ";

	modeNames[SynthMode] = "SYNTH     ";
	modeNames[SynthMode+ModeCount] = "SYNTH     ";

	modeNames[DrawMode] = "          ";
	modeNames[DrawMode+ModeCount] = "          ";

	modeNames[LevelMode] = "LEVEL     ";
	modeNames[LevelMode+ModeCount] = "LEVEL LOCK";
}
