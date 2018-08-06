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
#include "ui.h"

/* Externs -------------------------------------------------------------------*/

extern int lockArray[12];
extern int chordArray[12];
extern float noteperiod;
extern float pitchFactor;
extern float formantKnob;
extern float newFeedback;
extern float newDelay;
extern float newFeedbackDB;
extern float newDelayDB;
extern int bitDepth;
extern int rateRatio;
extern float inputLevel;
extern float outputLevel;

extern uint8_t numActiveVoices[ModeCount];

extern uint8_t autotuneLock;
extern uint8_t knobLock[ModeCount];
extern uint8_t formantCorrect;

void SFXInit(float sr, int blocksize);

// Using dummy arguments so they can be put into a function pointer array
void SFXVocoderFrame();
int32_t SFXVocoderTick(int32_t input);

void SFXFormantFrame();
int32_t SFXFormantTick(int32_t input);

void SFXPitchShiftFrame();
int32_t SFXPitchShiftTick(int32_t input);

void SFXAutotuneNearestFrame();
int32_t SFXAutotuneNearestTick(int32_t input);

void SFXAutotuneAbsoluteFrame();
int32_t SFXAutotuneAbsoluteTick(int32_t input);

void SFXDelayFrame();
int32_t SFXDelayTick(int32_t input);

void SFXReverbFrame();
int32_t SFXReverbTick(int32_t input);

void SFXBitcrusherFrame();
int32_t SFXBitcrusherTick(int32_t input);

void SFXDrumboxFrame();
int32_t SFXDrumboxTick(int32_t input);

void SFXSynthFrame();
int32_t SFXSynthTick(int32_t input);

void SFXDrawFrame();
int32_t SFXDrawTick(int32_t input);

void SFXLevelFrame();
int32_t SFXLevelTick(int32_t input);

void SFXNoteOn(int key, int velocity);
void SFXNoteOff(int key, int velocity);

#endif /* SFX_H_ */
