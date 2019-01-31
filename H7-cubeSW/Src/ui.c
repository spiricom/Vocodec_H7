/* USER CODE BEGIN Includes */
#include "oled.h"
#include "audiostream.h"
#include "gfx.h"
#include "ui.h"
#include "leaf.h"

GFX theGFX;
uint16_t* adcVals;


tRamp knobRamps[NUM_KNOBS];
float knobVals[NUM_KNOBS];
float lastval[NUM_KNOBS];

uint8_t buttonsHeld[NUM_BUTTONS];


static void buttonWasPressed(VocodecButton button);
static void upButtonWasPressed(void);
static void downButtonWasPressed(void);
static void aButtonWasPressed(void);
static void bButtonWasPressed(void);
static void buttonWasReleased(VocodecButton button);

uint8_t xPos;
uint8_t yPos;
uint8_t penWeight;
uint8_t penColor = 1;

void UIInit(uint16_t* myADCArray)
{
	float val;

	adcVals = myADCArray;

	for(int i = 0; i < NUM_KNOBS; i++)
	{
		tRamp_init(&knobRamps[i], 20.0f, 1);

		val = (int) (adcVals[(NUM_KNOBS-1)-i] * 0.00390625f);

		lastval[i] = val;

		val /= 255.0f;

		if (val < 0.025f) val = 0.025;
		else if (val > 0.975f) val = 0.975f;

		val = (val - 0.025f) / 0.95f;

		tRamp_setDest(&knobRamps[i], val);
		tRamp_setVal(&knobRamps[i], val);
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
			tRamp_setDest(&knobRamps[i], val);
		}
	}
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

#define ASCII_NUM_OFFSET 48


static void buttonWasPressed(VocodecButton button)
{
	buttonsHeld[button] = 1;
	if (button == ButtonUp) upButtonWasPressed();
	else if (button == ButtonDown) downButtonWasPressed();
	else if (button == ButtonA) aButtonWasPressed();
	else if (button == ButtonB) bButtonWasPressed();
}

static void upButtonWasPressed()
{

}

static void downButtonWasPressed()
{

}

static void aButtonWasPressed()
{
	tSample_start(&sample1);
}

static void bButtonWasPressed()
{
	tSample_start(&sample2);
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
