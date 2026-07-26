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

// Physics body setup, player locomotion input, hop, grounding, and contact feedback.

void AWieldPhysicsWeaponPawn::ServerSetMovementInput_Implementation(FVector MoveDirection, float MoveStrength)
{
	AuthMoveDirection = MoveDirection.GetSafeNormal2D();
	AuthMoveStrength = FMath::Clamp(MoveStrength, 0.0f, 1.0f);
	bMovementGlowActive = AuthMoveStrength >= MovementSettings.PhysicsStruggleMinInput && !AuthMoveDirection.IsNearlyZero();
}

void AWieldPhysicsWeaponPawn::ServerHop_Implementation(FVector HopDirection)
{
	DoPhysicsHop(HopDirection.GetSafeNormal2D());
}

void AWieldPhysicsWeaponPawn::MulticastPlayMovementBurst_Implementation(FVector Location, FRotator Rotation)
{
	if (MovementEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			MovementEffect,
			Location,
			Rotation);
	}
	if (MovementBurstSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			MovementBurstSound,
			Location);
	}
}

void AWieldPhysicsWeaponPawn::MulticastPlayGroundContactSound_Implementation(FVector Location)
{
	if (MetalClangSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			MetalClangSound,
			Location,
			GroundContactSoundVolume,
			1.0f,
			0.0f,
			MovementSoundAttenuation);
	}
}

void AWieldPhysicsWeaponPawn::HandleWeaponMeshHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!HasAuthority() || !bGroundContactSoundArmed || !WeaponMeshComponent || !OtherActor || OtherActor == this)
	{
		return;
	}

	if (Hit.ImpactNormal.Z < 0.35f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - GroundContactSoundArmedTime < GroundContactSoundArmDelay)
	{
		return;
	}

	if (CurrentTime - LastGroundContactSoundTime < GroundContactSoundCooldown)
	{
		return;
	}

	const float NormalImpulseStrength = NormalImpulse.Size();
	const float SpeedIntoSurface = FMath::Max(
		-FVector::DotProduct(WeaponMeshComponent->GetPhysicsLinearVelocity(), Hit.ImpactNormal),
		0.0f);
	if (NormalImpulseStrength < GroundContactMinNormalImpulse || SpeedIntoSurface < GroundContactMinSpeed)
	{
		return;
	}

	LastGroundContactSoundTime = CurrentTime;
	bGroundContactSoundArmed = false;
	MulticastPlayGroundContactSound(Hit.ImpactPoint);
}

void AWieldPhysicsWeaponPawn::MoveForward(float Value)
{
	MoveForwardInput = Value;
}

void AWieldPhysicsWeaponPawn::MoveRight(float Value)
{
	MoveRightInput = Value;
}

void AWieldPhysicsWeaponPawn::Hop()
{
	if (AWieldGameplayPlayerController* PC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		PC->PlayJumpButtonPressed();
	}
	
	const FVector HopDirection = LastMoveDirection.IsNearlyZero() ? GetActorForwardVector() : LastMoveDirection.GetSafeNormal2D();
	if (HasAuthority())
	{
		DoPhysicsHop(HopDirection);
	}
	else
	{
		if (WeaponMeshComponent && WeaponMeshComponent->IsSimulatingPhysics())
		{
			DoPhysicsHop(HopDirection, false);
		}
		ServerHop(HopDirection);
	}
}

