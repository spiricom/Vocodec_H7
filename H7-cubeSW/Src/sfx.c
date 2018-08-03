/* Includes ------------------------------------------------------------------*/
#include "sfx.h"
#include "main.h"
#include "codec.h"
#include "OOPSWavetables.h"
#include "ui.h"

#define NUM_FB_DELAY_TABLES 8

float detuneMax = 3.0f;

float inBuffer[2048] __ATTR_RAM_D2;
float outBuffer[NUM_SHIFTERS][2048] __ATTR_RAM_D2;

tFormantShifter* fs;
tPeriod* p;
tPitchShift* pshift[NUM_SHIFTERS];
tRamp* ramp[MPOLY_NUM_MAX_VOICES];
tMPoly* mpoly;
tSawtooth* osc[NUM_VOICES][NUM_OSC];
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

float delayFeedbackSamp = 0.0f;
float lpFreq;
float hpFreq;

#define MAX_DEPTH 16
int bitDepth = MAX_DEPTH;
int rateRatio;
int lastSamp;
int sampCount = 0;

float inputLevel = 1.0f;
float outputLevel = 1.0f;

float formantShiftFactor;
float formantKnob;

int activeVoices = 1;
int activeShifters = 1;

/* PSHIFT vars *************/

float pitchFactor;
float freq[MPOLY_NUM_MAX_VOICES];

float detuneAmounts[NUM_VOICES][NUM_OSC];
float detuneSeeds[NUM_VOICES][NUM_OSC];

float notePeriods[128];
int chordArray[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int lockArray[12];

//float centsDeviation[12] = {0.0f, 0.12f, 0.04f, 0.16f, -0.14f, -0.02f, -0.10f, 0.02f, 0.14f, -0.16f, -0.04f, -0.12f};
float centsDeviation[12] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
int keyCenter = 5;

uint8_t levelLock = 1;
uint8_t autotuneLock = 0;
uint8_t formantCorrect = 0;

/****************************************************************************************/

float nearestPeriod(float period);
float interpolateDelayControl(float raw_data);
float interpolateFeedback(float raw_data);
float ksTick(float noise_in);

/****************************************************************************************/

void SFXInit(float sr, int blocksize)
{
	// Initialize the audio library. OOPS.
	OOPSInit(sr, blocksize, &randomNumber);

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

	fs = tFormantShifterInit();

	mpoly = tMPoly_init(MPOLY_NUM_MAX_VOICES);
	tMPoly_setPitchGlideTime(mpoly, 50.0f);

	vocoder = tTalkboxInit();

	for (int i = 0; i < NUM_VOICES; i++)
	{
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneSeeds[i][j] = randomNumber();
			osc[i][j] = tSawtoothInit();
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
	}

	lowpass = tSVFInit(SVFTypeLowpass, 20000.0f, 1.0f);
	highpass = tSVFInit(SVFTypeHighpass, 20.0f, 1.0f);
	delay = tDelayLInit(1000.0f);
}

void SFXVocoderFrame()
{
	tMPoly_setNumVoices(mpoly, activeVoices);
	lpFreq = ((knobVals[2]) * 20000.0f);
	tSVFSetFreq(lowpass, lpFreq);
	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		tRampSetDest(ramp[i], (tMPoly_getVelocity(mpoly, i) > 0));
		calculateFreq(i);
		tSawtoothSetFreq(osc[i][0], freq[i]);
	}
}
int32_t SFXVocoderTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(mpoly);

	sample = (float) (input * INV_TWO_TO_31) * inputLevel;

	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		output += tSawtoothTick(osc[i][0]) * tRampTick(ramp[i]);
	}
	output *= 0.25f;
	output = tTalkboxTick(vocoder, output, sample);
	output = tSVFTick(lowpass, output);
	output = tanhf(output);

	return (int32_t) (output * TWO_TO_31 * outputLevel);
}

void SFXFormantFrame()
{
	formantKnob = knobVals[2];
	formantShiftFactor = (formantKnob * 2.0f) - 1.0f;
}
int32_t SFXFormantTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31 * 2)  * inputLevel;

	output = tFormantShifterTick(fs, sample, formantShiftFactor);

	return (int32_t) (output * TWO_TO_31 * 0.5f * outputLevel);
}

void SFXPitchShiftFrame()
{
	//pitchFactor = (adcVals[1] * INV_TWO_TO_16) * 3.55f + 0.45f; //knob values
	pitchFactor = (knobVals[0]) * 4.49f + 0.43f; //pedal values
	tPitchShift_setPitchFactor(pshift[0], pitchFactor);
	formantKnob = knobVals[1];
	formantShiftFactor = (formantKnob * 2.0f) - 1.0f;
}

