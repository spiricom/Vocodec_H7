/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __UI_H
#define __UI_H

#define CHAIN_LENGTH 3
#define LINE_LENGTH 10
#define HYSTERESIS 0.05f

#define __KNOBCHECK1__ if (knobActive[0] > 0)
#define __KNOBCHECK2__ if (knobActive[1] > 0)
#define __KNOBCHECK3__ if (knobActive[2] > 0)
#define __KNOBCHECK4__ if (knobActive[3] > 0)

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

typedef enum KnobInteraction
{
	Hysteresis = 0,
	Matching,
	NilInteraction
} KnobInteraction;

typedef enum VocodecMode
{
	VocoderMode = 0,
	FormantShiftMode,
	PitchShiftMode,
	AutotuneNearestMode,
	AutotuneAbsoluteMode,
	DelayMode,
	DrumboxMode,
	ReverbMode,
	BitcrusherMode,
	SynthMode,
	DrawMode,
	LevelMode,
	ModeNil,
	ModeCount
} VocodecMode;

typedef enum VocodecScreen
{
	HomeScreen = 0,
	EditScreen,
	ScreenCount,
	ScreenNil
} VocodecScreen;

typedef enum LockState
{
	Unlocked = 0,
	Locked,
	LockNil
} LockState;

extern VocodecMode modeChain[CHAIN_LENGTH];
extern uint8_t chainIndex;
extern uint8_t indexChained[CHAIN_LENGTH];
extern VocodecScreen screen;

#define NUM_BUTTONS 16
#define NUM_KNOBS 4
uint8_t buttonValues[NUM_BUTTONS];
uint8_t buttonValuesPrev[NUM_BUTTONS];
uint32_t buttonCounters[NUM_BUTTONS];
uint32_t buttonPressed[NUM_BUTTONS];

extern GFX theGFX;

extern uint16_t* adcVals;
extern float knobVals[NUM_KNOBS];
extern tRamp* knobRamps[NUM_KNOBS];
extern uint8_t knobActive[NUM_KNOBS];


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
void knobCheck(void);

void OLEDdrawPoint(int16_t x, int16_t y, uint16_t color);
void OLEDdrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void OLEDdrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
void OLEDclear();
void OLEDclearLine(OLEDLine line);
void OLEDwriteString(char* myCharArray, uint8_t arrayLength, uint8_t startCursor, OLEDLine line, uint8_t invert);
void OLEDwriteLine(char* myCharArray, uint8_t arrayLength, OLEDLine line);
void OLEDwriteInt(uint32_t myNumber, uint8_t numDigits, uint8_t startCursor, OLEDLine line);
void OLEDwriteIntLine(uint32_t myNumber, uint8_t numDigits, OLEDLine line);
void OLEDwritePitch(float midi, uint8_t startCursor, OLEDLine line);
void OLEDwritePitchLine(float midi, OLEDLine line);
void OLEDwriteFixedFloat(float input, uint8_t numDigits, uint8_t numDecimal, uint8_t startCursor, OLEDLine line);
void OLEDwriteFixedFloatLine(float input, uint8_t numDigits, uint8_t numDecimal, OLEDLine line);

#endif /* __UI_H */
