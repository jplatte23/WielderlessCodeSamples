#include "WieldPhysicsWeaponPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationInvokerComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Wielderless/Actors/WieldPossessableHost.h"
#include "Wielderless/Player/WieldGameplayPlayerController.h"
#include "Wielderless/Weapons/WieldWeaponDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Wielderless/Components/Shared/WieldCameraNavigationInvokerComponent.h"
#include "Wielderless/Components/Shared/WieldReviveInteractorComponent.h"

// Core lifecycle, construction, replication, and weapon-definition wiring.

AWieldPhysicsWeaponPawn::AWieldPhysicsWeaponPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(60.0f);
	SetMinNetUpdateFrequency(30.0f);

	WeaponMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhysicsWeaponMesh"));
	SetRootComponent(WeaponMeshComponent);
	WeaponMeshComponent->SetCollisionProfileName(TEXT("PhysicsActor"));
	WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WeaponMeshComponent->SetGenerateOverlapEvents(false);
	WeaponMeshComponent->SetNotifyRigidBodyCollision(true);
	WeaponMeshComponent->SetIsReplicated(true);
	// The owning client predicts weapon physics locally. Server physics still replicates to simulated proxies,
	// but not back onto the autonomous proxy where it would fight prediction and cause jitter.
	WeaponMeshComponent->bReplicatePhysicsToAutonomousProxy = false;
	WeaponMeshComponent->bEditableWhenInherited = true;
	WeaponMeshComponent->OnComponentHit.AddDynamic(this, &AWieldPhysicsWeaponPawn::HandleWeaponMeshHit);
	WeaponMeshComponent->SetHiddenInGame(true);
	WeaponMeshComponent->SetVisibility(false);
	
	WeaponReviveInteractorComponent = CreateDefaultSubobject<UWieldReviveInteractorComponent>(TEXT("ReviveInteractorComp"));
	
	WeaponVisualMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponVisualMesh"));
	WeaponVisualMeshComponent->SetupAttachment(RootComponent);
	WeaponVisualMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponVisualMeshComponent->SetGenerateOverlapEvents(false);
	WeaponVisualMeshComponent->SetIsReplicated(false);
	WeaponVisualMeshComponent->SetAbsolute(true, true, true);
	WeaponVisualMeshComponent->bEditableWhenInherited = true;
 
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SwordMesh(
		TEXT("/Game/Assets/Weapons/Sword/Not_Glowy_Sword/Meshy_AI_Emerald_Gloomblade_0609184349_texture.Meshy_AI_Emerald_Gloomblade_0609184349_texture")
	);
	if (SwordMesh.Succeeded())
	{
		WeaponMeshComponent->SetStaticMesh(SwordMesh.Object);
		WeaponVisualMeshComponent->SetStaticMesh(SwordMesh.Object);
	}
	else
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (CubeMesh.Succeeded())
		{
			FallbackWeaponMesh = CubeMesh.Object;
			WeaponMeshComponent->SetStaticMesh(FallbackWeaponMesh);
			WeaponVisualMeshComponent->SetStaticMesh(FallbackWeaponMesh);
		}
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteLocation(true);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEditableWhenInherited = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->bEditableWhenInherited = true;

	NavigationInvokerComponent = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavigationInvoker"));
	NavigationInvokerComponent->SetGenerationRadii(6000.0f, 10000.0f);

	CameraNavigationInvokerComponent = CreateDefaultSubobject<UWieldCameraNavigationInvokerComponent>(TEXT("CameraNavigationInvokerComponent"));
	CameraNavigationInvokerComponent->SetCameraComponent(FollowCamera);

	ApplyWeaponDefinition();
}

void AWieldPhysicsWeaponPawn::BeginPlay()
{
	Super::BeginPlay();
	
	if (ScanPostProcessMaterial)
	{
		ScanPostProcessMID = UMaterialInstanceDynamic::Create(ScanPostProcessMaterial, this);
		ScanPostProcessMID->SetScalarParameterValue(TEXT("ScanGreyAmount"), 0.0f);
		FollowCamera->PostProcessSettings.AddBlendable(ScanPostProcessMID, 1.0f);
	}

	if (HasAuthority() || IsLocallyControlled())
	{
		PlacePhysicsBodyOnGroundAtSpawn();
	}
	ConfigurePhysicsBody();
	if (CameraNavigationInvokerComponent)
	{
		CameraNavigationInvokerComponent->SetCameraComponent(FollowCamera);
	}
	UpdateWeaponVisualTransform(0.0f);
	UpdateCameraRig();
}

void AWieldPhysicsWeaponPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AWieldPossessableHost::ClearHoveredEnemyHealthBarFor(this);

	CleanupWeaponTransientEffects();

	Super::EndPlay(EndPlayReason);
}

void AWieldPhysicsWeaponPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyWeaponDefinition();
}

void AWieldPhysicsWeaponPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bScanGreyFading && ScanPostProcessMID)
	{
		ScanGreyFadeElapsed += DeltaSeconds;
		const float FadeDuration = FMath::Max(ScanGreyFadeDuration, KINDA_SMALL_NUMBER);
		const float alpha = FMath::Clamp(ScanGreyFadeElapsed / FadeDuration, 0.0f, 1.0f);
		ScanGreyCurrentAmount = FMath::Lerp(ScanGreyPeakAmount, 0.0f, alpha);
		ScanPostProcessMID->SetScalarParameterValue(TEXT("ScanGreyAmount"), ScanGreyCurrentAmount);
		
		if (alpha >= 1.0f)
		{
			bScanGreyFading = false;
			ScanGreyCurrentAmount = 0.0f;
		}
	}
	if (IsLocallyControlled() && !HasAuthority())
	{
		ServerSetAimDirection(GetLureProjectileDirection());
	}
	
	UpdatePossessProgress();
	UpdateAuthoritativeInput();
	UpdateStrugglePhysicsMovement(DeltaSeconds);
	UpdateFloorSafety(DeltaSeconds);
	UpdateWeaponVisualTransform(DeltaSeconds);
	UpdateCameraZoom(DeltaSeconds);
	UpdateCameraHandoff(DeltaSeconds);
	UpdateCameraRig();
	UpdateLureTetherVfx();
	UpdatePossessPrompt();
	UpdateLureHoldProgress();
	AWieldPossessableHost::UpdateHoveredEnemyHealthBarFor(
		this,
		Cast<APlayerController>(GetController()),
		EnemyHealthBarHoverTraceDistance);
	UpdateMovementMaterial(DeltaSeconds);
}

void AWieldPhysicsWeaponPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("Move Forward / Backward"), this, &AWieldPhysicsWeaponPawn::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("Move Right / Left"), this, &AWieldPhysicsWeaponPawn::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("Turn Right / Left Mouse"), this, &AWieldPhysicsWeaponPawn::TurnCameraMouse);
	PlayerInputComponent->BindAxis(TEXT("Look Up / Down Mouse"), this, &AWieldPhysicsWeaponPawn::LookCameraMouse);
	PlayerInputComponent->BindAxis(TEXT("Turn Right / Left Gamepad"), this, &AWieldPhysicsWeaponPawn::TurnCameraGamepad);
	PlayerInputComponent->BindAxis(TEXT("Look Up / Down Gamepad"), this, &AWieldPhysicsWeaponPawn::LookCameraGamepad);
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AWieldPhysicsWeaponPawn::ZoomCamera);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AWieldPhysicsWeaponPawn::BeginRightMouseCameraRotation);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AWieldPhysicsWeaponPawn::EndRightMouseCameraRotation);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &AWieldPhysicsWeaponPawn::Hop);
	PlayerInputComponent->BindAction(TEXT("Possess"), IE_Pressed, this, &AWieldPhysicsWeaponPawn::BeginPossessHold);
	PlayerInputComponent->BindAction(TEXT("Possess"), IE_Released, this, &AWieldPhysicsWeaponPawn::EndPossessHold);
	PlayerInputComponent->BindAction(TEXT("Scan"), IE_Pressed, this, &AWieldPhysicsWeaponPawn::ScanNearbyHosts);
	PlayerInputComponent->BindAction(TEXT("Lure"), IE_Pressed, this, &AWieldPhysicsWeaponPawn::LureNearbyHosts);
	PlayerInputComponent->BindAction(TEXT("Lure"), IE_Released, this, &AWieldPhysicsWeaponPawn::EndLureHold);
	PlayerInputComponent->BindAction(TEXT("Revive"), IE_Pressed, WeaponReviveInteractorComponent.Get(), &UWieldReviveInteractorComponent::BeginReviveHold);
	PlayerInputComponent->BindAction(TEXT("Revive"), IE_Released, WeaponReviveInteractorComponent.Get(), &UWieldReviveInteractorComponent::EndReviveHold);
}

void AWieldPhysicsWeaponPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	ConfigurePhysicsBody();
	ConfigureLocalGameplayInput();
}

void AWieldPhysicsWeaponPawn::OnRep_Controller()
{
	Super::OnRep_Controller();

	ConfigurePhysicsBody();
	ConfigureLocalGameplayInput();
}

void AWieldPhysicsWeaponPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWieldPhysicsWeaponPawn, WeaponDefinition);
	DOREPLIFETIME(AWieldPhysicsWeaponPawn, bMovementGlowActive);
	DOREPLIFETIME(AWieldPhysicsWeaponPawn, bAbilityGlowActive);
}

