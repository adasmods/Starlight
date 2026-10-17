#pragma once

namespace starlight::extras {

// Start position switcher: -1 = level start, otherwise the index of the start pos.
int startPosCount();
int startPosIndex();
// moves to the previous (-1) or next (+1) start position; false if there are none
bool cycleStartPos(int direction);

} // namespace starlight::extras
