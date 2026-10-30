/**
 * @file display/LicenseOpening.h
 * @brief The licence screen's opening buffer, declared for the two units that share it (S10c D4).
 *
 * @details The opening of the licence screen (the pack's own sentences, in the language of the installed pack) is composed on
 * Core 0 when the screen opens, because webDictValue( ) reads LittleFS and only Core 0 may; Core 1 draws it. The buffer is
 * defined in DisplayManager.cpp, next to composeLicenseOpening( ), which fills it. The Ângulo's licence screen reads it through
 * DisplayManager::licenseOpening( ); the original's, drawSettingsLicense( ), reads it directly. That draw is the original UI's
 * and, since S10c D4 (D4b-2), lives in DisplayManager_LicenseUi.cpp, a unit the two Ângulo images do not compile, apart from the
 * core that composes the opening, so the buffer needs external linkage: it was a file-static of DisplayManager.cpp until D4b-1.
 * DisplayManager.cpp includes this header, so the compiler checks the definition against the declaration (a size that differs
 * is an error, not a second buffer).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

extern char _licOpening[768];
