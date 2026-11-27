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

    components.add<UIShape>("UIShape")
        .label("UI Shape")
        .field<&UIShape::shape>("shape").options({"Rect", "RoundedRect", "Circle", "Line"})
        .field<&UIShape::color>("color", 0.01f).color()
        .field<&UIShape::cornerRadius>("cornerRadius", 0.5f)
        .field<&UIShape::borderWidth>("borderWidth", 0.1f)
        .field<&UIShape::borderColor>("borderColor", 0.01f).color()
        .field<&UIShape::lineWidth>("lineWidth", 0.1f);
}
