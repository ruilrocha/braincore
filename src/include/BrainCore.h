#pragma once

// BrainCore — Swift-facing umbrella header.
//
// Only includes BrainSession.h, which uses the Pimpl pattern so Swift's
// module generator sees only <cstddef> and <memory> — no C++20/23 constructs.
//
// Import in Swift:
//   import BrainCore
//   var session = audio.BrainSession()

#include "../BrainSession.h"
