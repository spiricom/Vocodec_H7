/* Includes ------------------------------------------------------------------*/
#include "sfx.h"
#include "main.h"
#include "codec.h"

#define NUM_FB_DELAY_TABLES 8
#define SCALE_LENGTH 7
#define TRIAD_LENGTH 3

float inBuffer[2048] __ATTR_RAM_D2;
float outBuffer[NUM_SHIFTERS][2048] __ATTR_RAM_D2;

tFormantShifter fs;
tPeriod p;
tPitchShift pshift[NUM_SHIFTERS];
tRamp ramp[MPOLY_NUM_MAX_VOICES];
tMPoly mpoly;
tSawtooth osc[NUM_VOICES][NUM_OSC];
tTalkbox vocoder;
tCycle sin1;
tNoise noise1;
tRamp rampFeedback;
tRamp rampSineFreq;
tRamp rampDelayFreq;
tHighpass highpass1;
tHighpass highpass2;
tEnvelopeFollower envFollowNoise;
tEnvelopeFollower envFollowSine;
tSVF lowpassVoc;
tSVF lowpassDel;
tSVF lowpassRev;
tSVF lowpassSyn;
tSVF highpassDel;
tSVF highpassRev;
tDelayL delay;
tPRCRev rev;

// sawtooth for harmonizer
tSawtooth saw;

#define FEEDBACK_LOOKUP_SIZE 5
#define DELAY_LOOKUP_SIZE 4
float FeedbackLookup[FEEDBACK_LOOKUP_SIZE] = { 0.0f, 0.8f, .999f, 1.0f, 1.03f };
//float DelayLookup[DELAY_LOOKUP_SIZE] = { 16000.f, 1850.f, 180.f, 40.f };
float DelayLookup[DELAY_LOOKUP_SIZE] = { 50.f, 180.f, 1400.f, 16300.f };
float feedbackDelayPeriod[NUM_FB_DELAY_TABLES];
//const float *feedbackDelayTable[NUM_FB_DELAY_TABLES] = { FB1, FB2, FB3, FB4, FB5, FB6, FB7, FB8 };
const int majorOffsets[7] = {0, 2, 4, 5, 7, 9, 11};
const int minorOffsets[7] = {0, 2, 3, 5, 7, 8, 10};

int count = 0;

/* PARAMS */
// Vocoder
float glideTimeVoc = 5.0f;
float lpFreqVoc = 10000.0f;
float detuneMaxVoc = 0.0f;

// Formant
float formantShiftFactor = -1.0f;
float formantKnob = 0.0f;

// PitchShift
float pitchFactor = 2.0f;
float formantShiftFactorPS = 0.0f;

// Autotune1

// Autotune2
float glideTimeAuto = 5.0f;

// Harmonizer
typedef enum Direction {
	NONE = 0,
	UP,
	DOWN
} Direction;

int sungNote = -1;
int prevSungNote = -1;
int playedNote = -1;
int prevPlayedNote = -1;

int prevPitchDetectedNote = -1;
int pitchDetectedSeq = 0;

int triad[3];
int tempTriad[3];
int lastTriad[3];
int shouldVoice = 0;
int lastSingleVoice = -1;

int harmonizerKey = 0;
int harmonizerScale = 0;
int harmonizerComplexity = 0;
int oldHarmonizerComplexity = 0;
int harmonizerMode = 0;

int harmonizeStep = -1;
int harmonizerSuccess = 0;
int computedFirst = 0;

// Delay
float hpFreqDel = 20.0f;
float lpFreqDel = 20000.0f;
float newDelay = 16000.0f;
float newFeedback = 0.4f;

float delayFeedbackSamp = 0.0f;

// Reverb
float hpFreqRev = 20.0f;
float lpFreqRev = 20000.0f;
float t60 = 5.0f;
float revMix = 0.5f;

// Bitcrusher
#define MAX_DEPTH 16
int rateRatio = 8;
int bitDepth = 8;

int lastSamp;
int sampCount = 0;

// Drumbox
int decayCoeff = 1000;
float newFreqDB = 100.0f;
float newDelayDB = 1000.0f;
float newFeedbackDB = 0.4f;

float dbFeedbackSamp = 0.0f;
float gainBoost = 1.0f;
float m_input1 = 0.0f;
float m_output0 = 0.0f;

// Synth
float glideTimeSynth = 5.0f;
float synthGain = 1.0f;
float lpFreqSynth = 5000.0f;
float detuneMaxSynth = 3.0f;

// Level
float inputLevel = 1.0f;
float outputLevel = 1.0f;

uint8_t numActiveVoices[ModeCount];

/* PSHIFT vars *************/


float freq[MPOLY_NUM_MAX_VOICES];

float detuneAmounts[NUM_VOICES][NUM_OSC];
float detuneSeeds[NUM_VOICES][NUM_OSC];

