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


typedef enum Knob
{
	KnobOne = 0,
	KnobTwo,
	KnobThree,
	KnobFour,
	KnobNil
};

tSample sample1;
tSample sample2;

tSamplePlayer player1;
tSamplePlayer player2;



void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand)
{
	// Initialize LEAF.
	LEAF_init(SAMPLE_RATE, AUDIO_FRAME_SIZE, &randomNumber);

	tSample_init(&sample1, leaf.sampleRate * 1.f);
	tSample_init(&sample2, leaf.sampleRate * 1.f);

	tSamplePlayer_init(&player1, &sample1);
	tSamplePlayer_init(&player2, &sample2);

	tSamplePlayer_setMode(&player1, Loop);
	tSamplePlayer_setMode(&player2, Loop);

	for (int i = 0; i < NUM_KNOBS; i++)
	{
		tRamp_init(&knobRamps[i], 50.0f, 1);
	}
	tRamp_setTime(&knobRamps[KnobFour], 2000.0f);

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

	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);

	tSample_start(&sample1);
	tSample_start(&sample2);

	tSamplePlayer_play(&player1);
	tSamplePlayer_play(&player2);
}

int numSamples = AUDIO_FRAME_SIZE;

int timer = 0;

#define CLICK 0

float samp;

static float tick(float in)
{
	// tick knob ramps˙
	for (int i = 0; i < NUM_KNOBS; i++)
	{
		knobVals[i] = tRamp_tick(&knobRamps[i]);
	}

	tSamplePlayer_setStart(&player1, leaf.sampleRate * 0.8f * knobVals[KnobOne]);
	tSamplePlayer_setEnd(&player1, leaf.sampleRate * 0.2f + leaf.sampleRate * 0.8f * knobVals[KnobOne]);

	tSamplePlayer_setStart(&player2, leaf.sampleRate * 0.8f * knobVals[KnobTwo]);
	tSamplePlayer_setEnd(&player2, leaf.sampleRate * 0.2f + leaf.sampleRate * 0.8f * knobVals[KnobTwo]);

	tSamplePlayer_setRate(&player1, -2.f + 4.f * knobVals[KnobThree]);
	tSamplePlayer_setRate(&player2, -2.f + 4.f * knobVals[KnobFour]);

	tSample_tick(&sample1, in);
	tSample_tick(&sample2, in);

	return (0.5f * tSamplePlayer_tick(&player1) + 0.5f * tSamplePlayer_tick(&player2));

}


void audioFrame(uint16_t buffer_offset)
{
	int i;
	int32_t current_sample = 0;

	processKnobs();
	buttonCheck();


	for (i = 0; i < (HALF_BUFFER_SIZE); i++)
	{
		if ((i & 1) == 0)
		{
			current_sample = (int32_t)(tick((float) (audioInBuffer[buffer_offset + i] * INV_TWO_TO_31)) * TWO_TO_31);
		}

		audioOutBuffer[buffer_offset + i] = current_sample;
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
