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

// Visible mesh smoothing, material glow, and mesh scale/transform helpers.

void AWieldPhysicsWeaponPawn::UpdateMovementMaterial(float DeltaSeconds)
{
	UStaticMeshComponent* MaterialMeshComponent = WeaponVisualMeshComponent ? WeaponVisualMeshComponent.Get() : WeaponMeshComponent.Get();
	if (!MaterialMeshComponent)
	{
		return;
	}

	if (!WeaponDynamicMaterial)
	{
		WeaponDynamicMaterial = MaterialMeshComponent->CreateDynamicMaterialInstance(0);
		CurrentMovementGlowValue = IdleMovementGlowValue;
		if (WeaponDynamicMaterial)
		{
			WeaponDynamicMaterial->SetScalarParameterValue(MovementGlowParameterName, CurrentMovementGlowValue);
		}
	}

	if (!WeaponDynamicMaterial)
	{
		return;
	}
	
	float TargetGlowValue = bMovementGlowActive ? MovingMovementGlowValue : IdleMovementGlowValue;
	TargetGlowValue = bAbilityGlowActive ? AbilityGlowValue : TargetGlowValue;
	
	CurrentMovementGlowValue = FMath::FInterpTo(
		CurrentMovementGlowValue,
		TargetGlowValue,
		DeltaSeconds,
		MovementGlowInterpSpeed);
	WeaponDynamicMaterial->SetScalarParameterValue(MovementGlowParameterName, CurrentMovementGlowValue);
}

void AWieldPhysicsWeaponPawn::UpdateWeaponVisualTransform(float DeltaSeconds)
{
	if (!WeaponVisualMeshComponent || !WeaponMeshComponent)
	{
		return;
	}

	const FTransform TargetTransform = WeaponMeshComponent->GetComponentTransform();
	const FVector TargetLocation = TargetTransform.GetLocation();
	const FVector CurrentLocation = WeaponVisualMeshComponent->GetComponentLocation();
	const bool bLocalView = IsLocallyControlled();
	const float LocationInterpSpeed = bLocalView ? LocalVisualLocationInterpSpeed : SimulatedProxyVisualLocationInterpSpeed;
	const float RotationInterpSpeed = bLocalView ? LocalVisualRotationInterpSpeed : SimulatedProxyVisualRotationInterpSpeed;
	const float SnapDistance = FMath::Max(bLocalView ? LocalVisualSnapDistance : SimulatedProxyVisualSnapDistance, 0.0f);
	const bool bShouldSnap =
		GetNetMode() == NM_DedicatedServer
		|| !bHasInitializedWeaponVisualTransform
		|| DeltaSeconds <= KINDA_SMALL_NUMBER
		|| LocationInterpSpeed <= KINDA_SMALL_NUMBER
		|| RotationInterpSpeed <= KINDA_SMALL_NUMBER
		|| (SnapDistance > 0.0f && FVector::DistSquared(CurrentLocation, TargetLocation) > FMath::Square(SnapDistance));

	if (bShouldSnap)
	{
		WeaponVisualMeshComponent->SetWorldTransform(TargetTransform, false, nullptr, ETeleportType::TeleportPhysics);
		bHasInitializedWeaponVisualTransform = true;
		return;
	}

	const FVector SmoothedLocation = FMath::VInterpTo(
		CurrentLocation,
		TargetLocation,
		DeltaSeconds,
		LocationInterpSpeed);
	const FQuat SmoothedRotation = FQuat::Slerp(
		WeaponVisualMeshComponent->GetComponentQuat(),
		TargetTransform.GetRotation(),
		FMath::Clamp(DeltaSeconds * RotationInterpSpeed, 0.0f, 1.0f)).GetNormalized();
	const FVector SmoothedScale = FMath::VInterpTo(
		WeaponVisualMeshComponent->GetComponentScale(),
		TargetTransform.GetScale3D(),
		DeltaSeconds,
		LocationInterpSpeed);

	WeaponVisualMeshComponent->SetWorldLocationAndRotation(
		SmoothedLocation,
		SmoothedRotation,
		false,
		nullptr,
		ETeleportType::None);
	WeaponVisualMeshComponent->SetWorldScale3D(SmoothedScale);
}

FRotator AWieldPhysicsWeaponPawn::GetGroundAlignedMeshRotation(const FRotator& BaseRotation) const
{
	const UStaticMesh* StaticMesh = WeaponMeshComponent ? WeaponMeshComponent->GetStaticMesh() : nullptr;
	if (!StaticMesh)
	{
		return BaseRotation;
	}

	const FVector Extent = StaticMesh->GetBounds().BoxExtent.GetAbs();
	FVector ThinLocalAxis = FVector::UpVector;
	if (Extent.X <= Extent.Y && Extent.X <= Extent.Z)
	{
		ThinLocalAxis = FVector::ForwardVector;
	}
	else if (Extent.Y <= Extent.X && Extent.Y <= Extent.Z)
	{
		ThinLocalAxis = FVector::RightVector;
	}

	const FQuat BaseQuat = BaseRotation.Quaternion();
	const FVector ThinAxisAfterBaseRotation = BaseQuat.RotateVector(ThinLocalAxis).GetSafeNormal();
	const FQuat LayFlatQuat = FQuat::FindBetweenNormals(ThinAxisAfterBaseRotation, FVector::UpVector);
	return (LayFlatQuat * BaseQuat).Rotator();
}

UStaticMesh* AWieldPhysicsWeaponPawn::GetEquippedWeaponStaticMesh() const
{
	return WeaponMeshComponent ? WeaponMeshComponent->GetStaticMesh() : nullptr;
}

FVector AWieldPhysicsWeaponPawn::GetHostHeldWeaponMeshScale() const
{
	const UStaticMesh* StaticMesh = GetEquippedWeaponStaticMesh();
	const float LongestAxis = GetStaticMeshLongestAxis(StaticMesh);
	if (bNormalizeWeaponMeshToTargetLength && LongestAxis > KINDA_SMALL_NUMBER)
	{
		const float NormalizedScale = HostHeldWeaponTargetLength / LongestAxis;
		return FVector(NormalizedScale * HostHeldWeaponScaleMultiplier);
	}

	return WeaponMeshScale * HostHeldWeaponScaleMultiplier;
}

bool AWieldPhysicsWeaponPawn::GetWeaponMeshWorldTransform(FTransform& OutTransform) const
{
	const UStaticMeshComponent* VisibleMeshComponent = WeaponVisualMeshComponent ? WeaponVisualMeshComponent.Get() : WeaponMeshComponent.Get();
	if (!VisibleMeshComponent || !VisibleMeshComponent->GetStaticMesh())
	{
		return false;
	}

	OutTransform = VisibleMeshComponent->GetComponentTransform();
	return true;
}

float AWieldPhysicsWeaponPawn::GetStaticMeshLongestAxis(const UStaticMesh* StaticMesh) const
{
	if (!StaticMesh)
	{
		return 0.0f;
	}

	const FVector Extent = StaticMesh->GetBounds().BoxExtent.GetAbs();
	return FMath::Max3(Extent.X, Extent.Y, Extent.Z) * 2.0f;
}
