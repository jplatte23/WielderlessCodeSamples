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

// Scan, lure, possession prompts, cooldown UI, projectile/tether flow, and host lookup.

void AWieldPhysicsWeaponPawn::ServerSetAimDirection_Implementation(FVector AimDirection)
{
	ServerKnownAimDirection = AimDirection.GetSafeNormal2D();
}

void AWieldPhysicsWeaponPawn::ServerSetLureHoldState_Implementation(bool bHeld)
{
	bLureKeyHeld = bHeld;
	if (bHeld)
	{
		if (const UWorld* World = GetWorld())
		{
			LureHoldStartTime = World->GetTimeSeconds();
		}
	}
}

void AWieldPhysicsWeaponPawn::ServerTryPossessNearestHost_Implementation()
{
	AWieldPossessableHost* TargetHost = FindNearestHost(PossessionSearchRadius, true);
	if (!TargetHost)
	{
		return;
	}

	TargetHost->TryPossessFromWeapon(GetController(), this);
}

void AWieldPhysicsWeaponPawn::ServerScanNearbyHosts_Implementation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	const float WorldTime = World->GetTimeSeconds();
	if (WorldTime - LastScanTime < ScanCooldown)
	{
		return;
	}
	LastScanTime = WorldTime;
	
	RemainingScanCooldownSeconds = FMath::CeilToInt(ScanCooldown);

	ClientSetScanCooldownVisual(true, RemainingScanCooldownSeconds);
	GetWorldTimerManager().SetTimer(ScanCooldownTickTimer, this, &ThisClass::TickScanCooldownVisual, 1.0f, true);
	GetWorldTimerManager().SetTimer(LastScanTimer, this, &ThisClass::ResetScanCooldown, ScanCooldown, false);

	const float ScanRadiusSquared = FMath::Square(ScanRadius);
	for (TActorIterator<AWieldPossessableHost> It(World); It; ++It)
	{
		AWieldPossessableHost* Host = *It;
		if (!Host || Host->IsDead())
		{
			continue;
		}

		if (FVector::DistSquared2D(GetActorLocation(), Host->GetActorLocation()) <= ScanRadiusSquared)
		{
			Host->MarkScanned();
		}
	}
	MulticastPlayScanEffect(GetActorLocation());
}

void AWieldPhysicsWeaponPawn::ServerLureNearbyHosts_Implementation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	if (ActiveCharmedHost)
	{
		return;
	}
	
	float WorldTime = World->GetTimeSeconds();
	// Pressing lure while a tether exists always cancels it.
	if (ActiveLureTetherTarget)
	{
		if (!ActiveLureTetherTarget->GetCharmed())
		{
			ActiveLureTetherTarget->ClearLureFromHost();
		}
		MulticastPlayTetherCutSound(WeaponMeshComponent->GetComponentLocation());
		MulticastStopLureTether();
		return;
	}

	// No active tether: do not allow another projectile during cooldown.
	if (WorldTime < LureAvailableTime || !LureAttackProjectileClass)
	{
		return;
	}

	LureAvailableTime = WorldTime + LureCooldown;
	RemainingLureCooldownSeconds = FMath::CeilToInt(LureCooldown);
	StartLureProjectileAttack();
	
	ClientSetLureCooldownVisual(true);
	GetWorldTimerManager().SetTimer(LureCooldownTickTimer, this, &ThisClass::TickLureCooldownVisual, 1.0f, true);
	GetWorldTimerManager().SetTimer(LastLureTimer, this, &ThisClass::ResetLureCooldown, LureCooldown, false);
}

void AWieldPhysicsWeaponPawn::MulticastPlayTetherCutSound_Implementation(FVector Location)
{
	if (TetherCutSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			TetherCutSound,
			Location);
	}
}

void AWieldPhysicsWeaponPawn::MulticastStopLureTether_Implementation()
{
	StopLureTetherVfx();
}

