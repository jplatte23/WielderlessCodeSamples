#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "NiagaraSystem.h"
#include "Wielderless/Weapons/WieldWeaponTypes.h"
#include "WieldPhysicsWeaponPawn.generated.h"

class UWieldReviveInteractorComponent;
class AWieldProjectile;
class UAudioComponent;
class UCameraComponent;
class UCameraShakeBase;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNavigationInvokerComponent;
class USoundAttenuation;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UWieldCameraNavigationInvokerComponent;
class UWieldWeaponDefinition;
class AWieldPossessableHost;
class USoundBase;
class UNiagaraComponent;

UCLASS(Blueprintable)
class WIELDERLESS_API AWieldPhysicsWeaponPawn : public APawn
{
	GENERATED_BODY()

public:
	AWieldPhysicsWeaponPawn();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ApplyWeaponDefinition();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetWeaponDefinition(UWieldWeaponDefinition* NewDefinition);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UWieldWeaponDefinition* GetWeaponDefinition() const { return WeaponDefinition; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UStaticMesh* GetEquippedWeaponStaticMesh() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetHostHeldWeaponMeshScale() const;
	
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void HandleLureProjectileHit(AWieldPossessableHost* TargetHost);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Camera")
	void SeedCameraView(float InYaw, float InPitch, float InTargetArmLength);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Camera")
	void SeedCameraHandoff(float InYaw, float InPitch, float InTargetArmLength, FVector InStartBoomWorldLocation, float InHandoffDuration);

	UFUNCTION(BlueprintCallable, Category = "Weapon|Camera")
	void SeedCameraHandoffFromCameraView(float InYaw, float InPitch, float InTargetArmLength, FVector InStartCameraWorldLocation, float InHandoffDuration);

	// Outgoing camera view for possession handoffs (world-space camera rotation/location).
	bool GetCameraHandoffView(float& OutYaw, float& OutPitch, float& OutTargetArmLength, FVector& OutCameraWorldLocation) const;

	// World transform of the visible sword mesh so possession can float it into the host's hand.
	bool GetWeaponMeshWorldTransform(FTransform& OutTransform) const;

	UFUNCTION(BlueprintCallable, Category = "Weapon|Camera")
	void PlayCameraShake();
	
	UFUNCTION(BlueprintCallable, Category = "Weapon|Cleanup")
	void CleanupWeaponTransientEffects();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
	TObjectPtr<UStaticMeshComponent> WeaponMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
	TObjectPtr<UStaticMeshComponent> WeaponVisualMeshComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
	TObjectPtr<UWieldReviveInteractorComponent> WeaponReviveInteractorComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
	TObjectPtr<UNavigationInvokerComponent> NavigationInvokerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Components")
	TObjectPtr<UWieldCameraNavigationInvokerComponent> CameraNavigationInvokerComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_WeaponDefinition, Category = "Weapon")
	TObjectPtr<UWieldWeaponDefinition> WeaponDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName WeaponId = TEXT("NecroSword");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FText WeaponDisplayName = NSLOCTEXT("WieldWeapon", "PhysicsWeaponPawnDefaultDisplayName", "Necro Sword");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	EWieldWeaponArchetype WeaponArchetype = EWieldWeaponArchetype::NecroSword;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals")
	FRotator WeaponMeshRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals")
	FVector WeaponMeshScale = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals")
	bool bNormalizeWeaponMeshToTargetLength = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals", meta = (ClampMin = "1.0"))
	float HostHeldWeaponTargetLength = 120.0f;
 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals", meta = (ClampMin = "0.0"))
	float HostHeldWeaponScaleMultiplier = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals")
	bool bAutoLayMeshFlatOnGround = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals")
	FName MovementGlowParameterName = TEXT("EmissiveGlowStrength");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals", meta = (ClampMin = "0.0"))
	float IdleMovementGlowValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals", meta = (ClampMin = "0.0"))
	float MovingMovementGlowValue = 0.4f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals", meta = (ClampMin = "0.0"))
	float AbilityGlowValue = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals", meta = (ClampMin = "0.0"))
	float MovementGlowInterpSpeed = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals", meta = (ClampMin = "0.0"))
	float MeshGroundClearance = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals|Network", meta = (ClampMin = "0.0"))
	float SimulatedProxyVisualLocationInterpSpeed = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals|Network", meta = (ClampMin = "0.0"))
	float SimulatedProxyVisualRotationInterpSpeed = 22.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals|Network", meta = (ClampMin = "0.0"))
	float SimulatedProxyVisualSnapDistance = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals|Network", meta = (ClampMin = "0.0"))
	float LocalVisualLocationInterpSpeed = 36.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals|Network", meta = (ClampMin = "0.0"))
	float LocalVisualRotationInterpSpeed = 44.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Visuals|Network", meta = (ClampMin = "0.0"))
	float LocalVisualSnapDistance = 260.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Visuals")
	TObjectPtr<UNiagaraSystem> MovementEffect;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Host|Visuals|Enemy Health Bar", meta = (ClampMin = "100.0"))
	float EnemyHealthBarHoverTraceDistance = 12000.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Scan VFX")
	TObjectPtr<UNiagaraSystem> ScanEffect;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Scan VFX")
	TSubclassOf<AActor> ScanEffectBP;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Scan VFX")
	TObjectPtr<USoundBase> ScanSound;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Scan VFX")
	TObjectPtr<UMaterialInterface> ScanPostProcessMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScanPostProcessMID;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Scan VFX")
	float ScanGreyPeakAmount = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Scan VFX", meta = (ClampMin = "0.0"))
	float ScanGreyFadeDuration = 0.6f;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Lure VFX")
	TObjectPtr<UNiagaraSystem> LureTetherEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lure VFX")
	FName LureTetherStartParameterName = TEXT("User.StartLocation");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lure VFX")
	FName LureTetherEndParameterName = TEXT("User.EndLocation");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lure VFX")
	FName LureTargetSocketName = TEXT("spine_03");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lure VFX")
	FVector LureTetherStartOffset = FVector(0.0f, 0.0f, 60.0f);
	
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ActiveLureSoundComponent;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Lure VFX")
	TObjectPtr<USoundBase> LureSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Lure VFX")
	TObjectPtr<USoundBase> LureCastSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Lure VFX")
	TObjectPtr<UNiagaraSystem> LureCast;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Lure VFX")
	TSubclassOf<UCameraShakeBase> HitCameraShake;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lure", meta = (ClampMin = "0.0"))
	float LureDamage = 6.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Lure", meta = (ClampMin = "0.0"))
	float LureTetherDamageInterval = 1.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Audio")
	TObjectPtr<USoundBase> MovementBurstSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Audio")
	TObjectPtr<USoundBase> MetalClangSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Audio")
	TObjectPtr<USoundBase> TetherCutSound;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Audio")
	TObjectPtr<USoundAttenuation> MovementSoundAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Audio", meta = (ClampMin = "0.0"))
	float GroundContactSoundCooldown = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Audio", meta = (ClampMin = "0.0"))
	float GroundContactSoundArmDelay = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Audio", meta = (ClampMin = "0.0"))
	float GroundContactMinNormalImpulse = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Audio", meta = (ClampMin = "0.0"))
	float GroundContactMinSpeed = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroundContactSoundVolume = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Movement")
	FWieldWeaponMovementSettings MovementSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Movement|Floor Safety", meta = (ClampMin = "0.0"))
	float FloorSafetyTraceUpDistance = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Movement|Floor Safety", meta = (ClampMin = "0.0"))
	float FloorSafetyTraceDownDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Movement|Floor Safety", meta = (ClampMin = "0.0"))
	float FloorSafetyGroundedDistance = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Movement|Floor Safety", meta = (ClampMin = "0.0"))
	float FloorRescuePenetrationDepth = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Movement|Floor Safety", meta = (ClampMin = "0.0"))
	float FloorRescueMissingGroundDrop = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Movement|Floor Safety", meta = (ClampMin = "0.0"))
	float FloorRescueRecentGroundMemory = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Camera")
	FWieldWeaponCameraSettings CameraSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Gameplay", meta = (ClampMin = "0.0"))
	float PossessionSearchRadius = 820.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Gameplay", meta = (ClampMin = "0.0"))
	float ScanRadius = 2600.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Gameplay", meta = (ClampMin = "0.0"))
	float ScanCooldown = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Gameplay", meta = (ClampMin = "0.0"))
	float LureRadius = 1450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Gameplay", meta = (ClampMin = "0.0"))
	float LureDuration = 7.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Gameplay", meta = (ClampMin = "0.0"))
	float LureCooldown = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Gameplay")
	TSubclassOf<AWieldProjectile> LureAttackProjectileClass;

private:
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveLureTetherComponent;

