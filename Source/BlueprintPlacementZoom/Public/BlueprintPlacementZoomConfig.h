#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Module/GameInstanceModule.h"
#include "BlueprintPlacementZoomConfig.generated.h"

namespace BlueprintPlacementZoom
{
	extern const TCHAR* const ModReference;
	extern const TCHAR* const CameraDistancePropertyName;

	constexpr float DefaultCameraDistance = 1600.0f;
	constexpr float MinCameraDistance = 300.0f;
	constexpr float MaxCameraDistance = 10000.0f;
}

/** Settings shown under Mods -> Blueprint Zoomer. Built entirely in C++, no content assets. */
UCLASS()
class BLUEPRINTPLACEMENTZOOM_API UBlueprintPlacementZoomConfiguration : public UModConfiguration
{
	GENERATED_BODY()

public:
	UBlueprintPlacementZoomConfiguration(const FObjectInitializer& ObjectInitializer);
};

/** Root module whose only job is registering the mod configuration with SML. */
UCLASS()
class BLUEPRINTPLACEMENTZOOM_API UBlueprintPlacementZoomGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UBlueprintPlacementZoomGameInstanceModule();
};
