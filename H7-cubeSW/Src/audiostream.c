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

uint8_t buttonAPressed = 0;

uint8_t doAudio = 0;

float sample = 0.0f;

float adcx[8];

float detuneMax = 3.0f;
uint8_t audioInCV = 0;
uint8_t audioInCVAlt = 0;
float myVol = 0.0f;

float audioTickL(float audioIn); 
float audioTickR(float audioIn);
void buttonCheck(void);

HAL_StatusTypeDef transmit_status;
HAL_StatusTypeDef receive_status;

float inBuffer[NUM_SHIFTERS][2048];
float outBuffer[NUM_SHIFTERS][2048];

tFormantShifter* fs;
tPitchShifter* ps[NUM_SHIFTERS];
tPeriod* p;
tPitchShift* pshift[NUM_SHIFTERS];
tRamp* ramp[MPOLY_NUM_MAX_VOICES];
tMPoly* mpoly;
tSawtooth* osc[NUM_VOICES];
tTalkbox* vocoder;

UpDownMode upDownMode = ModeChange;
VocodecMode mode = FormantShiftMode;
AutotuneType atType = NearestType;

float formantShiftFactor;
float formantKnob;
uint8_t formantCorrect = 0;

int activeVoices = 4;
/* PSHIFT vars *************/

int activeShifters = 1;
float pitchFactor;
float freq[MPOLY_NUM_MAX_VOICES];

float notePeriods[128];
int chordArray[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int lockArray[12];
int lock;
float centsDeviation[12] = {0.0f, 0.12f, 0.04f, 0.16f, -0.14f, -0.02f, -0.10f, 0.02f, 0.14f, -0.16f, -0.04f, -0.12f};
int keyCenter = 5;
/**********************************************/

typedef enum BOOL {
	FALSE = 0,
	TRUE
} BOOL;

static void writeModeToLCD(VocodecMode in, UpDownMode ud);

void noteOn(int key, int velocity)
{
	int voice;
	if (!velocity)
	{
		//myVol = 0.0f;

		if (chordArray[key%12] > 0) chordArray[key%12]--;

		voice = tMPoly_noteOff(mpoly, key);
		if (voice >= 0) tRampSetDest(ramp[voice], 0.0f);
		for (int i = 0; i < mpoly->numVoices; i++)
		{
			if (tMPoly_isOn(mpoly, i) == 1)
			{
				tRampSetDest(ramp[i], (float)(tMPoly_getVelocity(mpoly, i) * INV_TWO_TO_7));
				freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
				tSawtoothSetFreq(osc[i], freq[i]);
			}
		}

		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);    //LED
	}
	else
	{
		chordArray[key%12]++;
		tMPoly_noteOn(mpoly, key, velocity);

		for (int i = 0; i < mpoly->numVoices; i++)
		{
			if (tMPoly_isOn(mpoly, i) == 1)
			{
				tRampSetDest(ramp[i], (float)(tMPoly_getVelocity(mpoly, i) * INV_TWO_TO_7));
				freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
				tSawtoothSetFreq(osc[i], freq[i]);
			}
		}


		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);    //LED3
	}
}

void noteOff(int key, int velocity)
{
	myVol = 0.0f;
	int voice;

	if (chordArray[key%12] > 0) chordArray[key%12]--;

	voice = tMPoly_noteOff(mpoly, key);
	if (voice >= 0) tRampSetDest(ramp[voice], 0.0f);

	for (int i = 0; i < mpoly->numVoices; i++)
	{
		if (tMPoly_isOn(mpoly, i) == 1)
		{
			tRampSetDest(ramp[i], (float)(tMPoly_getVelocity(mpoly, i) * INV_TWO_TO_7));
			freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
			tSawtoothSetFreq(osc[i], freq[i]);
		}

	}

	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);    //LED3
}

void ctrlInput(int ctrl, int value)
{

}

float nearestPeriod(float period)
{
	float leastDifference = fabsf(period - notePeriods[0]);
	float difference;
	int index = -1;

	int* chord = chordArray;
	if (lock > 0) chord = lockArray;

	for(int i = 0; i < 128; i++)
	{
		if (chord[i%12] > 0)
		{
			difference = fabsf(period - notePeriods[i]);
			if(difference < leastDifference)
			{
				leastDifference = difference;
				index = i;
			}
		}
	}

	if (index == -1) return period;

	return notePeriods[index];
}