void AWieldPhysicsWeaponPawn::ConfigurePhysicsBody()
{
	if (!WeaponMeshComponent)
	{
		return;
	}

	WeaponMeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
	WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WeaponMeshComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	WeaponMeshComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	WeaponMeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	WeaponMeshComponent->SetIsReplicated(true);
	WeaponMeshComponent->bReplicatePhysicsToAutonomousProxy = false;
	WeaponMeshComponent->SetHiddenInGame(true);
	WeaponMeshComponent->SetVisibility(false);
	WeaponMeshComponent->SetUseCCD(true);
	WeaponMeshComponent->SetEnableGravity(true);
	WeaponMeshComponent->SetLinearDamping(MovementSettings.PhysicsLinearDamping);
	WeaponMeshComponent->SetAngularDamping(MovementSettings.PhysicsAngularDamping);
	WeaponMeshComponent->SetMassOverrideInKg(NAME_None, MovementSettings.PhysicsMassKg, true);
	WeaponMeshComponent->SetPhysicsMaxAngularVelocityInRadians(MovementSettings.PhysicsMaxAngularSpeed, false);
	WeaponMeshComponent->SetSimulatePhysics(HasAuthority() || IsLocallyControlled());

	if (WeaponVisualMeshComponent)
	{
		WeaponVisualMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponVisualMeshComponent->SetGenerateOverlapEvents(false);
		WeaponVisualMeshComponent->SetIsReplicated(false);
		WeaponVisualMeshComponent->SetHiddenInGame(false);
		WeaponVisualMeshComponent->SetVisibility(true);
	}
}

void AWieldPhysicsWeaponPawn::PlacePhysicsBodyOnGroundAtSpawn()
{
	UWorld* World = GetWorld();
	const UStaticMesh* StaticMesh = WeaponMeshComponent ? WeaponMeshComponent->GetStaticMesh() : nullptr;
	if (!World || !StaticMesh || !WeaponMeshComponent)
	{
		return;
	}

	const bool bWasSimulatingPhysics = WeaponMeshComponent->IsSimulatingPhysics();
	if (bWasSimulatingPhysics)
	{
		WeaponMeshComponent->SetSimulatePhysics(false);
	}

	const FVector ActorLocation = GetActorLocation();
	const FVector TraceStart = ActorLocation + FVector::UpVector * 500.0f;
	const FVector TraceEnd = ActorLocation - FVector::UpVector * 8000.0f;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WieldPhysicsWeaponSpawnGroundTrace), false, this);
	FHitResult Hit;
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	const bool bHitGround = World->LineTraceSingleByObjectType(
		Hit,
		TraceStart,
		TraceEnd,
		ObjectQueryParams,
		QueryParams)
		|| World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	if (!bHitGround)
	{
		if (bWasSimulatingPhysics)
		{
			WeaponMeshComponent->SetSimulatePhysics(true);
		}
		return;
	}

	const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
	const FVector Origin = Bounds.Origin;
	const FVector Extent = Bounds.BoxExtent;
	const FTransform MeshTransform(WeaponMeshComponent->GetComponentQuat(), FVector::ZeroVector, WeaponMeshComponent->GetComponentScale());

	float LowestPointZ = TNumericLimits<float>::Max();
	for (int32 XSign = -1; XSign <= 1; XSign += 2)
	{
		for (int32 YSign = -1; YSign <= 1; YSign += 2)
		{
			for (int32 ZSign = -1; ZSign <= 1; ZSign += 2)
			{
				const FVector LocalCorner = Origin + FVector(Extent.X * XSign, Extent.Y * YSign, Extent.Z * ZSign);
				LowestPointZ = FMath::Min(LowestPointZ, MeshTransform.TransformPosition(LocalCorner).Z);
			}
		}
	}

	FVector GroundedLocation = ActorLocation;
	GroundedLocation.Z = Hit.ImpactPoint.Z + MeshGroundClearance - LowestPointZ;
	SetActorLocation(GroundedLocation, false, nullptr, ETeleportType::TeleportPhysics);
	if (bWasSimulatingPhysics)
	{
		WeaponMeshComponent->SetSimulatePhysics(true);
		WeaponMeshComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		WeaponMeshComponent->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	}
}