float notePeriods[128];
int chordArray[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int lockArray[12];

//float centsDeviation[12] = {0.0f, 0.12f, 0.04f, 0.16f, -0.14f, -0.02f, -0.10f, 0.02f, 0.14f, -0.16f, -0.04f, -0.12f};
float centsDeviation[12] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
int keyCenter = 5;

uint8_t autotuneLock = 0;
uint8_t formantCorrect[ModeCount];

/****************************************************************************************/

float nearestPeriod(float period);
float interpolateDelayControl(float raw_data);
float interpolateFeedback(float raw_data);
float ksTick(float noise_in);
void calculateFreq(int voice);
int harmonize();
void voice();
int voiceSingle(int* offsets, Direction dir);
int inKey(int note);
int calcDistance(int* x, int* y, int l);
int copyTriad(int* src, int* dest);
void sortTriad(int* x);
void sortTriadRelative(int* x);
void voiceAvoid(int* x);
void swap(int* x, int i, int j);

/****************************************************************************************/

void SFXInit(float sr, int blocksize)
{
	for (int i = 0; i < ModeCount; i++)
	{
		formantCorrect[i] = 0;
	}

	// Initialize the audio library. LEAF.
	LEAF_init(sr, blocksize, &randomNumber);

	tCycle_init(&sin1);
	tNoise_init(&noise1, PinkNoise);
	tRamp_init(&rampFeedback, 10.0f, 1);
	tRamp_init(&rampSineFreq, 10.0f, 1);
	tRamp_init(&rampDelayFreq, 10.0f, 1);
	tHighpass_init(&highpass1, 20.0f);
	tHighpass_init(&highpass2, 20.0f);
	tEnvelopeFollower_init(&envFollowNoise, 0.00001f, 0.0f);
	tEnvelopeFollower_init(&envFollowSine, 0.00001f, 0.0f);
	tDelayL_init(&delay, 1000.0f, 5000.0f);

	for (int i = 0; i < 128; i++)
	{
		notePeriods[i] = 1.0f / LEAF_midiToFrequency(i) * leaf.sampleRate;
	}

	tFormantShifter_init(&fs);

	tMPoly_init(&mpoly, MPOLY_NUM_MAX_VOICES);
	tMPoly_setPitchGlideTime(&mpoly, 50.0f);
	numActiveVoices[VocoderMode] = 1;
	numActiveVoices[AutotuneAbsoluteMode] = 1;
	numActiveVoices[SynthMode] = 1;
	numActiveVoices[HarmonizerMode] = 1;
	tTalkbox_init(&vocoder);
	for (int i = 0; i < NUM_VOICES; i++)
	{
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneSeeds[i][j] = randomNumber();
			tSawtooth_init(&osc[i][j]);
		}
	}

	for (int i = 0; i < MPOLY_NUM_MAX_VOICES; i++)
	{
		tRamp_init(&ramp[i], 100.0f, 1);
	}

	tPeriod_init(&p, inBuffer, outBuffer[0], 2048, PS_FRAME_SIZE);
	tPeriod_setWindowSize(&p, ENV_WINDOW_SIZE);
	tPeriod_setHopSize(&p, ENV_HOP_SIZE);

	/* Initialize devices for pitch shifting */
	for (int i = 0; i < NUM_SHIFTERS; ++i)
	{
		tPitchShift_init(&pshift[i], &p, outBuffer[i], 2048);
	}

	tSVF_init(&lowpassVoc, SVFTypeLowpass, 20000.0f, 1.0f);
	tSVF_init(&lowpassDel, SVFTypeLowpass, 20000.0f, 1.0f);
	tSVF_init(&lowpassRev, SVFTypeLowpass, 20000.0f, 1.0f);
	tSVF_init(&lowpassSyn, SVFTypeLowpass, 20000.0f, 1.0f);
	tSVF_init(&highpassDel, SVFTypeHighpass, 20.0f, 1.0f);
	tSVF_init(&highpassRev, SVFTypeHighpass, 20.0f, 1.0f);

	tPRCRev_init(&rev, 1.0f);

	// initialize sawtooth for harmonizer
	tSawtooth_init(&saw);

	//set up knobValsPerMode with the correct values so that the proper hysteresis occurs
	knobValsPerMode[VocoderMode][0] = (glideTimeVoc - 5.0f) / 999.0f;
	knobValsPerMode[VocoderMode][2] = (lpFreqVoc - 400.0f) / 17600.0f;
	knobValsPerMode[VocoderMode][3] = detuneMaxVoc;

	knobValsPerMode[FormantShiftMode][2] = formantKnob;

	knobValsPerMode[PitchShiftMode][1] = (formantShiftFactor + 1.0f) * 0.5f;
	knobValsPerMode[PitchShiftMode][2] = (pitchFactor - 0.5f) / 3.5f;

	knobValsPerMode[AutotuneAbsoluteMode][0] = (glideTimeAuto - 5.0f) / 999.0f;

	knobValsPerMode[DelayMode][0] = (hpFreqDel - 10.0f) / 10000.0f;
	knobValsPerMode[DelayMode][1] = (lpFreqDel - 100.0f) / 19900.0f;

	knobValsPerMode[ReverbMode][0] = (hpFreqRev - 10.0f) / 10000.0f;
	knobValsPerMode[ReverbMode][1] = (lpFreqRev - 100.0f) / 19900.0f;
	knobValsPerMode[ReverbMode][2] = t60 * 0.1f;
	knobValsPerMode[ReverbMode][3] = revMix;

	knobValsPerMode[HarmonizerMode][0] = 0.5f; // key
	knobValsPerMode[HarmonizerMode][1] = 0.5f; // scale
	knobValsPerMode[HarmonizerMode][2] = 0.5f; // complexity
	knobValsPerMode[HarmonizerMode][3] = 0.5f; // heat

	knobValsPerMode[BitcrusherMode][2] = rateRatio / 128.0f;
	knobValsPerMode[BitcrusherMode][3] = bitDepth / 16.0f;

	knobValsPerMode[DrumboxMode][1] = newFreqDB * newDelayDB / (4.0f * leaf.sampleRate);

	knobValsPerMode[SynthMode][0] = (glideTimeSynth - 5.0f) / 999.0f;
	knobValsPerMode[SynthMode][1] = synthGain;
	knobValsPerMode[SynthMode][2] = (lpFreqSynth - 400.0f) / 17600.0f;
	knobValsPerMode[SynthMode][3] = detuneMaxSynth;


	knobValsPerMode[LevelMode][2] = inputLevel / 3.0f;
	knobValsPerMode[LevelMode][3] = outputLevel / 3.0f;

}

