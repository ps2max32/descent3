/*
 * Descent 3
 * Copyright (C) 2024 Descent Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <chrono>
#include "chrono_timer.h"

#ifdef __PS2__
/* the EE's cycles (ps2/ps2_gl.cpp) */
extern "C" uint64_t ps2_cycles(void);
#endif

namespace D3 {

#ifdef __PS2__
/* the time from the EE's cycle counter: steady_clock goes through
   clock_gettime and gettimeofday, several 64-bit divisions in software a
   call, and the game asks for the time often */
static uint64_t ps2_start;

void ChronoTimer::Initialize() { ps2_start = ps2_cycles(); }

/* 294912000 cycles a second: 294912 a millisecond, 36864 every 125 us */
float ChronoTimer::GetTime() { return (float)(ps2_cycles() - ps2_start) * (1.0f / 294912000); }

int64_t ChronoTimer::GetTimeMS() { return (int64_t)((ps2_cycles() - ps2_start) / 294912); }

int64_t ChronoTimer::GetTimeUS() { return (int64_t)((ps2_cycles() - ps2_start) * 125 / 36864); }

#else

void ChronoTimer::Initialize() { m_start_tstamp = std::chrono::steady_clock::now(); }

float ChronoTimer::GetTime() {
  return std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::steady_clock::now() - m_start_tstamp)
      .count();
}

int64_t ChronoTimer::GetTimeMS() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start_tstamp)
      .count();
}

int64_t ChronoTimer::GetTimeUS() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - m_start_tstamp)
      .count();
}

#endif
} // namespace D3
