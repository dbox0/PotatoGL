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

    components.add<UIShape>("UIShape")
        .label("UI Shape")
        .field<&UIShape::shape>("shape").options({"Rect", "RoundedRect", "Circle", "Line"})
        .field<&UIShape::color>("color", 0.01f).color()
        .field<&UIShape::cornerRadius>("cornerRadius", 0.5f)
        .field<&UIShape::borderWidth>("borderWidth", 0.1f)
        .field<&UIShape::borderColor>("borderColor", 0.01f).color()
        .field<&UIShape::lineWidth>("lineWidth", 0.1f);
}