void AWieldPhysicsWeaponPawn::OnRep_WeaponDefinition()
{
	ApplyWeaponDefinition();
	ConfigurePhysicsBody();
}

void AWieldPhysicsWeaponPawn::ApplyWeaponDefinition()
{
	WeaponMeshComponent->SetHiddenInGame(true);
	WeaponMeshComponent->SetVisibility(false);
	if (WeaponVisualMeshComponent)
	{
		WeaponVisualMeshComponent->SetHiddenInGame(false);
		WeaponVisualMeshComponent->SetVisibility(true);
		WeaponVisualMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponVisualMeshComponent->SetGenerateOverlapEvents(false);
	}

	if (WeaponDefinition)
	{
		WeaponId = WeaponDefinition->WeaponId;
		WeaponDisplayName = WeaponDefinition->DisplayName;
		WeaponArchetype = WeaponDefinition->Archetype;
		WeaponMeshRotation = WeaponDefinition->MeshRotation;
		WeaponMeshScale = WeaponDefinition->MeshScale;
		bNormalizeWeaponMeshToTargetLength = WeaponDefinition->bNormalizeMeshToTargetLength;
		HostHeldWeaponTargetLength = WeaponDefinition->HostHeldMeshTargetLength;
		bAutoLayMeshFlatOnGround = WeaponDefinition->bAutoLayMeshFlatOnGround;
		MeshGroundClearance = WeaponDefinition->MeshGroundClearance;
		MovementSettings = WeaponDefinition->Movement;
		PossessionSearchRadius = WeaponDefinition->Gameplay.PossessionSearchRadius;
		ScanRadius = WeaponDefinition->Gameplay.ScanRadius;
		LureRadius = WeaponDefinition->Gameplay.LureRadius;
		CameraSettings = WeaponDefinition->Camera;

		if (WeaponDefinition->StaticMesh)
		{
			WeaponMeshComponent->SetStaticMesh(WeaponDefinition->StaticMesh);
			if (WeaponVisualMeshComponent)
			{
				WeaponVisualMeshComponent->SetStaticMesh(WeaponDefinition->StaticMesh);
			}
		}
	}

	const FRotator AppliedMeshRotation = bAutoLayMeshFlatOnGround ? GetGroundAlignedMeshRotation(WeaponMeshRotation) : WeaponMeshRotation;
	WeaponMeshComponent->SetRelativeRotation(AppliedMeshRotation);
	WeaponMeshComponent->SetRelativeScale3D(WeaponMeshScale);
	if (WeaponVisualMeshComponent)
	{
		if (WeaponVisualMeshComponent->GetStaticMesh() != WeaponMeshComponent->GetStaticMesh())
		{
			WeaponVisualMeshComponent->SetStaticMesh(WeaponMeshComponent->GetStaticMesh());
		}
		const int32 MaterialCount = WeaponMeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			WeaponVisualMeshComponent->SetMaterial(MaterialIndex, WeaponMeshComponent->GetMaterial(MaterialIndex));
		}
		WeaponVisualMeshComponent->SetWorldTransform(WeaponMeshComponent->GetComponentTransform(), false, nullptr, ETeleportType::TeleportPhysics);
		bHasInitializedWeaponVisualTransform = true;
	}
	WeaponDynamicMaterial = nullptr;
	CurrentMovementGlowValue = IdleMovementGlowValue;

	ApplyMovementSettings();
	ApplyCameraSettings();
}

void AWieldPhysicsWeaponPawn::SetWeaponDefinition(UWieldWeaponDefinition* NewDefinition)
{
	WeaponDefinition = NewDefinition;
	ApplyWeaponDefinition();
	ConfigurePhysicsBody();
}

void AWieldPhysicsWeaponPawn::ConfigureLocalGameplayInput() const
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	PlayerController->SetInputMode(FInputModeGameOnly());
	PlayerController->SetShowMouseCursor(false);
	if (AWieldGameplayPlayerController* GameplayPC = Cast<AWieldGameplayPlayerController>(PlayerController))
	{
		GameplayPC->SetReticleVisibility(!GameplayPC->GetPauseVisbility());
	}
}

void AWieldPhysicsWeaponPawn::CleanupWeaponTransientEffects()
{
	StopLureTetherVfx();
	
	GetWorldTimerManager().ClearTimer(ScanCooldownTickTimer);
	GetWorldTimerManager().ClearTimer(JumpCooldownTickTimer);
	GetWorldTimerManager().ClearTimer(LureCooldownTickTimer);
	GetWorldTimerManager().ClearTimer(LastScanTimer);
	GetWorldTimerManager().ClearTimer(LastJumpTimer);
	GetWorldTimerManager().ClearTimer(LastLureTimer);
	
	ClientSetScanCooldownVisual(false, 0);
	ClientSetJumpCooldownVisual(false);
	ClientSetLureCooldownVisual(false);
}