int32_t SFXPitchShiftTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31 * 2)  * inputLevel;

	if (formantCorrect > 0) sample = tFormantShifterRemove(fs, sample);

	tPeriod_findPeriod(p, sample);
	output = tPitchShift_shift(pshift[0]);

	if (formantCorrect > 0) output = tFormantShifterAdd(fs, output, 0.0f);

	return (int32_t) (output * TWO_TO_31  * 0.5f * outputLevel);
}

void SFXAutotuneNearestFrame()
{

}

int32_t SFXAutotuneNearestTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31 * 2) * inputLevel;

	if (formantCorrect > 0) sample = tFormantShifterRemove(fs, sample);

	tPeriod_findPeriod(p, sample);
	output = tPitchShift_shiftToFunc(pshift[0], nearestPeriod);

	if (formantCorrect > 0) output = tFormantShifterAdd(fs, output, 0.0f);

	return (int32_t) (output * TWO_TO_31 * 0.5f * outputLevel);
}

void SFXAutotuneAbsoluteFrame()
{
	tMPoly_setNumVoices(mpoly, activeShifters);
	for (int i = 0; i < tMPoly_getNumVoices(mpoly); ++i)
	{
		calculateFreq(i);
	}
}
int32_t SFXAutotuneAbsoluteTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(mpoly);

	sample = (float) (input * INV_TWO_TO_31 * 2) * inputLevel;

	if (formantCorrect > 0) sample = tFormantShifterRemove(fs, sample);

	tPeriod_findPeriod(p, sample);

	for (int i = 0; i < tMPoly_getNumVoices(mpoly); ++i)
	{
		output += tPitchShift_shiftToFreq(pshift[i], freq[i]) * tRampTick(ramp[i]);
	}

	if (formantCorrect > 0) output = tFormantShifterAdd(fs, output, 0.0f);

	return (int32_t) (output * TWO_TO_31 * 0.5f * outputLevel);
}

void SFXDelayFrame()
{
	newFeedback = interpolateFeedback(knobVals[3]);
	tRampSetDest(rampFeedback, newFeedback);

	newDelay = interpolateDelayControl(TWO_TO_16 - knobVals[2]);
	tRampSetDest(rampDelayFreq, newDelay);

	lpFreq = ((knobVals[1]) * 20000.0f);
	if (lpFreq < hpFreq) lpFreq = hpFreq;
	hpFreq = ((knobVals[0]) * 10000.0f) - 100.0f;
	if (hpFreq > lpFreq) hpFreq = lpFreq;

	tSVFSetFreq(lowpass, lpFreq);
	tSVFSetFreq(highpass, hpFreq);
}

int32_t SFXDelayTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31) * inputLevel;

	tDelayLSetDelay(delay, tRampTick(rampDelayFreq));

	output = sample + (delayFeedbackSamp * tRampTick(rampFeedback));

	output = tDelayLTick(delay, output);

	output = tSVFTick(lowpass, output);
	output = tSVFTick(highpass, output);

	delayFeedbackSamp = output;

	output += sample;

	return (int32_t) (output * TWO_TO_31 * outputLevel);
}

void SFXBitcrusherFrame()
{
	bitDepth = (int) ((knobVals[3] * 16.0f) + 1);
	rateRatio = (int) (((knobVals[2]) - 0.015) * 129) + 1; // 1 - 128 range, need to use weird value
}
int32_t SFXBitcrusherTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31) * inputLevel;

	int samp = (int32_t) (sample * TWO_TO_31);
	int twoToCrush = (int) exp2f(32 - bitDepth);

	samp /= twoToCrush;
	samp *= twoToCrush;

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

	return (int32_t) (output * TWO_TO_31 * outputLevel);
}

