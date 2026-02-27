/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef CORE_EMULATION_HPP
#define CORE_EMULATION_HPP

#include <filesystem>

enum class CoreEmulationState
{
    Stopped = 1,
    Running,
    Paused
};

// starts emulation with given ROM
bool CoreStartEmulation(std::filesystem::path n64rom, std::filesystem::path n64ddrom, std::string address = "", int port = -1, int player = -1);

// stops emulation
bool CoreStopEmulation(void);

// pauses emulation
bool CorePauseEmulation(void);

// resumes emulation
bool CoreResumeEmulation(void);

// resets emulation
bool CoreResetEmulation(bool hard);

// returns whether emulation is running
bool CoreIsEmulationRunning(void);

// returns whether emulation is paused
bool CoreIsEmulationPaused(void);

// returns current VI (vertical interrupt) frame count
// used for synchronization in netplay/Kaillera
int CoreGetCurrentFrameCount(void);

// EEPROM buffer access for netplay sync
// Returns a pointer to the EEPROM buffer and its size (returns false if not available)
bool CoreGetEepromBuffer(uint8_t** buffer, size_t* size);
// Overwrites the EEPROM buffer with provided data (returns false if not available)
bool CoreSetEepromBuffer(const uint8_t* data, size_t size);

#endif // CORE_EMULATION_HPP
