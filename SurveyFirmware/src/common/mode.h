#pragma once
#define HAT 1
#define ROOMBA 2
// Change this one value to select the build. Both boards need a matching
// direct board-to-board protocol before the Roomba can be controlled.
#ifndef SURVEY_MODE
#define SURVEY_MODE HAT
#endif
#if SURVEY_MODE != HAT && SURVEY_MODE != ROOMBA
#error "SURVEY_MODE must be HAT or ROOMBA"
#endif
