/* Includes ------------------------------------------------------------------*/
#include "audiostream.h"
#include "main.h"
#include "codec.h"
#include "ui.h"
#include "sfx.h"
#include "tim.h"
// align is to make sure they are lined up with the data boundaries of the cache 
// at(0x3....) is to put them in the D2 domain of SRAM where the DMA can access them
// (otherwise the TX times out because the DMA can't see the data location) -JS

#define NUM_FB_DELAY_TABLES 8
tEnvelopeFollower* detector;

int32_t audioOutBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;
int32_t audioInBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;

float audioTickL(float audioIn); 
float audioTickR(float audioIn);

HAL_StatusTypeDef transmit_status;
HAL_StatusTypeDef receive_status;

void (*frameFunctions[ModeCount])(void);
int32_t  (*tickFunctions[ModeCount])(int32_t);
VocodecMode audioChain[3];

static void initFunctionPointers(void);

/**********************************************/

typedef enum BOOL {
	FALSE = 0,
	TRUE
} BOOL;

int sustain = 0;
int noteSounding[128];
int noteHeld[128];

void noteOn(int key, int velocity)
{
	if (!velocity)
	{
		noteOff(key, velocity);
	}
	else
	{
		SFXNoteOn(key, velocity);
		noteSounding[key] = 1;

		noteHeld[key] = 1;

		//HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);    //LED3
	}
}

void noteOff(int key, int velocity)
{
	if (!sustain)
	{
		SFXNoteOff(key, velocity);
		noteSounding[key] = 0;

		//HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);    //LED3
	}

	noteHeld[key] = 0;
}

void sustainOn(void)
{
	sustain = TRUE;
}

void sustainOff(void)
{
	sustain = FALSE;

	for (int key = 0; key < 128; key++)
	{
		if (noteSounding[key] && !noteHeld[key])
		{
			SFXNoteOff(key, 64);
			noteSounding[key] = 0;
		}
	}
}

void ctrlInput(int ctrl, int value)
{

}

float attackCoef = pow(0.01, 1.0/(100.0 * 48000.0 * 0.001));
float decayCoef = pow(0.01, 1.0/(100.0 * 48000.0 * 0.001));

void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand)
{
	// Initialize the audio library. OOPS.
	SFXInit(SAMPLE_RATE, AUDIO_FRAME_SIZE);

	detector = tEnvelopeFollowerInit(.1f, .9999f);
	//now to send all the necessary messages to the codec
	AudioCodec_init(hi2c);

	HAL_Delay(100);

	initFunctionPointers();

	for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
	{
		audioOutBuffer[i] = 0;
	}
	// set up the I2S driver to send audio data to the codec (and retrieve input as well)
	transmit_status = HAL_SAI_Transmit_DMA(hsaiOut, (uint8_t *)&audioOutBuffer[0], AUDIO_BUFFER_SIZE);
	receive_status = HAL_SAI_Receive_DMA(hsaiIn, (uint8_t *)&audioInBuffer[0], AUDIO_BUFFER_SIZE);
}

int numSamples = AUDIO_FRAME_SIZE;


float myRecordBuffer[SAMPLE_BUFFER_SIZE];
float myPlayBuffer[SAMPLE_BUFFER_SIZE_TIMES_TWO];
int32_t recWriteIndex = 0;
int32_t recReadIndex = 0;
int32_t playReadIndex = 0;
uint32_t delayTime = 10;

uint32_t loopLength = 4000;
uint32_t loopStart = 0;
float detectorBuffer[AUDIO_FRAME_SIZE];
int32_t detectorCountdown = 0;
float detectorVal = 0.0f;
int32_t recordContinueCountdown = 0;

void audioFrame(uint16_t buffer_offset)
{
	float sample = 0.0f;




	for (int cc=0; cc < numSamples; cc++)
	{
		for (int i = 0; i < NUM_KNOBS; i++)
		{
			knobVals[i] = tRampTick(knobRamps[i]);
		}

		sample = (float )((audioInBuffer[buffer_offset+(cc*2)] * inputLevel) * INV_TWO_TO_31);

		detectorVal = tEnvelopeFollowerTick(detector, sample);

		if (detectorVal > .02f)
		{

			__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 500); //bottom green
			if (detectorCountdown == 0)
			{
				grabBuffer();
			}
			detectorCountdown = 200;

		}

		detectorCountdown--;

		if (detectorCountdown< 0)
		{
			detectorCountdown = 0;
			__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0); //bottom green
		}

		myRecordBuffer[recWriteIndex] = sample;

		if (recordContinueCountdown > 0)
		{
			myPlayBuffer[SAMPLE_BUFFER_SIZE_TIMES_TWO - recordContinueCountdown] = sample;
		}
		recordContinueCountdown--;

		loopLength = (uint32_t)(knobVals[0] * 20000 + 10);


		if (recordContinueCountdown < 0)
		{
			recordContinueCountdown = 0;
		}


		recReadIndex = recWriteIndex - loopLength;
		if (recReadIndex < 0)
		{
			recReadIndex +=SAMPLE_BUFFER_SIZE;
		}

		recWriteIndex++;
		if (recWriteIndex > SAMPLE_BUFFER_SIZE)
		{
			recWriteIndex = 0;
		}


		sample += myPlayBuffer[playReadIndex];
		playReadIndex = (playReadIndex + 1) % SAMPLE_BUFFER_SIZE;


		//if the loop wraps around, check if it went over loop length
		if (playReadIndex < loopStart)
		{
			if (((playReadIndex + SAMPLE_BUFFER_SIZE) - loopStart) > loopLength)
			{
				playReadIndex = loopStart;
			}
		}

		//if it doesn't wrap around, check against loop length
		else if ((playReadIndex - loopStart) > loopLength)
		{
			playReadIndex = loopStart;
		}

		audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) ((sample * outputLevel) * TWO_TO_31);
	}




}


void grabBuffer(void)
{
	playReadIndex = recReadIndex;
	loopStart = playReadIndex;

	//copy a section of the buffer
	for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++)
	{
		myPlayBuffer[SAMPLE_BUFFER_SIZE-1-i] = myRecordBuffer[(i+playReadIndex) % SAMPLE_BUFFER_SIZE];
	}
	recordContinueCountdown = SAMPLE_BUFFER_SIZE;
}

static void initFunctionPointers(void)
{
	frameFunctions[VocoderMode] = SFXVocoderFrame;
	tickFunctions[VocoderMode] = SFXVocoderTick;

	frameFunctions[FormantShiftMode] = SFXFormantFrame;
	tickFunctions[FormantShiftMode] = SFXFormantTick;


	frameFunctions[DelayMode] = SFXDelayFrame;
	tickFunctions[DelayMode] = SFXDelayTick;



	frameFunctions[BitcrusherMode] = SFXBitcrusherFrame;
	tickFunctions[BitcrusherMode] = SFXBitcrusherTick;

	frameFunctions[DrumboxMode] = SFXDrumboxFrame;
	tickFunctions[DrumboxMode] = SFXDrumboxTick;

	frameFunctions[SynthMode] = SFXSynthFrame;
	tickFunctions[SynthMode] = SFXSynthTick;

	frameFunctions[DrawMode] = SFXDrawFrame;
	tickFunctions[DrawMode] = SFXDrawTick;

	frameFunctions[LevelMode] = SFXLevelFrame;
	tickFunctions[LevelMode] = SFXLevelTick;
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