	UPROPERTY(Transient)
	TObjectPtr<AWieldPossessableHost> ActiveLureTetherTarget;
	
	UPROPERTY(Transient)
	TObjectPtr<AWieldPossessableHost> ActiveCharmedHost;

	float CameraYaw = 0.0f;
	float CameraPitch = -58.0f;
	float LastHopTime = -1000.0f;
	float MoveForwardInput = 0.0f;
	float MoveRightInput = 0.0f;
	float DesiredCameraArmLength = 1050.0f;
	float CameraHandoffElapsed = 0.0f;
	float CameraHandoffDuration = 0.0f;
	float AuthMoveStrength = 0.0f;
	float StruggleChargeElapsed = 0.0f;
	float StruggleRecoveryElapsed = 1000.0f;
	float LastGroundContactSoundTime = -1000.0f;
	float GroundContactSoundArmedTime = -1000.0f;
	float LastSafeGroundTime = -1000.0f;
	
	float ScanGreyCurrentAmount = 0.0f;
	float ScanGreyFadeElapsed = 0.0f;
	bool bScanGreyFading = false;
	float LastScanTime = -1000.0f;
	float LastLureTetherDamageTime = -1000.0f;
	bool bPossessKeyHeld = false;
	float PossessHoldStartTime = -1000.0f;
	float PossessHoldDuration = 1.5f;
	bool bLureKeyHeld = false;
	float LureHoldStartTime = -1000.0f;
	float LureHoldDuration = 3.0f;
	
