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

HAL_StatusTypeDef transmit_status;
HAL_StatusTypeDef receive_status;

void (*frameFunctions[ModeCount])(void);
int32_t  (*tickFunctions[ModeCount])(int32_t);
static void initFunctionPointers();

/**********************************************/

typedef enum BOOL {
	FALSE = 0,
	TRUE
} BOOL;

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

void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand)
{
	// Initialize the audio library. OOPS.
	SFXInit(SAMPLE_RATE, AUDIO_FRAME_SIZE);

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

void audioFrame(uint16_t buffer_offset)
{
	if (mode != DrawMode)
	{
		frameFunctions[mode]();
		for (int cc=0; cc < numSamples; cc++)
		{
			audioOutBuffer[buffer_offset + (cc*2)] = tickFunctions[mode](audioInBuffer[buffer_offset+(cc*2)]);
		}
	}
}

static void initFunctionPointers()
{
	frameFunctions[VocoderMode] = SFXVocoderFrame;
	tickFunctions[VocoderMode] = SFXVocoderTick;

	frameFunctions[FormantShiftMode] = SFXFormantFrame;
	tickFunctions[FormantShiftMode] = SFXFormantTick;

	frameFunctions[PitchShiftMode] = SFXPitchShiftFrame;
	tickFunctions[PitchShiftMode] = SFXPitchShiftTick;

	frameFunctions[AutotuneNearestMode] = SFXAutotuneNearestFrame;
	tickFunctions[AutotuneNearestMode] = SFXAutotuneNearestTick;

	frameFunctions[AutotuneAbsoluteMode] = SFXAutotuneAbsoluteFrame;
	tickFunctions[AutotuneAbsoluteMode] = SFXAutotuneAbsoluteTick;

	frameFunctions[DelayMode] = SFXDelayFrame;
	tickFunctions[DelayMode] = SFXDelayTick;

	frameFunctions[BitcrusherMode] = SFXBitcrusherFrame;
	tickFunctions[BitcrusherMode] = SFXBitcrusherTick;

	frameFunctions[DrumboxMode] = SFXDrumboxFrame;
	tickFunctions[DrumboxMode] = SFXDrumboxTick;

	frameFunctions[SynthMode] = SFXSynthFrame;
	tickFunctions[SynthMode] = SFXSynthTick;

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