void SFXVocoderFrame()
{
	tMPoly_setNumVoices(&mpoly, numActiveVoices[VocoderMode]);
	if (modeChain[chainIndex] == VocoderMode)
	{
		__KNOBCHECK1__ { glideTimeVoc = (knobVals[0] * 999.0f) + 5.0f; }

		__KNOBCHECK3__ { lpFreqVoc = ((knobVals[2]) * 17600.0f) + 400.0f; }
		__KNOBCHECK4__
		{
			for (int i = 0; i < tMPoly_getNumVoices(&mpoly); i++)
			{
				detuneMaxVoc = (knobVals[3]) * freq[i] * 0.05f;
			}
		}
	}
	tMPoly_setPitchGlideTime(&mpoly, glideTimeVoc);
	tSVF_setFreq(&lowpassVoc, lpFreqVoc);
	for (int i = 0; i < tMPoly_getNumVoices(&mpoly); i++)
	{
		tRamp_setDest(&ramp[i], (tMPoly_getVelocity(&mpoly, i) > 0));
		calculateFreq(i);
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneAmounts[i][j] = (detuneSeeds[i][j] * detuneMaxVoc) - (detuneMaxVoc * 0.5f);
			tSawtooth_setFreq(&osc[i][j], freq[i] + detuneAmounts[i][j]);
		}
	}
}
int32_t SFXVocoderTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(&mpoly);

	sample = (float) (input * INV_TWO_TO_31);

	for (int i = 0; i < tMPoly_getNumVoices(&mpoly); i++)
	{
		for (int j = 0; j < NUM_OSC; j++)
		{
			output += tSawtooth_tick(&osc[i][j]) * tRamp_tick(&ramp[i]);
		}
	}

	output *= INV_NUM_OSC * 0.5f;
	output = tTalkbox_tick(&vocoder, output, sample);
	output = tSVF_tick(&lowpassVoc, output);
	output = tanhf(output);

	return (int32_t) (output * TWO_TO_31);
}

void SFXFormantFrame()
{
	if (modeChain[chainIndex] == FormantShiftMode)
	{
		__KNOBCHECK3__
		{
			formantKnob = knobVals[2];
			formantShiftFactor = (formantKnob * 4.0f) - 2.0f;
		}
	}
}
int32_t SFXFormantTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31 * 2);

	output = tFormantShifter_tick(&fs, sample, formantShiftFactor);

	return (int32_t) (output * TWO_TO_31 * 0.5f);
}

void SFXPitchShiftFrame()
{
	if (modeChain[chainIndex] == PitchShiftMode)
	{
		__KNOBCHECK1__ { formantShiftFactorPS = (knobVals[0] * 2.0f) - 1.0f; }
		__KNOBCHECK3__ { pitchFactor = knobVals[2] * 3.5f + 0.50f; }
	}
	tPitchShift_setPitchFactor(&pshift[0], pitchFactor);
}

int32_t SFXPitchShiftTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

	if (formantCorrect[PitchShiftMode] > 0) sample = tFormantShifter_remove(&fs, sample * 2.0f);

	tPeriod_findPeriod(&p, sample);
	output = tPitchShift_shift(&pshift[0]);

	if (formantCorrect[PitchShiftMode] > 0) output = tFormantShifter_add(&fs, output, 0.0f) * 0.5f;

	count++;

	return (int32_t) (output * TWO_TO_31);
}