void AWieldPhysicsWeaponPawn::MulticastPlayScanEffect_Implementation(FVector Location)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	if (ScanSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			ScanSound,
			Location);
	}

	if (ScanEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			ScanEffect,
			Location,
			FRotator::ZeroRotator);
	}

	if (!ScanEffectBP)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	AActor* Scanner = World->SpawnActor<AActor>(
		ScanEffectBP,
		Location,
		FRotator::ZeroRotator,
		SpawnParams
	);

	if (Scanner)
	{
		if (UFunction* DoScanFunction = Scanner->FindFunction(TEXT("DoScan")))
		{
			Scanner->ProcessEvent(DoScanFunction, nullptr);
		}

		Scanner->SetLifeSpan(3.0f);
	}
}

void AWieldPhysicsWeaponPawn::MulticastStartLureTether_Implementation(AWieldPossessableHost* TargetHost)
{
	if (!LureTetherEffect || !TargetHost)
	{
		return;
	}
	
	StopLureTetherVfx();
	
	if (LureSound)
	{
		USceneComponent* LureAttachComponent = WeaponVisualMeshComponent ? WeaponVisualMeshComponent.Get() : WeaponMeshComponent.Get();
		ActiveLureSoundComponent = UGameplayStatics::SpawnSoundAttached(
		LureSound,
		LureAttachComponent,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true,
		1.0f,
		1.0f,
		0.0f,
		nullptr,
		nullptr,
		false
		);
		if (ActiveLureSoundComponent)
		{
			ActiveLureSoundComponent->Play();
		}
	}
	
	ActiveLureTetherTarget = TargetHost;
	ActiveLureTetherTarget->SetPossessPromptProgress(0.0f);

	ActiveLureTetherComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this,
		LureTetherEffect,
		GetLureTetherStartLocation(),
		FRotator::ZeroRotator);

	UpdateLureTetherVfx();
}

void AWieldPhysicsWeaponPawn::UpdateLureTetherVfx()
{
	if (!ActiveLureTetherComponent)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !ActiveLureTetherTarget || ActiveLureTetherTarget->IsDead() || ActiveLureTetherTarget->GetCharmed())
	{
		StopLureTetherVfx();
		return;
	}
	
	if (HasAuthority() && ActiveLureTetherTarget)
	{
		const float CurrentWorldTime = World->GetTimeSeconds();
		const float DamageInterval = FMath::Max(LureTetherDamageInterval, 0.0f);
		if (CurrentWorldTime - LastLureTetherDamageTime >= DamageInterval 
			&& ActiveLureTetherTarget->GetHealthPercent() > ActiveLureTetherTarget->GetWeakenedHealthPercent())
		{
			LastLureTetherDamageTime = CurrentWorldTime;
			
			const float ThresholdHealth = ActiveLureTetherTarget->GetMaxHealth() * ActiveLureTetherTarget->GetWeakenedHealthPercent();

			const float DamageToApply = FMath::Min(
				LureDamage,
				ActiveLureTetherTarget->GetHealth() - ThresholdHealth);

			UGameplayStatics::ApplyDamage(
				ActiveLureTetherTarget,
				DamageToApply,
				GetController(),
				this,
				UDamageType::StaticClass());
		}
		else if (ActiveLureTetherTarget->GetHealthPercent() <= ActiveLureTetherTarget->GetWeakenedHealthPercent())
		{
			ActiveLureTetherTarget->SetCharmed(true);
			ActiveCharmedHost = ActiveLureTetherTarget;
			ClientSetLureCharmVisual(true);
		}
	}

	ActiveLureTetherComponent->SetVariableVec3(LureTetherStartParameterName, GetLureTetherStartLocation());
	ActiveLureTetherComponent->SetVariableVec3(LureTetherEndParameterName, GetLureTetherEndLocation(ActiveLureTetherTarget));
}

