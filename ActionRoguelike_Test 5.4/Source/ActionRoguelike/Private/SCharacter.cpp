// Fill out your copyright notice in the Description page of Project Settings.


#include "SCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SInteractionComponent.h"
#include "SAttributeComponent.h"
#include "SActionComponent.h"
#include "Components/CapsuleComponent.h"
#include "SharedGameplayTags.h"

// Enhanced Input
#include "ActionRoguelike.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "SPlayerController.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SCharacter)


// Sets default values
ASCharacter::ASCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	SpringArmComp = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmComp"));
	SpringArmComp->bUsePawnControlRotation = true;
	SpringArmComp->SetupAttachment(RootComponent);
	SpringArmComp->SetUsingAbsoluteRotation(true);

	CameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComp"));
	CameraComp->SetupAttachment(SpringArmComp);

	InteractionComp = CreateDefaultSubobject<USInteractionComponent>(TEXT("InteractionComp"));

	AttributeComp = CreateDefaultSubobject<USAttributeComponent>(TEXT("AttributeComp"));

	ActionComp = CreateDefaultSubobject<USActionComponent>(TEXT("ActionComp"));

	PerceptionStimuliComp = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("PerceptionStimuliComp"));

	GetCharacterMovement()->bOrientRotationToMovement = true;
	bUseControllerRotationYaw = false;

	// Skip performing overlap queries on the Physics Asset after animation (7 queries in case of our Gideon mesh)
	GetMesh()->bUpdateOverlapsOnAnimationFinalize = false;
	
	// Enabled on mesh to react to incoming projectiles
	GetMesh()->SetGenerateOverlapEvents(true);
	// Disable on capsule collision to avoid double-dipping and receiving 2 overlaps when entering trigger zones etc.
	// Once from the mesh, and 2nd time from capsule
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);

	//TimeToHitParamName = "TimeToHit";
	HitFlash_CustomPrimitiveIndex = 0;
}


void ASCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	AttributeComp->OnHealthChanged.AddDynamic(this, &ASCharacter::OnHealthChanged);
}


// Called to bind functionality to input
void ASCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	const APlayerController* PC = GetController<APlayerController>();
	const ULocalPlayer* LP = PC->GetLocalPlayer();
	
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	check(Subsystem);

	Subsystem->ClearAllMappings();

	// Add mappings for our game
	Subsystem->AddMappingContext(DefaultInputMapping, 0);

	// New Enhanced Input system
	UEnhancedInputComponent* InputComp = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

	// General
	InputComp->BindAction(Input_Move, ETriggerEvent::Triggered, this, &ASCharacter::Move);
	InputComp->BindAction(Input_Jump, ETriggerEvent::Triggered, this, &ACharacter::Jump);
	InputComp->BindAction(Input_Interact, ETriggerEvent::Triggered, this, &ASCharacter::PrimaryInteract);

	// Sprint while key is held
	InputComp->BindAction(Input_Sprint, ETriggerEvent::Started, this, &ASCharacter::SprintStart);
	InputComp->BindAction(Input_Sprint, ETriggerEvent::Completed, this, &ASCharacter::SprintStop);

	// MKB
	InputComp->BindAction(Input_LookMouse, ETriggerEvent::Triggered, this, &ASCharacter::LookMouse);
	// Gamepad
	InputComp->BindAction(Input_LookStick, ETriggerEvent::Triggered, this, &ASCharacter::LookStick);

	// Abilities
	InputComp->BindAction(Input_PrimaryAttack, ETriggerEvent::Triggered, this, &ASCharacter::PrimaryAttack);
	InputComp->BindAction(Input_SecondaryAttack, ETriggerEvent::Triggered, this, &ASCharacter::BlackHoleAttack);
	InputComp->BindAction(Input_Dash, ETriggerEvent::Triggered, this, &ASCharacter::Dash);
}

void ASCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FindCrosshairTarget();
}


void ASCharacter::FindCrosshairTarget()
{
	// Ignore if not using GamePad
	ASPlayerController* PC = GetController<ASPlayerController>();

	if (PC == nullptr || !PC->IsUsingGamepad())
	{
		bHasPawnTarget = false;
		return;
	}

	FVector EyeLocation;
	FRotator EyeRotation;
	GetActorEyesViewPoint(EyeLocation, EyeRotation);

	const float AimAssistDistance = 5000.f;
	const FVector TraceEnd = EyeLocation + (EyeRotation.Vector() * AimAssistDistance);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	FCollisionShape Shape;
	Shape.SetSphere(50.f);

	// Called next frame when the trace has completed
	FTraceDelegate Delegate = FTraceDelegate::CreateUObject(this, &ASCharacter::CrosshairTraceComplete);
	
	TraceHandle = GetWorld()->AsyncSweepByChannel(EAsyncTraceType::Single, EyeLocation, TraceEnd, FQuat::Identity, ECC_Pawn, Shape, Params, FCollisionResponseParams::DefaultResponseParam, &Delegate);
}


