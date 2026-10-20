#pragma once
#include <cstddef>
#include <cstdint>
struct MCPTitleListType {
    uint64_t titleId;
    uint32_t appType;
    char path[256];
    char indexedDevice[16];
};
constexpr uint32_t MCP_APP_TYPE_GAME = 0x80000000;
constexpr uint32_t MCP_APP_TYPE_GAME_WII = 0x8000002E;
#ifdef UFLOW_TEST_INSTALLED
#include <vector>
#include <cstring>
inline std::vector<MCPTitleListType> testInstalledTitles;
inline bool testMcpFailure = false;
inline int32_t MCP_Open() { return testMcpFailure ? -1 : 1; }
inline int32_t MCP_Close(int32_t) { return 0; }
inline int32_t MCP_TitleCount(int32_t) { return testInstalledTitles.size(); }
inline int32_t MCP_TitleList(int32_t, uint32_t *count, MCPTitleListType *titles, std::size_t) {
    *count = testInstalledTitles.size();
    std::memcpy(titles, testInstalledTitles.data(), testInstalledTitles.size() * sizeof(*titles));
    return 0;
}
#else
inline int32_t MCP_Open() { return -1; }
inline int32_t MCP_Close(int32_t) { return 0; }
inline int32_t MCP_TitleCount(int32_t) { return 0; }
inline int32_t MCP_TitleList(int32_t, uint32_t *, MCPTitleListType *, std::size_t) { return -1; }
#endif
