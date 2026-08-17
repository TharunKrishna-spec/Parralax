#pragma once

// Thin app-level entry points, called directly from the sketch's
// setup()/loop(). Kept separate from the .ino so the real logic lives in a
// normal, testable .cpp translation unit rather than the specially
// preprocessed sketch file.
namespace app {

void setup();
void loop();

}  // namespace app
