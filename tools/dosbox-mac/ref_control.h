// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
bool REF_Enabled();
void REF_Pump();
void REF_Frame(const void *pixels, unsigned w, unsigned h, unsigned bpp,
               unsigned pitch, const void *palette);
void REF_RawLine(const uint8_t*, unsigned, unsigned, unsigned, unsigned, unsigned, const uint32_t*);