void AWieldPhysicsWeaponPawn::UpdateAuthoritativeInput()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	const FVector LocalMoveDirection = GetCameraRelativeDirection(MoveForwardInput, MoveRightInput);
	const float LocalMoveStrength = FMath::Clamp(FVector2D(MoveForwardInput, MoveRightInput).Size(), 0.0f, 1.0f);

	AuthMoveDirection = LocalMoveDirection;
	AuthMoveStrength = LocalMoveStrength;
	bMovementGlowActive = LocalMoveStrength >= MovementSettings.PhysicsStruggleMinInput && !LocalMoveDirection.IsNearlyZero();

	if (!HasAuthority())
	{
		ServerSetMovementInput(LocalMoveDirection, LocalMoveStrength);
	}
}

void AWieldPhysicsWeaponPawn::ResetJumpCooldown()
{
	GetWorldTimerManager().ClearTimer(JumpCooldownTickTimer);
	ClientSetJumpCooldownVisual(false);
}

void AWieldPhysicsWeaponPawn::TickJumpCooldownVisual()
{
	RemainingJumpCooldownSeconds = FMath::Max(0, RemainingJumpCooldownSeconds - 1);
	if (RemainingJumpCooldownSeconds > 0)
	{
		ClientSetJumpCooldownVisual(true);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(JumpCooldownTickTimer);
		ClientSetJumpCooldownVisual(false);
	}
}

void AWieldPhysicsWeaponPawn::ClientSetJumpCooldownVisual_Implementation(bool bOnCooldown)
{
	if (AWieldGameplayPlayerController* PC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		PC->SetJumpCooldownVisual(bOnCooldown);
	}
}

