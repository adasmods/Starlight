#include "Config.hpp"
#include "State.hpp"
#include "hacks/Extras.hpp"
#include "recorder/Recorder.hpp"
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

// Custom jump / platformer keys -> GJBaseGameLayer::handleButton.
// Returns true if the key is bound and was handled.
static bool handleGameBind(int key, KeyboardInputData::Action action) {
    auto& c = cfg();
    int button = 0;
    bool player1 = true;
    for (int k : c.jumpKeys) {
        if (k != 0 && k == key) button = 1;
    }
    if (!button && c.p2JumpKey != 0 && key == c.p2JumpKey) {
        button = 1;
        player1 = false;
    }
    if (!button && c.leftKey != 0 && key == c.leftKey) button = 2;
    if (!button && c.rightKey != 0 && key == c.rightKey) button = 3;
    if (!button) return false;

    auto pl = PlayLayer::get();
    if (!pl) return false;
    // let the key through to the pause menu
    if (auto scene = CCDirector::get()->getRunningScene(); scene && scene->getChildByType<PauseLayer>(0)) return false;

    if (action == KeyboardInputData::Action::Repeat) return true;
    pl->handleButton(action == KeyboardInputData::Action::Press, button, player1);
    return true;
}

// Click teleport: moves player 1 to the mouse cursor (cheat).
static bool teleportToMouse() {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_player1 || !pl->m_player1->getParent()) return false;
    auto player = pl->m_player1;
    CCPoint local = player->getParent()->convertToNodeSpace(geode::cocos::getMousePos());
    player->m_position = local;
    player->setPosition(local);
    player->m_yVelocity = 0.0;
    state().cheatedAttempt = true;
    return true;
}

// Keyboard: menu toggle, keybind capture, noclip key, custom jump keys.
// Priority -10 so we see keys before imgui-cocos swallows them.
$execute {
    KeyboardInputEvent()
        .listen(
            [](KeyboardInputData& evt) -> bool {
                int key = static_cast<int>(evt.key);
                bool press = evt.action == KeyboardInputData::Action::Press;

                if (press && menu::isOpen() && menu::captureKey(key)) return ListenerResult::Stop;

                // don't steal keys while typing in an imgui text field
                if (ImGuiCocos::get().isInitialized() && ImGuiCocos::get().isVisible() && ImGui::GetIO().WantTextInput) {
                    return ListenerResult::Propagate;
                }

                if (handleGameBind(key, evt.action)) return ListenerResult::Stop;

                auto& c = cfg();

                // frame stepper: each press (or key repeat while held) advances one tick
                if (c.frameStepper && c.stepKey != 0 && key == c.stepKey && PlayLayer::get()) {
                    if (evt.action != KeyboardInputData::Action::Release) state().stepRequests++;
                    return ListenerResult::Stop;
                }

                if (!press) return ListenerResult::Propagate;

                if (c.startposSwitcher && PlayLayer::get()) {
                    if (c.prevStartPosKey != 0 && key == c.prevStartPosKey && extras::cycleStartPos(-1)) return ListenerResult::Stop;
                    if (c.nextStartPosKey != 0 && key == c.nextStartPosKey && extras::cycleStartPos(1)) return ListenerResult::Stop;
                }
                if (c.editorUiKey != 0 && key == c.editorUiKey) {
                    if (auto editor = LevelEditorLayer::get(); editor && editor->m_editorUI) {
                        static bool hidden = false;
                        hidden = !hidden;
                        editor->m_editorUI->showUI(!hidden);
                        return ListenerResult::Stop;
                    }
                }
                if (c.recordKey != 0 && key == c.recordKey) {
                    recorder::toggle();
                    return ListenerResult::Stop;
                }
                if (c.teleportKey != 0 && key == c.teleportKey) {
                    if (teleportToMouse()) return ListenerResult::Stop;
                }
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