void ASCharacter::CrosshairTraceComplete(const FTraceHandle& InTraceHandle, FTraceDatum& InTraceDatum)
{
	// at most expect one hit
	if (InTraceDatum.OutHits.IsValidIndex(0))
	{
		FHitResult Hit = InTraceDatum.OutHits[0];
		// Figure out if dealing with a Pawn, may want aim assist on other 'things', which requires a different check
		bHasPawnTarget = Hit.IsValidBlockingHit() && Hit.GetActor()->IsA(APawn::StaticClass());

		//UE_LOG(LogGame, Log, TEXT("has pawn target: %s"), bHasPawnTarget ? TEXT("true") : TEXT("false"));
	}
}


void ASCharacter::Move(const FInputActionInstance& Instance)
{
	FRotator ControlRot = GetControlRotation();
	ControlRot.Pitch = 0.0f;
	ControlRot.Roll = 0.0f;

	// Get value from input (combined value from WASD keys or single Gamepad stick) and convert to Vector (x,y)
	const FVector2D AxisValue = Instance.GetValue().Get<FVector2D>();

	// Move forward/back
	AddMovementInput(ControlRot.Vector(), AxisValue.Y);

	// Move Right/Left strafe
	const FVector RightVector = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::Y);
	AddMovementInput(RightVector, AxisValue.X);

	// Alternative Copied from Lyra
	/*
	{
		const FRotator MovementRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);

		if (Value.X != 0.0f)
		{
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::RightVector);
			Pawn->AddMovementInput(MovementDirection, Value.X);
		}

		if (Value.Y != 0.0f)
		{
			const FVector MovementDirection = MovementRotation.RotateVector(FVector::ForwardVector);
			Pawn->AddMovementInput(MovementDirection, Value.Y);
		}
	}*/
}

void ASCharacter::LookMouse(const FInputActionValue& InputValue)
{
	const FVector2D Value = InputValue.Get<FVector2D>();
	
	AddControllerYawInput(Value.X);
	AddControllerPitchInput(Value.Y);
}


void ASCharacter::LookStick(const FInputActionValue& InputValue)
{
	FVector2D Value = InputValue.Get<FVector2D>();

	// Track negative as we'll lose this during the conversion
	bool XNegative = Value.X < 0.f;
	bool YNegative = Value.Y < 0.f;

	// Can further modify with 'sensitivity' settings
	static const float LookYawRate = 100.0f;
	static const float LookPitchRate = 50.0f;

	// non-linear to make aiming a little easier
	Value = Value * Value;

	if (XNegative)
	{
		Value.X *= -1.f;
	}
	if (YNegative)
	{
		Value.Y *= -1.f;
	}

	// Aim assist
	// todo: may need to ease this out and/or change strength based on distance to target
	float RateMultiplier = 1.0f;
	if (bHasPawnTarget)
	{
		RateMultiplier = 0.5f;
	}

	AddControllerYawInput(Value.X * (LookYawRate * RateMultiplier) * GetWorld()->GetDeltaSeconds());
	AddControllerPitchInput(Value.Y * (LookPitchRate * RateMultiplier) * GetWorld()->GetDeltaSeconds());
}


void ASCharacter::SprintStart()
{
	ActionComp->StartActionByName(this, SharedGameplayTags::Action_Sprint);
}

void ASCharacter::SprintStop()
{
	ActionComp->StopActionByName(this, SharedGameplayTags::Action_Sprint);
}


void ASCharacter::PrimaryAttack()
{	
	ActionComp->StartActionByName(this, SharedGameplayTags::Action_PrimaryAttack);
}


void ASCharacter::BlackHoleAttack()
{
	ActionComp->StartActionByName(this, SharedGameplayTags::Action_Blackhole);
}


void ASCharacter::Dash()
{	
	ActionComp->StartActionByName(this, SharedGameplayTags::Action_Dash);
}


void ASCharacter::PrimaryInteract()
{
	InteractionComp->PrimaryInteract();
}


FGenericTeamId ASCharacter::GetGenericTeamId() const
{
	// We have no team switching support during gameplay
	return FGenericTeamId(TEAM_ID_PLAYERS);
}


void ASCharacter::OnHealthChanged(AActor* InstigatorActor, USAttributeComponent* OwningComp, float NewHealth, float Delta)
{
	// Damaged
	if (Delta < 0.0f)
	{
		// Replaces the above "old" method of requiring unique material instances for every mesh element on the player 
		GetMesh()->SetCustomPrimitiveDataFloat(HitFlash_CustomPrimitiveIndex, GetWorld()->TimeSeconds);

		// Rage added equal to damage received (Abs to turn into positive rage number)
		const float RageDelta = FMath::Abs(Delta);
		AttributeComp->ApplyRage(InstigatorActor, RageDelta);
	}

	// Died
	if (NewHealth <= 0.0f && Delta < 0.0f)
	{
		APlayerController* PC = GetController<ASPlayerController>();
		DisableInput(PC);

		SetLifeSpan(5.0f);

		// Prevent bots from seeing us as a threat
		PerceptionStimuliComp->UnregisterFromPerceptionSystem();
	}
}


FVector ASCharacter::GetPawnViewLocation() const
{
	return CameraComp->GetComponentLocation();
}
