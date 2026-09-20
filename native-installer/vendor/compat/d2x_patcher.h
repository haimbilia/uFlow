/*
 * d2x_patcher.h
 * 
 * The cIOS patching logic in the corresponding implementation is 
 * adapted from the d2x-cios-installer project.
 * Original credits go to davebaol, xperia64, blackb0x / wiidev, 
 * and other contributors to the d2x cIOS and patchmii projects.
 *
 * Licensed under the GPLv2.
 */

#pragma once

#include <string>
#include <vector>
#include <utility>

// Will install d2x cIOS using the selected version folder on the SD card.
// Prompts the user to confirm the installation of the standard configurations.
void InstallD2X(const std::string& versionFolder);

// Uninstalls selected d2x cIOS configurations. Returns true if uninstallation work was performed.
bool UninstallD2X();

// Batch functions (installs/uninstalls all 4 slots automatically)
void InstallD2XBatch(const std::string& versionFolder, std::vector<std::pair<int, bool>>& slotResults);
void PrepareD2XBatch(const std::string& versionFolder, std::vector<std::pair<int, bool>>& slotResults);
void UninstallD2XBatch(std::vector<std::pair<int, bool>>& slotResults);