void SFXAutotuneNearestFrame()
{
	if (modeChain[chainIndex] == AutotuneNearestMode)
	{

	}
}

int32_t SFXAutotuneNearestTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

	if (formantCorrect[AutotuneNearestMode] > 0) sample = tFormantShifter_remove(&fs, sample * 2.0f);

	tPeriod_findPeriod(&p, sample);
	output = tPitchShift_shiftToFunc(&pshift[0], nearestPeriod);

	if (formantCorrect[AutotuneNearestMode] > 0) output = tFormantShifter_add(&fs, output, 0.0f) * 0.5f;

	return (int32_t) (output * TWO_TO_31);
}

void SFXAutotuneAbsoluteFrame()
{
	tMPoly_setNumVoices(&mpoly, numActiveVoices[AutotuneAbsoluteMode]);
	__KNOBCHECK1__
	{
		if (modeChain[chainIndex] == AutotuneAbsoluteMode)
		{
			glideTimeAuto = (knobVals[0] * 999.0f) + 5.0f;
		}
	}
	tMPoly_setPitchGlideTime(&mpoly, glideTimeAuto);
	for (int i = 0; i < tMPoly_getNumVoices(&mpoly); ++i)
	{
		calculateFreq(i);
	}
}
int32_t SFXAutotuneAbsoluteTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(&mpoly);

	sample = (float) (input * INV_TWO_TO_31);

	if (formantCorrect[AutotuneAbsoluteMode] > 0) sample = tFormantShifter_remove(&fs, sample * 2.0f);

	tPeriod_findPeriod(&p, sample);

	for (int i = 0; i < tMPoly_getNumVoices(&mpoly); ++i)
	{
		output += tPitchShift_shiftToFreq(&pshift[i], freq[i]) * tRamp_tick(&ramp[i]);
	}

	if (formantCorrect[AutotuneAbsoluteMode] > 0) output = tFormantShifter_add(&fs, output, 0.0f) * 0.5f;

	return (int32_t) (output * TWO_TO_31);
}

//int frameCount = 0;

void SFXHarmonizeFrame()
{
	int mpolyMonoNote = -1;
	int mpolyMonoVel = -1;

	tMPoly_setNumVoices(&mpoly, 1);

	__KNOBCHECK1__ { harmonizerKey = (int) floor(knobVals[0] * 11.0f + 0.5f); }
	__KNOBCHECK2__ { harmonizerScale = (int) floor(knobVals[1] + 0.5f); }
	__KNOBCHECK3__ {
		harmonizerComplexity = (int) floor(knobVals[2] * 3.0f + 0.5f);
		if (harmonizerMode == 0 || harmonizerMode == 1)
		{
			 if (harmonizerComplexity > 1) 
			 {
				 harmonizerComplexity = 1;
			 }
		}
		else if (harmonizerMode == 2)
		{
			if (harmonizerComplexity > 2) 
			{
				 harmonizerComplexity = 2;
			 }
		}
	}
	__KNOBCHECK4__ { harmonizerMode = (int) floor(knobVals[3] * 4.0f + 0.5f); }

	numActiveVoices[HarmonizerMode] = harmonizerComplexity;

	// get mono pitch
	mpolyMonoNote = tMPoly_getPitch(&mpoly, 0);
	mpolyMonoVel = tMPoly_getVelocity(&mpoly, 0);

	if (mpolyMonoVel > 0)
	{
		playedNote = mpolyMonoNote;
	}
	else
	{
		playedNote = -1;
	}

	if (prevSungNote != sungNote || prevPlayedNote != playedNote)
	{
		if (harmonizeStep == -1) 
		{
			harmonizeStep = 0;
		}
	}

	// step through computation
	if (harmonizeStep != -1) {
		harmonizerSuccess = harmonize();
		if (harmonizerSuccess == 1)
		{
			if (harmonizeStep == 1)
			{
				computedFirst = 1;
				prevSungNote = sungNote;
				prevPlayedNote = playedNote;
				harmonizeStep = -1;
			}
			else
			{
				harmonizeStep++;
			}
		}
		else
		{
			harmonizeStep = -1;
		}
	}
}
int32_t SFXHarmonizeTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;
	float freq;

	tMPoly_tick(&mpoly);

	sample = (float) (input * INV_TWO_TO_31);

	// attenuate to simulate velocity
	output += sample * 0.2;

	freq = leaf.sampleRate / tPeriod_findPeriod(&p, sample);

	// sungNote smoothing
	int pitchDetectedNote = round(LEAF_frequencyToMidi(freq));
	if (pitchDetectedNote != sungNote)
	{
		if (pitchDetectedNote == prevPitchDetectedNote)
		{
			pitchDetectedSeq++;

			// wait for # of same pitchDetected notes in a row, then change
			if (pitchDetectedSeq > 2048)
			{
				if (pitchDetectedNote <= 127 && pitchDetectedNote >= 0)
				{
					sungNote = pitchDetectedNote;
				}
			}
		}
		else
		{
			pitchDetectedSeq = 0;
			prevPitchDetectedNote = pitchDetectedNote;
		}
	}

	if (computedFirst == 0)
	{
		return (int32_t) (output * TWO_TO_31);
	}

	if (oldHarmonizerComplexity < harmonizerComplexity)
	{
		// ramp in new voice and ramp other voices
		tRamp_setDest(&ramp[harmonizerComplexity], ramp[0].dest);
	}
	else if (oldHarmonizerComplexity > harmonizerComplexity)
	{
		// ramp out old voice and ramp other voices
		tRamp_setDest(&ramp[oldHarmonizerComplexity], 0.0f);
	}

	oldHarmonizerComplexity = harmonizerComplexity;

	for (int i = 0; i < harmonizerComplexity; i++)
	{
		tRamp_setDest(&ramp[i + 1], ramp[0].dest);
	}

	// tick all ramps
	for (int i = 0; i < 4; i++)
	{
		tRamp_tick(&ramp[i]);
	}

	// pitch shifting
	sample = tFormantShifter_remove(&fs, sample * 2.0f);

	for (int i = 0; i < harmonizerComplexity; i++)
	{
		output += tPitchShift_shiftToFreq(&pshift[i], LEAF_midiToFrequency(triad[i] + (transpose * 12))) * tRamp_sample(&ramp[i]);
	}

	output = tFormantShifter_add(&fs, output, 0.0f) * 0.5f;

	output /= (float) (1 + harmonizerComplexity);

	return (int32_t) (output * TWO_TO_31);
}

