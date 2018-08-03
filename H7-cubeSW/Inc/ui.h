/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __UI_H
#define __UI_H

typedef enum LCDModeType
{
	LCDModeDisplayPitchClass = 0,
	LCDModeDisplayPitchMidi,
	LCDModeTypeNil,
	LCDModeCount = LCDModeTypeNil
} LCDModeType;

typedef enum VocodecButton
{
	ButtonA = 0,
	ButtonB,
	ButtonUp,
	ButtonDown,
	ButtonNil
} VocodecButton;

typedef enum UpDownMode
{
	ModeChange = 0,
	ParameterChange,
	NilChange
} UpDownMode;

typedef enum VocodecMode
{
	VocoderMode = 0,
	FormantShiftMode,
	PitchShiftMode,
	AutotuneNearestMode,
	AutotuneAbsoluteMode,
	DelayMode,
	BitcrusherMode,
	DrumboxMode,
	SynthMode,
	DrawMode,
	LevelMode,
	ModeCount,
	ModeNil
} VocodecMode;

extern VocodecMode mode;
extern LCDModeType lcdMode;

uint8_t oled_buffer[32];


#define NUM_BUTTONS 16
uint8_t buttonValues[NUM_BUTTONS];
uint8_t buttonValuesPrev[NUM_BUTTONS];
uint32_t buttonCounters[NUM_BUTTONS];
uint32_t buttonPressed[NUM_BUTTONS];

extern GFX theGFX;

extern uint16_t* adcVals;

typedef enum _OLEDLine
{
	FirstLine = 0,
	SecondLine,
	BothLines,
	NilLine
} OLEDLine;

void UIInit(uint16_t* myADCArray);

void buttonCheck(void);
void processKnobs(void);


void OLEDdrawPoint(int16_t x, int16_t y, uint16_t color);
void OLEDdrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void OLEDdrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void OLEDclear();
void OLEDclearLine(OLEDLine line);
void OLEDwriteString(char* myCharArray, uint8_t arrayLength, uint8_t startCursor, OLEDLine line);
void OLEDwriteLine(char* myCharArray, uint8_t arrayLength, OLEDLine line);
void OLEDwriteInt(uint32_t myNumber, uint8_t numDigits, uint8_t startCursor, OLEDLine line);
void OLEDwriteIntLine(uint32_t myNumber, uint8_t numDigits, OLEDLine line);
void OLEDwritePitch(float midi, uint8_t startCursor, OLEDLine line);
void OLEDwritePitchLine(float midi, OLEDLine line);
void OLEDwriteFixedFloat(float input, uint8_t numDigits, uint8_t numDecimal, uint8_t startCursor, OLEDLine line);
void OLEDwriteFixedFloatLine(float input, uint8_t numDigits, uint8_t numDecimal, OLEDLine line);

#endif /* __UI_H */
