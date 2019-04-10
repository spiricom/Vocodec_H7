/* Includes ------------------------------------------------------------------*/
#include "sfx.h"
#include "main.h"
#include "codec.h"
#include "OOPSWavetables.h"

#define NUM_FB_DELAY_TABLES 8
#define SCALE_LENGTH 7
#define TRIAD_LENGTH 3

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
tSVF* lowpassVoc;
tSVF* lowpassDel;
tSVF* lowpassRev;
tSVF* lowpassSyn;
tSVF* highpassDel;
tSVF* highpassRev;
tDelayL* delay;
tPRCRev* rev;

// sawtooth for harmonizer
tSawtooth* saw;

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

typedef enum NoteType {
	NEITHER = 0,
	PLAYED,
	SUNG
} NoteType;

int sungNote = -1;
int prevSungNote = -1;
int playedNote = -1;
int prevPlayedNote = -1;
int latchedNote = -1;

int prevPitchDetectedNote = -1;
int pitchDetectedSeq = 0;

int triad[3];
int tempTriad[3];
int lastTriad[3];
int shouldVoice = 0;
int lastSingleVoice = -1;
Direction voiceDirection = NONE;
NoteType harmonizeMoved = NEITHER;

int harmonizerKey = 0;
int harmonizerScale = 0;
int harmonizerComplexity = 0;
int oldHarmonizerComplexity = 0;
int harmonizerMode = 0;

int harmonizeStep = -1;
int harmonizerSuccess = 0;
int computedFirst = 0;

InputMode harmonizerInputMode = Momentary;

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
void voice(int* bestTriad);
void voiceSingle(int* offsets, Direction dir);
int inKey(int note);
int calcDistance(int* x, int* y, int l);
int copyTriad(int* src, int* dest);
void sortTriad(int* x);
void sortTriadRelative(int* x);
void swap(int* x, int i, int j);

/****************************************************************************************/

void SFXInit(float sr, int blocksize)
{
	for (int i = 0; i < ModeCount; i++)
	{
		formantCorrect[i] = 0;
	}

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
	delay = tDelayLInit(1000.0f);

	for (int i = 0; i < 128; i++)
	{
		notePeriods[i] = 1.0f / OOPS_midiToFrequency(i) * oops.sampleRate;
	}

	fs = tFormantShifterInit();

	mpoly = tMPoly_init(MPOLY_NUM_MAX_VOICES);
	tMPoly_setPitchGlideTime(mpoly, 50.0f);
	numActiveVoices[VocoderMode] = 1;
	numActiveVoices[AutotuneAbsoluteMode] = 1;
	numActiveVoices[SynthMode] = 1;
	numActiveVoices[HarmonizerMode] = 1;
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
		ramp[i] = tRampInit(100.0f, 1);
	}

	p = tPeriod_init(inBuffer, outBuffer[0], 2048, PS_FRAME_SIZE);
	tPeriod_setWindowSize(p, ENV_WINDOW_SIZE);
	tPeriod_setHopSize(p, ENV_HOP_SIZE);

	/* Initialize devices for pitch shifting */
	for (int i = 0; i < NUM_SHIFTERS; ++i)
	{
		pshift[i] = tPitchShift_init(p, outBuffer[i], 2048);
	}

	lowpassVoc = tSVFInit(SVFTypeLowpass, 20000.0f, 1.0f);
	lowpassDel = tSVFInit(SVFTypeLowpass, 20000.0f, 1.0f);
	lowpassRev = tSVFInit(SVFTypeLowpass, 20000.0f, 1.0f);
	lowpassSyn = tSVFInit(SVFTypeLowpass, 20000.0f, 1.0f);
	highpassDel = tSVFInit(SVFTypeHighpass, 20.0f, 1.0f);
	highpassRev = tSVFInit(SVFTypeHighpass, 20.0f, 1.0f);

	rev = tPRCRevInit(1.0f);

	// initialize sawtooth for harmonizer
	saw = tSawtoothInit();

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

	knobValsPerMode[DrumboxMode][1] = newFreqDB * newDelayDB / (4.0f * oops.sampleRate);

	knobValsPerMode[SynthMode][0] = (glideTimeSynth - 5.0f) / 999.0f;
	knobValsPerMode[SynthMode][1] = synthGain;
	knobValsPerMode[SynthMode][2] = (lpFreqSynth - 400.0f) / 17600.0f;
	knobValsPerMode[SynthMode][3] = detuneMaxSynth;


	knobValsPerMode[LevelMode][2] = inputLevel / 3.0f;
	knobValsPerMode[LevelMode][3] = outputLevel / 3.0f;

}

