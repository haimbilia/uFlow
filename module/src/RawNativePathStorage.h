#pragma once
#include <memory>
#include <new>

// Retail games can issue FS calls from 4 KiB worker stacks. In particular,
// a synchronous FS entry may call its async counterpart, so neither hook may
// reserve a 1 KiB automatic path array. Each invocation owns its own buffer:
// concurrent/nested calls cannot overwrite another call's translated path.
class RawNativePathStorage {
public:
    static constexpr unsigned Capacity = 0x400;
    explicit RawNativePathStorage(bool active)
        : bytes_(active ? new (std::nothrow) char[Capacity] : nullptr) {}
    char *get() const { return bytes_.get(); }
private:
    std::unique_ptr<char[]> bytes_;
};
