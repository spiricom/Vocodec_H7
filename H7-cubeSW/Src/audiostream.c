/* Includes ------------------------------------------------------------------*/
#include "audiostream.h"
#include "main.h"
#include "codec.h"



// align is to make sure they are lined up with the data boundaries of the cache 
// at(0x3....) is to put them in the D2 domain of SRAM where the DMA can access them
// (otherwise the TX times out because the DMA can't see the data location) -JS


int32_t audioOutBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;
int32_t audioInBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;

uint16_t* adcVals;

float sample = 0.0f;

float audioTickL(float audioIn); 
float audioTickR(float audioIn);

HAL_StatusTypeDef transmit_status;
HAL_StatusTypeDef receive_status;

float inBuffer[NUM_SHIFTERS][4096];
float outBuffer[NUM_SHIFTERS][4096];
float pitchFactor = 1.0f;


tPitchShifter* ps[NUM_SHIFTERS];


typedef enum BOOL {
	FALSE = 0,
	TRUE
} BOOL;


void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand, uint16_t* myADCArray)
{ 
	// Initialize the audio library. OOPS.
	OOPSInit(SAMPLE_RATE, AUDIO_FRAME_SIZE, &randomNumber);

	//now to send all the necessary messages to the codec
	AudioCodec_init(hi2c);

	HAL_Delay(100);

	adcVals = myADCArray;

	// set up the I2S driver to send audio data to the codec (and retrieve input as well)
	transmit_status = HAL_SAI_Transmit_DMA(hsaiOut, (uint8_t *)&audioOutBuffer[0], AUDIO_BUFFER_SIZE);
	receive_status = HAL_SAI_Receive_DMA(hsaiIn, (uint8_t *)&audioInBuffer[0], AUDIO_BUFFER_SIZE);

	/* Initialize devices for pitch shifting */
	for (int i = 0; i < NUM_SHIFTERS; ++i)
	{
		ps[i] = tPitchShifter_init(inBuffer[i], outBuffer[i], 4096, 1024);
		tPitchShifter_setWindowSize(ps[i], 1024);
		tPitchShifter_setHopSize(ps[i], 256);
		tPitchShifter_setPitchFactor(ps[i], 1.0f);
	}
}

void audioFrame(uint16_t buffer_offset)
{
	uint16_t i = 0;
	int32_t current_sample = 0;

	for (i = 0; i < (HALF_BUFFER_SIZE); i++)
	{
		if ((i & 1) == 0)
		{
			current_sample = (int32_t)(audioTickL((float) (audioInBuffer[buffer_offset + i] * INV_TWO_TO_31)) * TWO_TO_31);
		}
		else
		{
			current_sample = (int32_t)(audioTickR((float) (audioInBuffer[buffer_offset + i] * INV_TWO_TO_31)) * TWO_TO_31);
		}
		audioOutBuffer[buffer_offset + i] = current_sample;
	}
}

float audioTickL(float audioIn) 
{
	sample = 0.0f;


	pitchFactor = ((adcVals[1]>> 4) * INV_TWO_TO_12) * 3.5f + 0.5f;

	tPitchShifter_setPitchFactor(ps[0], pitchFactor);

	//or you could just set the pitch factor directly like this (since you'll be taking it from some SPI data eventually)
	//tPitchShifter_setPitchFactor(ps[0], 2.0f);

	sample = tPitchShifter_tick(ps[0], audioIn);

	return sample;
}

float audioTickR(float audioIn)
{
	sample = 0.0f;

	pitchFactor = ((adcVals[0] >> 4) * INV_TWO_TO_12) * 3.5f + 0.5f;

	tPitchShifter_setPitchFactor(ps[1], pitchFactor);

	//or you could just set the pitch factor directly like this (since you'll be taking it from some SPI data eventually)
	//tPitchShifter_setPitchFactor(ps[0], 0.5f);

	sample = tPitchShifter_tick(ps[1], audioIn);

	return sample;
}



void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
	;
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
