#pragma once

#include <coreinit/filesystem_fsa.h>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

enum class RuleUid : uint32_t {
    ROOT        = 0,
    SYSTEM_MENU = 4096,
    WFS         = 19,
    DYNAMIC     = 0xFFFFFFFF,
};

enum class RuleGid : uint32_t {
    ROOT        = 0,
    SYSTEM_MENU = 1,
    WFS         = 19,
    DYNAMIC     = 0xFFFFFFFF,
};

struct PathRuleDef {
    const char* pattern;
    uint32_t mode;
    RuleUid uid;
    RuleGid gid;
    const char* description;
};

struct ResolvedPathRule {
    bool matched = false;
    std::string pattern;
    FSMode mode = (FSMode)0;
    uint32_t uid = 0;
    uint16_t gid = 0;
    uint64_t titleId = 0;
    const char* description = "";
};

// Initializes the path rules tree (called automatically on first lookup or explicitly)
void PathRules_Init();

// Resolves the rule for a given path.
// If fsaClient != 0 and the rule has dynamic UID, resolves the UID using uid.sys and SLCCMPT.
// If tmdGroupId != 0 and the rule has dynamic GID, uses tmdGroupId; otherwise falls back to standard GID.
ResolvedPathRule PathRules_Resolve(FSAClientHandle fsaClient, std::string_view path, uint16_t tmdGroupId = 0);

// Convenience overload when no FSAClientHandle is available or required.
inline ResolvedPathRule PathRules_Resolve(std::string_view path, uint16_t tmdGroupId = 0) {
    return PathRules_Resolve(0, path, tmdGroupId);
}

// Checks if stat matches the expected permissions for path. Returns true if acceptable.
// If outExpectedRule is non-null, it is populated with the resolved rule.
bool PathRules_CheckPermissions(FSAClientHandle fsaClient, std::string_view path, const FSStat& stat,
                                ResolvedPathRule* outExpectedRule = nullptr, uint16_t tmdGroupId = 0);

// Returns the list of all stock root directory rules
std::vector<ResolvedPathRule> PathRules_GetStockRootDirs();
