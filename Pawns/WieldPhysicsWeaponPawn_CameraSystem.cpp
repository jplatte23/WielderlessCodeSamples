#include "WieldPhysicsWeaponPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationInvokerComponent.h"
#include "NiagaraComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Wielderless/Actors/WieldPossessableHost.h"
#include "Wielderless/Player/WieldGameplayPlayerController.h"
#include "Wielderless/Weapons/WieldWeaponDefinition.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Wielderless/Actors/Projectiles/WieldProjectile.h"
#include "Wielderless/Components/Shared/WieldCameraNavigationInvokerComponent.h"
#include "Wielderless/Components/Shared/WieldReviveInteractorComponent.h"

// Camera rotation, zoom, and possession/sacrifice camera handoff.

void AWieldPhysicsWeaponPawn::TurnCameraMouse(float Value)
{
	const bool bCanRotateCamera = CameraSettings.bAllowMouseCameraRotation || (CameraSettings.bEnableRightMouseCameraRotation && bIsRightMouseCameraRotating);
	if (!bCanRotateCamera || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const float YawSign = CameraSettings.bInvertMouseYaw ? -1.0f : 1.0f;
	CameraYaw += Value * CameraSettings.MouseYawRate * YawSign;
	UpdateCameraRig();
}

void AWieldPhysicsWeaponPawn::LookCameraMouse(float Value)
{
	const bool bCanRotateCamera = CameraSettings.bAllowMouseCameraRotation || (CameraSettings.bEnableRightMouseCameraRotation && bIsRightMouseCameraRotating);
	if (!bCanRotateCamera || FMath::IsNearlyZero(Value))
	{
		return;
	}

	const float PitchSign = CameraSettings.bInvertMousePitch ? -1.0f : 1.0f;
	CameraPitch = FMath::Clamp(CameraPitch + Value * CameraSettings.MousePitchRate * PitchSign, CameraSettings.MinPitch, CameraSettings.MaxPitch);
	UpdateCameraRig();
}

void AWieldPhysicsWeaponPawn::TurnCameraGamepad(float Value)
{
	if (!CameraSettings.bAllowGamepadCameraRotation || FMath::IsNearlyZero(Value))
	{
		return;
	}

	UWorld* World = GetWorld();
	CameraYaw += Value * CameraSettings.GamepadYawRate * (World ? World->GetDeltaSeconds() : 0.0f);
	UpdateCameraRig();
}

void AWieldPhysicsWeaponPawn::LookCameraGamepad(float Value)
{
	if (!CameraSettings.bAllowGamepadCameraRotation || FMath::IsNearlyZero(Value))
	{
		return;
	}

	UWorld* World = GetWorld();
	CameraPitch = FMath::Clamp(
		CameraPitch + Value * CameraSettings.GamepadPitchRate * (World ? World->GetDeltaSeconds() : 0.0f),
		CameraSettings.MinPitch,
		CameraSettings.MaxPitch);
	UpdateCameraRig();
}

void AWieldPhysicsWeaponPawn::ZoomCamera(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	const float ZoomDirection = CameraSettings.bInvertZoom ? -Value : Value;
	DesiredCameraArmLength = FMath::Clamp(
		DesiredCameraArmLength - ZoomDirection * CameraSettings.ZoomStep,
		CameraSettings.MinTargetArmLength,
		CameraSettings.MaxTargetArmLength);
}

void AWieldPhysicsWeaponPawn::BeginRightMouseCameraRotation()
{
	bIsRightMouseCameraRotating = true;

	if (AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		GameplayPC->SetReticleVisibility(false);
	}
}

void AWieldPhysicsWeaponPawn::EndRightMouseCameraRotation()
{
	bIsRightMouseCameraRotating = false;

	if (AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		if (GameplayPC->GetPauseVisbility() != true)
		{
			GameplayPC->SetReticleVisibility(true);
		}
	}
}

void AWieldPhysicsWeaponPawn::UpdateCameraZoom(float DeltaSeconds)
{
	if (!CameraBoom)
	{
		return;
	}

	CameraBoom->TargetArmLength = FMath::FInterpTo(
		CameraBoom->TargetArmLength,
		DesiredCameraArmLength,
		DeltaSeconds,
		CameraSettings.ZoomInterpSpeed);
}

void AWieldPhysicsWeaponPawn::UpdateCameraHandoff(float DeltaSeconds)
{
	if (!bCameraHandoffActive)
	{
		return;
	}

	CameraHandoffElapsed += DeltaSeconds;
	if (CameraHandoffElapsed >= CameraHandoffDuration)
	{
		bCameraHandoffActive = false;
		if (CameraBoom)
		{
			CameraBoom->bEnableCameraLag = bCameraHandoffRestoreCameraLag;
		}
	}
}

void AWieldPhysicsWeaponPawn::UpdateCameraRig()
{
	if (!CameraBoom || !WeaponMeshComponent)
	{
		return;
	}

	const USceneComponent* CameraTargetComponent =
		IsLocallyControlled() && WeaponVisualMeshComponent
			? WeaponVisualMeshComponent.Get()
			: WeaponMeshComponent.Get();
	const FVector TargetBoomLocation = CameraTargetComponent->GetComponentLocation() + CameraSettings.PivotOffset;
	FVector BoomLocation = TargetBoomLocation;
	if (bCameraHandoffActive && CameraHandoffDuration > KINDA_SMALL_NUMBER)
	{
		const float HandoffAlpha = FMath::Clamp(CameraHandoffElapsed / CameraHandoffDuration, 0.0f, 1.0f);
		const float EasedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, HandoffAlpha, 2.0f);
		BoomLocation = FMath::Lerp(CameraHandoffStartBoomLocation, TargetBoomLocation, EasedAlpha);
	}

	CameraBoom->SetWorldLocation(BoomLocation);
	CameraBoom->SetWorldRotation(FRotator(CameraPitch, CameraYaw, 0.0f));
}