void AWieldPhysicsWeaponPawn::UpdatePossessPrompt()
{
	if (!IsLocallyControlled())
	{
		if (AWieldPossessableHost* PreviousPromptTarget = ActivePossessPromptTarget.Get())
		{
			PreviousPromptTarget->SetPossessPromptVisibility(false);
			ActivePossessPromptTarget = nullptr;
		}
		return;
	}

	AWieldPossessableHost* NewPromptTarget =
		FindNearestHost(PossessionSearchRadius, true);
	const bool bShouldShowPrompt = NewPromptTarget && NewPromptTarget->GetCharmed();

	if (AWieldPossessableHost* PreviousPromptTarget = ActivePossessPromptTarget.Get(); PreviousPromptTarget
		&& (PreviousPromptTarget != NewPromptTarget || !bShouldShowPrompt))
	{
		PreviousPromptTarget->SetPossessPromptVisibility(false);
	}

	if (bShouldShowPrompt)
	{
		NewPromptTarget->SetPossessPromptVisibility(true);
		ActivePossessPromptTarget = NewPromptTarget;
	}
	else
	{
		ActivePossessPromptTarget = nullptr;
	}
}

void AWieldPhysicsWeaponPawn::StopLureTetherVfx()
{
	if (ActiveLureTetherComponent)
	{
		ActiveLureTetherComponent->DeactivateImmediate();
		ActiveLureTetherComponent->DestroyComponent();
		ActiveLureTetherComponent = nullptr;
	}
	
	if (ActiveLureSoundComponent)
	{
		ActiveLureSoundComponent->Stop();
		ActiveLureSoundComponent->DestroyComponent();
		ActiveLureSoundComponent = nullptr;
	}
	
	if (ActiveLureTetherTarget)
	{
		ActiveLureTetherTarget->SetPossessPromptVisibility(false);
	}
	ActiveLureTetherTarget = nullptr;
}

FVector AWieldPhysicsWeaponPawn::GetLureTetherStartLocation() const
{
	const UStaticMeshComponent* TetherStartComponent = WeaponVisualMeshComponent ? WeaponVisualMeshComponent.Get() : WeaponMeshComponent.Get();
	return TetherStartComponent
		? TetherStartComponent->GetComponentLocation() + LureTetherStartOffset
		: GetActorLocation() + LureTetherStartOffset;
}

FVector AWieldPhysicsWeaponPawn::GetLureTetherEndLocation(const AWieldPossessableHost* TargetHost) const
{
	if (!TargetHost)
	{
		return GetActorLocation();
	}

	if (const USkeletalMeshComponent* TargetMesh = TargetHost->GetMesh())
	{
		if (LureTargetSocketName != NAME_None && TargetMesh->DoesSocketExist(LureTargetSocketName))
		{
			return TargetMesh->GetSocketLocation(LureTargetSocketName);
		}
	}

	return TargetHost->GetActorLocation() + FVector(0.0f, 0.0f, 90.0f);
}

void AWieldPhysicsWeaponPawn::StartLureProjectileAttack()
{
	UWorld* World = GetWorld();
	if (!World || !LureAttackProjectileClass)
	{
		return;
	}

	FTimerHandle AttackTimerHandle;
	const float AttackDelayTime = 1.0f;
	
	MulticastStartLureCastVfx(GetActorLocation());
	
	FTimerDelegate Delegate;
	Delegate.BindUObject(this, &AWieldPhysicsWeaponPawn::LureAttackTimerElapsed);
	World->GetTimerManager().SetTimer(AttackTimerHandle, Delegate, AttackDelayTime, false);
}

