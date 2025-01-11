// Fill out your copyright notice in the Description page of Project Settings.


#include "SAction.h"
#include "SActionComponent.h"
#include "ActionRoguelike.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SAction)


void USAction::Initialize(USActionComponent* NewActionComp)
{
	ActionComp = NewActionComp;
}


bool USAction::CanStart_Implementation(AActor* Instigator)
{
	if (IsRunning())
	{
		return false;
	}

	USActionComponent* Comp = GetOwningComponent();
	
	if (Comp->ActiveGameplayTags.HasAny(BlockedTags))
	{
		return false;
	}

	return true;
}


void USAction::StartAction_Implementation(AActor* Instigator)
{
	UE_LOGFMT(LogGame, Log, "Started: {ActionName}", GetName());

	USActionComponent* Comp = GetOwningComponent();	
	Comp->ActiveGameplayTags.AppendTags(GrantsTags);

	RepData.bIsRunning = true;
	RepData.Instigator = Instigator;

	if (GetOwningComponent()->GetOwnerRole() == ROLE_Authority)
	{
		TimeStarted = GetWorld()->TimeSeconds; 
	}

	GetOwningComponent()->OnActionStarted.Broadcast(GetOwningComponent(), this);
}


/**
 * Stops the action and notifies the owning component.
 *
 * @param Instigator The actor that stopped the action.
 */
void USAction::StopAction_Implementation(AActor* Instigator)
{
	// Log a message to indicate the action has stopped
	UE_LOGFMT(LogGame, Log, "Stopped: {name}", GetName());

	// Ensure the action is not running (currently commented out)
	// ensureAlways(bIsRunning);

	// Get the owning component
	USActionComponent* Comp = GetOwningComponent();

	// Remove the gameplay tags granted by this action
	Comp->ActiveGameplayTags.RemoveTags(GrantsTags);

	// Update the replication data
	RepData.bIsRunning = false;
	RepData.Instigator = Instigator;

	// Notify the owning component that the action has stopped
	GetOwningComponent()->OnActionStopped.Broadcast(GetOwningComponent(), this);
}


/**
 * @brief Returns the world of the outer actor, if it exists.
 *
 * @return UWorld* The world of the outer actor, or nullptr if it does not exist.
 */
UWorld* USAction::GetWorld() const
{
	// Get the outer actor, if it exists
	AActor* Actor = Cast<AActor>(GetOuter());
	if (Actor)
	{
		// Return the world of the outer actor
		return Actor->GetWorld();
	}

	// Return nullptr if the outer actor does not exist
	return nullptr;
}


USActionComponent* USAction::GetOwningComponent() const
{
	return ActionComp;
}


/**
 * This function is called when the replicated data changes.
 * It checks the value of the 'bIsRunning' flag in the replicated data.
 * If it is true, it calls the 'StartAction' function with the 'Instigator' as the parameter.
 * If it is false, it calls the 'StopAction' function with the 'Instigator' as the parameter.
 */
void USAction::OnRep_RepData()
{
	// Check if the action is running
	if (RepData.bIsRunning)
	{
		// Start the action with the instigator
		StartAction(RepData.Instigator);
	}
	else
	{
		// Stop the action with the instigator
		StopAction(RepData.Instigator);
	}
}

/**
 * Checks if the action is currently running.
 *
 * @return True if the action is running, false otherwise.
 */
bool USAction::IsRunning() const
{
	// Return the current running state of the action
	return RepData.bIsRunning;
}


/**
 * Gets the lifetime replicated properties for this action.
 *
 * This function is called by the Unreal Engine to determine which properties
 * should be replicated across the network.
 *
 * @param OutLifetimeProps The array of lifetime properties to populate.
 */
void USAction::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	// Call the parent class's implementation to ensure we don't miss any properties.
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate the action's data, start time, and component.
	DOREPLIFETIME(USAction, RepData);  // Replicate the action's data.
	DOREPLIFETIME(USAction, TimeStarted);  // Replicate the time the action started.
	DOREPLIFETIME(USAction, ActionComp);  // Replicate the action's component.
}