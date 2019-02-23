/* USER CODE BEGIN Includes */
#include "oled.h"
#include "audiostream.h"
#include "gfx.h"
#include "ui.h"

GFX theGFX;
char oled_buffer[32];

uint16_t* adcVals;
float knobVals[NUM_KNOBS];
tRamp* knobRamps[NUM_KNOBS];
float lastval[NUM_KNOBS];
uint8_t knobActive[NUM_KNOBS];
float knobValsOnModeChange[NUM_KNOBS];
float knobValsPerMode[ModeCount][NUM_KNOBS];
KnobInteraction knobInteraction = Hysteresis;
char* knobNamesPerMode[ModeCount][NUM_KNOBS];
uint8_t lastKnob;
uint8_t buttonActive;

uint8_t buttonsHeld[NUM_BUTTONS];

char* modeNames[ModeCount];
char* shortModeNames[ModeCount];
char homeScreenString[LINE_LENGTH];


static void initKnobs(void);
static void resetKnobs(void);
static void buttonWasPressed(VocodecButton button);
static void upButtonWasPressed(void);
static void downButtonWasPressed(void);
static void aButtonWasPressed(void);
static void bButtonWasPressed(void);
static void buttonWasReleased(VocodecButton button);
static void writeScreen(void);
static void initModeNames(void);
static void setAvailableModes(void);


uint8_t xPos;
uint8_t yPos;
uint8_t penWeight;
uint8_t penColor = 1;

VocodecScreen screen = HomeScreen;
VocodecMode modeChain[CHAIN_LENGTH];
uint8_t chainIndex = 0;
uint8_t indexChained[CHAIN_LENGTH];
uint8_t modeAvail[CHAIN_LENGTH][ModeCount];
VocodecButton lastButtonPressed = ButtonNil;

char* formantEmojis[9] =
{
	":O        ",
	" :0       ",
	"  :o      ",
	"   :o     ",
	"    :*    ",
	"     :|   ",
	"      :|  ",
	"       :) ",
	"        :D"
};

char* harmonizerKeys[10] =
{
	"   C",
	"C#Db",
	"   D",
	"D#Eb",
	"   E",
	"   F",
	"F#Gb",
	"G#Ab",
	"A#Bb",
	"   B"
};

char* harmonizerScales[2] =
{
	"M",
	"m"
};

char harmonizerParams[5];

void UIInit(uint16_t* myADCArray)
{
	float val;

	initModeNames();

	adcVals = myADCArray;

	for (int i = 0; i < CHAIN_LENGTH; i++)
	{
		modeChain[i] = ModeNil;
		indexChained[i] = 1;
	}
	modeChain[0] = HarmonizeMode;
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
	initKnobs();
	//buttonActive = 0;
}

void UIDrawFrame(void)
{
	__KNOBCHECK2__ penWeight = (int) (knobVals[1] * 10);
	__KNOBCHECK3__ xPos = (int) (knobVals[2] * 128);
	__KNOBCHECK4__ yPos = (int) (32 - (knobVals[3] * 32));
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
			//if the second knob is used and active, it keeps overriding the other knobs
			if(knobActive[i] > 0)
			{
				lastKnob = i;
				//buttonActive = 0;
			}
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
	if (knobInteraction == Hysteresis)
	{
		float diff;
		for(int i = 0; i < NUM_KNOBS; ++i)
		{
			diff = fabsf(knobVals[i] - knobValsOnModeChange[i]);
			if (diff > HYSTERESIS)
			{
				knobActive[i] = 1;
			}
		}
	}
	else if (knobInteraction == Matching)
	{
		float diff;
		for(int i = 0; i < NUM_KNOBS; ++i)
		{
			diff = fabsf(knobVals[i] - knobValsPerMode[modeChain[chainIndex]][i]);
			if (diff < HYSTERESIS) knobActive[i] = 1;
			lastKnob = i;
		}
	}
}

