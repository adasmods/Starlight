// Your own profile page: cosmic gradient name, a clickable "S" badge that
// expands "Starlight mod user." and a UID line. Visual only.

#include "Config.hpp"
#include "visuals/Textures.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>

#include <cmath>
#include <string>

using namespace geode::prelude;
using namespace starlight;

namespace {

constexpr int kTagFx = 0x51A1;
constexpr int kTagBubble = 0x51A2;
constexpr int kTagUid = 0x51A3;
constexpr int kTagBadge = 0x51A4;

constexpr char const* kThemeFont = "Candara";

ccColor3B lerpC(ccColor3B a, ccColor3B b, float t) {
    return {static_cast<GLubyte>(a.r + (b.r - a.r) * t), static_cast<GLubyte>(a.g + (b.g - a.g) * t),
            static_cast<GLubyte>(a.b + (b.b - a.b) * t)};
}

// looping pink -> violet -> blue -> violet -> pink
ccColor3B cosmic(float f) {
    static ccColor3B const stops[] = {{255, 140, 220}, {190, 120, 255}, {120, 160, 255}, {190, 120, 255}, {255, 140, 220}};
    f = f - std::floor(f);
    float x = f * 4.f;
    int i = std::min(static_cast<int>(x), 3);
    return lerpC(stops[i], stops[i + 1], x - i);
}

CCLabelBMFont* findLabel(CCNode* node, std::string const& a, std::string const& b) {
    if (!node) return nullptr;
    if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) {
        std::string s = label->getString();
        if ((!a.empty() && s == a) || (!b.empty() && s == b)) return label;
    }
    auto children = node->getChildren();
    if (!children) return nullptr;
    for (auto child : CCArrayExt<CCNode*>(children)) {
        if (auto found = findLabel(child, a, b)) return found;
    }
    return nullptr;
}

// Animates each letter of the name with a flowing cosmic gradient + soft glow.
class CosmicNameFx : public CCNode {
public:
    static CosmicNameFx* create(CCLabelBMFont* label) {
        auto ret = new CosmicNameFx();
        if (ret->init(label)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    ~CosmicNameFx() override {
        if (m_label) m_label->release();
    }

    bool init(CCLabelBMFont* label) {
        if (!CCNode::init()) return false;
        m_label = label;
        m_label->retain();

        m_glow = CCSprite::createWithTexture(tex::glow(64));
        m_glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        m_glow->setColor({160, 100, 255});
        this->addChild(m_glow);

        this->scheduleUpdate();
        this->update(0.f);
        return true;
    }

    void update(float dt) override {
        m_time += dt;
        auto children = m_label->getChildren();
        if (children) {
            int n = static_cast<int>(children->count());
            int i = 0;
            for (auto child : CCArrayExt<CCNode*>(children)) {
                if (auto spr = typeinfo_cast<CCSprite*>(child)) {
                    float f = n > 1 ? static_cast<float>(i) / (n - 1) : 0.f;
                    spr->setColor(cosmic(f * 0.75f - m_time * 0.18f));
                }
                i++;
            }
        }

        CCRect bb = m_label->boundingBox();
        float breathe = 0.85f + 0.15f * std::sin(m_time * 1.4f);
        m_glow->setPosition({bb.getMidX(), bb.getMidY()});
        auto cs = m_glow->getContentSize();
        m_glow->setScaleX((bb.size.width * 1.25f + 30.f) / cs.width);
        m_glow->setScaleY((bb.size.height * 1.5f + 16.f) / cs.height);
        m_glow->setOpacity(static_cast<GLubyte>(70.f * breathe));
    }

private:
    CCLabelBMFont* m_label = nullptr;
    CCSprite* m_glow = nullptr;
    float m_time = 0.f;
};

} // namespace

class $modify(StarlightProfilePage, ProfilePage) {
    struct Fields {
        CCNode* bubble = nullptr;
        bool bubbleOpen = false;
    };

