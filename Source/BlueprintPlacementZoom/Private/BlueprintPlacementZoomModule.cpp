#include "BlueprintPlacementZoomModule.h"

#include "BlueprintPlacementZoomConfig.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Engine/AssetManager.h"
#include "EnhancedPlayerInput.h"
#include "FGPlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Input/FGChildInputMappingContext.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintPlacementZoom, Log, All);

namespace BlueprintPlacementZoom
{
	constexpr float BuildRange = 35000.0f;
	const FVector CameraOffset(0.0f, 140.0f, 220.0f);

	struct FCameraState
	{
		float TargetArmLength = 0.0f;
		FVector SocketOffset = FVector::ZeroVector;
		ECameraMode PreviousCameraMode = ECameraMode::ECM_None;
		bool ForcedThirdPerson = false;
		bool SpringArmCollisionTest = true;
	};

	TMap<TWeakObjectPtr<AFGCharacterPlayer>, FCameraState> ActiveCameraStates;

	// Flipped by the toggle keybind; not saved, so every session starts with the zoom on.
	bool bZoomEnabled = true;

	// Each mouse wheel notch while holding the zoom modifier scales the distance by this much.
	constexpr float ScrollZoomStep = 0.1f;

	// Bound only while placing a blueprint with the modifier held. Higher than any of the game's
	// build gun contexts so the scroll action consumes the wheel before hologram rotation sees it.
	constexpr int32 ScrollContextPriority = 1000;

	const TCHAR* const InputContentPath = TEXT("/BlueprintPlacementZoom/Input");

	struct FInputAssets
	{
		UInputAction* ToggleZoom = nullptr;
		UInputAction* ZoomModifier = nullptr;
		UInputAction* ZoomScroll = nullptr;
		UInputMappingContext* ScrollContext = nullptr;
	};

