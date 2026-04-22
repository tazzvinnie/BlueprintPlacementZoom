#include "BlueprintPlacementZoomModule.h"

#include "GameFramework/SpringArmComponent.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintPlacementZoom, Log, All);

namespace BlueprintPlacementZoom
{
	constexpr float BuildRange = 35000.0f;
	constexpr float CameraDistance = 1600.0f;
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
		SpringArm->TargetArmLength = CameraDistance;
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
			if (BlueprintPlacementZoom::IsBlueprintPlacementActive(Self, BuildState))
			{
				BlueprintPlacementZoom::ApplyPlacementCamera(Self);
				BlueprintPlacementZoom::KeepBlueprintHologramVisible(BuildState);
			}
			else
			{
				BlueprintPlacementZoom::RestoreCameraState(Self);
			}
		});

	UE_LOG(LogBlueprintPlacementZoom, Display, TEXT("Blueprint Placement Zoom hooks installed"));
}

void FBlueprintPlacementZoomModule::ShutdownModule()
{
	BlueprintPlacementZoom::ActiveCameraStates.Empty();
}

IMPLEMENT_MODULE(FBlueprintPlacementZoomModule, BlueprintPlacementZoom)
