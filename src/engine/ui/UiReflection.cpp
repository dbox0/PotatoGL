#include "UiReflection.h"

#include "engine/reflect/ComponentRegistry.h"
#include "engine/ui/UiComponents.h"

void registerUiComponents(ComponentRegistry& components)
{
    components.add<Canvas>("Canvas")
        .field<&Canvas::referenceResolution>("referenceResolution", 1.0f)
        .field<&Canvas::scaleMode>("scaleMode").options({"ConstantPixelSize", "ScaleWithScreenSize"})
        .field<&Canvas::matchWidthOrHeight>("matchWidthOrHeight", 0.01f)
        .field<&Canvas::sortOrder>("sortOrder");

    components.add<RectTransform>("RectTransform")
        .field<&RectTransform::anchorMin>("anchorMin", 0.01f)
        .field<&RectTransform::anchorMax>("anchorMax", 0.01f)
        .field<&RectTransform::pivot>("pivot", 0.01f)
        .field<&RectTransform::anchoredPosition>("anchoredPosition", "Position", 1.0f)
        .field<&RectTransform::sizeDelta>("sizeDelta", 1.0f)
        .field<&RectTransform::rotationDeg>("rotationDeg", "Rotation", 0.5f)
        .field<&RectTransform::scale>("scale", 0.01f);

    components.add<UIImage>("UIImage")
        .label("UI Image")
        .property(
            "sprite", "Sprite", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const UIImage*>(c)->sprite; },
            [](void* c, const FieldValue& v) {
                auto* image = static_cast<UIImage*>(c);
                image->sprite = std::get<std::string>(v);
                image->texture = InvalidTexture;
            })
        .asset(AssetKind::Texture)
        .field<&UIImage::color>("color", 0.01f).color()
        .field<&UIImage::type>("type").options({"Simple", "Sliced", "Filled"})
        .field<&UIImage::fillAmount>("fillAmount", 0.005f)
        .field<&UIImage::fillMethod>("fillMethod").options({"Horizontal", "Vertical", "Radial360"})
        .field<&UIImage::fillOrigin>("fillOrigin").options({"LeftOrBottom", "RightOrTop"})
        .field<&UIImage::preserveAspect>("preserveAspect");

    components.add<UIText>("UIText")
        .label("UI Text")
        .field<&UIText::text>("text").multiline()
        .field<&UIText::font>("font").asset(AssetKind::Font)
        .field<&UIText::fontSize>("fontSize", 0.5f)
        .field<&UIText::color>("color", 0.01f).color()
        .field<&UIText::alignH>("alignH", "Horizontal").options({"Left", "Center", "Right"})
        .field<&UIText::alignV>("alignV", "Vertical").options({"Top", "Middle", "Bottom"})
        .field<&UIText::wrap>("wrap")
        .field<&UIText::overflow>("overflow").options({"Overflow", "Truncate"})
        .field<&UIText::lineSpacing>("lineSpacing", 0.01f);

    components.add<UIButton>("UIButton")
        .label("UI Button")
        .field<&UIButton::interactable>("interactable")
        .field<&UIButton::target>("target")
        .field<&UIButton::normal>("normal", 0.01f).color()
        .field<&UIButton::hover>("hover", 0.01f).color()
        .field<&UIButton::pressed>("pressed", 0.01f).color()
        .field<&UIButton::disabled>("disabled", 0.01f).color()
        .field<&UIButton::fadeDuration>("fadeDuration", 0.01f)
        .field<&UIButton::onClick>("onClick");

    components.add<UISlider>("UISlider")
        .label("UI Slider")
        .field<&UISlider::interactable>("interactable")
        .field<&UISlider::min>("min", 0.01f)
        .field<&UISlider::max>("max", 0.01f)
        .field<&UISlider::value>("value", 0.01f)
        .field<&UISlider::wholeNumbers>("wholeNumbers")
        .field<&UISlider::direction>("direction").options({"LeftToRight", "RightToLeft", "BottomToTop", "TopToBottom"})
        .field<&UISlider::fill>("fill")
        .field<&UISlider::handle>("handle")
        .field<&UISlider::onChanged>("onChanged");

    components.add<UIBar>("UIBar")
        .label("UI Bar")
        .field<&UIBar::fill>("fill")
        .field<&UIBar::label>("label")
        .field<&UIBar::labelFormat>("labelFormat")
        .field<&UIBar::value>("value", 0.1f)
        .field<&UIBar::max>("max", 0.1f)
        .property(
            "listen", "Listen", FieldType::String,
            [](const void* c) -> FieldValue { return static_cast<const UIBar*>(c)->listen; },
            [](void* c, const FieldValue& v) {
                auto* bar = static_cast<UIBar*>(c);
                bar->listen = std::get<std::string>(v);
                bar->listenHash = 0;
            })
        .field<&UIBar::smoothing>("smoothing", 0.1f)
        .field<&UIBar::lowColor>("lowColor", 0.01f).color()
        .field<&UIBar::highColor>("highColor", 0.01f).color()
        .field<&UIBar::gradient>("gradient");

    components.add<UIShape>("UIShape")
        .label("UI Shape")
        .field<&UIShape::shape>("shape").options({"Rect", "RoundedRect", "Circle", "Line"})
        .field<&UIShape::color>("color", 0.01f).color()
        .field<&UIShape::cornerRadius>("cornerRadius", 0.5f)
        .field<&UIShape::borderWidth>("borderWidth", 0.1f)
        .field<&UIShape::borderColor>("borderColor", 0.01f).color()
        .field<&UIShape::lineWidth>("lineWidth", 0.1f);
}