    void loadPageFromUserInfo(GJUserScore* score) {
        ProfilePage::loadPageFromUserInfo(score);
        auto& c = cfg();
        if (!c.cosmicProfile || !score || !m_mainLayer) return;

        auto am = GJAccountManager::sharedState();
        if (!am || am->m_accountID <= 0 || score->m_accountID != am->m_accountID) return;

        std::string realName = score->m_userName;
        std::string shown = c.nickChanger && !c.nick.empty() ? c.nick : realName;
        auto label = findLabel(m_mainLayer, shown, realName);
        if (!label || !label->getParent()) return;
        auto parent = label->getParent();

        // clean up a previous pass (the page can reload)
        for (int tag : {kTagFx, kTagBubble, kTagUid}) {
            if (auto old = parent->getChildByTag(tag)) old->removeFromParent();
        }
        if (m_buttonMenu) {
            if (auto old = m_buttonMenu->getChildByTag(kTagBadge)) old->removeFromParent();
        }
        m_fields->bubble = nullptr;
        m_fields->bubbleOpen = false;

        // ---- gradient name
        auto fx = CosmicNameFx::create(label);
        fx->setTag(kTagFx);
        parent->addChild(fx, label->getZOrder() - 1);

        CCRect bb = label->boundingBox();

        // ---- "S" badge left of the name
        if (m_buttonMenu) {
            auto badge = CCSprite::createWithTexture(tex::disc(64, false));
            float d = 20.f;
            badge->setScale(d / badge->getContentSize().width);
            badge->setColor({150, 90, 255});

            auto badgeNode = CCNode::create();
            badgeNode->setContentSize({d, d});
            badge->setPosition({d * 0.5f, d * 0.5f});
            badgeNode->addChild(badge);

            auto ring = CCSprite::createWithTexture(tex::glow(64));
            ring->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            ring->setColor({200, 140, 255});
            ring->setOpacity(150);
            ring->setScale(d * 2.2f / ring->getContentSize().width);
            ring->setPosition({d * 0.5f, d * 0.5f});
            badgeNode->addChild(ring, -1);

            auto s = CCLabelBMFont::create("S", "bigFont.fnt");
            s->setScale(0.42f);
            s->setPosition({d * 0.5f + 0.5f, d * 0.5f + 1.f});
            s->setColor({255, 243, 226});
            badgeNode->addChild(s, 1);

            auto item = CCMenuItemSpriteExtra::create(badgeNode, this, menu_selector(StarlightProfilePage::sl_onBadge));
            item->setTag(kTagBadge);
            CCPoint world = parent->convertToWorldSpace({bb.getMinX() - 16.f, bb.getMidY()});
            item->setPosition(m_buttonMenu->convertToNodeSpace(world));
            m_buttonMenu->addChild(item);

            // ---- bubble (hidden until the badge is clicked)
            auto bubble = CCNode::create();
            bubble->setTag(kTagBubble);
            bubble->setAnchorPoint({0.f, 1.f});
            bubble->setPosition({bb.getMinX() - 26.f, bb.getMinY() - 4.f});

            auto text = CCLabelTTF::create("Starlight mod user.", kThemeFont, 30.f);
            text->setScale(0.42f);
            text->setColor({255, 243, 226});
            CCSize ts{text->getContentSize().width * 0.42f, text->getContentSize().height * 0.42f};
            CCSize size{ts.width + 22.f, ts.height + 10.f};
            bubble->setContentSize(size);

            auto bg = CCScale9Sprite::create("square02_small.png");
            bg->setContentSize(size);
            bg->setColor({28, 14, 52});
            bg->setOpacity(230);
            bg->setPosition({size.width * 0.5f, size.height * 0.5f});
            bubble->addChild(bg);

            auto glow = CCSprite::createWithTexture(tex::glow(64));
            glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            glow->setColor({150, 90, 255});
            glow->setOpacity(90);
            glow->setScaleX((size.width + 30.f) / glow->getContentSize().width);
            glow->setScaleY((size.height + 24.f) / glow->getContentSize().height);
            glow->setPosition({size.width * 0.5f, size.height * 0.5f});
            bubble->addChild(glow, -1);

            text->setPosition({size.width * 0.5f, size.height * 0.5f});
            bubble->addChild(text, 1);

            bubble->setScale(0.f);
            bubble->setVisible(false);
            parent->addChild(bubble, 100);
            m_fields->bubble = bubble;
        }

        // ---- UID under the name
        auto uid = CCLabelTTF::create(fmt::format("UID: {}", c.uid).c_str(), kThemeFont, 26.f);
        uid->setTag(kTagUid);
        uid->setScale(0.38f);
        uid->setColor({225, 205, 255});
        uid->setOpacity(220);
        uid->setPosition({bb.getMidX(), bb.getMinY() - 5.f});
        parent->addChild(uid, label->getZOrder());
    }

    void sl_onBadge(CCObject*) {
        auto bubble = m_fields->bubble;
        if (!bubble) return;
        bubble->stopAllActions();
        m_fields->bubbleOpen = !m_fields->bubbleOpen;
        if (m_fields->bubbleOpen) {
            bubble->setVisible(true);
            bubble->runAction(CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.f)));
        } else {
            bubble->runAction(CCSequence::create(
                CCEaseIn::create(CCScaleTo::create(0.15f, 0.f), 2.f), CCHide::create(), nullptr
            ));
        }
    }
};
