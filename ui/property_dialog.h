#pragma once

#include <windows.h>
#include "../config/encoder_config.h"

INT_PTR ShowEncoderSettingsDialog(HWND hParent, EncoderConfig& config, int initialTab = 0);