void AWieldPhysicsWeaponPawn::LureAttackTimerElapsed()
{
	UWorld* World = GetWorld();
	if (!World || !WeaponMeshComponent || !LureAttackProjectileClass)
	{
		bAbilityGlowActive = false;
		return;
	}

	const FVector FireDirection = GetLureProjectileDirection();
	constexpr float FireOffset = 200.0f;
	const FRotator SpawnRotation = FireDirection.Rotation();
	const FVector SpawnLocation = WeaponMeshComponent->GetComponentLocation() + FireDirection * FireOffset;
	FActorSpawnParameters SpawnParams;
	SpawnParams.Instigator = this;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	bAbilityGlowActive = false;

	if (AWieldProjectile* NewProjectile = World->SpawnActor<AWieldProjectile>(
		LureAttackProjectileClass,
		SpawnLocation,
		SpawnRotation,
		SpawnParams))
	{
		MoveIgnoreActorAdd(NewProjectile);
	}
}

FVector AWieldPhysicsWeaponPawn::GetLureProjectileDirection() const
{
	if (!IsLocallyControlled() && !ServerKnownAimDirection.IsNearlyZero())
	{
		return ServerKnownAimDirection.GetSafeNormal2D();
	}
	
	if (const AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		FVector2D ReticleInput = FVector2D::ZeroVector;
		if (GameplayPC->GetReticleInputVector(ReticleInput))
		{
			const FVector ReticleDirection = GetCameraRelativeDirection(ReticleInput.Y, ReticleInput.X);
			if (!ReticleDirection.IsNearlyZero())
			{
				return ReticleDirection.GetSafeNormal2D();
			}
		}
	}

	return LastMoveDirection.IsNearlyZero()
		? GetActorForwardVector().GetSafeNormal2D()
		: LastMoveDirection.GetSafeNormal2D();
}

void AWieldPhysicsWeaponPawn::MulticastStartLureCastVfx_Implementation(FVector Location)
{
	if (!LureCast)
	{
		return;
	}

	USceneComponent* LureCastAttachComponent = WeaponVisualMeshComponent ? WeaponVisualMeshComponent.Get() : WeaponMeshComponent.Get();
	if (!LureCastAttachComponent)
	{
		return;
	}

	UNiagaraFunctionLibrary::SpawnSystemAttached(
		LureCast,
		LureCastAttachComponent,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true
	);
	
	if (LureCastSound)
	{
		UGameplayStatics::SpawnSoundAttached(
			LureCastSound,
			LureCastAttachComponent,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			true,
			1.0f,
			1.0f,
			0.0f,
			nullptr,
			nullptr,
			false
		);
	}

	bAbilityGlowActive = true;
}

void AWieldPhysicsWeaponPawn::BeginPossessHold()
{
	bPossessKeyHeld = true;
	PossessHoldStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
}

void AWieldPhysicsWeaponPawn::EndPossessHold()
{
	bPossessKeyHeld = false;
	AWieldPossessableHost* NewPromptTarget =
		FindNearestHost(PossessionSearchRadius, true);
	if (NewPromptTarget)
	{
		NewPromptTarget->SetPossessPromptProgress(0.0f);
	}
}

void AWieldPhysicsWeaponPawn::EndLureHold()
{
	bLureKeyHeld = false;
	if (AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		GameplayPC->SetCharmHoldProgress(0.0f);
		GameplayPC->SetCharmHoldVisual(false);
	}
	if (!HasAuthority())
	{
		ServerSetLureHoldState(false);
	}
}

void AWieldPhysicsWeaponPawn::TryPossessNearbyHost()
{
	ServerTryPossessNearestHost();
}

void AWieldPhysicsWeaponPawn::UpdatePossessProgress()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	float CurrentHoldTime = World->GetTimeSeconds() - PossessHoldStartTime;
	if (bPossessKeyHeld)
	{
		AWieldPossessableHost* NewPromptTarget = FindNearestHost(PossessionSearchRadius, true);
		if (NewPromptTarget)
		{
			float HoldPercent = FMath::Clamp(CurrentHoldTime / PossessHoldDuration, 0.0f, 1.0f);
			NewPromptTarget->SetPossessPromptProgress(HoldPercent);
			if (HoldPercent >= 1.0f && NewPromptTarget->GetCharmed())
			{
				bPossessKeyHeld = false;
				ClientSetLureCharmVisual(false);
				NewPromptTarget->SetPossessPromptVisibility(false);
				TryPossessNearbyHost();
				ActiveCharmedHost = nullptr;
			}
		}
	}
}

