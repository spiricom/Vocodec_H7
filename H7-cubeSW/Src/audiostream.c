/* Includes ------------------------------------------------------------------*/
#include "audiostream.h"
#include "main.h"
#include "codec.h"

#define ADCJoyY 0
#define ADCKnob 1
#define ADCPedal 2
#define ADCBreath 3
#define ADCSlide 4

#define NUM_HARMONICS 15.0f

#define ACOUSTIC_DELAY				132

// align is to make sure they are lined up with the data boundaries of the cache 
// at(0x3....) is to put them in the D2 domain of SRAM where the DMA can access them
// (otherwise the TX times out because the DMA can't see the data location) -JS


int32_t audioOutBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;
int32_t audioInBuffer[AUDIO_BUFFER_SIZE] __ATTR_RAM_D2;

uint16_t* adcVals;

float sample = 0.0f;


float fundamental_hz = 58.27;
float fundamental_cm;
float fundamental_m = 2.943195469366741f;
float inv_fundamental_m;
float cutoff_offset;
long long click_counter = 0;
const int D = 2*AUDIO_FRAME_SIZE + ACOUSTIC_DELAY;

float slideLengthPreRamp;

HAL_StatusTypeDef transmit_status;
HAL_StatusTypeDef receive_status;

float LN2;
float amp_mult = 5.0f;

float valPerM;
float mPerVal;


tSawtooth* osc;
tRamp* adc[5];
tRamp* slideRamp;
tRamp* finalFreqRamp;
tSVF* oldFilter;

float breath_baseline = 0.0f;
float breath_mult = 0.0f;

uint16_t knobValue;

uint16_t slideValue;

float slideLengthDiff = 0;
float slideLength = 0;

float fundamental = 0.0f;
float customFundamental = 48.9994294977f;
float position = 0.f;
float firstPositionValue = 0.f;


float harmonicHysteresis = 0.60f;


float knobValueToUse = 0.0f;

float floatHarmonic, floatPeak, intPeak, mix;
float intHarmonic;

int flip = 1;
int envTimeout;


FTMode ftMode = FTSynthesisOne;
float val;

float breath = 0.0f;
float rampedBreath = 0.0f;

float slide_tune = 1.0f;

int hysteresisKnob = 0;
int hysteresisAmount = 512;

int octave = 3;
float octaveTransp[7] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

float slidePositions[8] = {0.0f, 0.09f, 0.17f, 0.27f, 0.34f, 0.39f, 0.47f, 0.7f};
float slideLengthM = 0.0f;
float slideLengthChange = 0.0f;
float oldSlideLengthM = 0.0f;
float newTestDelay = 0.0f;
float oldIntHarmonic = 0.0f;

typedef enum BOOL {
	FALSE = 0,
	TRUE
} BOOL;

float audioTickSynth(float audioIn);
float audioTickFeedback(float audioIn);


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

	osc = tSawtoothInit();
	tSawtoothSetFreq(osc, 200.f);


	adc[ADCJoyY] = tRampInit(18, 1);
	adc[ADCKnob] = tRampInit(30,1);
	adc[ADCPedal] = tRampInit(18, 1);
	adc[ADCBreath] = tRampInit(19, 1);
	adc[ADCSlide] = tRampInit(20, AUDIO_FRAME_SIZE);

	slideRamp = tRampInit(20, 1);
	finalFreqRamp = tRampInit(5, 1);

	breath_baseline = ((adcVals[ADCBreath] * INV_TWO_TO_16) + 0.1f);
	breath_mult = 1.0f / (1.0f-breath_baseline);

	valPerM = 1430.0f / powf(2.0f,SLIDE_BITS);
	mPerVal = 1.0f/valPerM;

	firstPositionValue = adcVals[ADCSlide]>>SLIDE_BITS;
	tRampSetVal(slideRamp,firstPositionValue);

	oldFilter = tSVFInit(SVFTypeBandpass, 2000.0f, 20000.0f);

}

