#pragma once

#include "library.hpp"

// Empty means the directory is structurally ready for the launch adapter.
// This is not a complete content-integrity check.
std::string ValidateRawGame(const GameEntry &game);
