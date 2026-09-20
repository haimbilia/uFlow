#ifndef UFLOW_RAW_RPL_SCOPE_H
#define UFLOW_RAW_RPL_SCOPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Separate from ONE_RPX_LAUNCH: imports can arrive after the final RPX chunk. */
typedef struct {
    char directory[256];
    char executable[64];
    bool prepared;
    bool active;
} RawRplScope;

static inline void RawRplReset(RawRplScope *scope) {
    memset(scope, 0, sizeof(*scope));
}

static inline size_t RawRplLength(const char *s, size_t limit) {
    size_t n = 0;
    while (n < limit && s[n]) ++n;
    return n;
}

static inline bool RawRplPrepare(RawRplScope *scope, const char *path,
                                 const char *argstr, bool enabled, uint32_t offset) {
    RawRplReset(scope);
    if (!enabled || offset != 0) return false; // Not a loose game (e.g. WUHB).
    size_t length = RawRplLength(path, 256);
    if (!length || length == 256 || path[0] == '/') return false;
    size_t slash = 0, component = 0;
    for (size_t i = 0; i <= length; ++i) {
        unsigned char c = (unsigned char) path[i];
        if (c == '/' || c == 0) {
            size_t n = i - component;
            if (!n || (n == 1 && path[component] == '.') ||
                (n == 2 && path[component] == '.' && path[component + 1] == '.')) return false;
            if (c == '/') slash = i + 1;
            component = i + 1;
        } else if (c < 32 || c == '\\' || c == ':' || c == '|' || c == '~') return false;
    }
    size_t nameLength = length - slash;
    if (!slash || nameLength < 5 || nameLength >= sizeof(scope->executable) ||
        strncmp(path + length - 4, ".rpx", 5) != 0) return false;
    size_t argLength = 0;
    while (argLength < sizeof(scope->executable) && argstr[argLength] && argstr[argLength] != ' ') ++argLength;
    if (argLength != nameLength || memcmp(argstr, path + slash, nameLength) != 0) return false;
    memcpy(scope->directory, path, slash);
    memcpy(scope->executable, path + slash, nameLength);
    scope->prepared = true;
    return true;
}

static inline void RawRplBeginExecutable(RawRplScope *scope, const char *name) {
    /* Any subsequent title, including an installed copy with the same RPX name,
       must not inherit this directory. Preparation belongs to one launch only. */
    if (!scope->prepared ||
        (strncmp(name, scope->executable, 64) != 0 && strncmp(name, "safe.rpx", 64) != 0 &&
         strncmp(name, "ply.rpx", 64) != 0)) RawRplReset(scope);
}

static inline void RawRplExecutableLoaded(RawRplScope *scope) {
    if (scope->prepared) {
        scope->prepared = false;
        scope->active = true;
    }
}

static inline bool RawRplPath(const RawRplScope *scope, const char *name,
                             uint32_t pid, uint32_t type, char output[256]) {
    if (!scope->active || pid != 7 || type != 0) return false;
    size_t n = RawRplLength(name, 64);
    if (!n || n == 64) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char) name[i];
        if (c < 33 || c == '/' || c == '\\' || c == ':' || c == '|' || c == '~' ||
            (c == '.' && i + 1 < n && name[i + 1] == '.')) return false;
    }
    if (name[0] == '.') return false;
    const char *dot = strrchr(name, '.');
    bool suffix = dot != NULL;
    if (suffix && strncmp(dot, ".rpl", 5) != 0 && strncmp(dot, ".RPL", 5) != 0) return false;
    size_t dir = strlen(scope->directory);
    size_t total = dir + n + (suffix ? 0 : 4);
    /* MCP_LoadCustomFile prefixes this into its own 256-byte path buffer. */
    if (sizeof("/vol/storage_iosu_homebrew/") - 1 + total >= 256) return false;
    memcpy(output, scope->directory, dir);
    memcpy(output + dir, name, n);
    if (!suffix) memcpy(output + dir + n, ".rpl", 4);
    output[total] = 0;
    return true;
}
#endif