	template <typename T>
	T* LoadInputAsset(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), InputContentPath, Name, Name);
		T* Asset = LoadObject<T>(nullptr, *Path);
		if (Asset == nullptr)
		{
			UE_LOG(LogBlueprintPlacementZoom, Error, TEXT("Failed to load input asset %s"), *Path);
			return nullptr;
		}
		// Only native code references these, so keep them from being garbage collected.
		Asset->AddToRoot();
		return Asset;
	}

	const FInputAssets& GetInputAssets()
	{
		static const FInputAssets Assets = []
		{
			FInputAssets Loaded;
			Loaded.ToggleZoom = LoadInputAsset<UInputAction>(TEXT("IA_BPZ_ToggleZoom"));
			Loaded.ZoomModifier = LoadInputAsset<UInputAction>(TEXT("IA_BPZ_ZoomModifier"));
			Loaded.ZoomScroll = LoadInputAsset<UInputAction>(TEXT("IA_BPZ_ZoomScroll"));
			Loaded.ScrollContext = LoadInputAsset<UInputMappingContext>(TEXT("MC_BPZ_Scroll"));
			return Loaded;
		}();
		return Assets;
	}

	TMap<TWeakObjectPtr<AFGPlayerController>, FBoundMappingContextHandle> ScrollContextHandles;

	// The rebindable keys live in a child context of the build gun's build context. The game finds
	// child contexts through the Asset Manager, which only scans game feature plugins for them,
	// so register this mod's input folder explicitly.
	void RegisterChildInputContexts()
	{
		UAssetManager::CallOrRegister_OnAssetManagerCreated(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
		{
			const int32 NumFound = UAssetManager::Get().ScanPathForPrimaryAssets(
				FPrimaryAssetType(UFGChildInputMappingContext::StaticClass()->GetFName()),
				InputContentPath, UFGChildInputMappingContext::StaticClass(), false);
			UE_LOG(LogBlueprintPlacementZoom, Display, TEXT("Registered %d child input context(s) from %s"), NumFound, InputContentPath);
		}));
	}

	UConfigManager* GetConfigManager(const AFGCharacterPlayer* Player)
	{
		UGameInstance* GameInstance = Player != nullptr ? Player->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	}

	FConfigId GetConfigId()
	{
		FConfigId ConfigId;
		ConfigId.ModReference = ModReference;
		return ConfigId;
	}

	UConfigPropertyFloat* GetCameraDistanceProperty(const AFGCharacterPlayer* Player)
	{
		UConfigManager* ConfigManager = GetConfigManager(Player);
		UConfigPropertySection* Root = ConfigManager != nullptr ? ConfigManager->GetConfigurationRootSection(GetConfigId()) : nullptr;
		return Root != nullptr ? Cast<UConfigPropertyFloat>(Root->SectionProperties.FindRef(CameraDistancePropertyName)) : nullptr;
	}

	float GetCameraDistance(const AFGCharacterPlayer* Player)
	{
		if (const UConfigPropertyFloat* Property = GetCameraDistanceProperty(Player))
		{
			return FMath::Clamp(Property->Value, MinCameraDistance, MaxCameraDistance);
		}
		return DefaultCameraDistance;
	}

	void AdjustCameraDistance(const AFGCharacterPlayer* Player, float WheelDelta)
	{
		UConfigPropertyFloat* Property = GetCameraDistanceProperty(Player);
		if (Property == nullptr)
		{
			return;
		}

		// Scrolling up moves the camera in, like zooming in.
		const float Scale = FMath::Pow(1.0f - ScrollZoomStep, WheelDelta);
		Property->Value = FMath::Clamp(Property->Value * Scale, MinCameraDistance, MaxCameraDistance);

		// Write it back to the mod config so the slider shows it and it persists between sessions.
		GetConfigManager(Player)->MarkConfigurationDirty(GetConfigId());
	}

	bool WasActionTriggered(const UEnhancedPlayerInput* PlayerInput, const UInputAction* Action)
	{
		const FInputActionInstance* Instance = Action != nullptr ? PlayerInput->FindActionInstanceData(Action) : nullptr;
		return Instance != nullptr && Instance->GetTriggerEvent() == ETriggerEvent::Triggered;
	}

	void HandleInput(AFGCharacterPlayer* Player, bool bPlacingBlueprint)
	{
		if (Player == nullptr || !Player->IsLocallyControlled())
		{
			return;
		}

		AFGPlayerController* PlayerController = Cast<AFGPlayerController>(Player->GetController());
		const UEnhancedPlayerInput* PlayerInput = PlayerController != nullptr ? Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput) : nullptr;
		if (PlayerInput == nullptr)
		{
			return;
		}

		const FInputAssets& Assets = GetInputAssets();
		if (WasActionTriggered(PlayerInput, Assets.ToggleZoom))
		{
			bZoomEnabled = !bZoomEnabled;
			UE_LOG(LogBlueprintPlacementZoom, Display, TEXT("Blueprint placement zoom %s"), bZoomEnabled ? TEXT("enabled") : TEXT("disabled"));
		}

		const bool bModifierHeld = Assets.ZoomModifier != nullptr && PlayerInput->GetActionValue(Assets.ZoomModifier).Get<bool>();
		const bool bScrollZoomActive = bPlacingBlueprint && bZoomEnabled && bModifierHeld;
		if (Assets.ScrollContext != nullptr)
		{
			FBoundMappingContextHandle& Handle = ScrollContextHandles.FindOrAdd(PlayerController);
			PlayerController->SetMappingContextBoundWithHandle(Handle, Assets.ScrollContext, bScrollZoomActive, ScrollContextPriority);
		}

		if (bScrollZoomActive && Assets.ZoomScroll != nullptr)
		{
			const float WheelDelta = PlayerInput->GetActionValue(Assets.ZoomScroll).Get<float>();
			if (!FMath::IsNearlyZero(WheelDelta))
			{
				AdjustCameraDistance(Player, WheelDelta);
			}
		}
	}

	bool IsBlueprintPlacementActive(UFGBuildGunStateBuild* BuildState)
	{
		if (BuildState == nullptr)
		{
			return false;
		}

		return Cast<AFGBlueprintHologram>(BuildState->GetHologram()) != nullptr;
	}

	bool IsBlueprintPlacementActive(AFGCharacterPlayer* Player, UFGBuildGunStateBuild*& OutBuildState)
	{
		OutBuildState = nullptr;

		if (Player == nullptr || !Player->IsLocallyControlled() || !Player->IsBuildGunEquipped())
		{
			return false;
		}

		AFGBuildGun* BuildGun = Player->GetBuildGun();
		if (BuildGun == nullptr || !BuildGun->IsInState(EBuildGunState::BGS_BUILD))
		{
			return false;
		}

		OutBuildState = Cast<UFGBuildGunStateBuild>(BuildGun->GetCurrentState());
		return IsBlueprintPlacementActive(OutBuildState);
	}

	void RestoreCameraState(AFGCharacterPlayer* Player)
	{
		if (Player == nullptr)
		{
			return;
		}

		const TWeakObjectPtr<AFGCharacterPlayer> PlayerKey(Player);
		FCameraState* SavedState = ActiveCameraStates.Find(PlayerKey);
		if (SavedState == nullptr)
		{
			return;
		}

		if (USpringArmComponent* SpringArm = Player->GetSpringArmComponent())
		{
			SpringArm->TargetArmLength = SavedState->TargetArmLength;
			SpringArm->SocketOffset = SavedState->SocketOffset;
			SpringArm->bDoCollisionTest = SavedState->SpringArmCollisionTest;
		}

		if (SavedState->ForcedThirdPerson && SavedState->PreviousCameraMode != ECameraMode::ECM_None)
		{
			Player->SetCameraMode(SavedState->PreviousCameraMode);
		}

		ActiveCameraStates.Remove(PlayerKey);
	}

	void ApplyPlacementCamera(AFGCharacterPlayer* Player)
	{
		if (Player == nullptr)
		{
			return;
		}

		USpringArmComponent* SpringArm = Player->GetSpringArmComponent();
		if (SpringArm == nullptr)
		{
			return;
		}

		const TWeakObjectPtr<AFGCharacterPlayer> PlayerKey(Player);
		FCameraState& SavedState = ActiveCameraStates.FindOrAdd(PlayerKey);
		if (SavedState.PreviousCameraMode == ECameraMode::ECM_None)
		{
			SavedState.TargetArmLength = SpringArm->TargetArmLength;
			SavedState.SocketOffset = SpringArm->SocketOffset;
			SavedState.SpringArmCollisionTest = SpringArm->bDoCollisionTest;
			SavedState.PreviousCameraMode = Player->GetCameraMode();
			SavedState.ForcedThirdPerson = Player->GetCameraMode() != ECameraMode::ECM_ThirdPerson;

			if (SavedState.ForcedThirdPerson)
			{
				Player->SetCameraMode(ECameraMode::ECM_ThirdPerson);
			}
		}

		SpringArm->bDoCollisionTest = false;
		SpringArm->TargetArmLength = GetCameraDistance(Player);
		SpringArm->SocketOffset = CameraOffset;
	}

	void KeepBlueprintHologramVisible(UFGBuildGunStateBuild* BuildState)
	{
		if (BuildState == nullptr)
		{
			return;
		}

		if (AFGBlueprintHologram* BlueprintHologram = Cast<AFGBlueprintHologram>(BuildState->GetHologram()))
		{
			if (BlueprintHologram->IsHidden())
			{
				BlueprintHologram->SetActorHiddenInGame(false);
			}
		}
	}
}