void SFXDrumboxFrame()
{
	newFeedback = interpolateFeedback(knobVals[2]);
	tRampSetDest(rampFeedback, newFeedback);

	newDelay = interpolateDelayControl(TWO_TO_16 - knobVals[1]);
	tRampSetDest(rampDelayFreq, newDelay);

	newFreq =  ((knobVals[2]) * 4.0f) * ((1.0f / newDelay) * 48000.0f);
	tRampSetDest(rampSineFreq,newFreq);

	tEnvelopeFollowerDecayCoeff(envFollowSine,decayCoeffTable[(adcVals[3] >> 4)]);
	tEnvelopeFollowerDecayCoeff(envFollowNoise,0.80f);
}
int32_t SFXDrumboxTick(int32_t input)
{
	float audioIn = 0.0f;
	float sample = 0.0f;
	float output = 0.0f;

	audioIn = (float) (input * INV_TWO_TO_31) * inputLevel;

	tCycleSetFreq(sin1, tRampTick(rampSineFreq));

	tDelayLSetDelay(ksDelay, tRampTick(rampDelayFreq));

	sample = ((ksTick(audioIn) * 0.7f) + audioIn * 0.8f);
	float tempSinSample = OOPS_shaper(((tCycleTick(sin1) * tEnvelopeFollowerTick(envFollowSine, audioIn)) * 0.6f), 0.5f);
	sample += tempSinSample * 0.6f;
	sample += (tNoiseTick(noise1) * tEnvelopeFollowerTick(envFollowNoise, audioIn));

	sample *= gainBoost;

	sample = tHighpassTick(highpass1, sample);
	sample = OOPS_shaper(sample * 0.6, 1.0f);

	return (int32_t) (sample * TWO_TO_31 * outputLevel);
}

void SFXSynthFrame()
{
	tMPoly_setNumVoices(mpoly, activeVoices);
	lpFreq = ((knobVals[2]) * 20000.0f);
	tSVFSetFreq(lowpass, lpFreq);

	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		calculateFreq(i);
		detuneMax = (knobVals[3]) * freq[i] * 0.05f;
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneAmounts[i][j] = (detuneSeeds[i][j] * detuneMax) - (detuneMax * 0.5f);
			tSawtoothSetFreq(osc[i][j], freq[i] + detuneAmounts[i][j]);
		}
	}
}

int32_t SFXSynthTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(mpoly);

	//input = (float) (audioInBuffer[buffer_offset+(cc*2)] * INV_TWO_TO_31)  * inputLevel;

	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		for (int j = 0; j < NUM_OSC; j++)
		{
			sample += tSawtoothTick(osc[i][j]) * tRampTick(ramp[i]);
		}
	}

	//sample = tTalkboxTick(vocoder, sample, input);

	sample = tSVFTick(lowpass, sample) * INV_NUM_OSC * 0.5f;

	output = tanhf(sample);
	return (int32_t) (output * TWO_TO_31 * outputLevel);
}

void SFXLevelFrame()
{
	if (levelLock == 0)
	{
		inputLevel = (knobVals[2]) * 3.0f;
		outputLevel = (knobVals[3]) * 3.0f / inputLevel;
	}
}
int32_t SFXLevelTick(int32_t input)
{
	float output = 0.0f;

	output = (float) (input * INV_TWO_TO_31)  * inputLevel;
	return (int32_t) (output * TWO_TO_31 * outputLevel);
}

/**************** MPoly Handling ***********************/

void SFXNoteOn(int key, int velocity)
{
	chordArray[key%12]++;
	tMPoly_noteOn(mpoly, key, velocity);

	for (int i = 0; i < mpoly->numVoices; i++)
	{
		if (tMPoly_isOn(mpoly, i) == 1)
		{
			tRampSetDest(ramp[i], (float)(tMPoly_getVelocity(mpoly, i) * INV_TWO_TO_7));
			calculateFreq(i);
		}
	}
}

void SFXNoteOff(int key, int velocity)
{
	if (chordArray[key%12] > 0) chordArray[key%12]--;

	int voice = tMPoly_noteOff(mpoly, key);
	if (voice >= 0) tRampSetDest(ramp[voice], 0.0f);
	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		if (tMPoly_isOn(mpoly, i) == 1)
		{
			tRampSetDest(ramp[i], (float)(tMPoly_getVelocity(mpoly, i) * INV_TWO_TO_7));
			calculateFreq(i);
		}
	}
}

/**************** Helper Functions *********************/

void calculateFreq(int voice)
{
	float tempNote = tMPoly_getPitch(mpoly, voice);
	float tempPitchClass = ((((int)tempNote) - keyCenter) % 12 );
	float tunedNote = tempNote + centsDeviation[(int)tempPitchClass];
	freq[voice] = OOPS_midiToFrequency(tunedNote);
}

float nearestPeriod(float period)
{
	float leastDifference = fabsf(period - notePeriods[0]);
	float difference;
	int index = -1;

	int* chord = chordArray;
	if (autotuneLock > 0) chord = lockArray;

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
	float scaled_raw = raw_data;
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
	float scaled_raw = raw_data;
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
