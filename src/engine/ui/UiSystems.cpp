#include "UiSystems.h"

#include "engine/core/Input.h"
#include "engine/scene/Components.h"
#include "engine/scene/Scene.h"
#include "engine/ui/UiComponents.h"
#include "engine/ui/UiHitTest.h"
#include "engine/ui/UiLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static bool hasGraphic(const entt::registry& registry, entt::entity e)
{
    return registry.any_of<UIImage, UIShape, UIText>(e);
}

// A slider's or bar's parts live under it; a stale reference elsewhere is ignored rather than rewritten
static bool partOf(const Scene& scene, entt::entity owner, entt::entity part)
{
    return part != entt::null && scene.registry().valid(part) && scene.isAncestor(owner, part);
}

static float sliderRatio(const UISlider& slider)
{
    const float range = slider.max - slider.min;
    return range != 0.0f ? std::clamp((slider.value - slider.min) / range, 0.0f, 1.0f) : 0.0f;
}

static float sliderValueAt(const UISlider& slider, const UIRect& rect, glm::vec2 point)
{
    const glm::vec2 local = uiToLocal(rect, point);
    const glm::vec2 lo = glm::min(rect.min, rect.max), size = glm::abs(rect.max - rect.min);
    float t = 0.0f;
    switch (slider.direction) {
    case SliderDirection::LeftToRight: t = size.x > 0.0f ? (local.x - lo.x) / size.x : 0.0f; break;
    case SliderDirection::RightToLeft: t = size.x > 0.0f ? 1.0f - (local.x - lo.x) / size.x : 0.0f; break;
    case SliderDirection::BottomToTop: t = size.y > 0.0f ? 1.0f - (local.y - lo.y) / size.y : 0.0f; break;
    case SliderDirection::TopToBottom: t = size.y > 0.0f ? (local.y - lo.y) / size.y : 0.0f; break;
    }
    float value = slider.min + std::clamp(t, 0.0f, 1.0f) * (slider.max - slider.min);
    if (slider.wholeNumbers)
        value = std::round(value);
    return value;
}

void uiInputSystem(Scene& scene, Input& input, float dt)
{
    entt::registry& registry = scene.registry();
    const glm::vec2 mouse = input.mousePosition();
    const auto interactive = [&](entt::entity e) {
        const UIButton* button = registry.try_get<UIButton>(e);
        const UISlider* slider = registry.try_get<UISlider>(e);
        return (button && button->interactable) || (slider && slider->interactable);
    };

    entt::entity hit = entt::null;
    bool overUi = false;
    std::vector<entt::entity> canvases = uiCanvases(scene);
    for (auto it = canvases.rbegin(); it != canvases.rend() && hit == entt::null; ++it) {
        hit = uiHitTest(scene, *it, mouse, interactive);
        overUi = overUi || hit != entt::null ||
                 uiHitTest(scene, *it, mouse, [&](entt::entity e) { return hasGraphic(registry, e); }) != entt::null;
    }
    input.pointerOverUi = overUi;

    const bool pressed = input.mousePressed(SDL_BUTTON_LEFT);
    const bool released = input.mouseReleased(SDL_BUTTON_LEFT);
    const bool down = input.isMouseDown(SDL_BUTTON_LEFT);
    // Sent once the views are done: listeners may add or remove components
    std::vector<entt::entity> clicked;
    std::vector<std::pair<entt::entity, float>> changed;

    for (auto [e, button] : registry.view<UIButton>().each()) {
        UIInteraction& state = registry.get_or_emplace<UIInteraction>(e);
        state.hovered = e == hit;
        if (pressed && state.hovered)
            state.pressed = true;
        if (released && state.pressed) {
            state.pressed = false;
            if (state.hovered && button.interactable)
                clicked.push_back(e);
        }
        if (!down && !released)
            state.pressed = false;

        const glm::vec4 goal = !button.interactable              ? button.disabled
                               : state.pressed && state.hovered ? button.pressed
                               : state.hovered                  ? button.hover
                                                                 : button.normal;
        const entt::entity target =
            button.target != entt::null && registry.valid(button.target) ? button.target : e;
        if (!registry.valid(target))
            continue;
        const bool fresh = !registry.all_of<UITint>(target);
        UITint& tint = registry.get_or_emplace<UITint>(target);
        const float t = fresh || button.fadeDuration <= 0.0f ? 1.0f : std::min(dt / button.fadeDuration, 1.0f);
        tint.color += (goal - tint.color) * t;
    }

    for (auto [e, slider] : registry.view<UISlider>().each()) {
        UIInteraction& state = registry.get_or_emplace<UIInteraction>(e);
        state.hovered = e == hit;
        if (pressed && state.hovered && slider.interactable)
            state.captured = true;
        if (!down)
            state.captured = false;
        if ((state.captured || (pressed && state.hovered && slider.interactable)) && registry.all_of<UIRect>(e)) {
            const float value = sliderValueAt(slider, registry.get<UIRect>(e), mouse);
            if (value != slider.value) {
                slider.value = value;
                changed.emplace_back(e, value);
            }
        }
    }

    EventBus* bus = registry.ctx().find<EventBus>();
    if (!bus)
        return;
    for (const entt::entity e : clicked) {
        bus->emit(UIClicked{e});
        if (const UIButton* button = registry.try_get<UIButton>(e); button && !button->onClick.empty())
            bus->emit(NamedEvent::make(button->onClick, 0.0f, 1.0f, e));
    }
    for (const auto& [e, value] : changed) {
        bus->emit(UISliderChanged{e, value});
        if (const UISlider* slider = registry.try_get<UISlider>(e); slider && !slider->onChanged.empty())
            bus->emit(NamedEvent::make(slider->onChanged, value, slider->max, e));
    }
}

