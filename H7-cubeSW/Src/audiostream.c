/* Includes ------------------------------------------------------------------*/
#include "audiostream.h"
#include "main.h"
#include "codec.h"
#include "OOPSWavetables.h"



// align is to make sure they are lined up with the data boundaries of the cache 
// at(0x3....) is to put them in the D2 domain of SRAM where the DMA can access them
// (otherwise the TX times out because the DMA can't see the data location) -JS

#define NUM_FB_DELAY_TABLES 8

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

float inBuffer[2048];
float outBuffer[NUM_SHIFTERS][2048];

tFormantShifter* fs;
tPitchShifter* ps[NUM_SHIFTERS];
tPeriod* p;
tPitchShift* pshift[NUM_SHIFTERS];
tRamp* ramp[MPOLY_NUM_MAX_VOICES];
tMPoly* mpoly;
tSawtooth* osc[NUM_VOICES];
tSawtooth* saw[NUM_VOICES][NUM_OSC];
tTalkbox* vocoder;
tCycle* sin1;
tNoise* noise1;
tRamp* rampFeedback;
tRamp* rampSineFreq;
tRamp* rampDelayFreq;
tHighpass* highpass1;
tHighpass* highpass2;
tEnvelopeFollower* envFollowNoise;
tEnvelopeFollower* envFollowSine;
tDelayL* ksDelay;
tSVF* lowpass;
tSVF* highpass;
tDelayL* delay;

float dbFeedbackSamp = 0.0f;
float newFreq = 0.0f;
float newDelay = 0.0f;
float newFeedback = 0.0f;
float gainBoost = 1.0f;
float m_input1 = 0.0f;
float m_output0 = 0.0f;
#define FEEDBACK_LOOKUP_SIZE 5
#define DELAY_LOOKUP_SIZE 4
float FeedbackLookup[FEEDBACK_LOOKUP_SIZE] = { 0.0f, 0.8f, .999f, 1.0f, 1.03f };
//float DelayLookup[DELAY_LOOKUP_SIZE] = { 16000.f, 1850.f, 180.f, 40.f };
float DelayLookup[DELAY_LOOKUP_SIZE] = { 50.f, 180.f, 1400.f, 16300.f };
float feedbackDelayPeriod[NUM_FB_DELAY_TABLES];
//const float *feedbackDelayTable[NUM_FB_DELAY_TABLES] = { FB1, FB2, FB3, FB4, FB5, FB6, FB7, FB8 };
void readExternalADC(void);
float interpolateDelayControl(float raw_data);
float interpolateFeedback(float raw_data);
float ksTick(float noise_in);

float delayFeedbackSamp = 0.0f;
float lpFreq;
float hpFreq;

#define MAX_DEPTH 16
int bitDepth = MAX_DEPTH;
int rateRatio;
int lastSamp;
int sampCount = 0;

uint8_t levelLock = 1;
float inputLevel = 1.0f;
float outputLevel = 1.0f;

UpDownMode upDownMode = ModeChange;
VocodecMode mode = 0;
AutotuneType atType = NearestType;

float formantShiftFactor;
float formantKnob;
uint8_t formantCorrect = 0;

int activeVoices = 8;
/* PSHIFT vars *************/

int activeShifters = 1;
float pitchFactor;
float freq[MPOLY_NUM_MAX_VOICES];

float detuneAmounts[NUM_VOICES][NUM_OSC];
float detuneSeeds[NUM_VOICES][NUM_OSC];

float notePeriods[128];
int chordArray[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int lockArray[12];
int lock;
//float centsDeviation[12] = {0.0f, 0.12f, 0.04f, 0.16f, -0.14f, -0.02f, -0.10f, 0.02f, 0.14f, -0.16f, -0.04f, -0.12f};
float centsDeviation[12] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
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
				float tempNote = tMPoly_getPitch(mpoly, i);
				float tempPitchClass = ((((int)tempNote) - keyCenter) % 12 );
				float tunedNote = tempNote + centsDeviation[(int)tempPitchClass];
				freq[i] = OOPS_midiToFrequency(tunedNote);
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
				float tempNote = tMPoly_getPitch(mpoly, i);
				float tempPitchClass = ((((int)tempNote) - keyCenter) % 12 );
				float tunedNote = tempNote + centsDeviation[(int)tempPitchClass];
				freq[i] = OOPS_midiToFrequency(tunedNote);
				tSawtoothSetFreq(osc[i], freq[i]);
			}
		}

		voice = tMPoly_noteOn(mpoly, key, velocity);
		HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);    //LED3
	}
}

