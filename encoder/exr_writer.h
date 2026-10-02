#pragma once

#include <windows.h>
#include <string>

bool WriteExrFrame(const std::wstring& path,
                   const BYTE* data,
                   int width,
                   int height,
                   long stride,
                   const std::wstring& pixfmt,
                   bool bottomUp,
                   bool withAlpha);
