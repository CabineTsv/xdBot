#pragma once

#include "../../core/bot.hpp"
#include "../../hacks/intentional_death.hpp"
#include "../../hacks/intentional_death_planner.hpp"
#include "../../utils/utils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/Prelude.hpp>

#include <algorithm>

using namespace geode::prelude;

class IntentionalDeathLayer : public geode::Popup, public TextInputDelegate {

  public:
    static IntentionalDeathLayer* create() {
        auto* layer = new IntentionalDeathLayer();
        if (layer && layer->init()) {
            layer->autorelease();
            return layer;
        }
        delete layer;
        return nullptr;
    }

  private:
    TextInput* minInput = nullptr;
    TextInput* maxInput = nullptr;
    TextInput* framesInput = nullptr;

    CCLabelBMFont* rangeLabel = nullptr;
    CCLabelBMFont* framesLabel = nullptr;

    ~IntentionalDeathLayer() override {
        for (auto* input : {minInput, maxInput, framesInput}) {
            if (input)
                input->getInputNode()->setDelegate(nullptr);
        }
    }

    static int readNumber(TextInput* input, int low, int high) {
        int value = geode::utils::numFromString<int>(input->getString()).unwrapOr(0);
        return std::clamp(value, low, high);
    }

    TextInput* createInput(CCPoint position, char const* placeholder, int maxChars, int value) {
        TextInput* input = TextInput::create(50, placeholder, "chatFont.fnt");
        input->setPosition(position);
        input->setString(geode::utils::numToString(value).c_str());
        input->getInputNode()->setAllowedChars("0123456789");
        input->getInputNode()->setMaxLabelLength(maxChars);
        input->getInputNode()->setDelegate(this);
        m_mainLayer->addChild(input);
        return input;
    }

    void updateLabels() {
        auto* mod = Mod::get();

        int minPercent = std::clamp<int>(
            static_cast<int>(mod->getSavedValue<int64_t>("intentional_death_min")), 0, 100);
        int maxPercent = std::clamp<int>(
            static_cast<int>(mod->getSavedValue<int64_t>("intentional_death_max")), 0, 100);
        int frames = std::clamp<int>(
            static_cast<int>(mod->getSavedValue<int64_t>("intentional_death_frames")),
            1,
            intentional_death::maxFrameShift);

        float dt = 1.f / Bot::getTPS();

        rangeLabel->setString(fmt::format("Fails at a random point in {}% - {}%",
                                          std::min(minPercent, maxPercent),
                                          std::max(minPercent, maxPercent))
                                  .c_str());
        framesLabel->setString(fmt::format("Up to {} frames ({:.3f}s) early or late",
                                           frames,
                                           dt * static_cast<float>(frames))
                                   .c_str());
    }

    void textChanged(CCTextInputNode*) override {
        auto* mod = Mod::get();

        mod->setSavedValue("intentional_death_min", readNumber(minInput, 0, 100));
        mod->setSavedValue("intentional_death_max", readNumber(maxInput, 0, 100));
        mod->setSavedValue("intentional_death_frames",
                           readNumber(framesInput, 1, intentional_death::maxFrameShift));

        IntentionalDeath::reload();
        updateLabels();
    }

    bool init() override {
        if (!Popup::init(250, 205, Utils::getTexture().c_str()))
            return false;
        setTitle("Intentional Death");

        Utils::setBackgroundColor(m_bgSprite);

        auto* mod = Mod::get();
        float x = m_size.width - 72;

        CCLabelBMFont* lbl = CCLabelBMFont::create("Min %", "bigFont.fnt");
        lbl->setPosition({72, 152});
        lbl->setScale(0.37f);
        m_mainLayer->addChild(lbl);

        lbl = CCLabelBMFont::create("Max %", "bigFont.fnt");
        lbl->setPosition({x, 152});
        lbl->setScale(0.37f);
        m_mainLayer->addChild(lbl);

        minInput = createInput(
            {72, 128},
            "%",
            3,
            static_cast<int>(mod->getSavedValue<int64_t>("intentional_death_min")));
        maxInput = createInput(
            {x, 128},
            "%",
            3,
            static_cast<int>(mod->getSavedValue<int64_t>("intentional_death_max")));

        rangeLabel = CCLabelBMFont::create("", "chatFont.fnt");
        rangeLabel->setPosition({m_size.width / 2, 103});
        rangeLabel->setScale(0.4f);
        rangeLabel->setOpacity(86);
        m_mainLayer->addChild(rangeLabel);

        lbl = CCLabelBMFont::create("Frames", "bigFont.fnt");
        lbl->setPosition({m_size.width / 2, 80});
        lbl->setScale(0.37f);
        m_mainLayer->addChild(lbl);

        framesInput = createInput(
            {m_size.width / 2, 56},
            "Frames",
            2,
            static_cast<int>(mod->getSavedValue<int64_t>("intentional_death_frames")));

        framesLabel = CCLabelBMFont::create("", "chatFont.fnt");
        framesLabel->setPosition({m_size.width / 2, 34});
        framesLabel->setScale(0.4f);
        framesLabel->setOpacity(86);
        m_mainLayer->addChild(framesLabel);

        ButtonSprite* btnSpr = ButtonSprite::create("OK");
        btnSpr->setScale(0.6f);
        CCMenuItemSpriteExtra* btn =
            CCMenuItemExt::createSpriteExtra(btnSpr, [this](CCMenuItemSpriteExtra* sender) {
                IntentionalDeathLayer::onClose(sender);
            });
        btn->setPosition({m_size.width / 2, 14});
        m_buttonMenu->addChild(btn);

        updateLabels();

        return true;
    }
};