void noteOff(int key, int velocity)
{
	myVol = 0.0f;
	int myVoice;

	if (chordArray[key%12] > 0) chordArray[key%12]--;

	myVoice = tMPoly_noteOff(mpoly, key);
	if (myVoice >= 0) tRampSetDest(ramp[myVoice], 0.0f);

	for (int i = 0; i < mpoly->numVoices; i++)
	{
		if (tMPoly_isOn(mpoly, i) == 1)
		{
			tRampSetDest(ramp[i], (float)(tMPoly_getVelocity(mpoly, i) * INV_TWO_TO_7));
			float tempNote = tMPoly_getPitch(mpoly, i);
			float tempPitchClass = ((((int)tempNote) - keyCenter) % 12 );
			float tunedNote = tempNote + centsDeviation[(int)tempPitchClass];
			freq[i] = OOPS_midiToFrequency(tunedNote);
			tSawtoothSetFreq(osc[i], freq[i]);
		}

	}
	myVoice = tMPoly_noteOff(mpoly, key);
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
char* modeNames[ModeCount*2];
void audioInit(I2C_HandleTypeDef* hi2c, SAI_HandleTypeDef* hsaiOut, SAI_HandleTypeDef* hsaiIn, RNG_HandleTypeDef* hrand, uint16_t* myADCArray)
{ 
	modeNames[VocoderMode] = "VOCODER   ";
	modeNames[VocoderMode+ModeCount] = "V0CODER   ";
	modeNames[FormantShiftMode] = "FORMANT   ";
	modeNames[FormantShiftMode+ModeCount] = "F0RMANT   ";
	modeNames[PitchShiftMode] = "PITCHSHIFT";
	modeNames[PitchShiftMode+ModeCount] = "P0RCHSHIFT";
	modeNames[AutotuneMode] = "AUTOTUNE  ";
	modeNames[AutotuneMode+ModeCount] = "0TTERTUNE ";
	modeNames[DelayMode] = "DELAY     ";
	modeNames[DelayMode+ModeCount] = "DELAY     ";
	modeNames[BitcrusherMode] = "BITCOINER ";
	modeNames[BitcrusherMode+ModeCount] = "BITC0INER ";
	modeNames[DrumboxMode] = "DRUMBIES  ";
	modeNames[DrumboxMode+ModeCount] = "DRUMBOX   ";
	modeNames[SynthMode] = "SYNTH     ";
	modeNames[SynthMode+ModeCount] = "SYNTH     ";
	modeNames[LevelMode] = "LEVEL     ";
	modeNames[LevelMode+ModeCount] = "LEVEL LOCK";
	// Initialize the audio library. OOPS.
	OOPSInit(SAMPLE_RATE, AUDIO_FRAME_SIZE, &randomNumber);

	sin1 = tCycleInit();
	noise1 = tNoiseInit(PinkNoise);
	rampFeedback = tRampInit(10.0f, 1);
	rampSineFreq = tRampInit(10.0f, 1);
	rampDelayFreq = tRampInit(10.0f, 1);
	highpass1 = tHighpassInit(20.0f);
	highpass2 = tHighpassInit(20.0f);
	envFollowNoise = tEnvelopeFollowerInit(0.00001f, 0.0f);
	envFollowSine = tEnvelopeFollowerInit(0.00001f, 0.0f);
	ksDelay = tDelayLInit(1000.0f);

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
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneSeeds[i][j] = randomNumber();
			//detuneAmounts[i][j] = (detuneSeeds[i][j] * detuneMax) - (detuneMax * 0.5f);
			saw[i][j] = tSawtoothInit();
		}

	}

	for (int i = 0; i < MPOLY_NUM_MAX_VOICES; i++)
	{
		ramp[i] = tRampInit(10.0f, 1);
	}

	p = tPeriod_init(inBuffer, outBuffer[0], 2048, PS_FRAME_SIZE);
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

	lowpass = tSVFInit(SVFTypeLowpass, 20000.0f, 1.0f);
	highpass = tSVFInit(SVFTypeHighpass, 20.0f, 1.0f);
	delay = tDelayLInit(1000.0f);

	writeModeToLCD(mode, upDownMode);
}

int numSamples = AUDIO_FRAME_SIZE;