void SFXVocoderFrame()
{
	tMPoly_setNumVoices(mpoly, numActiveVoices[VocoderMode]);
	if (modeChain[chainIndex] == VocoderMode)
	{
		__KNOBCHECK1__ { glideTimeVoc = (knobVals[0] * 999.0f) + 5.0f; }

		__KNOBCHECK3__ { lpFreqVoc = ((knobVals[2]) * 17600.0f) + 400.0f; }
		__KNOBCHECK4__
		{
			for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
			{
				detuneMaxVoc = (knobVals[3]) * freq[i] * 0.05f;
			}
		}
	}
	tMPoly_setPitchGlideTime(mpoly, glideTimeVoc);
	tSVFSetFreq(lowpassVoc, lpFreqVoc);
	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		tRampSetDest(ramp[i], (tMPoly_getVelocity(mpoly, i) > 0));
		calculateFreq(i);
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneAmounts[i][j] = (detuneSeeds[i][j] * detuneMaxVoc) - (detuneMaxVoc * 0.5f);
			tSawtoothSetFreq(osc[i][j], freq[i] + detuneAmounts[i][j]);
		}
	}
}
int32_t SFXVocoderTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(mpoly);

	sample = (float) (input * INV_TWO_TO_31);

	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		for (int j = 0; j < NUM_OSC; j++)
		{
			output += tSawtoothTick(osc[i][j]) * tRampTick(ramp[i]);
		}
	}

	output *= INV_NUM_OSC * 0.5f;
	output = tTalkboxTick(vocoder, output, sample);
	output = tSVFTick(lowpassVoc, output);
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

	output = tFormantShifterTick(fs, sample, formantShiftFactor);

	return (int32_t) (output * TWO_TO_31 * 0.5f);
}

void SFXPitchShiftFrame()
{
	if (modeChain[chainIndex] == PitchShiftMode)
	{
		__KNOBCHECK1__ { formantShiftFactorPS = (knobVals[0] * 2.0f) - 1.0f; }
		__KNOBCHECK3__ { pitchFactor = knobVals[2] * 3.5f + 0.50f; }
	}
	tPitchShift_setPitchFactor(pshift[0], pitchFactor);
}

int32_t SFXPitchShiftTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

	if (formantCorrect[PitchShiftMode] > 0) sample = tFormantShifterRemove(fs, sample * 2.0f);

	tPeriod_findPeriod(p, sample);
	output = tPitchShift_shift(pshift[0]);

	if (formantCorrect[PitchShiftMode] > 0) output = tFormantShifterAdd(fs, output, 0.0f) * 0.5f;

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

	if (formantCorrect[AutotuneNearestMode] > 0) sample = tFormantShifterRemove(fs, sample * 2.0f);

	tPeriod_findPeriod(p, sample);
	output = tPitchShift_shiftToFunc(pshift[0], nearestPeriod);

	if (formantCorrect[AutotuneNearestMode] > 0) output = tFormantShifterAdd(fs, output, 0.0f) * 0.5f;

	return (int32_t) (output * TWO_TO_31);
}