void FBlueprintPlacementZoomModule::StartupModule()
{
#if !WITH_EDITOR
	// FactoryGame's editor build only has link stubs, too short for funchook to patch,
	// so hooking there crashes the editor (and Alpakit's cook). Hooks only run in game.
	BlueprintPlacementZoom::RegisterChildInputContexts();

	SUBSCRIBE_UOBJECT_METHOD(UFGBuildGunStateBuild, GetBuildGunRangeOverride_Implementation,
		[](auto& Scope, UFGBuildGunStateBuild* Self)
		{
			if (BlueprintPlacementZoom::IsBlueprintPlacementActive(Self))
			{
				Scope.Override(BlueprintPlacementZoom::BuildRange);
			}
		});

	SUBSCRIBE_UOBJECT_METHOD(AFGHologram, GetBuildGunRangeOverride_Implementation,
		[](auto& Scope, const AFGHologram* Self)
		{
			if (Cast<AFGBlueprintHologram>(Self) != nullptr)
			{
				Scope.Override(BlueprintPlacementZoom::BuildRange);
			}
		});

	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGCharacterPlayer, Tick,
		[](AFGCharacterPlayer* Self, float DeltaSeconds)
		{
			UFGBuildGunStateBuild* BuildState = nullptr;
			const bool bPlacingBlueprint = BlueprintPlacementZoom::IsBlueprintPlacementActive(Self, BuildState);
			BlueprintPlacementZoom::HandleInput(Self, bPlacingBlueprint);

			if (bPlacingBlueprint)
			{
				BlueprintPlacementZoom::KeepBlueprintHologramVisible(BuildState);
			}

			if (bPlacingBlueprint && BlueprintPlacementZoom::bZoomEnabled)
			{
				BlueprintPlacementZoom::ApplyPlacementCamera(Self);
			}
			else
			{
				BlueprintPlacementZoom::RestoreCameraState(Self);
			}
		});

	UE_LOG(LogBlueprintPlacementZoom, Display, TEXT("Blueprint Placement Zoom hooks installed"));
#endif
}

void FBlueprintPlacementZoomModule::ShutdownModule()
{
	BlueprintPlacementZoom::ActiveCameraStates.Empty();
	BlueprintPlacementZoom::ScrollContextHandles.Empty();
}

IMPLEMENT_MODULE(FBlueprintPlacementZoomModule, BlueprintPlacementZoom)
