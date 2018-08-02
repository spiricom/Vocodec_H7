/*
 * sfx.h
 *
 *  Created on: Aug 2, 2018
 *      Author: Matthew
 */

#ifndef __SFX_H_
#define __SFX_H_

/* Includes ------------------------------------------------------------------*/
#include "OOPS.h"
#include "stm32h7xx_hal.h"
#include "audiostream.h"

/* Externs -------------------------------------------------------------------*/

extern tMPoly* mpoly;
extern int lockArray[12];
extern int chordArray[12];
extern float noteperiod;
extern float pitchFactor;
extern float formantKnob;
extern float newFeedback;
extern float newDelay;
extern int bitDepth;
extern int rateRatio;
extern float inputLevel;
extern float outputLevel;

void SFXInit(float sr, int blocksize, uint16_t* myADCArray);

// Using dummy arguments so they can be put into a function pointer array
void VocoderFrame(uint8_t _a);
int32_t VocoderTick(int32_t input, uint8_t _a, uint8_t _b);

void FormantFrame(uint8_t _a);
int32_t FormantTick(int32_t input, uint8_t _a, uint8_t _b);

void PitchShiftFrame(uint8_t _a);
int32_t PitchShiftTick(int32_t input, uint8_t fCorr, uint8_t _b);

void AutotuneNearestFrame(uint8_t _a);
int32_t AutotuneNearestTick(int32_t input, uint8_t fCorr, uint8_t lock);

void AutotuneAbsoluteFrame(uint8_t _a);
int32_t AutotuneAbsoluteTick(int32_t input, uint8_t fCorr, uint8_t _b);

void DelayFrame(uint8_t _a);
int32_t DelayTick(int32_t input, uint8_t _a, uint8_t _b);

void BitcrusherFrame(uint8_t _a);
int32_t BitcrusherTick(int32_t input, uint8_t _a, uint8_t _b);

void DrumboxFrame(uint8_t _a);
int32_t DrumboxTick(int32_t input, uint8_t _a, uint8_t _b);

void SynthFrame(uint8_t _a);
int32_t SynthTick(int32_t input, uint8_t _a, uint8_t _b);

void LevelFrame(uint8_t lock);
int32_t LevelTick(int32_t input, uint8_t _a, uint8_t _b);

void SFXNoteOn(int key, int velocity);
void SFXNoteOff(int key, int velocity);

#endif /* SFX_H_ */