void SFXAutotuneAbsoluteFrame()
{
	tMPoly_setNumVoices(mpoly, numActiveVoices[AutotuneAbsoluteMode]);
	__KNOBCHECK1__
	{
		if (modeChain[chainIndex] == AutotuneAbsoluteMode)
		{
			glideTimeAuto = (knobVals[0] * 999.0f) + 5.0f;
		}
	}
	tMPoly_setPitchGlideTime(mpoly, glideTimeAuto);
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

	sample = (float) (input * INV_TWO_TO_31);

	if (formantCorrect[AutotuneAbsoluteMode] > 0) sample = tFormantShifterRemove(fs, sample * 2.0f);

	tPeriod_findPeriod(p, sample);

	for (int i = 0; i < tMPoly_getNumVoices(mpoly); ++i)
	{
		output += tPitchShift_shiftToFreq(pshift[i], freq[i]) * tRampTick(ramp[i]);
	}

	if (formantCorrect[AutotuneAbsoluteMode] > 0) output = tFormantShifterAdd(fs, output, 0.0f) * 0.5f;

	return (int32_t) (output * TWO_TO_31);
}

//int frameCount = 0;

void SFXHarmonizeFrame()
{
	tMPoly_setNumVoices(mpoly, 1);

	__KNOBCHECK1__ { harmonizerKey = (int) floor(knobVals[0] * 11.0f + 0.5f); }
	__KNOBCHECK2__ { harmonizerScale = (int) floor(knobVals[1] + 0.5f); }
	__KNOBCHECK3__ {
		harmonizerComplexity = (int) floor(knobVals[2] * 3.0f + 0.5f);
		if (harmonizerMode == 0)
		{
			 if (harmonizerComplexity > 1) {
				 harmonizerComplexity = 1;
			 }
		}
	}
	__KNOBCHECK4__ { harmonizerMode = (int) floor(knobVals[3] * 3.0f + 0.5f); }

	if (prevSungNote != sungNote || prevPlayedNote != playedNote)
	{
		if (harmonizeStep == -1) {
			harmonizeStep = 0;
			if (prevPlayedNote != playedNote)
			{
				harmonizeMoved = PLAYED;
			} else {
				harmonizeMoved = SUNG;
			}
		}
	}  else {
		harmonizeMoved = NONE;
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
	int mpolyMonoNote = -1;
	int mpolyMonoVel = -1;
	int voices;

	// get mono pitch
	tMPoly_tick(mpoly);
	mpolyMonoNote = tMPoly_getPitch(mpoly, 0);
	mpolyMonoVel = tMPoly_getVelocity(mpoly, 0);

	// set playedNote based on whether latching is turned on
	if (harmonizerInputMode == Latch)
	{
		if (mpolyMonoVel > 0)
		{
			latchedNote = mpolyMonoNote;
		}
	}
	else
	{
		latchedNote = -1;
	}

	if (mpolyMonoVel > 0)
	{
		playedNote = mpolyMonoNote;
	}
	else
	{
		playedNote = -1;
	}

	sample = (float) (input * INV_TWO_TO_31);

	// attenuate to simulate velocity
	output += sample * 0.2;

	freq = oops.sampleRate / tPeriod_findPeriod(p, sample);

	// sungNote smoothing
	int pitchDetectedNote = round(OOPS_frequencyToMidi(freq));
	if (pitchDetectedNote != sungNote)
	{
		if (pitchDetectedNote == prevPitchDetectedNote)
		{
			pitchDetectedSeq++;

			// wait for # of same pitchDetected notes in a row, then change
			if (pitchDetectedSeq > 6000)
			{
				if (pitchDetectedNote <= 127 && pitchDetectedNote >= 0)
				{
					sungNote = pitchDetectedNote;

					// voice direction
					if (sungNote > prevSungNote)
					{
						voiceDirection = UP;
					}
					else
					{
						voiceDirection = DOWN;
					}
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
		tRampSetDest(ramp[harmonizerComplexity], ramp[0]->dest);
	}
	else if (oldHarmonizerComplexity > harmonizerComplexity)
	{
		// ramp out old voice and ramp other voices
		tRampSetDest(ramp[oldHarmonizerComplexity], 0.0f);
	}

	oldHarmonizerComplexity = harmonizerComplexity;

	for (int i = 0; i < harmonizerComplexity; i++)
	{
		tRampSetDest(ramp[i + 1], ramp[0]->dest);
	}

	// tick all ramps
	for (int i = 0; i < 4; i++)
	{
		tRampTick(ramp[i]);
	}

	// pitch shifting
	sample = tFormantShifterRemove(fs, sample * 2.0f);

	if (numActiveVoices[HarmonizerMode] < harmonizerComplexity) {
		voices = numActiveVoices[HarmonizerMode];
	} else {
		voices = harmonizerComplexity;
	}

	for (int i = 0; i < voices; i++)
	{
		output += tPitchShift_shiftToFreq(pshift[i], OOPS_midiToFrequency(triad[i])) * tRampSample(ramp[i + 1]);
	}

	output = tFormantShifterAdd(fs, output, 0.0f) * 0.5f;

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
	tRampSetDest(rampFeedback, newFeedback);
	tRampSetDest(rampDelayFreq, newDelay);

	tSVFSetFreq(lowpassDel, lpFreqDel);
	tSVFSetFreq(highpassDel, hpFreqDel);
}

int32_t SFXDelayTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

	tDelayLSetDelay(delay, tRampTick(rampDelayFreq));

	output = sample + (delayFeedbackSamp * tRampTick(rampFeedback));

	output = tDelayLTick(delay, output);

	output = tSVFTick(lowpassDel, output);
	output = tSVFTick(highpassDel, output);

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
	tSVFSetFreq(lowpassRev, lpFreqRev);
	tSVFSetFreq(highpassRev, hpFreqRev);
	tPRCRevSetT60(rev, t60);
	tPRCRevSetMix(rev, revMix);
}

int32_t SFXReverbTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	sample = (float) (input * INV_TWO_TO_31);

	output = tPRCRevTick(rev, sample);

	output = tSVFTick(lowpassRev, output);
	output = tSVFTick(highpassRev, output);

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
	if (modeChain[chainIndex] == DrumboxMode)
	{
		__KNOBCHECK1__ { decayCoeff = knobVals[0] * 2000; }
		__KNOBCHECK2__ { newFreqDB =  ((knobVals[1]) * 4.0f) * ((1.0f / newDelayDB) * oops.sampleRate); }
		__KNOBCHECK3__ { newDelayDB = interpolateDelayControl(1.0f - knobVals[2]); }
		__KNOBCHECK4__ { newFeedbackDB = interpolateFeedback(knobVals[3]); }
	}
	tRampSetDest(rampFeedback, newFeedbackDB);
	tRampSetDest(rampDelayFreq, newDelayDB);
	tRampSetDest(rampSineFreq, newFreqDB);
	tEnvelopeFollowerDecayCoeff(envFollowSine,decayCoeffTable[decayCoeff]);
	tEnvelopeFollowerDecayCoeff(envFollowNoise,0.80f);
}

int32_t SFXDrumboxTick(int32_t input)
{
	float audioIn = 0.0f;
	float sample = 0.0f;
	float output = 0.0f;

	audioIn = (float) (input * INV_TWO_TO_31);

	tCycleSetFreq(sin1, tRampTick(rampSineFreq));

	tDelayLSetDelay(delay, tRampTick(rampDelayFreq));

	sample = ((ksTick(audioIn) * 0.7f) + audioIn * 0.8f);
	float tempSinSample = OOPS_shaper(((tCycleTick(sin1) * tEnvelopeFollowerTick(envFollowSine, audioIn)) * 0.6f), 0.5f);
	sample += tempSinSample * 0.6f;
	sample += (tNoiseTick(noise1) * tEnvelopeFollowerTick(envFollowNoise, audioIn));

	sample *= gainBoost;

	sample = tHighpassTick(highpass1, sample);
	sample = OOPS_shaper(sample * 0.6, 1.0f);

	return (int32_t) (sample * TWO_TO_31);
}

void SFXSynthFrame()
{
	tMPoly_setNumVoices(mpoly, numActiveVoices[SynthMode]);
	if (modeChain[chainIndex] == SynthMode)
	{
		__KNOBCHECK1__ { glideTimeSynth = (knobVals[0] * 999.0f) + 5.0f; }
		__KNOBCHECK2__ { synthGain = knobVals[1]; }
		__KNOBCHECK3__ { lpFreqSynth = ((knobVals[2]) * 17600.0f) + 400.0f; }
		__KNOBCHECK4__
		{
			for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
			{
				detuneMaxSynth = (knobVals[3]) * freq[i] * 0.05f;
			}
		}
	}
	tSVFSetFreq(lowpassSyn, lpFreqSynth);
	tMPoly_setPitchGlideTime(mpoly, glideTimeSynth);
	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		calculateFreq(i);
		for (int j = 0; j < NUM_OSC; j++)
		{
			detuneAmounts[i][j] = (detuneSeeds[i][j] * detuneMaxSynth) - (detuneMaxSynth * 0.5f);
			tSawtoothSetFreq(osc[i][j], freq[i] + detuneAmounts[i][j]);
		}
	}
}

int32_t SFXSynthTick(int32_t input)
{
	float sample = 0.0f;
	float output = 0.0f;

	tMPoly_tick(mpoly);

	for (int i = 0; i < tMPoly_getNumVoices(mpoly); i++)
	{
		for (int j = 0; j < NUM_OSC; j++)
		{
			sample += tSawtoothTick(osc[i][j]) * tRampTick(ramp[i]);
		}
	}

	sample = tSVFTick(lowpassSyn, sample) * INV_NUM_OSC * 0.5;

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

	for (int i = 0; i < mpoly->numVoices; i++)
	{
		if (tMPoly_isOn(mpoly, i) == 1)
		{
			tRampSetDest(ramp[i], (float)(tMPoly_getVelocity(mpoly, i) * INV_TWO_TO_7));
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
		int computedNote;

		if (harmonizerInputMode == Latch)
		{
			computedNote = latchedNote;
		}
		else
		{
			computedNote = playedNote;
		}

		if (sungNote == -1 || computedNote == -1 || inKey(computedNote) == 0)
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
			if ((computedNote % 12 - harmonizerKey + 12) % 12 == offsets[i])
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
		int computedNote;

		if (harmonizerInputMode == Latch)
		{
			computedNote = latchedNote;
		}
		else
		{
			computedNote = playedNote;
		}

		if (sungNote == -1 || computedNote == -1 || inKey(computedNote) == 0)
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
			voiceSingle(offsets, UP);
		}
		else if (harmonizerMode == 1)
		{
			voiceSingle(offsets, DOWN);
		}
		else
		{
			// copy triad to be rearranged and evaluated
			int evalTriad[3];
			copyTriad(triad, evalTriad);

			// set to first inversion down
			triad[2] = triad[2] - 12;

			// triad ends up with best voicing
			if (shouldVoice == 1)
			{
				voice(evalTriad);
			}

			// preserve voiced triad in lastTriad
			copyTriad(triad, lastTriad);

			// sort from closest to farthest from sung note
			sortTriadRelative(triad);
		}

		// always voice after triad has been voiced
		shouldVoice = 1;

		return 1;
	}
}

void voice(int* evalTriad)
{
	int distance = calcDistance(evalTriad, lastTriad, TRIAD_LENGTH);
    int bestDistance = distance;

    // first inversion up
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

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

    // second inversion up
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

//    distance = calcDistance(triad, lastTriad);
//    if (harmonizerMode == 1)
//	{
//		if (distance < bestDistance)
//		{
//			bestDistance = distance;
//			copyTriad(triad, bestTriad);
//		}
//	}
//	else
//	{
//		if (distance > bestDistance)
//		{
//			bestDistance = distance;
//			copyTriad(triad, bestTriad);
//		}
//	}

    // one octave positive transpose of original triad
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

    // transpose two octaves down
    for (int i = 0; i < TRIAD_LENGTH; i++)
    {
    	evalTriad[i] = evalTriad[i] - 24;
    }

    // first inversion down
    evalTriad[0] = evalTriad[0] + 12;
    sortTriad(evalTriad);

//    distance = calcDistance(triad, lastTriad);
//    if (harmonizerMode == 1)
//	{
//		if (distance < bestDistance)
//		{
//			bestDistance = distance;
//			copyTriad(triad, bestTriad);
//		}
//	}
//	else
//	{
//		if (distance > bestDistance)
//		{
//			bestDistance = distance;
//			copyTriad(triad, bestTriad);
//		}
//	}

    // second inversion down
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

// TODO: free up constraints if playedNote changes, not sungNote
void voiceSingle(int* offsets, Direction dir)
{
	// prioritize single voice leading over triad voice leading from here on out

	// choose chord tone closest to sung note
	int voice = -1;
	int bestDist = 127;

	for (int i = 0; i < TRIAD_LENGTH; i++)
	{
		int dist = abs(triad[i] - sungNote);
		if (dist < bestDist)
		{
			bestDist = dist;
			voice = triad[i];
		}
	}

	// detect whether the sungNote is a chord tone or not
	int chordTone = 0;

	for (int i = 0; i < TRIAD_LENGTH; i++)
	{
		if ((sungNote - harmonizerKey) % 12 == (triad[i] - harmonizerKey) % 12)
		{
			chordTone = 1;
			break;
		}
	}


	if (chordTone == 1)
	{
		if (harmonizeMoved == SUNG) {
			// if sungNote moved pick other chord tone that is arrived at from same direction as melody
			if (voiceDirection == UP)
			{
				for (int i = 0; i < TRIAD_LENGTH; i++)
				{
					if (triad[i] > lastSingleVoice)
					{
						voice = triad[i];
						if ((voice - harmonizerKey) % 12 == (sungNote - harmonizerKey) % 12)
						{
							voice = triad[(i + 1 + TRIAD_LENGTH) % TRIAD_LENGTH];
						}
						break;
					}
					else if (i == TRIAD_LENGTH - 1)
					{
						voice = triad[0] + 12;
						if ((voice - harmonizerKey) % 12 == (sungNote - harmonizerKey) % 12)
						{
							voice = triad[1] + 12;
						}
					}
				}
			}
			else
			{
				for (int i = TRIAD_LENGTH - 1; i >= 0; i--)
				{
					if (triad[i] < lastSingleVoice)
					{
						voice = triad[i];
						if ((voice - harmonizerKey) % 12 == (sungNote - harmonizerKey) % 12)
						{
							voice = triad[(i - 1 + TRIAD_LENGTH) % TRIAD_LENGTH];
						}
						break;
					}
					else if (i == 0)
					{
						voice = triad[TRIAD_LENGTH - 1] - 12;
						if ((voice - harmonizerKey) % 12 == (sungNote - harmonizerKey) % 12)
						{
							voice = triad[TRIAD_LENGTH - 2] - 12;
						}
					}
				}
			}
		}
	}
	else
	{
		// if it's a non-chord tone, ensure harmonizer voice is also on a non-chord tone
		for (int i = 0; i < SCALE_LENGTH; i++)
		{
			if (offsets[i] == (voice - harmonizerKey) % 12)
			{
				if (harmonizeMoved == SUNG)
				{
					// make sure it moves to a non-chord tone in the direction of the melody
					if (voiceDirection == UP)
					{
						// move up one scale degree
						voice += offsets[(i + 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i];
					}
					else
					{
						// move down one scale degree
						voice += offsets[(i - 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i];
					}
				}
				else
				{
					// move to non-chord tone in direction of melody
					if (sungNote - voice > 0)
					{
						// move up one scale degree
						voice += offsets[(i + 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i];
					}
					else
					{
						// move down one scale degree
						voice += offsets[(i - 1 + SCALE_LENGTH) % SCALE_LENGTH] - offsets[i];
					}
				}
				break;
			}
		}
	}

	triad[0] = voice;
	lastSingleVoice = voice;
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

	dbFeedbackSamp = tDelayLTick(delay, temp_sample);

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