void SFXDelayFrame()
{
	if (modeChain[chainIndex] == DelayMode)
	{
		__KNOBCHECK1__
		{
			hpFreqDel = ((knobVals[0]) * 10000.0f) + 10.0f;
			if (hpFreqDel > lpFreqDel) hpFreqDel = lpFreqDel;
		}
		__KNOBCHECK2__
		{
			lpFreqDel = ((knobVals[1]) * 19900.0f) + 100.0f;
			if (lpFreqDel < hpFreqDel) lpFreqDel = hpFreqDel;
		}
		__KNOBCHECK3__ { newDelay = interpolateDelayControl(1.0f - knobVals[2]); }
		__KNOBCHECK4__ { newFeedback = interpolateFeedback(knobVals[3]); }


	}
	tRamp_setDest(&rampFeedback, newFeedback);
	tRamp_setDest(&rampDelayFreq, newDelay);

	tSVF_setFreq(&lowpassDel, lpFreqDel);
	tSVF_setFreq(&highpassDel, hpFreqDel);
}

int32_t SFXDelayTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

	tDelayL_setDelay(&delay, tRamp_tick(&rampDelayFreq));

	output = sample + (delayFeedbackSamp * tRamp_tick(&rampFeedback));

	output = tDelayL_tick(&delay, output);

	output = tSVF_tick(&lowpassDel, output);
	output = tSVF_tick(&highpassDel, output);

	delayFeedbackSamp = output;

	output += sample;

	return (int32_t) (output * TWO_TO_31);
}

void SFXReverbFrame()
{
	if (modeChain[chainIndex] == ReverbMode)
	{
		__KNOBCHECK1__
		{
			hpFreqRev = ((knobVals[0]) * 10000.0f) + 10.0f;
			if (hpFreqRev > lpFreqRev) hpFreqRev = lpFreqRev;
		}
		__KNOBCHECK2__
		{
			lpFreqRev = ((knobVals[1]) * 19900.0f) + 100.0f;
			if (lpFreqRev < hpFreqRev) lpFreqRev = hpFreqRev;
		}
		__KNOBCHECK3__ { t60 = knobVals[2] * 10.0f; }
		__KNOBCHECK4__ { revMix = knobVals[3] * 1.0f; }

	}
	tSVF_setFreq(&lowpassRev, lpFreqRev);
	tSVF_setFreq(&highpassRev, hpFreqRev);
	tPRCRev_setT60(&rev, t60);
	tPRCRev_setMix(&rev, revMix);
}

int32_t SFXReverbTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

	output = tPRCRev_tick(&rev, sample);

	output = tSVF_tick(&lowpassRev, output);
	output = tSVF_tick(&highpassRev, output);

	return (int32_t) (output * TWO_TO_31);
}

void SFXBitcrusherFrame()
{
	if (modeChain[chainIndex] == BitcrusherMode)
	{
		__KNOBCHECK3__ { rateRatio = (((int) (knobVals[2] * 128)) > 0) ? (int) (knobVals[2] * 128) : 1; }
		__KNOBCHECK4__ { bitDepth = (((int) (knobVals[3] * 16)) > 0) ? (int) (knobVals[3] * 16) : 1; }
	}
}
int32_t SFXBitcrusherTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

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

	return (int32_t) (output * TWO_TO_31);
}