void audioFrame(uint16_t buffer_offset)
{
	uint16_t i = 0;
	int32_t current_sample = 0;


	tRampSetDest(adc[ADCPedal], (adcVals[ADCPedal] * INV_TWO_TO_16));
	tRampSetDest(adc[ADCKnob], adcVals[ADCKnob] * INV_TWO_TO_16);
	tRampSetDest(adc[ADCJoyY], 1.0f - ((adcVals[ADCJoyY] * INV_TWO_TO_16) - 0.366f) * 3.816f);

	tRampSetDest(adc[ADCSlide], adcVals[ADCSlide]>>SLIDE_BITS);
	position = tRampTick(adc[ADCSlide]);

	slideLengthDiff = (position - firstPositionValue) * mPerVal * slide_tune;
		slideLengthM = (position - firstPositionValue) * mPerVal;
		slideLengthPreRamp = fundamental_m + slideLengthDiff;
		tRampSetDest(slideRamp, slideLengthPreRamp);



	for (i = 0; i < (HALF_BUFFER_SIZE); i++)
	{
		if ((i & 1) == 0)
		{
			//current_sample = (int32_t)(audioTickSynth((float) (audioInBuffer[buffer_offset + i] * INV_TWO_TO_31)) * TWO_TO_31);
			current_sample = (int32_t)(audioTickFeedback((float) (audioInBuffer[buffer_offset + i] * INV_TWO_TO_31)) * TWO_TO_31);
		}
		else
		{

		}
		audioOutBuffer[buffer_offset + i] = current_sample;
	}
}

//ADC values are =
// [0] = joystick
// [1] = knob
// [2] = pedal
// [3] = breath
// [4] = slide

float audioTickSynth(float audioIn)
{
	sample = 0.0f;

	slideLength = tRampTick(slideRamp);
	float x = 12.0f * logf(slideLength / fundamental_m) * INV_LOG2;
	fundamental = fundamental_hz * powf(2.0f, (-x * INV_TWELVE));

	floatHarmonic = NUM_HARMONICS * (1.0f - tRampTick(adc[ADCJoyY])) + 1.0f;

	if (((floatHarmonic - intHarmonic) > (harmonicHysteresis)) || ((floatHarmonic - intHarmonic) < ( -1.0f * harmonicHysteresis)))
	{
		intHarmonic = (uint16_t) (floatHarmonic + 0.5f);
	}


	floatPeak = fundamental * floatHarmonic * octaveTransp[octave];
	intPeak = fundamental * intHarmonic * octaveTransp[octave];

	tRampSetDest(finalFreqRamp, intPeak);

	float pedal = tRampTick(adc[ADCPedal]);
	knobValueToUse = tRampTick(adc[ADCKnob]);


	breath = adcVals[ADCBreath];
	breath = breath * INV_TWO_TO_16;
	breath = breath - breath_baseline;
	breath = breath * breath_mult;
	breath *= amp_mult;

	if (breath < 0.0f)					breath = 0.0f;
	else if (breath > 1.0f)		  breath = 1.0f;

	tRampSetDest(adc[ADCBreath], breath);

	rampedBreath = tRampTick(adc[ADCBreath]);

	tSawtoothSetFreq(osc, tRampTick(finalFreqRamp));

	sample = tSawtoothTick(osc);
	sample *= rampedBreath;


	return sample;
}


float audioTickFeedback(float audioIn)
{
	sample = 0.0f;

	slideLength = tRampTick(slideRamp);
	float x = 12.0f * logf(slideLength / fundamental_m) * INV_LOG2;
	fundamental = fundamental_hz * powf(2.0f, (-x * INV_TWELVE));

	floatHarmonic = NUM_HARMONICS * (1.0f - tRampTick(adc[ADCJoyY])) + 1.0f;

	if (((floatHarmonic - intHarmonic) > (harmonicHysteresis)) || ((floatHarmonic - intHarmonic) < ( -1.0f * harmonicHysteresis)))
	{
		intHarmonic = (uint16_t) (floatHarmonic + 0.5f);
	}


	floatPeak = fundamental * floatHarmonic * octaveTransp[octave];
	intPeak = fundamental * intHarmonic * octaveTransp[octave];

	tRampSetDest(finalFreqRamp, intPeak);

	float pedal = tRampTick(adc[ADCPedal]);
	knobValueToUse = tRampTick(adc[ADCKnob]);


	breath = adcVals[ADCBreath];
	breath = breath * INV_TWO_TO_16;
	breath = breath - breath_baseline;
	breath = breath * breath_mult;
	breath *= amp_mult;

	if (breath < 0.0f)					breath = 0.0f;
	else if (breath > 1.0f)		  breath = 1.0f;

	tRampSetDest(adc[ADCBreath], breath);

	rampedBreath = tRampTick(adc[ADCBreath]);

	//tSawtoothSetFreq(osc, tRampTick(finalFreqRamp));

	tSVFSetFreq(oldFilter, intPeak);

	sample = tSVFTick(oldFilter, audioIn);
	sample *= rampedBreath;
	return sample;
}


int d(float Tfreq){
	int T = SAMPLE_RATE / Tfreq;
	return (T - D % T);
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