void audioFrame(uint16_t buffer_offset)
{
	float input, sample, output;

	if (mode == VocoderMode)
	{
		lpFreq = ((adcVals[1] * INV_TWO_TO_16) * 20000.0f);
		tSVFSetFreq(lowpass, lpFreq);
		for (int i = 0; i < activeVoices; i++)
		{
			tRampSetDest(ramp[i], (tMPoly_getVelocity(mpoly, i) > 0));
			freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
			tSawtoothSetFreq(osc[i], freq[i]);
		}
		for (int cc=0; cc < numSamples; cc++)
		{
			tMPoly_tick(mpoly);

			//float quality = adcVals[1] * INV_TWO_TO_16;

			//tTalkboxSetQuality(vocoder, quality);

			input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31) * inputLevel;
			output = 0.0f;

			for (int i = 0; i < activeVoices; i++)
			{
				output += tSawtoothTick(osc[i]) * tRampTick(ramp[i]);
			}
			output *= 0.25f;

			output = tTalkboxTick(vocoder, output, input);

			output = tSVFTick(lowpass, output);

			output = tanhf(output);
			audioOutBuffer[buffer_offset + (cc*2)]  = (int32_t) (output * TWO_TO_31 * outputLevel);
		}
	}
	else if (mode == FormantShiftMode)
	{
		for (int cc=0; cc < numSamples; cc++)
		{
			input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2)  * inputLevel;

			formantKnob = adcVals[1] * INV_TWO_TO_16;
			formantShiftFactor = (formantKnob * 2.0f) - 1.0f;
			output = tFormantShifterTick(fs, input, formantShiftFactor);
			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31 * 0.5f * outputLevel);
		}
	}
	else if (mode == PitchShiftMode)
	{

		for (int cc=0; cc < numSamples; cc++)
		{
			input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2)  * inputLevel;
			sample = 0.0f;
			output = 0.0f;

			//pitchFactor = (adcVals[1] * INV_TWO_TO_16) * 3.55f + 0.45f; //knob values
			pitchFactor = (adcVals[4] * INV_TWO_TO_16) * 4.49f + 0.43f; //pedal values
			formantKnob = adcVals[3] * INV_TWO_TO_16;
			formantShiftFactor = (formantKnob * 2.0f) - 1.0f;
			tPitchShift_setPitchFactor(pshift[0], pitchFactor);

			if (formantCorrect > 0)
			{
				sample = tFormantShifterRemove(fs, input);
				tPeriod_findPeriod(p, sample);
				sample = tPitchShift_shift(pshift[0]);
				output = tFormantShifterAdd(fs, sample, 0.0f); //can replace 0.0f with formantShiftFactor
			}
			else
			{
				tPeriod_findPeriod(p, input);

				output = tPitchShift_shift(pshift[0]);

			}

			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31  * 0.5f * outputLevel);
		}
	}
	else if (mode == AutotuneMode)
	{
		if (atType == NearestType)
		{
			for (int cc=0; cc < numSamples; cc++)
			{
				input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2)  * inputLevel;
				sample = 0.0f;
				output = 0.0f;

				if (formantCorrect > 0)
				{
					sample = tFormantShifterRemove(fs, input);
					tPeriod_findPeriod(p, sample);
					sample = tPitchShift_shiftToFunc(pshift[0], nearestPeriod);
					output = tFormantShifterAdd(fs, sample, 0.0f);
				}
				else
				{
					tPeriod_findPeriod(p, input);
					output = tPitchShift_shiftToFunc(pshift[0], nearestPeriod);

				}

				audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31 * 0.5f * outputLevel) ;
			}
		}
		else if (atType == AbsoluteType)
		{
			for (int cc=0; cc < numSamples; cc++)
			{
				tMPoly_tick(mpoly);

				input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31 * 2) * inputLevel;
				sample = input;
				output = 0.0f;

				if (formantCorrect > 0) sample = tFormantShifterRemove(fs, input);

				tPeriod_findPeriod(p, sample);

				for (int i = 0; i < activeShifters; ++i)
				{
					freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
					output += tPitchShift_shiftToFreq(pshift[i], freq[i]) * tRampTick(ramp[i]);
				}

				if (formantCorrect > 0) output = tFormantShifterAdd(fs, output, 0.0f);

				audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31 * 0.5f * outputLevel);
			}
		}
	}
	else if (mode == DelayMode)
	{
		for (int cc=0; cc < numSamples; cc++)
		{
			sample = 0.0f;
			input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31) * inputLevel;

			newFeedback = interpolateFeedback(adcVals[0]);
			tRampSetDest(rampFeedback, newFeedback);

			newDelay = interpolateDelayControl(TWO_TO_16 - adcVals[1]);
			tRampSetDest(rampDelayFreq, newDelay);

			lpFreq = ((adcVals[2] * INV_TWO_TO_16) * 20000.0f);
			if (lpFreq < hpFreq) lpFreq = hpFreq;
			hpFreq = ((adcVals[3] * INV_TWO_TO_16) * 10000.0f) - 100.0f;
			if (hpFreq > lpFreq) hpFreq = lpFreq;

			tSVFSetFreq(lowpass, lpFreq);
			tSVFSetFreq(highpass, hpFreq);

			tDelayLSetDelay(delay, tRampTick(rampDelayFreq));

			sample = input + (delayFeedbackSamp * tRampTick(rampFeedback));

			sample = tDelayLTick(delay, sample);

			sample = tSVFTick(lowpass, sample);
			output = tSVFTick(highpass, sample);

			delayFeedbackSamp = output;

			output += input;

			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31 * outputLevel);
		}
	}
	else if (mode == BitcrusherMode)
	{
		for (int cc=0; cc < numSamples; cc++)
		{
			sample = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31) * inputLevel;

			int samp = (int32_t) (sample * TWO_TO_31);

			bitDepth = (int) ((adcVals[0] * INV_TWO_TO_16 * 16.0f) + 1);

			int twoToCrush = (int) exp2f(32 - bitDepth);

			samp /= twoToCrush;
			samp *= twoToCrush;

			rateRatio = (int) (((adcVals[1] * INV_TWO_TO_16) - 0.015) * 129) + 1; // 1 - 128 range, need to use weird value

			if (sampCount == 0)
			{
				lastSamp = samp;
			}
			sampCount++;
			if (sampCount >= (rateRatio - 1))
			{
				sampCount = 0;
			}

			output = (float) (lastSamp * INV_TWO_TO_31);

			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (output * TWO_TO_31 * outputLevel);
		}
	}
	else if (mode == DrumboxMode)
	{
		for (int cc=0; cc < numSamples; cc++)
		{
			sample = 0.0f;
			float audioIn = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31)  * inputLevel;

			newFeedback = interpolateFeedback(adcVals[1]);
			tRampSetDest(rampFeedback, newFeedback);

			newDelay = interpolateDelayControl(TWO_TO_16 - adcVals[2]);
			tRampSetDest(rampDelayFreq, newDelay);

			newFreq =  ((adcVals[1] * INV_TWO_TO_16) * 4.0f) * ((1.0f / newDelay) * 48000.0f);
			tRampSetDest(rampSineFreq,newFreq);

			tEnvelopeFollowerDecayCoeff(envFollowSine,decayCoeffTable[(adcVals[3] >> 4)]);
			tEnvelopeFollowerDecayCoeff(envFollowNoise,0.80f);

			tCycleSetFreq(sin1, tRampTick(rampSineFreq));

			tDelayLSetDelay(ksDelay, tRampTick(rampDelayFreq));

			sample = ((ksTick(audioIn) * 0.7f) + audioIn * 0.8f);
			float tempSinSample = OOPS_shaper(((tCycleTick(sin1) * tEnvelopeFollowerTick(envFollowSine, audioIn)) * 0.6f), 0.5f);
			sample += tempSinSample * 0.6f;
			sample += (tNoiseTick(noise1) * tEnvelopeFollowerTick(envFollowNoise, audioIn));

			sample *= gainBoost;

			sample = tHighpassTick(highpass1, sample);
			sample = OOPS_shaper(sample * 0.6, 1.0f);

			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (sample * TWO_TO_31 * outputLevel);
		}
	}
	else if (mode == SynthMode)
	{
		lpFreq = ((adcVals[1] * INV_TWO_TO_16) * 20000.0f);
		tSVFSetFreq(lowpass, lpFreq);
		detuneMax = (adcVals[0] * INV_TWO_TO_16) * 10.0f;
		for (int i = 0; i < activeVoices; i++)
		{
			freq[i] = OOPS_midiToFrequency(tMPoly_getPitch(mpoly, i));
			for (int j = 0; j < NUM_OSC; j++)
			{
				detuneAmounts[i][j] = (detuneSeeds[i][j] * detuneMax) - (detuneMax * 0.5f);
				tSawtoothSetFreq(saw[i][j], freq[i] + detuneAmounts[i][j]);
			}
		}
		for (int cc=0; cc < numSamples; cc++)
		{
			tMPoly_tick(mpoly);

			//input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31)  * inputLevel;

			sample = 0.0f;
			output = 0.0f;

			for (int i = 0; i < activeVoices; i++)
			{
				for (int j = 0; j < NUM_OSC; j++)
				{
					sample += tSawtoothTick(saw[i][j]) * tRampTick(ramp[i]);
				}
			}

			//sample = tTalkboxTick(vocoder, sample, input);

			sample = tSVFTick(lowpass, sample) * INV_NUM_OSC;

			output = tanhf(sample);
			audioOutBuffer[buffer_offset + (cc*2)]  = (int32_t) (output * TWO_TO_31 * outputLevel);
		}
	}
	else if (mode == LevelMode)
	{
		for (int cc=0; cc < numSamples; cc++)
		{
			if (levelLock == 0)
			{
				inputLevel = (adcVals[3] * INV_TWO_TO_16) * 3.0f;
				outputLevel = (adcVals[2] * INV_TWO_TO_16) * 3.0f / inputLevel;
			}
			sample = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31)  * inputLevel;
			audioOutBuffer[buffer_offset + (cc*2)] = (int32_t) (sample * TWO_TO_31 * outputLevel);
		}
	}
}