void SFXDrumboxFrame()
{
//	if (modeChain[chainIndex] == DrumboxMode)
//	{
//		__KNOBCHECK1__ { decayCoeff = knobVals[0] * 2000; }
//		__KNOBCHECK2__ { newFreqDB =  ((knobVals[1]) * 4.0f) * ((1.0f / newDelayDB) * leaf.sampleRate); }
//		__KNOBCHECK3__ { newDelayDB = interpolateDelayControl(1.0f - knobVals[2]); }
//		__KNOBCHECK4__ { newFeedbackDB = interpolateFeedback(knobVals[3]); }
//	}
//	tRamp_setDest(rampFeedback, newFeedbackDB);
//	tRamp_setDest(rampDelayFreq, newDelayDB);
//	tRamp_setDest(rampSineFreq, newFreqDB);
//	tEnvelopeFollower_decayCoeff(envFollowSine, decayCoeffTable[decayCoeff]);
//	tEnvelopeFollower_decayCoeff(envFollowNoise,0.80f);
}

int32_t SFXDrumboxTick(int32_t input)
{
	float audioIn = 0.0f;
	float sample = 0.0f;
	float output = 0.0f;

	audioIn = (float) (input * INV_TWO_TO_31);

	tCycle_setFreq(&sin1, tRamp_tick(&rampSineFreq));

	tDelayL_setDelay(&delay, tRamp_tick(&rampDelayFreq));

	sample = ((ksTick(audioIn) * 0.7f) + audioIn * 0.8f);
	float tempSinSample = LEAF_shaper(((tCycle_tick(&sin1) * tEnvelopeFollower_tick(&envFollowSine, audioIn)) * 0.6f), 0.5f);
	sample += tempSinSample * 0.6f;
	sample += (tNoise_tick(&noise1) * tEnvelopeFollower_tick(&envFollowNoise, audioIn));

	sample *= gainBoost;

	sample = tHighpass_tick(&highpass1, sample);
	sample = LEAF_shaper(sample * 0.6, 1.0f);

	return (int32_t) (sample * TWO_TO_31);
}

void SFXSynthFrame()
{
	tMPoly_setNumVoices(&mpoly, numActiveVoices[SynthMode]);
	if (modeChain[chainIndex] == SynthMode)
	{
		__KNOBCHECK1__ { glideTimeSynth = (knobVals[0] * 999.0f) + 5.0f; }
		__KNOBCHECK2__ { synthGain = knobVals[1]; }
		__KNOBCHECK3__ { lpFreqSynth = ((knobVals[2]) * 17600.0f) + 400.0f; }
		__KNOBCHECK4__
		{
			for (int i = 0; i < tMPoly_getNumVoices(&mpoly); i++)
			{
				detuneMaxSynth = (knobVals[3]) * freq[i] * 0.05f;
			}
		}
	}
	tSVF_setFreq(&lowpassSyn, lpFreqSynth);
	tMPoly_setPitchGlideTime(&mpoly, glideTimeSynth);
	for (int i = 0; i < tMPoly_getNumVoices(&mpoly); i++)
	{
		calculateFreq(i);
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneAmounts[i][j] = (detuneSeeds[i][j] * detuneMaxSynth) - (detuneMaxSynth * 0.5f);
			tSawtooth_setFreq(&osc[i][j], freq[i] + detuneAmounts[i][j]);
		}
	}
}

int32_t SFXSynthTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(&mpoly);

	for (int i = 0; i < tMPoly_getNumVoices(&mpoly); i++)
	{
		for (int j = 0; j < NUM_OSC; j++)
		{
			sample += tSawtooth_tick(&osc[i][j]) * tRamp_tick(&ramp[i]);
		}
	}

	sample = tSVF_tick(&lowpassSyn, sample) * INV_NUM_OSC * 0.5;

	output = tanhf(sample * synthGain); // gain before or after clip?

	return ((int32_t) (output * TWO_TO_31) + input);
}

void SFXDrawFrame()
{

}

int32_t SFXDrawTick(int32_t input)
{
	return input;
}

void SFXLevelFrame()
{
	if (modeChain[chainIndex] == LevelMode)
	{
		__KNOBCHECK3__ { inputLevel = knobVals[2] * 3.0f; }
		__KNOBCHECK4__ { outputLevel = knobVals[3] * 3.0f; }
	}
}
int32_t SFXLevelTick(int32_t input)
{
	float output = 0.0f;

	output = (float) (input * INV_TWO_TO_31);
	return (int32_t) (output * TWO_TO_31);
}

/**************** MPoly Handling ***********************/

void SFXNoteOn(int key, int velocity)
{
	chordArray[key%12]++;

	tMPoly_noteOn(&mpoly, key, velocity);

	for (int i = 0; i < mpoly.numVoices; i++)
	{
		if (tMPoly_isOn(&mpoly, i) == 1)
		{
			tRamp_setDest(&ramp[i], 1.0f);
			calculateFreq(i);
		}
	}
}