	FTimerHandle LastScanTimer;
	FTimerHandle LastJumpTimer;
	FTimerHandle LastLureTimer;

	FTimerHandle ScanCooldownTickTimer;
	FTimerHandle JumpCooldownTickTimer;
	FTimerHandle LureCooldownTickTimer;

	int32 RemainingScanCooldownSeconds = 0;
	int32 RemainingLureCooldownSeconds = 0;
	int32 RemainingJumpCooldownSeconds = 0;
	float LureAvailableTime = -1000.0f;

	bool bGroundContactSoundArmed = false;
	bool bHasLastSafeGroundLocation = false;
	FVector AuthMoveDirection = FVector::ZeroVector;
	FVector CameraHandoffStartBoomLocation = FVector::ZeroVector;
	FVector LastSafeGroundActorLocation = FVector::ZeroVector;
	FVector LastSafeGroundLinearVelocity = FVector::ZeroVector;
	FVector LastMoveDirection = FVector::ForwardVector;
	bool bCameraHandoffActive = false;
	bool bCameraHandoffRestoreCameraLag = false;
	bool bIsRightMouseCameraRotating = false;
	bool bHasInitializedWeaponVisualTransform = false;
	float CurrentMovementGlowValue = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FallbackWeaponMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WeaponDynamicMaterial;
	
	UPROPERTY()
	FVector ServerKnownAimDirection = FVector::ForwardVector;
	
	UPROPERTY(Replicated)
	bool bMovementGlowActive = false;
	
	UPROPERTY(Replicated)
	bool bAbilityGlowActive = false;

	UFUNCTION()
	void OnRep_WeaponDefinition();

	UFUNCTION(Server, Unreliable)
	void ServerSetMovementInput(FVector MoveDirection, float MoveStrength);
	
	UFUNCTION(Server, Unreliable)
	void ServerSetAimDirection(FVector AimDirection);

	UFUNCTION(Server, Reliable)
	void ServerHop(FVector HopDirection);

	UFUNCTION(Server, Reliable)
	void ServerTryPossessNearestHost(); 

	UFUNCTION(Server, Reliable)
	void ServerScanNearbyHosts();

	UFUNCTION(Server, Reliable)
	void ServerLureNearbyHosts();
	