void AWieldPhysicsWeaponPawn::UpdateLureHoldProgress()
{
	UWorld* World = GetWorld();
	AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(GetController());

	const bool bInvalidCharmedHost =
		!ActiveCharmedHost
		|| !ActiveCharmedHost->GetCharmed()
		|| ActiveCharmedHost->IsPossessedByWeapon()
		|| ActiveCharmedHost->IsDead();

	if (!World || bInvalidCharmedHost)
	{
		ActiveCharmedHost = World ? FindNearestCharmedHost(LureRadius) : nullptr;
		if (ActiveCharmedHost)
		{
			ClientSetLureCharmVisual(true);
		}
	}

	const bool bStillInvalidCharmedHost =
		!World
		|| !ActiveCharmedHost
		|| !ActiveCharmedHost->GetCharmed()
		|| ActiveCharmedHost->IsPossessedByWeapon()
		|| ActiveCharmedHost->IsDead();

	if (bStillInvalidCharmedHost)
	{
		bLureKeyHeld = false;
		ActiveCharmedHost = nullptr;

		if (GameplayPC)
		{
			GameplayPC->SetCharmHoldProgress(0.0f);
			GameplayPC->SetCharmHoldVisual(false);
		}

		ClientSetLureCharmVisual(false);
		return;
	}

	if (!bLureKeyHeld)
	{
		return;
	}

	const float CurrentHoldTime = World->GetTimeSeconds() - LureHoldStartTime;
	const float HoldPercent = FMath::Clamp(CurrentHoldTime / LureHoldDuration, 0.0f, 1.0f);

	if (GameplayPC)
	{
		GameplayPC->SetCharmHoldProgress(HoldPercent);
		GameplayPC->SetCharmHoldVisual(true);
	}

	if (HoldPercent >= 1.0f)
	{
		bLureKeyHeld = false;

		if (GameplayPC)
		{
			GameplayPC->SetCharmHoldProgress(0.0f);
			GameplayPC->SetCharmHoldVisual(false);
		}

		if (HasAuthority())
		{
			const float DamageToApply = ActiveCharmedHost->GetHealth();
			ActiveCharmedHost->SetSacrificable(true);
			UGameplayStatics::ApplyDamage(ActiveCharmedHost, DamageToApply, GetController(), this, UDamageType::StaticClass());
		}

		ActiveCharmedHost = nullptr;
		ClientSetLureCharmVisual(false);
	}
}

void AWieldPhysicsWeaponPawn::ScanNearbyHosts()
{
	if (AWieldGameplayPlayerController* PC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		PC->PlayScanButtonPressed();
	}
	
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	const float WorldTime = World->GetTimeSeconds();
	if (WorldTime - LastScanTime < ScanCooldown)
	{
		return;
	}

	if (!HasAuthority())
	{
		LastScanTime = WorldTime;
	}
	
	PlayLocalScanPostProcess();
	PlayCameraShake();

	ServerScanNearbyHosts();
}

void AWieldPhysicsWeaponPawn::LureNearbyHosts()
{
	if (AWieldGameplayPlayerController* PC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		PC->PlayLureButtonPressed();
	}
	bLureKeyHeld = true;
	LureHoldStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (!HasAuthority())
	{
		ServerSetLureHoldState(true);
	}
	
	ServerLureNearbyHosts();
}

void AWieldPhysicsWeaponPawn::PlayLocalScanPostProcess()
{
	if (!ScanPostProcessMID)
	{
		return;
	}

	ScanGreyCurrentAmount = ScanGreyPeakAmount;
	ScanGreyFadeElapsed = 0.0f;
	bScanGreyFading = true;
	
	ScanPostProcessMID->SetScalarParameterValue(TEXT("ScanGreyAmount"), ScanGreyCurrentAmount);
}