void AWieldPhysicsWeaponPawn::UpdateStrugglePhysicsMovement(float DeltaSeconds)
{
	if (!WeaponMeshComponent || !WeaponMeshComponent->IsSimulatingPhysics())
	{
		return;
	}

	const FVector UpVector = FVector::UpVector;
	const FVector LinearVelocity = WeaponMeshComponent->GetPhysicsLinearVelocity();
	const FVector PlanarVelocity(LinearVelocity.X, LinearVelocity.Y, 0.0f);
	const bool bHasInput = AuthMoveStrength >= MovementSettings.PhysicsStruggleMinInput && !AuthMoveDirection.IsNearlyZero();
	const bool bGrounded = IsGrounded();
	const float MaxPlanarSpeed = MovementSettings.MaxWalkSpeed * 0.9f;

	auto ClampPlanarVelocity = [&]()
	{
		const FVector CurrentVelocity = WeaponMeshComponent->GetPhysicsLinearVelocity();
		const FVector CurrentPlanarVelocity(CurrentVelocity.X, CurrentVelocity.Y, 0.0f);
		if (CurrentPlanarVelocity.SizeSquared() <= FMath::Square(MaxPlanarSpeed))
		{
			return;
		}

		const FVector ClampedPlanarVelocity = CurrentPlanarVelocity.GetSafeNormal() * MaxPlanarSpeed;
		WeaponMeshComponent->SetPhysicsLinearVelocity(
			FVector(ClampedPlanarVelocity.X, ClampedPlanarVelocity.Y, CurrentVelocity.Z),
			false);
	};

	if (bGrounded)
	{
		WeaponMeshComponent->AddForce(-UpVector * MovementSettings.PhysicsGroundDownforce, NAME_None, true);
	}

	if (!bHasInput)
	{
		StruggleChargeElapsed = 0.0f;
		StruggleRecoveryElapsed += DeltaSeconds;
		WeaponMeshComponent->AddForce(-PlanarVelocity * MovementSettings.PhysicsBrakingGrip, NAME_None, true);
		WeaponMeshComponent->AddTorqueInRadians(
			-WeaponMeshComponent->GetPhysicsAngularVelocityInRadians() * MovementSettings.PhysicsAngularBrake,
			NAME_None,
			true);
		ClampPlanarVelocity();
		return;
	}

	const FVector DesiredDirection = AuthMoveDirection.GetSafeNormal2D();
	LastMoveDirection = DesiredDirection;
	StruggleRecoveryElapsed += DeltaSeconds;

	const float SpeedAlongInput = FVector::DotProduct(PlanarVelocity, DesiredDirection);
	const FVector LateralVelocity = PlanarVelocity - DesiredDirection * SpeedAlongInput;
	WeaponMeshComponent->AddForce(-LateralVelocity * MovementSettings.PhysicsLateralGrip, NAME_None, true);

	if (!bGrounded)
	{
		ClampPlanarVelocity();
		return;
	}

	const float ChargeDuration = FMath::Max(MovementSettings.PhysicsStruggleChargeTime, KINDA_SMALL_NUMBER);
	const bool bCanCharge = StruggleRecoveryElapsed >= MovementSettings.PhysicsStruggleRecoveryTime;
	if (bCanCharge)
	{
		StruggleChargeElapsed += DeltaSeconds * AuthMoveStrength;
	}

	const float ChargeAlpha = FMath::Clamp(StruggleChargeElapsed / ChargeDuration, 0.0f, 1.0f);
	const FVector RollAxis = FVector::CrossProduct(UpVector, DesiredDirection).GetSafeNormal();

	WeaponMeshComponent->AddTorqueInRadians(
		RollAxis * MovementSettings.PhysicsStruggleLeanTorque * ChargeAlpha,
		NAME_None,
		true);
	WeaponMeshComponent->AddForce(
		DesiredDirection * MovementSettings.PhysicsStruggleCrawlForce * ChargeAlpha * AuthMoveStrength,
		NAME_None,
		true);

	if (StruggleChargeElapsed < ChargeDuration)
	{
		ClampPlanarVelocity();
		return;
	}

	const FVector FlopImpulse =
		DesiredDirection * MovementSettings.PhysicsStruggleFlopImpulse * AuthMoveStrength
		+ UpVector * MovementSettings.PhysicsStruggleFlopUpImpulse;
	bGroundContactSoundArmed = true;
	if (UWorld* World = GetWorld())
	{
		GroundContactSoundArmedTime = World->GetTimeSeconds();
	}
	WeaponMeshComponent->AddImpulse(FlopImpulse, NAME_None, true);
	WeaponMeshComponent->AddTorqueInRadians(
		RollAxis * MovementSettings.PhysicsStruggleFlopTorque * AuthMoveStrength,
		NAME_None,
		true);

	if (HasAuthority())
	{
		MulticastPlayMovementBurst(WeaponMeshComponent->GetComponentLocation(), DesiredDirection.Rotation());
	}
	
	StruggleChargeElapsed = 0.0f;
	StruggleRecoveryElapsed = 0.0f;
	ClampPlanarVelocity();
}

void AWieldPhysicsWeaponPawn::ApplyMovementSettings()
{
	if (!WeaponMeshComponent)
	{
		return;
	}
 
	WeaponMeshComponent->SetLinearDamping(MovementSettings.PhysicsLinearDamping);
	WeaponMeshComponent->SetAngularDamping(MovementSettings.PhysicsAngularDamping);
	WeaponMeshComponent->SetPhysicsMaxAngularVelocityInRadians(MovementSettings.PhysicsMaxAngularSpeed, false);
	WeaponMeshComponent->SetUseCCD(true);
}

