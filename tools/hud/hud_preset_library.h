// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hud_layout.h"
#include <memory>

namespace So5Hud {
enum class LibraryStatus {Idle,Scanning,Applying,Queued,Failed,Saved};
struct LibraryEntry {std::string name;bool compatible=false;};
struct LibrarySnapshot {std::vector<LibraryEntry> entries;LibraryStatus status=LibraryStatus::Idle;bool busy=false;std::string active_name;};
// Files are read off the render thread. A selection uses the same validated,
// frame-boundary transaction as live editing, including preview-owner exclusion.
class PresetLibrary {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    PresetLibrary(std::filesystem::path prepared,Aspect,bool offline=false);
    ~PresetLibrary();
    LibrarySnapshot Snapshot() const;
    void Refresh();
    void Apply(size_t index);
    void Restore();
};
}