float rightInput = 0.0f;

float ksTick(float noise_in)
{
	float temp_sample;
	temp_sample = noise_in + (dbFeedbackSamp * tRampTick(rampFeedback)); //feedback param actually

	dbFeedbackSamp = tDelayLTick(ksDelay, temp_sample);

	m_output0 = 0.5f * m_input1 + 0.5f * dbFeedbackSamp;
	m_input1 = dbFeedbackSamp;
	dbFeedbackSamp = m_output0;
	temp_sample = (tHighpassTick(highpass2, dbFeedbackSamp)) * 0.5f;
	temp_sample = OOPS_shaper(temp_sample, 1.5f);

	return temp_sample;
}

float interpolateDelayControl(float raw_data)
{
	float scaled_raw = raw_data * INV_TWO_TO_16;
	if (scaled_raw < 0.2f)
	{
		return (DelayLookup[0] + ((DelayLookup[1] - DelayLookup[0]) * (scaled_raw * 5.f)));
	}
	else if (scaled_raw < 0.6f)
	{
		return (DelayLookup[1] + ((DelayLookup[2] - DelayLookup[1]) * ((scaled_raw - 0.2f) * 2.5f)));
	}
	else
	{
		return (DelayLookup[2] + ((DelayLookup[3] - DelayLookup[2]) * ((scaled_raw - 0.6f) * 2.5f)));
	}
}