void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand, uint16_t* myADCArray)
{ 
	// Initialize the audio library. OOPS.
	OOPSInit(SAMPLE_RATE, AUDIO_FRAME_SIZE, &randomNumber);

	for (int i = 0; i < 128; i++)
	{
		notePeriods[i] = 1.0f / OOPS_midiToFrequency(i) * oops.sampleRate;
	}

	//now to send all the necessary messages to the codec
	AudioCodec_init(hi2c);

	HAL_Delay(100);

	adcVals = myADCArray;

	fs = tFormantShifterInit();

	mpoly = tMPoly_init(MPOLY_NUM_MAX_VOICES);
	tMPoly_setPitchGlideTime(mpoly, 10.0f);

	vocoder = tTalkboxInit();
	for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
	{
		audioOutBuffer[i] = 0;
	}
	// set up the I2S driver to send audio data to the codec (and retrieve input as well)
	transmit_status = HAL_SAI_Transmit_DMA(hsaiOut, (uint8_t *)&audioOutBuffer[0], AUDIO_BUFFER_SIZE);
	receive_status = HAL_SAI_Receive_DMA(hsaiIn, (uint8_t *)&audioInBuffer[0], AUDIO_BUFFER_SIZE);

	for (int i = 0; i < NUM_VOICES; i++)
	{
		osc[i] = tSawtoothInit();
	}

	for (int i = 0; i < MPOLY_NUM_MAX_VOICES; i++)
	{
		ramp[i] = tRampInit(10.0f, 1);
	}

	p = tPeriod_init(inBuffer[0], outBuffer[0], 2048, PS_FRAME_SIZE);

	tPeriod_setWindowSize(p, ENV_WINDOW_SIZE);
	tPeriod_setHopSize(p, ENV_HOP_SIZE);

	/* Initialize devices for pitch shifting */
	for (int i = 0; i < NUM_SHIFTERS; ++i)
	{
		pshift[i] = tPitchShift_init(p, outBuffer[i], 2048);
		/*ps[i] = tPitchShifter_init(inBuffer[i], outBuffer[i], 2048, PS_FRAME_SIZE);
		tPitchShifter_setWindowSize(ps[i], ENV_WINDOW_SIZE);
		tPitchShifter_setHopSize(ps[i], ENV_HOP_SIZE);
		tPitchShifter_setPitchFactor(ps[i], 1.0f);*/

	}

	writeModeToLCD(mode, upDownMode);
}

int numSamples = AUDIO_FRAME_SIZE;

void audioFrame(uint16_t buffer_offset)
{
	float input, sample, output, output2, mix;

	if (mode == FormantShiftMode)
	{
		for (int cc=0; cc < numSamples; cc++)
		{
			input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2);

			formantKnob = adcVals[1] * INV_TWO_TO_16;
			formantShiftFactor = (formantKnob * 2.0f) - 1.0f;
			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (tFormantShifterTick(fs, input, formantShiftFactor) * TWO_TO_31);
		}
	}
	else if (mode == PitchShiftMode)
	{

		for (int cc=0; cc < numSamples; cc++)
		{
			input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2);
			sample = 0.0f;
			output = 0.0f;

			pitchFactor = (adcVals[1] * INV_TWO_TO_16) * 3.55f + 0.45f;
			formantKnob = adcVals[3] * INV_TWO_TO_16;
			formantShiftFactor = (formantKnob * 2.0f) - 1.0f;
			tPitchShift_setPitchFactor(pshift[0], pitchFactor);

			if (formantCorrect > 0)
			{
				sample = tFormantShifterRemove(fs, input);
				//sample = tPitchShifter_tick(ps[0], sample);
				tPeriod_findPeriod(p, sample);
				sample = tPitchShift_shift(pshift[0]);
				output = tFormantShifterAdd(fs, sample, 0.0f); //can replace 0.0f with formantShiftFactor
			}
			else
			{
				//output = tPitchShifter_tick(ps[0], input);
				tPeriod_findPeriod(p, input);

				output = tPitchShift_shift(pshift[0]);

			}

			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31);
		}
	}
	else if (mode == AutotuneMode)
	{
		if (atType == NearestType)
		{
			for (int cc=0; cc < numSamples; cc++)
			{
				input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2);
				sample = 0.0f;
				output = 0.0f;

				if (formantCorrect > 0)
				{
					sample = tFormantShifterRemove(fs, input);
					tPeriod_findPeriod(p, sample);
					sample = tPitchShift_shiftToFunc(pshift[0], nearestPeriod);
					//sample = tPitchShifterToFunc_tick(ps[0], sample, nearestPeriod);
					output = tFormantShifterAdd(fs, sample, 0.0f);
				}
				else
				{
					//output = tPitchShifterToFunc_tick(ps[0], input, nearestPeriod);
					tPeriod_findPeriod(p, input);
					output = tPitchShift_shiftToFunc(pshift[0], nearestPeriod);

				}

				audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31);
			}
		}
		else if (atType == AbsoluteType)
		{
			for (int cc=0; cc < numSamples; cc++)
			{
				tMPoly_tick(mpoly);

				input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2);
				sample = input;
				output = 0.0f;

				if (formantCorrect > 0) sample = tFormantShifterRemove(fs, input);

				tPeriod_findPeriod(p, sample);

				for (int i = 0; i < activeShifters; ++i)
				{
					freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
					output += tPitchShift_shiftToFreq(pshift[i], freq[i]) * tRampTick(ramp[i]);
					//output += tPitchShifterToFreq_tick(ps[i], sample, freq[i]) * tRampTick(ramp[i]);
				}

				if (formantCorrect > 0) output = tFormantShifterAdd(fs, output, 0.0f);

				audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31);
			}
		}
	}
	if (mode == VocoderMode)
	{
		for (int i = 0; i < activeVoices; i++)
		{
			freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
			tSawtoothSetFreq(osc[i], freq[i]);
		}
		for (int cc=0; cc < numSamples; cc++)
		{
			tMPoly_tick(mpoly);

			//float quality = adcVals[1] * INV_TWO_TO_16;

			//tTalkboxSetQuality(vocoder, quality);

			input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31);
			output = 0.0f;

			for (int i = 0; i < activeVoices; i++)
			{
				output += tSawtoothTick(osc[i]) * tRampTick(ramp[i]);
			}
			output *= 0.25f;

			output = tTalkboxTick(vocoder, output, input);

			output = tanhf(output);
			audioOutBuffer[buffer_offset + (cc*2)]  = (int32_t) (output * TWO_TO_31);
		}
	}
}