void SFXNoteOff(int key, int velocity)
{
	if (chordArray[key%12] > 0) chordArray[key%12]--;

	int voice = tMPoly_noteOff(&mpoly, key);
	if (voice >= 0) tRamp_setDest(&ramp[voice], 0.0f);

	for (int i = 0; i < mpoly.numVoices; i++)
	{
		if (tMPoly_isOn(&mpoly, i) == 1)
		{
			tRamp_setDest(&ramp[i], 1.0f);
			calculateFreq(i);
		}
	}
}

/**************** Helper Functions *********************/

int harmonize()
{
	if (harmonizeStep == 0)
	{
		// section 1

		int* offsets;

		if (sungNote == -1 || playedNote == -1 || inKey(playedNote) == 0)
		{
			return 0;
		}

		if (harmonizerScale == 1)
		{
			offsets = minorOffsets;
		}
		else
		{
			offsets = majorOffsets;
		}

		int startIndex = -1;
		for (int i = 0; i < SCALE_LENGTH; i++)
		{
			if ((playedNote % 12 - harmonizerKey + 12) % 12 == offsets[i])
			{
				startIndex = i;
				break;
			}
		}

		int triadIndex = 0;
		for (int i = startIndex; i < startIndex + 5; i += 2)
		{
			int noteOffset;
			if (i < SCALE_LENGTH)
			{
				noteOffset = offsets[i];
			}
			else
			{
				noteOffset = offsets[i % SCALE_LENGTH] + 12;
			}
			triad[triadIndex] = noteOffset + sungNote - ((sungNote - harmonizerKey) % 12);
			triadIndex++;
		}
		return 1;
	}
	else
	{
		// section 2
		int* offsets;

		if (sungNote == -1 || playedNote == -1 || inKey(playedNote) == 0)
		{
			return 0;
		}

		if (harmonizerScale == 1)
		{
			offsets = minorOffsets;
		}
		else
		{
			offsets = majorOffsets;
		}

		if (harmonizerMode == 0)
		{
			triad[0] = voiceSingle(offsets, UP);
		}
		else if (harmonizerMode == 1)
		{
			triad[0] = voiceSingle(offsets, DOWN);
		}
		else if (harmonizerMode == 2)
		{
			int upVoice = voiceSingle(offsets, UP);
			int downVoice = voiceSingle(offsets, DOWN);
			triad[0] = upVoice;
			triad[1] = downVoice;
		}
		else
		{
			// triad ends up with best voicing
			if (shouldVoice == 1)
			{
				voice();
			}

			// always voice after triad has been voiced
			shouldVoice = 1;

			// preserve voiced triad in lastTriad
			copyTriad(triad, lastTriad);

			// sort from closest to farthest from sung note
			sortTriadRelative(triad);

			voiceAvoid(triad);
		}

		return 1;
	}
}

void voice()
{
    // set to first inversion down
    triad[2] = triad[2] - 12;

    // copy triad to be rearranged and evaluated
	int evalTriad[3];
	copyTriad(triad, evalTriad);

    // root position
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

    int distance = calcDistance(evalTriad, lastTriad, TRIAD_LENGTH);
	int bestDistance = distance;

    distance = calcDistance(evalTriad, lastTriad, TRIAD_LENGTH);
    if (harmonizerMode != 3)
    {
		if (distance < bestDistance)
		{
			bestDistance = distance;
			copyTriad(evalTriad, triad);
		}
    }
    else
    {
    	if (distance > bestDistance)
		{
			bestDistance = distance;
			copyTriad(evalTriad, triad);
		}
    }

    // first inversion up
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

	distance = calcDistance(evalTriad, lastTriad, TRIAD_LENGTH);
    if (harmonizerMode != 3)
	{
		if (distance < bestDistance)
		{
			copyTriad(evalTriad, triad);
		}
	}
	else
	{
		if (distance > bestDistance)
		{
			copyTriad(evalTriad, triad);
		}
	}

    // second inversion up
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

    // transpose two octaves down
    for (int i = 0; i < TRIAD_LENGTH; i++)
    {
    	evalTriad[i] = evalTriad[i] - 24;
    }

    // second inversion down
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

    // first inversion down
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

    distance = calcDistance(evalTriad, lastTriad, TRIAD_LENGTH);
    if (harmonizerMode != 3)
	{
		if (distance < bestDistance)
		{
			copyTriad(evalTriad, triad);
		}
	}
	else
	{
		if (distance > bestDistance)
		{
			copyTriad(evalTriad, triad);
		}
	}
}