float interpolateFeedback(float raw_data)
{
	float scaled_raw = raw_data * INV_TWO_TO_16;
	if (scaled_raw < 0.2f)
	{
		return (FeedbackLookup[0] + ((FeedbackLookup[1] - FeedbackLookup[0]) * ((scaled_raw) * 5.0f)));
	}
	else if (scaled_raw < 0.6f)
	{
		return (FeedbackLookup[1] + ((FeedbackLookup[2] - FeedbackLookup[1]) * ((scaled_raw - 0.2f) * 2.5f)));
	}
	if (scaled_raw < .95f)
	{
		return (FeedbackLookup[2] + ((FeedbackLookup[3] - FeedbackLookup[2]) * ((scaled_raw - 0.6f) * 2.857142857142f)));
	}
	else
	{
		return (FeedbackLookup[3] + ((FeedbackLookup[4] - FeedbackLookup[3]) * ((scaled_raw - 0.95f) * 20.0f )));
	}
}




#define ASCII_NUM_OFFSET 48
static void writeModeToLCD(VocodecMode in, UpDownMode ud)
{
	int i = in;
	if ((formantCorrect > 0) && (in != LevelMode)) i += ModeCount;
	else if (levelLock && (in == LevelMode)) i += ModeCount;
	OLEDwriteLine(modeNames[i], 10, FirstLine);
	OLEDwriteLine("          ", 10, SecondLine);
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
	else if (in == VocoderMode || in == SynthMode)
	{
		OLEDwriteIntLine(activeVoices, 2, SecondLine);
	}
	else if (in == BitcrusherMode)
	{
		OLEDwriteInt(bitDepth, 2, 76, SecondLine);
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
			if (modex < ModeCount - 1) modex++;
			else modex = 0;
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
			else if (mode == VocoderMode || mode == SynthMode)
			{
				upDownMode = ParameterChange;
			}
		}
		else upDownMode = ModeChange;
	}

	mode = (VocodecMode) modex;

	if (mode == AutotuneMode) tMPoly_setNumVoices(mpoly, activeShifters);
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
