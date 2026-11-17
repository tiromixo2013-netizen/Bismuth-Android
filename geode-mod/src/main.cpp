#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

using namespace geode::prelude;

namespace {
void addPauseSettingsButton(CCNode* layer, CCObject* target, SEL_MenuHandler selector) {
    auto icon = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    if (!icon) {
        log::warn("Bismuth: pause settings icon sprite was not found.");
        return;
    }

    icon->setScale(0.62f);
    auto button = CCMenuItemSpriteExtra::create(icon, target, selector);
    if (!button) return;

    auto menu = CCMenu::create();
    if (!menu) return;
    menu->addChild(button);
    menu->setPosition(CCPointZero);
    menu->setID("bismuth.android/pause-settings-menu"_spr);
    layer->addChild(menu, 1000);

    // Keep it in the left rail, below the usual top-left mod-button position.
    // A separate menu prevents changing or reordering another mod's controls.
    const auto size = CCDirector::sharedDirector()->getWinSize();
    button->setPosition({28.f, size.height - 88.f});
    button->setID("bismuth.android/pause-settings"_spr);
}
}

class $modify(BismuthPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        addPauseSettingsButton(this, this, menu_selector(BismuthPauseLayer::onBismuthSettings));
    }

    void onBismuthSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }
};

class $modify(BismuthEditorPauseLayer, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();
        addPauseSettingsButton(this, this, menu_selector(BismuthEditorPauseLayer::onBismuthSettings));
    }

    void onBismuthSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }
};

$on_mod(Loaded) {
    // Safe mode: do not patch Cocos2d from Geode's Loaded event. The game
    // renderer may already be active and other mods may own hooks on the same
    // methods. Native hook integration remains dormant until a separately
    // validated pre-render installation path is available.
    log::info("Bismuth: Geode UI loaded in safe mode; native Cocos hooks are not installed.");
    if (Mod::get()->getSettingValue<bool>("diagnostics")) {
        log::info("Bismuth diagnostics: pause-menu settings entry is available; renderer hooks are disabled.");
    }
}
