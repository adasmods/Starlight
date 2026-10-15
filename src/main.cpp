#include "Config.hpp"
#include "State.hpp"
#include "ui/Menu.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <imgui-cocos.hpp>
#include <imgui.h>

#include <chrono>

using namespace geode::prelude;
using namespace starlight;

double starlight::nowSeconds() {
    using namespace std::chrono;
    static auto const start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

$on_mod(Loaded) {
    loadConfig();

    ImGuiCocos::get()
        .setup([] { menu::setup(); })
        .draw([] { menu::draw(); });
    ImGuiCocos::get().setVisible(false);
}

// Keyboard: menu toggle, keybind capture, noclip key.
// Priority -10 so we see keys before imgui-cocos swallows them.
$execute {
    KeyboardInputEvent()
        .listen(
            [](KeyboardInputData& evt) -> bool {
                if (evt.action != KeyboardInputData::Action::Press) return ListenerResult::Propagate;
                int key = static_cast<int>(evt.key);

                if (menu::isOpen() && menu::captureKey(key)) return ListenerResult::Stop;

                // don't steal keys while typing in an imgui text field
                if (ImGuiCocos::get().isInitialized() && ImGuiCocos::get().isVisible() && ImGui::GetIO().WantTextInput) {
                    return ListenerResult::Propagate;
                }

                auto& c = cfg();
                if (c.menuKey != 0 && key == c.menuKey) {
                    menu::toggle();
                    return ListenerResult::Stop;
                }
                if (c.noclipKey != 0 && key == c.noclipKey) {
                    c.noclip = !c.noclip;
                    return ListenerResult::Stop;
                }
                return ListenerResult::Propagate;
            },
            -10
        )
        .leak();
}
