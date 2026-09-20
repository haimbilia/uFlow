#ifndef UFLOW_MCP_PATH_H
#define UFLOW_MCP_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* MCP paths are fixed-size IPC fields, not necessarily terminated strings.
   Do not call the firmware's variadic formatter to assemble loader paths. */
static inline bool MCPJoinPath(char *output, size_t capacity,
                               const char *prefix, size_t prefixCapacity,
                               const char *path, size_t pathCapacity) {
    if (!output || !capacity) return false;
    output[0] = '\0';
    if (!prefix || !path) return false;
    size_t a = 0, b = 0;
    while (a < prefixCapacity && prefix[a]) ++a;
    while (b < pathCapacity && path[b]) ++b;
    if (a == prefixCapacity || b == pathCapacity || !b) return false;
    size_t separator = a != 0 && prefix[a - 1] != '/';
    if (a >= capacity || separator >= capacity - a ||
        b >= capacity - a - separator) return false;
    memcpy(output, prefix, a);
    if (separator) output[a] = '/';
    memcpy(output + a + separator, path, b);
    output[a + separator + b] = '\0';
    return true;
}

#endif
