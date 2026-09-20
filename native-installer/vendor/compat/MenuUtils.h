#pragma once
#include <string>
#include <vector>

// Shows a generic vertical menu.
// Returns the index of the selected item, or -1 if canceled (B button when allowCancel is true).
int ShowMenu(const std::vector<std::string>& header, const std::vector<std::string>& options, int defaultSelected = 0, bool allowCancel = true);

// Shows a multi-select vertical menu.
// Returns a vector of selected indices, or an empty vector if canceled (B button).
std::vector<int> ShowMultiSelectMenu(const std::vector<std::string>& header, const std::vector<std::string>& options, bool defaultSelected = false);

// Waits for A or B. Returns true for A, false for B.
bool WaitPrompt();