void AWieldPhysicsWeaponPawn::ResetScanCooldown()
{
	GetWorldTimerManager().ClearTimer(ScanCooldownTickTimer);
	ClientSetScanCooldownVisual(false, 0);
}

void AWieldPhysicsWeaponPawn::ResetLureCooldown()
{
	GetWorldTimerManager().ClearTimer(LureCooldownTickTimer);
	ClientSetLureCooldownVisual(false);
}

void AWieldPhysicsWeaponPawn::TickScanCooldownVisual()
{
	RemainingScanCooldownSeconds = FMath::Max(0, RemainingScanCooldownSeconds - 1);
	if (RemainingScanCooldownSeconds > 0)
	{
		ClientSetScanCooldownVisual(true, RemainingScanCooldownSeconds);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(ScanCooldownTickTimer);
		ClientSetScanCooldownVisual(false, 0);
	}
}

void AWieldPhysicsWeaponPawn::TickLureCooldownVisual()
{
	RemainingLureCooldownSeconds = FMath::Max(0, RemainingLureCooldownSeconds - 1);
	if (RemainingLureCooldownSeconds > 0)
	{
		ClientSetLureCooldownVisual(true);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(LureCooldownTickTimer);
		ClientSetLureCooldownVisual(false);
	}
}

void AWieldPhysicsWeaponPawn::ClientSetScanCooldownVisual_Implementation(bool bOnCooldown, int32 RemainingTime)
{
	if (AWieldGameplayPlayerController* PC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		PC->SetScanCooldownVisual(bOnCooldown, RemainingTime);
	}
}

void AWieldPhysicsWeaponPawn::ClientSetLureCooldownVisual_Implementation(bool bOnCooldown)
{
	if (AWieldGameplayPlayerController* PC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		PC->SetLureCooldownVisual(bOnCooldown);
	}
}

void AWieldPhysicsWeaponPawn::ClientSetLureCharmVisual_Implementation(bool bAvailable)
{
	if (AWieldGameplayPlayerController* PC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		PC->SetLureCharmVisual(bAvailable);
	}
}

void AWieldPhysicsWeaponPawn::HandleLureProjectileHit(AWieldPossessableHost* TargetHost)
{
	if (!HasAuthority() || !TargetHost || TargetHost->IsDead() || TargetHost->IsPossessedByWeapon())
	{
		return;
	}

	TargetHost->ApplyLureFromSource(this);
	
	if (AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(GetController()))
	{
		GameplayPC->ClientStartCameraShake(HitCameraShake);
	}
	
	MulticastStartLureTether(TargetHost);
}

AWieldPossessableHost* AWieldPhysicsWeaponPawn::FindNearestHost(float Radius, bool bRequirePossessable) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AWieldPossessableHost* BestHost = nullptr;
	float BestDistanceSquared = FMath::Square(Radius);
	for (TActorIterator<AWieldPossessableHost> It(World); It; ++It)
	{
		AWieldPossessableHost* Candidate = *It;
		if (!Candidate || Candidate->IsDead() || (bRequirePossessable && !Candidate->CanBePossessedByWeapon()))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestHost = Candidate;
		}
	}

	return BestHost;
}

AWieldPossessableHost* AWieldPhysicsWeaponPawn::FindNearestCharmedHost(float Radius) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AWieldPossessableHost* BestHost = nullptr;
	float BestDistanceSquared = FMath::Square(Radius);
	for (TActorIterator<AWieldPossessableHost> It(World); It; ++It)
	{
		AWieldPossessableHost* Candidate = *It;
		if (!Candidate
			|| Candidate->IsDead()
			|| Candidate->IsPossessedByWeapon()
			|| !Candidate->GetCharmed())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestHost = Candidate;
		}
	}

	return BestHost;
}