float rightInput = 0.0f;

float audioTickL(float audioIn) 
{
	sample = 0.0f;

	return sample;
}

float audioTickR(float audioIn)
{
	rightInput = audioIn;
	//sample = audioIn;
	return audioIn;
}

char* modeNames[8] =
{
	"FORMANT   ", "PITCHSHIFT",
	"AUTOTUNE  ", "VOCODER   ",
	"FORMANT  c", "PITCHSHIFc",
	"AUTOTUNE c", "VOCODER   ",
};

#define ASCII_NUM_OFFSET 48
static void writeModeToLCD(VocodecMode in, UpDownMode ud)
{
	int i = in;
	if (formantCorrect > 0) i += 4;
	OLEDwriteLine(modeNames[i], 10, FirstLine);
	if (in == AutotuneMode)
	{
		if ((atType == NearestType) && (lock > 0))
		{
			OLEDwriteLine("LOCK", 4, SecondLine);
		}
		else if (atType == AbsoluteType)
		{
			OLEDwriteIntLine(activeShifters, 2, SecondLine);
		}
		else OLEDwriteLine("          ", 10, SecondLine);
	}
	else if (in == VocoderMode)
	{
		OLEDwriteIntLine(activeVoices, 2, SecondLine);
	}
	//else OLEDwriteLine("          ", 10, SecondLine);
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
			modex++;
			if (modex >= ModeNil) modex = 3;
		}
		else if (upDownMode == ParameterChange)
		{
			if (mode == AutotuneMode)
			{
				if (atType == NearestType)
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

					lock = 1;
				}
				else if (atType == AbsoluteType)
				{
					if (activeShifters < NUM_SHIFTERS) activeShifters++;
					else activeShifters = 1;
					//else activeShifters = NUM_SHIFTERS;
				}
			}
			else if (mode == VocoderMode)
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
			modex--;
			if ((int)modex < 0) modex = 0;
		}
		else if (upDownMode == ParameterChange)
		{
			if (mode == AutotuneMode)
			{
				if (atType == AbsoluteType)
				{
					if (activeShifters > 1) activeShifters--;
					else activeShifters = NUM_SHIFTERS;
					//else activeShifters = 1;
				}
			}
			else if (mode == VocoderMode)
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
		else if (mode == AutotuneMode)
		{
			if (atType == NearestType)
			{
				atType = AbsoluteType;
			}
			else if (atType == AbsoluteType)
			{
				if (upDownMode == ModeChange) atType = NearestType;
				else formantCorrect = (formantCorrect > 0) ? 0 : 1;
			}
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
			else if (mode == AutotuneMode)
			{
				if (atType == NearestType)
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

					lock = (lock > 0) ? 0 : 1;
				}
				else if (atType == AbsoluteType)
				{
					upDownMode = ParameterChange;
				}
			}
			else if (mode == VocoderMode)
			{
				upDownMode = ParameterChange;
			}
		}
		else upDownMode = ModeChange;
	}

	mode = (VocodecMode) modex;

	if (mode == AutotuneMode) tMPoly_setNumVoices(mpoly, activeShifters);
	if (mode == VocoderMode) tMPoly_setNumVoices(mpoly, activeVoices);

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