void AWieldPhysicsWeaponPawn::DoPhysicsHop(const FVector& HopDirection, bool bPlayCosmetics)
{
	UWorld* World = GetWorld();
	if (!World || !WeaponMeshComponent || !WeaponMeshComponent->IsSimulatingPhysics())
	{
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastHopTime < MovementSettings.HopCooldown)
	{
		return;
	}
	
	RemainingJumpCooldownSeconds = FMath::CeilToInt(MovementSettings.HopCooldown);

	LastHopTime = CurrentTime;
	ClientSetJumpCooldownVisual(true);
	GetWorldTimerManager().SetTimer(JumpCooldownTickTimer, this, &ThisClass::TickJumpCooldownVisual, 1.0f, true);
	GetWorldTimerManager().SetTimer(LastJumpTimer, this, &ThisClass::ResetJumpCooldown, MovementSettings.HopCooldown, false);

	const FVector LaunchDirection = HopDirection.IsNearlyZero() ? LastMoveDirection.GetSafeNormal2D() : HopDirection.GetSafeNormal2D();
	const FVector Impulse = LaunchDirection * MovementSettings.HopForwardStrength + FVector::UpVector * MovementSettings.HopUpStrength;
	bGroundContactSoundArmed = true;
	GroundContactSoundArmedTime = CurrentTime;
	WeaponMeshComponent->AddImpulse(Impulse, NAME_None, true);
	if (bPlayCosmetics)
	{
		MulticastPlayMovementBurst(WeaponMeshComponent->GetComponentLocation(), LaunchDirection.Rotation());
	}
}

bool AWieldPhysicsWeaponPawn::IsGrounded() const
{
	if (!WeaponMeshComponent)
	{
		return false;
	}

	const FVector Extent = WeaponMeshComponent->Bounds.BoxExtent.GetAbs();
	FHitResult Hit;
	return FindFloorBelow(8.0f, Extent.Z + 24.0f, Hit);
}

bool AWieldPhysicsWeaponPawn::FindFloorBelow(float TraceUpDistance, float TraceDownDistance, FHitResult& OutHit) const
{
	UWorld* World = GetWorld();
	if (!World || !WeaponMeshComponent)
	{
		return false;
	}

	const FVector Origin = WeaponMeshComponent->GetComponentLocation();
	const FVector Start = Origin + FVector::UpVector * FMath::Max(TraceUpDistance, 0.0f);
	const FVector End = Origin - FVector::UpVector * FMath::Max(TraceDownDistance, 0.0f);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WieldPhysicsWeaponFloorTrace), false, this);
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	return World->LineTraceSingleByObjectType(OutHit, Start, End, ObjectQueryParams, QueryParams)
		|| World->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);
}

float AWieldPhysicsWeaponPawn::GetWeaponMeshLowestLocalPointZ() const
{
	const UStaticMesh* StaticMesh = WeaponMeshComponent ? WeaponMeshComponent->GetStaticMesh() : nullptr;
	if (!StaticMesh || !WeaponMeshComponent)
	{
		return 0.0f;
	}

	const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
	const FVector Origin = Bounds.Origin;
	const FVector Extent = Bounds.BoxExtent;
	const FTransform MeshTransform(WeaponMeshComponent->GetComponentQuat(), FVector::ZeroVector, WeaponMeshComponent->GetComponentScale());

	float LowestPointZ = TNumericLimits<float>::Max();
	for (int32 XSign = -1; XSign <= 1; XSign += 2)
	{
		for (int32 YSign = -1; YSign <= 1; YSign += 2)
		{
			for (int32 ZSign = -1; ZSign <= 1; ZSign += 2)
			{
				const FVector LocalCorner = Origin + FVector(Extent.X * XSign, Extent.Y * YSign, Extent.Z * ZSign);
				LowestPointZ = FMath::Min(LowestPointZ, MeshTransform.TransformPosition(LocalCorner).Z);
			}
		}
	}

	return LowestPointZ == TNumericLimits<float>::Max() ? 0.0f : LowestPointZ;
}

FVector AWieldPhysicsWeaponPawn::GetActorLocationForGroundHit(const FHitResult& GroundHit) const
{
	FVector GroundedLocation = GetActorLocation();
	GroundedLocation.Z = GroundHit.ImpactPoint.Z + MeshGroundClearance - GetWeaponMeshLowestLocalPointZ();
	return GroundedLocation;
}

