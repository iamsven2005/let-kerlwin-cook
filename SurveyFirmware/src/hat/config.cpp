#include "../common/mode.h"
#if SURVEY_MODE == HAT
#include "config.h"

// Default values of the settings declared in config.h
float stepMm = 700;
float stepThreshG = 0.15f;
float irRangeMm = 100;
float hangDirDeg = -90;
float hangMm = 250;
float pointMm = 450;
unsigned long fastIntervalMs = 300;
unsigned long slowIntervalMs = 150;
int slowSamples = 20;
unsigned long ackTimeoutMs = 500;

#endif // SURVEY_MODE == HAT