	UFUNCTION(Server, Reliable)
	void ServerSetLureHoldState(bool bHeld);
	
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayMovementBurst(FVector Location, FRotator Rotation);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayGroundContactSound(FVector Location);
	
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayTetherCutSound(FVector Location);
	
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStopLureTether();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayScanEffect(FVector Location);
	
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartLureTether(AWieldPossessableHost* TargetHost);
	
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastStartLureCastVfx(FVector Location);
	
	TWeakObjectPtr<AWieldPossessableHost> ActivePossessPromptTarget;

	// Movement system: physics body setup, player locomotion input, hop, grounding, and contact feedback.
	UFUNCTION()
	void HandleWeaponMeshHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse, 
		const FHitResult& Hit);

	void MoveForward(float Value);
	void MoveRight(float Value);
	void Hop();
	void ConfigurePhysicsBody();
	void PlacePhysicsBodyOnGroundAtSpawn();
	void UpdateAuthoritativeInput();
	void UpdateStrugglePhysicsMovement(float DeltaSeconds);
	void UpdateFloorSafety(float DeltaSeconds);
	void ApplyMovementSettings();
	void DoPhysicsHop(const FVector& HopDirection, bool bPlayCosmetics = true);
	bool IsGrounded() const;
	bool FindFloorBelow(float TraceUpDistance, float TraceDownDistance, FHitResult& OutHit) const;
	float GetWeaponMeshLowestLocalPointZ() const;
	FVector GetActorLocationForGroundHit(const FHitResult& GroundHit) const;
	void SnapPhysicsBodyToGroundHit(const FHitResult& GroundHit);
	FVector GetCameraRelativeDirection(float ForwardValue, float RightValue) const;
	void ResetJumpCooldown();
	void TickJumpCooldownVisual();

	UFUNCTION(Client, Reliable)
	void ClientSetJumpCooldownVisual(bool bOnCooldown);

	// Camera system: rotation, zoom, and possession/sacrifice camera handoff.
	void TurnCameraMouse(float Value);
	void LookCameraMouse(float Value);
	void TurnCameraGamepad(float Value);
	void LookCameraGamepad(float Value);
	void ZoomCamera(float Value);
	void BeginRightMouseCameraRotation();
	void EndRightMouseCameraRotation();
	void UpdateCameraZoom(float DeltaSeconds);
	void UpdateCameraHandoff(float DeltaSeconds);
	void UpdateCameraRig();
	void ApplyCameraSettings();

	// Ability system: scan, lure, possession prompts, cooldown UI, projectile/tether flow, and host lookup.
	void BeginPossessHold();
	void TryPossessNearbyHost();
	void EndPossessHold();
	void EndLureHold();
	void ScanNearbyHosts();
	void LureNearbyHosts();
	void PlayLocalScanPostProcess();
	void ResetScanCooldown();
	void ResetLureCooldown();
	void TickScanCooldownVisual();
	void TickLureCooldownVisual();
	void UpdateLureTetherVfx();
	void StopLureTetherVfx();
	FVector GetLureTetherStartLocation() const;
	FVector GetLureTetherEndLocation(const AWieldPossessableHost* TargetHost) const;
	void StartLureProjectileAttack();
	void LureAttackTimerElapsed();
	FVector GetLureProjectileDirection() const;
	void UpdatePossessProgress();
	void UpdateLureHoldProgress();
	void UpdatePossessPrompt();
	AWieldPossessableHost* FindNearestHost(float Radius, bool bRequirePossessable) const;
	AWieldPossessableHost* FindNearestCharmedHost(float Radius) const;
	
	UFUNCTION(Client, Reliable)
	void ClientSetScanCooldownVisual(bool bOnCooldown, int32 RemainingTime);

	UFUNCTION(Client, Reliable)
	void ClientSetLureCooldownVisual(bool bOnCooldown);

	UFUNCTION(Client, Reliable)
	void ClientSetLureCharmVisual(bool bAvailable);
	
	// Core/local presentation wiring.
	void ConfigureLocalGameplayInput() const;

	// Visual system: visible mesh smoothing, material glow, and mesh scaling helpers.
	void UpdateWeaponVisualTransform(float DeltaSeconds);
	void UpdateMovementMaterial(float DeltaSeconds);
	FRotator GetGroundAlignedMeshRotation(const FRotator& BaseRotation) const;
	float GetStaticMeshLongestAxis(const UStaticMesh* StaticMesh) const;
};
