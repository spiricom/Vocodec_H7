/* Includes ------------------------------------------------------------------*/
#include "audiostream.h"
#include "main.h"
#include "codec.h"

// align is to make sure they are lined up with the data boundaries of the cache 
// at(0x3....) is to put them in the D2 domain of SRAM where the DMA can access them
// (otherwise the TX times out because the DMA can't see the data location) -JS

#define NUM_FB_DELAY_TABLES 8

int32_t audioOutBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;
int32_t audioInBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;

float audioTickL(float audioIn); 
float audioTickR(float audioIn);
void buttonCheck(void);

HAL_StatusTypeDef transmit_status;
HAL_StatusTypeDef receive_status;

uint8_t levelLock = 1;
uint8_t autotuneLock = 0;
uint8_t formantCorrect = 0;

uint8_t xPos;
uint8_t yPos;
uint8_t penWeight;
uint8_t penColor = 1;

UpDownMode upDownMode = ModeChange;
VocodecMode mode = DrawMode;

int activeVoices = 1;
int activeShifters = 1;

uint16_t* adcVals;
void (*frameFunctions[ModeCount])(uint8_t);
int32_t  (*tickFunctions[ModeCount])(int32_t, uint8_t, uint8_t);
char* modeNames[ModeCount*2];

/**********************************************/

typedef enum BOOL {
	FALSE = 0,
	TRUE
} BOOL;

static void writeModeToLCD(VocodecMode in, UpDownMode ud);

void noteOn(int key, int velocity)
{
	if (!velocity)
	{
		SFXNoteOff(key, velocity);
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);    //LED
	}
	else
	{
		SFXNoteOn(key, velocity);
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);    //LED3
	}
}

void noteOff(int key, int velocity)
{
	SFXNoteOff(key, velocity);
	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);    //LED3
}

void ctrlInput(int ctrl, int value)
{

}

void initModeNames()
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

void initFunctionPointers()
{
	frameFunctions[VocoderMode] = VocoderFrame;
	tickFunctions[VocoderMode] = VocoderTick;

	frameFunctions[FormantShiftMode] = FormantFrame;
	tickFunctions[FormantShiftMode] = FormantTick;

	frameFunctions[PitchShiftMode] = PitchShiftFrame;
	tickFunctions[PitchShiftMode] = PitchShiftTick;

	frameFunctions[AutotuneNearestMode] = AutotuneNearestFrame;
	tickFunctions[AutotuneNearestMode] = AutotuneNearestTick;

	frameFunctions[AutotuneAbsoluteMode] = AutotuneAbsoluteFrame;
	tickFunctions[AutotuneAbsoluteMode] = AutotuneAbsoluteTick;

	frameFunctions[DelayMode] = DelayFrame;
	tickFunctions[DelayMode] = DelayTick;

	frameFunctions[BitcrusherMode] = BitcrusherFrame;
	tickFunctions[BitcrusherMode] = BitcrusherTick;

	frameFunctions[DrumboxMode] = DrumboxFrame;
	tickFunctions[DrumboxMode] = DrumboxTick;

	frameFunctions[SynthMode] = SynthFrame;
	tickFunctions[SynthMode] = SynthTick;

	frameFunctions[LevelMode] = LevelFrame;
	tickFunctions[LevelMode] = LevelTick;
}


void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand, uint16_t* myADCArray)
{
	// Initialize the audio library. OOPS.
	SFXInit(SAMPLE_RATE, AUDIO_FRAME_SIZE, myADCArray);

	//now to send all the necessary messages to the codec
	AudioCodec_init(hi2c);

	HAL_Delay(100);

	initModeNames();
	initFunctionPointers();

	adcVals = myADCArray;

	for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
	{
		audioOutBuffer[i] = 0;
	}
	// set up the I2S driver to send audio data to the codec (and retrieve input as well)
	transmit_status = HAL_SAI_Transmit_DMA(hsaiOut, (uint8_t *)&audioOutBuffer[0], AUDIO_BUFFER_SIZE);
	receive_status = HAL_SAI_Receive_DMA(hsaiIn, (uint8_t *)&audioInBuffer[0], AUDIO_BUFFER_SIZE);

	writeModeToLCD(mode, upDownMode);
}

int numSamples = AUDIO_FRAME_SIZE;

void audioFrame(uint16_t buffer_offset)
{
	if (mode == DrawMode)
	{
		xPos = (int) (adcVals[1] * INV_TWO_TO_16 * 128);
		yPos = (int) (32 - (adcVals[0] * INV_TWO_TO_16 * 32));
		penWeight = (int) (adcVals[2] * INV_TWO_TO_16 * 10);
	}
	else
	{
		frameFunctions[mode](levelLock);
		for (int cc=0; cc < numSamples; cc++)
		{
			audioOutBuffer[buffer_offset + (cc*2)] = tickFunctions[mode](audioInBuffer[buffer_offset+(cc*2)], formantCorrect, autotuneLock);
		}
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

void buttonWasPressed(VocodecButton button)
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

	if (mode == AutotuneAbsoluteMode) tMPoly_setNumVoices(mpoly, activeShifters);
	if (mode == VocoderMode || mode == SynthMode) tMPoly_setNumVoices(mpoly, activeVoices);

	writeModeToLCD(mode, upDownMode);
}

void buttonWasReleased(VocodecButton button)
{

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


void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
	//HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
	;
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  ;
}


void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
	audioFrame(HALF_BUFFER_SIZE);
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
	audioFrame(0);
}
