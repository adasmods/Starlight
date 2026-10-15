#pragma once

namespace starlight::menu {

// ImGui setup callback (theme + fonts). Runs again whenever ImGui is re-initialized.
void setup();
// Draws the whole menu (called every frame by imgui-cocos while visible).
void draw();

void toggle();
bool isOpen();

// If a keybind button is waiting for a key, consumes it and returns true.
bool captureKey(int key);

} // namespace starlight::menu
