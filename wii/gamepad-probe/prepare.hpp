#pragma once

namespace GamePadProbe {
// Backend operations return true only after verifying their result. A failed
// setter may still have changed the setting, so always attempt restoration.
enum class Preparation { Ready, ReadFailed, JournalFailed, EnableFailed,
                         PrepareFailed, RestoreFailed, CleanupFailed };
template<class Backend> Preparation PrepareWithDrc(Backend &b) {
    int original = -1;
    if (!b.Read(original) || (original != 0 && original != 1))
        return Preparation::ReadFailed;
    if (original == 1)
        return b.Prepare() ? Preparation::Ready : Preparation::PrepareFailed;
    if (!b.Journal(original)) return Preparation::JournalFailed;
    bool enabled = b.Set(1);
    bool prepared = enabled && b.Prepare();
    if (!b.Set(original)) return Preparation::RestoreFailed;
    if (!b.ClearJournal()) return Preparation::CleanupFailed;
    if (!enabled) return Preparation::EnableFailed;
    return prepared ? Preparation::Ready : Preparation::PrepareFailed;
}
}