void AWieldPhysicsWeaponPawn::ApplyCameraSettings()
{
	CameraSettings.UpgradeLegacyCloseCameraDefaults();

	CameraYaw = CameraSettings.StartingYaw;
	CameraPitch = FMath::Clamp(CameraSettings.StartingPitch, CameraSettings.MinPitch, CameraSettings.MaxPitch);
	DesiredCameraArmLength = FMath::Clamp(CameraSettings.TargetArmLength, CameraSettings.MinTargetArmLength, CameraSettings.MaxTargetArmLength);

	CameraBoom->TargetArmLength = DesiredCameraArmLength;
	CameraBoom->SocketOffset = CameraSettings.SocketOffset;
	CameraBoom->bEnableCameraLag = CameraSettings.bEnableCameraLag;
	CameraBoom->CameraLagSpeed = CameraSettings.CameraLagSpeed;
	CameraBoom->bDoCollisionTest = CameraSettings.bDoCollisionTest;
	FollowCamera->SetFieldOfView(CameraSettings.FieldOfView);

	UpdateCameraRig();
}

void AWieldPhysicsWeaponPawn::SeedCameraView(float InYaw, float InPitch, float InTargetArmLength)
{
	CameraYaw = InYaw;
	CameraPitch = FMath::Clamp(InPitch, CameraSettings.MinPitch, CameraSettings.MaxPitch);
	DesiredCameraArmLength = FMath::Clamp(
		InTargetArmLength,
		CameraSettings.MinTargetArmLength,
		CameraSettings.MaxTargetArmLength);

	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = DesiredCameraArmLength;
	}
	UpdateCameraRig();
}

void AWieldPhysicsWeaponPawn::SeedCameraHandoff(float InYaw, float InPitch, float InTargetArmLength, FVector InStartBoomWorldLocation, float InHandoffDuration)
{
	SeedCameraView(InYaw, InPitch, InTargetArmLength);

	if (!CameraBoom || InHandoffDuration <= KINDA_SMALL_NUMBER)
	{
		bCameraHandoffActive = false;
		return;
	}

	CameraHandoffStartBoomLocation = InStartBoomWorldLocation;
	CameraHandoffElapsed = 0.0f;
	CameraHandoffDuration = InHandoffDuration;
	bCameraHandoffActive = true;
	bCameraHandoffRestoreCameraLag = CameraBoom->bEnableCameraLag;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->SetWorldLocation(CameraHandoffStartBoomLocation);
	CameraBoom->SetWorldRotation(FRotator(CameraPitch, CameraYaw, 0.0f));
}

void AWieldPhysicsWeaponPawn::SeedCameraHandoffFromCameraView(float InYaw, float InPitch, float InTargetArmLength, FVector InStartCameraWorldLocation, float InHandoffDuration)
{
	SeedCameraView(InYaw, InPitch, InTargetArmLength);

	if (!CameraBoom || InHandoffDuration <= KINDA_SMALL_NUMBER)
	{
		bCameraHandoffActive = false;
		return;
	}

	const FRotator CameraRotation(CameraPitch, CameraYaw, 0.0f);
	const FVector CameraForward = CameraRotation.Vector();
	const FVector SocketWorldOffset = CameraRotation.RotateVector(CameraBoom->SocketOffset);
	CameraHandoffStartBoomLocation =
		InStartCameraWorldLocation + CameraForward * CameraBoom->TargetArmLength - SocketWorldOffset;
	CameraHandoffElapsed = 0.0f;
	CameraHandoffDuration = InHandoffDuration;
	bCameraHandoffActive = true;
	bCameraHandoffRestoreCameraLag = CameraBoom->bEnableCameraLag;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->SetWorldLocation(CameraHandoffStartBoomLocation);
	CameraBoom->SetWorldRotation(CameraRotation);
}

bool AWieldPhysicsWeaponPawn::GetCameraHandoffView(float& OutYaw, float& OutPitch, float& OutTargetArmLength, FVector& OutCameraWorldLocation) const
{
	if (!FollowCamera || !CameraBoom)
	{
		return false;
	}

	const FRotator CameraRotation = FollowCamera->GetComponentRotation();
	OutYaw = CameraRotation.Yaw;
	OutPitch = CameraRotation.Pitch;
	OutTargetArmLength = CameraBoom->TargetArmLength;
	OutCameraWorldLocation = FollowCamera->GetComponentLocation();
	return true;
}

void AWieldPhysicsWeaponPawn::PlayCameraShake()
{
	if (!HitCameraShake)
	{
		return;
	}
	
	if (AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		GameplayPC->ClientStartCameraShake(HitCameraShake);
	}
}
