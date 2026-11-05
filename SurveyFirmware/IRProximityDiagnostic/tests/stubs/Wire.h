#pragma once
#include "Arduino.h"
struct WireFake { void begin() {} }; extern WireFake Wire;
