#include "BlueprintPlacementZoomConfig.h"

#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"

namespace BlueprintPlacementZoom
{
	const TCHAR* const ModReference = TEXT("BlueprintPlacementZoom");
	const TCHAR* const CameraDistancePropertyName = TEXT("CameraDistance");

	// The native config property classes register and save fine but render no widgets;
	// only SML's Blueprint subclasses implement CreateEditorWidget. The UCP_* classes are
	// abstract, so fall back to the plain native class if the Blueprint can't be loaded.
	UClass* LoadPropertyClass(const TCHAR* BlueprintName, UClass* Fallback)
	{
		const FString Path = FString::Printf(TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/%s.%s_C"), BlueprintName, BlueprintName);
		UClass* Class = StaticLoadClass(UConfigProperty::StaticClass(), nullptr, *Path);
		return Class != nullptr ? Class : Fallback;
	}

	UClass* LoadSectionClass()
	{
		static UClass* Class = LoadPropertyClass(TEXT("BP_ConfigPropertySection"), UConfigPropertySection::StaticClass());
		return Class;
	}

	UClass* LoadFloatClass()
	{
		static UClass* Class = LoadPropertyClass(TEXT("BP_ConfigPropertyFloat"), UConfigPropertyFloat::StaticClass());
		return Class;
	}
}

UBlueprintPlacementZoomConfiguration::UBlueprintPlacementZoomConfiguration(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	using namespace BlueprintPlacementZoom;

	ConfigId.ModReference = ModReference;
	DisplayName = FText::FromString(TEXT("Blueprint Zoomer"));
	Description = FText::FromString(TEXT("Camera distance used while placing blueprints. The toggle and zoom keys are under Options -> Controls."));

	UConfigPropertySection* Root = NewObject<UConfigPropertySection>(this, LoadSectionClass(), TEXT("RootSection"));
	if (UCP_Section* WidgetSection = Cast<UCP_Section>(Root))
	{
		WidgetSection->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	}
	RootSection = Root;

	UConfigPropertyFloat* CameraDistance = NewObject<UConfigPropertyFloat>(Root, LoadFloatClass(), CameraDistancePropertyName);
	if (UCP_Float* WidgetFloat = Cast<UCP_Float>(CameraDistance))
	{
		WidgetFloat->WidgetType = ECP_FloatWidgetType::CPF_Slider;
		WidgetFloat->MinValue = MinCameraDistance;
		WidgetFloat->MaxValue = MaxCameraDistance;
	}
	CameraDistance->DisplayName = FText::FromString(TEXT("Zoom Distance"));
	CameraDistance->Tooltip = FText::FromString(TEXT("How far the camera pulls back while placing a blueprint. Hold the zoom modifier and scroll to change it in game."));
	CameraDistance->DefaultValue = DefaultCameraDistance;
	CameraDistance->Value = DefaultCameraDistance;
	CameraDistance->bRequiresWorldReload = false;
	Root->SectionProperties.Add(CameraDistancePropertyName, CameraDistance);
}

UBlueprintPlacementZoomGameInstanceModule::UBlueprintPlacementZoomGameInstanceModule()
{
	// Native root modules are silently skipped by SML unless this is set.
	bRootModule = true;
	ModConfigurations.Add(UBlueprintPlacementZoomConfiguration::StaticClass());
}