// TODO: free up constraints on playedNote changes, think about lastSingleVoice
int voiceSingle(int* offsets, Direction dir)
{
	int evalTriad[3];
	copyTriad(triad, evalTriad);

	// transpose triad notes to be strictly greater or less than sung note
	for (int i = 0; i < TRIAD_LENGTH; i++)
	{
		if (evalTriad[i] < sungNote && dir == UP)
		{
			evalTriad[i] = evalTriad[i] + 12;
		}
		else if (evalTriad[i] > sungNote && dir == DOWN)
		{
			evalTriad[i] = evalTriad[i] - 12;
		}
	}


	// sort triad relative to sungNote
	sortTriadRelative(evalTriad);

	// detect whether the sungNote is a chord tone or not
	int chordTone = 0;

	for (int i = 0; i < TRIAD_LENGTH; i++)
	{
		if ((sungNote - harmonizerKey) % 12 == (evalTriad[i] - harmonizerKey) % 12)
		{
			chordTone = 1;
			break;
		}
	}
	
	int voice = evalTriad[0];

	if (chordTone == 1)
	{
		// find closest chord tone harmonization
		for (int i = 0; i < TRIAD_LENGTH; i++)
		{
			if (evalTriad[i] != sungNote)
			{
				voice = evalTriad[i];
				break;
			}
		}
	}
	else
	{
		// find closest none chord tone harmonization
		for (int i = 0; i < SCALE_LENGTH; i++)
		{
			if (offsets[i] == (voice - harmonizerKey) % 12)
			{
				// make sure it moves to a non-chord tone in the direction of the melody
				if (dir == UP)
				{
					if (i != SCALE_LENGTH - 1)
					{
						// move up one scale degree
						voice += offsets[(i + 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i];
					}
					else
					{
						voice += offsets[(i + 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i] + 12;
					}
				}
				else
				{

					if (i != 0)
					{
						// move down one scale degree
						voice += offsets[(i - 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i];
					}
					else
					{
						voice += offsets[(i - 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i] - 12;
					}
				}
				break;
			}
		}
	}

	return voice;
}

int calcDistance(int* x, int* y, int l)
{
	int d = 0;
	for (int i = 0; i < l; i++)
	{
		d += abs(x[i] - y[i]);
	}
	return d;
}

int copyTriad(int* src, int* dest) {
	for (int i = 0; i < TRIAD_LENGTH; i++)
	{
		dest[i] = src[i];
	}
	return 1;
}

void sortTriad(int* x)
{
	// simple sort method for 3 integer array
	if (x[0] > x[1])
	{
		// swap first two elements
		swap(x, 0, 1);
	}
	if (x[1] > x[2])
	{
		// swap second two elements
		swap(x, 1, 2);
	}
	if (x[0] > x[1])
	{
		// swap first two elements
		swap(x, 0, 1);
	}
}

void sortTriadRelative(int* x)
{
	// sort method for 3 integer array relative to given note
	if (abs(x[0] - sungNote) > abs(x[1] - sungNote))
	{
		// swap first two elements
		swap(x, 0, 1);
	}
	if (abs(x[1] - sungNote) > abs(x[2] - sungNote))
	{
		// swap second two elements
		swap(x, 1, 2);
	}
	if (abs(x[0] - sungNote) > abs(x[1] - sungNote))
	{
		// swap first two elements
		swap(x, 0, 1);
	}
}

void voiceAvoid(int* x)
{
	if (abs(x[0] - sungNote) == 0)
	{
		// if the closest note is the same as the sung note, move to end
		swap(x, 0, 1);
		swap(x, 1, 2);
	}
	if (abs(x[1] - sungNote) == 0)
	{
		// if the second-closest note is the same as the sung note, move to end
		swap(x, 1, 2);
	}
}

void swap(int* x, int i, int j) {
	int t;
	t = x[i];
	x[i] = x[j];
	x[j] = t;
}

int inKey(int note)
{
	int offset = (note + 12) % 12;
	for (int i = 0; i < SCALE_LENGTH; i++)
	{
		if (harmonizerScale > 0.5f)
		{
			if (offset == ((minorOffsets[i] + harmonizerKey + 12) % 12)) return 1;
		}
		else
		{
			if (offset == ((majorOffsets[i] + harmonizerKey + 12) % 12)) return 1;
		}
	}
	return 0;
}


void calculateFreq(int voice)
{
	float tempNote = tMPoly_getPitch(&mpoly, voice);
	float tempPitchClass = ((((int)tempNote) - keyCenter) % 12 );
	float tunedNote = tempNote + centsDeviation[(int)tempPitchClass];
	freq[voice] = LEAF_midiToFrequency(tunedNote);
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
	temp_sample = noise_in + (dbFeedbackSamp * tRamp_tick(&rampFeedback)); //feedback param actually

	dbFeedbackSamp = tDelayL_tick(&delay, temp_sample);

	m_output0 = 0.5f * m_input1 + 0.5f * dbFeedbackSamp;
	m_input1 = dbFeedbackSamp;
	dbFeedbackSamp = m_output0;
	temp_sample = (tHighpass_tick(&highpass2, dbFeedbackSamp)) * 0.5f;
	temp_sample = LEAF_shaper(temp_sample, 1.5f);

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