void uiSliderVisualSystem(Scene& scene)
{
    entt::registry& registry = scene.registry();
    for (auto [e, slider] : registry.view<UISlider>().each()) {
        if (slider.wholeNumbers)
            slider.value = std::round(slider.value);
        const float lo = std::min(slider.min, slider.max), hi = std::max(slider.min, slider.max);
        slider.value = std::clamp(slider.value, lo, hi);
        const float r = sliderRatio(slider);
        const bool horizontal =
            slider.direction == SliderDirection::LeftToRight || slider.direction == SliderDirection::RightToLeft;
        const int axis = horizontal ? 0 : 1;
        // y points down, so bottom-to-top grows from 1
        const bool fromEnd =
            slider.direction == SliderDirection::RightToLeft || slider.direction == SliderDirection::BottomToTop;

        if (partOf(scene, e, slider.fill)) {
            UIImage* image = registry.try_get<UIImage>(slider.fill);
            if (image && image->type == UIImageType::Filled) {
                image->fillAmount = r;
            } else if (RectTransform* rt = registry.try_get<RectTransform>(slider.fill)) {
                rt->anchorMin[axis] = fromEnd ? 1.0f - r : 0.0f;
                rt->anchorMax[axis] = fromEnd ? 1.0f : r;
            }
        }
        if (partOf(scene, e, slider.handle)) {
            if (RectTransform* rt = registry.try_get<RectTransform>(slider.handle)) {
                const float at = fromEnd ? 1.0f - r : r;
                rt->anchorMin[axis] = at;
                rt->anchorMax[axis] = at;
            }
        }
    }
}

void UiBarSystem::attach(Scene& scene, EventBus& bus)
{
    m_scene = &scene;
    m_connection = bus.on<NamedEvent, &UiBarSystem::onNamed>(*this);
}

void UiBarSystem::onNamed(const NamedEvent& event)
{
    if (!m_scene)
        return;
    for (auto [e, bar] : m_scene->registry().view<UIBar>().each()) {
        if (bar.listen.empty())
            continue;
        if (bar.listenHash == 0)
            bar.listenHash = NamedEvent::hash(bar.listen);
        if (bar.listenHash == event.name) {
            bar.value = event.value;
            bar.max = event.max;
        }
    }
}

static std::string formatNumber(float v)
{
    char buffer[32];
    if (std::abs(v - std::round(v)) < 0.005f)
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(v));
    return buffer;
}

static std::string formatLabel(const std::string& format, float value, float max, float ratio)
{
    std::string out;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] == '{' && i + 2 < format.size() && format[i + 2] == '}') {
            const char which = format[i + 1];
            if (which == '0' || which == '1' || which == '2') {
                out += which == '0' ? formatNumber(value) : which == '1' ? formatNumber(max)
                                                                         : formatNumber(std::round(ratio * 100.0f));
                i += 2;
                continue;
            }
        }
        out += format[i];
    }
    return out;
}

void UiBarSystem::update(Scene& scene, float dt, bool playing)
{
    entt::registry& registry = scene.registry();
    for (auto [e, bar] : registry.view<UIBar>().each()) {
        const float target = bar.max > 0.0f ? std::clamp(bar.value / bar.max, 0.0f, 1.0f) : 0.0f;
        if (!playing || bar.smoothing <= 0.0f)
            bar.shown = target;
        else
            bar.shown += (target - bar.shown) * (1.0f - std::exp(-bar.smoothing * dt));
        if (partOf(scene, e, bar.fill)) {
            if (UIImage* image = registry.try_get<UIImage>(bar.fill)) {
                image->fillAmount = bar.shown;
                if (bar.gradient)
                    image->color = glm::mix(bar.lowColor, bar.highColor, bar.shown);
            }
        }
        if (partOf(scene, e, bar.label)) {
            if (UIText* text = registry.try_get<UIText>(bar.label))
                text->text = formatLabel(bar.labelFormat, bar.value, bar.max, target);
        }
    }
}