void displayScreen(void)
{
	if (screen == HomeScreen)
	{
		if (modeChain[chainIndex] == VocoderMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[VocoderMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[VocoderMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[VocoderMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(glideTimeVoc*0.001f, 4, 3, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteKRangeFixedFloat(lpFreqVoc, 4, 1, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(detuneMaxVoc, 4, 2, 64, SecondLine);
		}
		else if (modeChain[chainIndex] == FormantShiftMode)
		{
			//display NONE for non-formant knobs
			if(strcmp(knobNamesPerMode[FormantShiftMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[FormantShiftMode][lastKnob], 4, SecondLine);
			else
			{
				GFXfillRect(&theGFX, 0, 16, 128, 16, 0);
				int emojiIndex = (int)(formantKnob * 8);
				int pixel = (int)(formantKnob * 8 * 12);
				if (pixel > 104) pixel = 104;
				OLEDwriteString(formantEmojis[emojiIndex], 10, pixel%12, SecondLine, 0);
			}
		}
		else if (modeChain[chainIndex] == PitchShiftMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[PitchShiftMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[PitchShiftMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[PitchShiftMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 2) OLEDwriteFixedFloat(pitchFactor, 4, 2, 64, SecondLine);
			else if(lastKnob == 0)
			{
				//handle negative/positive values
				if(formantShiftFactorPS >= 0.0f) OLEDwriteFixedFloat(formantShiftFactorPS, 4, 2, 64, SecondLine);
				else if(formantShiftFactorPS < 0.0f)
				{
					OLEDwriteString("-", 1, 64, SecondLine, 0);
					OLEDwriteFixedFloat((formantShiftFactorPS*(-1.0f)), 3, 2, 76, SecondLine);
				}
			}
		}
		else if (modeChain[chainIndex] == AutotuneAbsoluteMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[AutotuneAbsoluteMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[AutotuneAbsoluteMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[AutotuneAbsoluteMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(glideTimeAuto*0.001f, 4, 3, 64, SecondLine); //displayed in seconds
		}
		else if (modeChain[chainIndex] == AutotuneNearestMode)
		{
			//display name of knob parameter or NONE
			if(autotuneLock == 0) OLEDclearLine(SecondLine);
		}
		else if (modeChain[chainIndex] == HarmonizeMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[HarmonizeMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[HarmonizeMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[HarmonizeMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0 || lastKnob == 1)
			{
				strcpy(harmonizerParams, harmonizerKeys[harmonizerKey]);
				strcat(harmonizerParams, harmonizerScales[harmonizerScale]);
				OLEDwriteString(harmonizerKeys[harmonizerKey], 5, 64, SecondLine, 0);
			}
			else
			{
				OLEDwriteInt(sungNote, 2, 48, SecondLine);
				OLEDwriteInt(playedNote, 2, 96, SecondLine);
			}
		}
		else if (modeChain[chainIndex] == DelayMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[DelayMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[DelayMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[DelayMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteKRangeFixedFloat(hpFreqDel, 4, 1, 64, SecondLine);
			else if(lastKnob == 1) OLEDwriteKRangeFixedFloat(lpFreqDel, 4, 1, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteFixedFloat(newDelay*oops.invSampleRate, 4, 3, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(newFeedback, 4, 2, 64, SecondLine);
			/*
			OLEDwriteFixedFloat(newDelay*oops.invSampleRate, 4, 3, 4, SecondLine);
			OLEDwriteFixedFloat(newFeedback, 3, 2, 76, SecondLine);
			*/
		}
		else if (modeChain[chainIndex] == ReverbMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[ReverbMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[ReverbMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[ReverbMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteKRangeFixedFloat(hpFreqRev, 4, 1, 64, SecondLine);
			else if(lastKnob == 1) OLEDwriteKRangeFixedFloat(lpFreqRev, 4, 1, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteFixedFloat(t60, 4, 2, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(revMix, 4, 3, 64, SecondLine);
		}
		else if (modeChain[chainIndex] == DrumboxMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[DrumboxMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[DrumboxMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[DrumboxMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(decayCoeff*0.001, 3, 2, 64, SecondLine); //displayed in seconds
			//best solution so far, maybe put everything in smaller range?
			else if(lastKnob == 1) OLEDwriteKRangeFixedFloat(newFreqDB, 4, 1, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteFixedFloat(newDelayDB*oops.invSampleRate, 4, 3, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(newFeedbackDB, 4, 3, 64, SecondLine);
			/*
			OLEDwriteFixedFloat(newDelayDB*oops.invSampleRate, 4, 3, 4, SecondLine);
			OLEDwriteFixedFloat(newFeedbackDB, 3, 2, 76, SecondLine);
			*/
		}
		else if (modeChain[chainIndex] == BitcrusherMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[BitcrusherMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[BitcrusherMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[BitcrusherMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 2) OLEDwriteInt(rateRatio, 3, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteInt(bitDepth, 3, 64, SecondLine);
			/*
			OLEDwriteInt(rateRatio, 3, 4, SecondLine);
			OLEDwriteInt(bitDepth, 3, 76, SecondLine);
			*/
		}
		else if (modeChain[chainIndex] == SynthMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[SynthMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[SynthMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[SynthMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(glideTimeSynth*0.001f, 4, 3, 64, SecondLine); //displayed in seconds
			else if(lastKnob == 1) OLEDwriteFixedFloat(synthGain, 4, 3, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteKRangeFixedFloat(lpFreqSynth, 4, 1, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(detuneMaxSynth, 4, 2, 64, SecondLine);
		}
		else if (modeChain[chainIndex] == LevelMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[LevelMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[LevelMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[LevelMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 2) OLEDwriteFixedFloat(inputLevel, 3, 2, 76, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(outputLevel, 3, 2, 76, SecondLine);
			/*
			OLEDwriteFixedFloat(inputLevel, 3, 2, 4, SecondLine);
			OLEDwriteFixedFloat(outputLevel, 3, 2, 76, SecondLine);
			*/
		}
	}
	else if (screen == EditScreen)
	{
		if (modeChain[chainIndex] == VocoderMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[VocoderMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[VocoderMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[VocoderMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(glideTimeVoc*0.001f, 4, 3, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteKRangeFixedFloat(lpFreqVoc, 4, 1, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(detuneMaxVoc, 4, 2, 64, SecondLine);
		}
		else if (modeChain[chainIndex] == FormantShiftMode)
		{
			//display NONE for non-formant knobs
			if(strcmp(knobNamesPerMode[FormantShiftMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[FormantShiftMode][lastKnob], 4, SecondLine);
			else
			{
				GFXfillRect(&theGFX, 0, 16, 128, 16, 0);
				int emojiIndex = (int)(formantKnob * 8);
				int pixel = (int)(formantKnob * 8 * 12);
				if (pixel > 104) pixel = 104;
				OLEDwriteString(formantEmojis[emojiIndex], 10, pixel%12, SecondLine, 0);
			}
		}
		else if (modeChain[chainIndex] == PitchShiftMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[PitchShiftMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[PitchShiftMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[PitchShiftMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 2) OLEDwriteFixedFloat(pitchFactor, 4, 2, 64, SecondLine);
			else if(lastKnob == 0)
			{
				//handle negative/positive values
				if(formantShiftFactorPS >= 0.0f) OLEDwriteFixedFloat(formantShiftFactorPS, 4, 2, 64, SecondLine);
				else if(formantShiftFactorPS < 0.0f)
				{
					OLEDwriteString("-", 1, 64, SecondLine, 0);
					OLEDwriteFixedFloat((formantShiftFactorPS*(-1.0f)), 3, 2, 76, SecondLine);
				}
			}
		}
		else if (modeChain[chainIndex] == AutotuneAbsoluteMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[AutotuneAbsoluteMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[AutotuneAbsoluteMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[AutotuneAbsoluteMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(glideTimeAuto*0.001f, 4, 3, 64, SecondLine); //displayed in seconds
		}
		else if (modeChain[chainIndex] == AutotuneNearestMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[AutotuneNearestMode][lastKnob], "NONE")==0 && autotuneLock == 0) OLEDwriteLine(knobNamesPerMode[AutotuneNearestMode][lastKnob], 4, SecondLine);
		}
		else if (modeChain[chainIndex] == DelayMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[DelayMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[DelayMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[DelayMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteKRangeFixedFloat(hpFreqDel, 4, 1, 64, SecondLine);
			else if(lastKnob == 1) OLEDwriteKRangeFixedFloat(lpFreqDel, 4, 1, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteFixedFloat(newDelay*oops.invSampleRate, 4, 3, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(newFeedback, 4, 2, 64, SecondLine);
			/*
			OLEDwriteFixedFloat(newDelay*oops.invSampleRate, 4, 3, 4, SecondLine);
			OLEDwriteFixedFloat(newFeedback, 3, 2, 76, SecondLine);
			*/
		}
		else if (modeChain[chainIndex] == ReverbMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[ReverbMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[ReverbMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[ReverbMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteKRangeFixedFloat(hpFreqRev, 4, 1, 64, SecondLine);
			else if(lastKnob == 1) OLEDwriteKRangeFixedFloat(lpFreqRev, 4, 1, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteFixedFloat(t60, 4, 2, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(revMix, 4, 3, 64, SecondLine);
		}
		else if (modeChain[chainIndex] == DrumboxMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[DrumboxMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[DrumboxMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[DrumboxMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(decayCoeff*0.001, 3, 2, 64, SecondLine); //displayed in seconds
			else if(lastKnob == 1) OLEDwriteKRangeFixedFloat(newFreqDB, 4, 1, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteFixedFloat(newDelayDB*oops.invSampleRate, 4, 3, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(newFeedbackDB, 4, 3, 64, SecondLine);
			/*
			OLEDwriteFixedFloat(newDelayDB*oops.invSampleRate, 4, 3, 4, SecondLine);
			OLEDwriteFixedFloat(newFeedbackDB, 3, 2, 76, SecondLine);
			*/
		}
		else if (modeChain[chainIndex] == BitcrusherMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[BitcrusherMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[BitcrusherMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[BitcrusherMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 2) OLEDwriteInt(rateRatio, 3, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteInt(bitDepth, 3, 64, SecondLine);
			/*
			OLEDwriteInt(rateRatio, 3, 4, SecondLine);
			OLEDwriteInt(bitDepth, 3, 76, SecondLine);
			*/
		}
		else if (modeChain[chainIndex] == SynthMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[SynthMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[SynthMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[SynthMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 0) OLEDwriteFixedFloat(glideTimeSynth*0.001f, 4, 3, 64, SecondLine); //displayed in seconds
			else if(lastKnob == 1) OLEDwriteFixedFloat(synthGain, 4, 3, 64, SecondLine);
			else if(lastKnob == 2) OLEDwriteKRangeFixedFloat(lpFreqSynth, 4, 1, 64, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(detuneMaxSynth, 4, 2, 64, SecondLine);
		}
		else if (modeChain[chainIndex] == LevelMode)
		{
			//display name of knob parameter or NONE
			if(strcmp(knobNamesPerMode[LevelMode][lastKnob], "NONE")==0) OLEDwriteLine(knobNamesPerMode[LevelMode][lastKnob], 4, SecondLine);
			else OLEDwriteString(knobNamesPerMode[LevelMode][lastKnob], 4, 4, SecondLine, 0);

			//display knob values if they correspond to a parameter
			if(lastKnob == 2) OLEDwriteFixedFloat(inputLevel, 3, 2, 76, SecondLine);
			else if(lastKnob == 3) OLEDwriteFixedFloat(outputLevel, 3, 2, 76, SecondLine);
			/*
			OLEDwriteFixedFloat(inputLevel, 3, 2, 4, SecondLine);
			OLEDwriteFixedFloat(outputLevel, 3, 2, 76, SecondLine);
			*/
		}
		else if (modeChain[chainIndex] == DrawMode)
		{
			UIDrawFrame();
		}
	}
}

static void initKnobs(void)
{
	for (int i = 0; i < NUM_KNOBS; i++)
	{
		knobValsOnModeChange[i] = knobVals[i];
		//for (int j = 0; j < ModeCount; j++) knobValsPerMode[j][i] = 0.0f;
		knobActive[i] = 0;
	}

	knobNamesPerMode[VocoderMode][0] = "GLID";
	knobNamesPerMode[VocoderMode][1] = "NONE";
	knobNamesPerMode[VocoderMode][2] = "LPHZ";
	knobNamesPerMode[VocoderMode][3] = "DETN";

	knobNamesPerMode[FormantShiftMode][0] = "NONE";
	knobNamesPerMode[FormantShiftMode][1] = "NONE";
	knobNamesPerMode[FormantShiftMode][2] = "FMNT";
	knobNamesPerMode[FormantShiftMode][3] = "NONE";

	knobNamesPerMode[PitchShiftMode][0] = "FMNT";
	knobNamesPerMode[PitchShiftMode][1] = "NONE";
	knobNamesPerMode[PitchShiftMode][2] = "PTCH";
	knobNamesPerMode[PitchShiftMode][3] = "NONE";

	knobNamesPerMode[AutotuneAbsoluteMode][0] = "GLID";
	knobNamesPerMode[AutotuneAbsoluteMode][1] = "NONE";
	knobNamesPerMode[AutotuneAbsoluteMode][2] = "NONE";
	knobNamesPerMode[AutotuneAbsoluteMode][3] = "NONE";

	knobNamesPerMode[AutotuneNearestMode][0] = "NONE";
	knobNamesPerMode[AutotuneNearestMode][1] = "NONE";
	knobNamesPerMode[AutotuneNearestMode][2] = "NONE";
	knobNamesPerMode[AutotuneNearestMode][3] = "NONE";

	knobNamesPerMode[HarmonizeMode][0] = "KEY";
	knobNamesPerMode[HarmonizeMode][1] = "SCL";
	knobNamesPerMode[HarmonizeMode][2] = "CPLX";
	knobNamesPerMode[HarmonizeMode][3] = "HEAT";

	knobNamesPerMode[DelayMode][0] = "HPHZ";
	knobNamesPerMode[DelayMode][1] = "LPHZ";
	knobNamesPerMode[DelayMode][2] = "DLAY";
	knobNamesPerMode[DelayMode][3] = "FDBK";

	knobNamesPerMode[ReverbMode][0] = "HPHZ";
	knobNamesPerMode[ReverbMode][1] = "LPHZ";
	knobNamesPerMode[ReverbMode][2] = "t60 ";
	knobNamesPerMode[ReverbMode][3] = "MIX ";

	knobNamesPerMode[BitcrusherMode][0] = "NONE";
	knobNamesPerMode[BitcrusherMode][1] = "NONE";
	knobNamesPerMode[BitcrusherMode][2] = "SR  ";
	knobNamesPerMode[BitcrusherMode][3] = "BD  ";

	knobNamesPerMode[DrumboxMode][0] = "DCAY";
	knobNamesPerMode[DrumboxMode][1] = "FREQ";
	knobNamesPerMode[DrumboxMode][2] = "DLAY";
	knobNamesPerMode[DrumboxMode][3] = "FDBK";

	knobNamesPerMode[SynthMode][0] = "GLID";
	knobNamesPerMode[SynthMode][1] = "GAIN";
	knobNamesPerMode[SynthMode][2] = "LPHZ";
	knobNamesPerMode[SynthMode][3] = "DETN";

	knobNamesPerMode[LevelMode][0] = "NONE";
	knobNamesPerMode[LevelMode][1] = "NONE";
	knobNamesPerMode[LevelMode][2] = "IN  ";
	knobNamesPerMode[LevelMode][3] = "OUT ";
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

#define ASCII_NUM_OFFSET 48
static void writeScreen(void)
{
	if (screen == HomeScreen)
	{
		for (int i = 0; i < LINE_LENGTH; i++)
		{
			homeScreenString[i] = ' ';
		}
		for (int i = 0; i < CHAIN_LENGTH; i++)
		{
			if (indexChained[i]) homeScreenString[i*3] = '`';
			else homeScreenString[i*3] = '|';
			homeScreenString[(i*3)+1] = shortModeNames[modeChain[i]][0];
			homeScreenString[(i*3)+2] = shortModeNames[modeChain[i]][1];
		}
		if (buttonsHeld[ButtonA] > 0)
		{
			//homeScreenString[9] = '{';
		}
		else
		{
			homeScreenString[9] = ' ';
		}
		//OLEDclearLine(SecondLine);
		if (modeChain[chainIndex] == AutotuneNearestMode)
		{
			if (autotuneLock > 0) OLEDwriteLine("LOCK", 4, SecondLine);
		}
		else if (modeChain[chainIndex] == AutotuneAbsoluteMode || modeChain[chainIndex] == VocoderMode || modeChain[chainIndex] == SynthMode)
		{
			//OLEDwriteIntLine(numActiveVoices[modeChain[chainIndex]], 1, SecondLine);
			OLEDwriteInt(numActiveVoices[modeChain[chainIndex]], 1, 116, FirstLine);
		}
		else
		{
			OLEDclearLine(SecondLine);
			OLEDwriteString(" ", 1, 116, FirstLine, 0);
		}

		OLEDwriteString(homeScreenString, 9, 5, FirstLine, 0);
		OLEDwriteString(&homeScreenString[(chainIndex*3)+1], 2, 4+(12*((chainIndex*3)+1)), FirstLine, 1);
	}
	else if (screen == EditScreen)
	{
		if (modeChain[chainIndex] == DrawMode)
		{
			OLEDclear();
			return;
		}
		OLEDwriteLine(modeNames[modeChain[chainIndex]], 10, FirstLine);
		if (indexChained[chainIndex]) OLEDwriteString("`", 1, 102, FirstLine, 0);
		else OLEDwriteString("|", 1, 102, FirstLine, 0);
		//if (buttonsHeld[ButtonA] > 0) OLEDwriteString("{", 1, 112, FirstLine, 0);
		OLEDclearLine(SecondLine);
		if (modeChain[chainIndex] == AutotuneNearestMode)
		{
			if (autotuneLock > 0) OLEDwriteLine("LOCK", 4, SecondLine);
		}
		else if (modeChain[chainIndex] == AutotuneAbsoluteMode || modeChain[chainIndex] == VocoderMode || modeChain[chainIndex] == SynthMode)
		{
			OLEDwriteInt(numActiveVoices[modeChain[chainIndex]], 1, 116, FirstLine);
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
	/*
	if (screen == HomeScreen)
	{
		if (buttonsHeld[ButtonA] > 0)
		{
			if (knobInteraction == Hysteresis)
			{
				knobInteraction = Matching;
				OLEDclear();
				OLEDwriteLine("MATCHING  ", 10, FirstLine);
			}
			else if (knobInteraction == Matching)
			{
				knobInteraction = Hysteresis;
				OLEDclear();
				OLEDwriteLine("HYSTERESIS", 10, FirstLine);
			}


			return;
		}
	}
	else if (screen == EditScreen)
	{
		if (buttonsHeld[ButtonA] > 0)
		{
			screen = HomeScreen;
			writeScreen();
			return;
		}
	} */
	//buttonActive = 1;
	if (buttonsHeld[ButtonA] > 0)
	{
		resetKnobs();
		for (int i = 0; i < ModeCount; i++)
		{
			if (modeChain[chainIndex] < ModeNil) modeChain[chainIndex]++;
			else modeChain[chainIndex] = VocoderMode;
			if (modeAvail[chainIndex][modeChain[chainIndex]] > 0) break;
		}
		setAvailableModes();
		writeScreen();
	}
	else
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
}

static void downButtonWasPressed()
{
	/*
	if (screen == HomeScreen)
	{
		if (buttonsHeld[ButtonA] > 0)
		{
			if (modeChain[chainIndex] != ModeNil)
			{
				screen = EditScreen;
				writeScreen();
			}
			return;
		}
	}
	else if (screen == EditScreen)
	{
		if (buttonsHeld[ButtonA] > 0) return;
	} */
	//buttonActive = 1;
	if (buttonsHeld[ButtonA] > 0)
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
	else
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
}

static void aButtonWasPressed()
{
	//writeScreen();
}

static void bButtonWasPressed()
{
	if (buttonsHeld[ButtonA] > 0)
	{
		indexChained[chainIndex] = (indexChained[chainIndex] > 0) ? 0 : 1;
		writeScreen();
		return;
	}
	resetKnobs();
	if (chainIndex < (CHAIN_LENGTH - 1)) chainIndex++;
	else chainIndex = 0;
	writeScreen();
}

static void buttonWasReleased(VocodecButton button)
{
	buttonsHeld[button] = 0;
	if (button == ButtonA)
	{
		if (lastButtonPressed == ButtonA)
		{
			screen = (screen == HomeScreen) ? EditScreen : HomeScreen;
		}
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

void OLEDwriteString(char* myCharArray, uint8_t arrayLength, uint8_t startCursor, OLEDLine line, uint8_t invert)
{
	uint8_t cursorX = startCursor;
	uint8_t cursorY = 15 + (16 * (line%2));
	GFXsetCursor(&theGFX, cursorX, cursorY);

	GFXfillRect(&theGFX, startCursor, line*16, arrayLength*12, (line*16)+16, invert);
	GFXsetTextColor(&theGFX, 1-invert, invert);
	for (int i = 0; i < arrayLength; ++i)
	{
		GFXwrite(&theGFX, myCharArray[i]);
	}
	GFXsetTextColor(&theGFX, 1, 0);
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

	OLEDwriteString(oled_buffer, len, startCursor, line, 0);
}

void OLEDwriteIntLine(uint32_t myNumber, uint8_t numDigits, OLEDLine line)
{
	int len = OLEDparseInt(oled_buffer, myNumber, numDigits);

	OLEDwriteLine(oled_buffer, len, line);
}

void OLEDwritePitch(float midi, uint8_t startCursor, OLEDLine line)
{
	int len = OLEDparsePitch(oled_buffer, midi);

	OLEDwriteString(oled_buffer, len, startCursor, line, 0);
}

void OLEDwritePitchLine(float midi, OLEDLine line)
{
	int len = OLEDparsePitch(oled_buffer, midi);

	OLEDwriteLine(oled_buffer, len, line);
}

void OLEDwriteFixedFloat(float input, uint8_t numDigits, uint8_t numDecimal, uint8_t startCursor, OLEDLine line)
{
	int len = OLEDparseFixedFloat(oled_buffer, input, numDigits, numDecimal);

	OLEDwriteString(oled_buffer, len, startCursor, line, 0);
}

void OLEDwriteFixedFloatLine(float input, uint8_t numDigits, uint8_t numDecimal, OLEDLine line)
{
	int len = OLEDparseFixedFloat(oled_buffer, input, numDigits, numDecimal);

	OLEDwriteLine(oled_buffer, len, line);
}

void OLEDwriteKRangeFixedFloat(float input, uint8_t numDigits, uint8_t numDecimal, uint8_t startCursor, OLEDLine line)
{
	if(input >= 100000.0f)
	{
		input *= 0.001;
		OLEDwriteInt(input, numDigits-1, startCursor+12, line);
		OLEDwriteString("k", 1, startCursor+(numDigits*12), SecondLine, 0);
	}
	if(input >= 10000.0f)
	{
		input *= 0.001;
		OLEDwriteFixedFloat(input, numDigits-1, numDecimal, startCursor, line);
		OLEDwriteString("k", 1, startCursor+(numDigits*12), SecondLine, 0);
	}
	else if(input >= 1000.0f)
	{
			input *= 0.001;
			OLEDwriteFixedFloat(input, numDigits-1, numDecimal+1, startCursor, line);
			OLEDwriteString("k", 1, startCursor+(numDigits*12), SecondLine, 0);
	}
	else
	{
		OLEDwriteFixedFloat(input, numDigits, numDecimal, startCursor, line);
	}
}


static void initModeNames(void)
{
	modeNames[VocoderMode] = "VOCODER   ";
	shortModeNames[VocoderMode] = "VC";
	modeNames[FormantShiftMode] = "FORMANT   ";
	shortModeNames[FormantShiftMode] = "FS";
	modeNames[PitchShiftMode] = "PSHIFT    ";
	shortModeNames[PitchShiftMode] = "PS";
	modeNames[AutotuneNearestMode] = "NEARTUNE  ";
	shortModeNames[AutotuneNearestMode] = "A1";
	modeNames[AutotuneAbsoluteMode] = "ABSLTUNE  ";
	shortModeNames[AutotuneAbsoluteMode] = "A2";
	modeNames[HarmonizeMode] = "HARMNZ  ";
	shortModeNames[HarmonizeMode] = "HZ";
	modeNames[DelayMode] = "DELAY     ";
	shortModeNames[DelayMode] = "DL";
	modeNames[ReverbMode] = "REVERB    ";
	shortModeNames[ReverbMode] = "RV";
	modeNames[BitcrusherMode] = "BITCRUSH  ";
	shortModeNames[BitcrusherMode] = "BC";
	modeNames[DrumboxMode] = "DRUMBOX   ";
	shortModeNames[DrumboxMode] = "DB";
	modeNames[SynthMode] = "SYNTH     ";
	shortModeNames[SynthMode] = "SY";
	modeNames[DrawMode] = "DRAW      ";
	shortModeNames[DrawMode] = "DW";
	modeNames[LevelMode] = "LEVEL     ";
	shortModeNames[LevelMode] = "LV";
	modeNames[ModeNil] = "EMPTY     ";
	shortModeNames[ModeNil] = "  ";
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