void AWieldPhysicsWeaponPawn::SnapPhysicsBodyToGroundHit(const FHitResult& GroundHit)
{
	if (!WeaponMeshComponent)
	{
		return;
	}

	FHitResult SweepHit;
	SetActorLocation(GetActorLocationForGroundHit(GroundHit), true, &SweepHit, ETeleportType::TeleportPhysics);
	FVector LinearVelocity = WeaponMeshComponent->GetPhysicsLinearVelocity();
	LinearVelocity.Z = FMath::Max(LinearVelocity.Z, 0.0f);
	WeaponMeshComponent->SetPhysicsLinearVelocity(LinearVelocity, false);
}

void AWieldPhysicsWeaponPawn::UpdateFloorSafety(float /*DeltaSeconds*/)
{
	if (!WeaponMeshComponent || !WeaponMeshComponent->IsSimulatingPhysics())
	{
		return;
	}

	UWorld* World = GetWorld();
	const float CurrentTime = World ? World->GetTimeSeconds() : -1000.0f;
	const auto TryRestoreRecentSafeGround = [&]() -> bool
	{
		const bool bRecentSafeGround = bHasLastSafeGroundLocation
			&& CurrentTime - LastSafeGroundTime <= FMath::Max(FloorRescueRecentGroundMemory, 0.0f);
		if (!bRecentSafeGround
			|| GetActorLocation().Z >= LastSafeGroundActorLocation.Z - FMath::Max(FloorRescueMissingGroundDrop, 0.0f))
		{
			return false;
		}

		FVector RestoreLocation = GetActorLocation();
		RestoreLocation.Z = LastSafeGroundActorLocation.Z;
		FHitResult SweepHit;
		SetActorLocation(RestoreLocation, true, &SweepHit, ETeleportType::TeleportPhysics);
		FVector RestoredVelocity = LastSafeGroundLinearVelocity;
		RestoredVelocity.Z = FMath::Max(RestoredVelocity.Z, 0.0f);
		WeaponMeshComponent->SetPhysicsLinearVelocity(RestoredVelocity, false);
		return true;
	};

	if (TryRestoreRecentSafeGround())
	{
		return;
	}

	FHitResult GroundHit;
	if (!FindFloorBelow(FloorSafetyTraceUpDistance, FloorSafetyTraceDownDistance, GroundHit)
		|| GroundHit.ImpactNormal.Z < 0.25f)
	{
		return;
	}

	const float BottomZ = GetActorLocation().Z + GetWeaponMeshLowestLocalPointZ();
	const float DistanceAboveFloor = BottomZ - GroundHit.ImpactPoint.Z;
	const float RescueDepth = FMath::Max(FloorRescuePenetrationDepth, 0.0f);
	if (DistanceAboveFloor < -RescueDepth)
	{
		SnapPhysicsBodyToGroundHit(GroundHit);
		return;
	}

	if (DistanceAboveFloor >= -RescueDepth
		&& DistanceAboveFloor <= FMath::Max(FloorSafetyGroundedDistance, RescueDepth))
	{
		LastSafeGroundActorLocation = GetActorLocation();
		LastSafeGroundLinearVelocity = WeaponMeshComponent->GetPhysicsLinearVelocity();
		LastSafeGroundTime = CurrentTime;
		bHasLastSafeGroundLocation = true;
	}
}

FVector AWieldPhysicsWeaponPawn::GetCameraRelativeDirection(float ForwardValue, float RightValue) const
{
	FVector Forward = FollowCamera ? FollowCamera->GetForwardVector() : GetActorForwardVector();
	FVector Right = FollowCamera ? FollowCamera->GetRightVector() : GetActorRightVector();

	Forward.Z = 0.0f;
	Right.Z = 0.0f;
	Forward.Normalize();
	Right.Normalize();

	const float ForwardSign = MovementSettings.bInvertScreenForward ? -1.0f : 1.0f;
	const float RightSign = MovementSettings.bInvertScreenRight ? -1.0f : 1.0f;
	return (Forward * ForwardValue * ForwardSign + Right * RightValue * RightSign).GetSafeNormal();
}
