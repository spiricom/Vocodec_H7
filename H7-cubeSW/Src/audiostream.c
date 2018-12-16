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
VocodecMode audioChain[3];

static void initFunctionPointers(void);

/**********************************************/

typedef enum BOOL {
	FALSE = 0,
	TRUE
} BOOL;

int sustain = 0;
int sustainInverted = 0;
int noteSounding[128];
int noteHeld[128];
int bypass = 0;

void clearNotes(void)
{
	for (int key = 0; key < 128; key++)
	{
		noteSounding[key] = 0;
		noteHeld[key] = 0;
		SFXNoteOff(key, 64);
	}
}

void toggleBypass(void)
{
	if (bypass) bypass = 0;
	else		bypass = 1;

	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, bypass ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void toggleSustain(void)
{
	if (sustainInverted) 	sustainInverted = 0;
	else 					sustainInverted = 1;
}

void noteOn(int key, int velocity)
{
	if (!velocity)
	{
		noteOff(key, velocity);
	}
	else
	{
		noteSounding[key] = 1;

		noteHeld[key] = 1;

		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);    //LED3
	}
}

void noteOff(int key, int velocity)
{
	if (!sustain)
	{
		noteSounding[key] = 0;

		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);    //LED3
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
			noteSounding[key] = 0;
		}
	}
}

void ctrlInput(int ctrl, int value)
{

}

void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand)
{
	// Initialize LEAF.

	LEAF_init(SAMPLE_RATE, AUDIO_FRAME_SIZE, &randomNumber);



	//now to send all the necessary messages to the codec
	AudioCodec_init(hi2c);

	HAL_Delay(100);

	for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
	{
		audioOutBuffer[i] = 0;
	}
	// set up the I2S driver to send audio data to the codec (and retrieve input as well)
	transmit_status = HAL_SAI_Transmit_DMA(hsaiOut, (uint8_t *)&audioOutBuffer[0], AUDIO_BUFFER_SIZE);
	receive_status = HAL_SAI_Receive_DMA(hsaiIn, (uint8_t *)&audioInBuffer[0], AUDIO_BUFFER_SIZE);
}

int numSamples = AUDIO_FRAME_SIZE;

static float tick(float in)
{

}

void audioFrame(uint16_t buffer_offset)
{
	int i;
	int32_t current_sample = 0;

	for (i = 0; i < (HALF_BUFFER_SIZE); i++)
	{
		if ((i & 1) == 0)
		{
			current_sample = (int32_t)(tick((float) (audioInBuffer[buffer_offset + i] * INV_TWO_TO_31)) * TWO_TO_31);
		}

		audioOutBuffer[buffer_offset + i] = current_sample;
	}
}

static void initFunctionPointers(void)
{

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
