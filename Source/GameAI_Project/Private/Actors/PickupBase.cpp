// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/PickupBase.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "Characters/MocCharacter.h"

APickupBase::APickupBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create root component
	RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = RootComp;

	// Create mesh
	PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
	PickupMesh->SetupAttachment(RootComponent);
	PickupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupMesh->SetGenerateOverlapEvents(false);

	// Enable custom depth for through-wall visibility
	PickupMesh->SetRenderCustomDepth(true);
	PickupMesh->SetCustomDepthStencilValue(252); // Custom stencil value for pickups

	// Create collection sphere
	CollectionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollectionSphere"));
	CollectionSphere->SetupAttachment(RootComponent);
	CollectionSphere->SetSphereRadius(CollectionRadius);
	CollectionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollectionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollectionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollectionSphere->SetGenerateOverlapEvents(true);

	// Create visual effect
	VisualEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("VisualEffect"));
	VisualEffect->SetupAttachment(RootComponent);
	VisualEffect->bAutoActivate = true;
}

void APickupBase::BeginPlay()
{
	Super::BeginPlay();

	// Bind overlap event
	CollectionSphere->OnComponentBeginOverlap.AddDynamic(this, &APickupBase::OnCollectionSphereBeginOverlap);

	// Create dynamic material
	if (PickupMesh && PickupMesh->GetMaterial(0))
	{
		DynamicMaterial = PickupMesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	// Set initial state
	bIsAvailable = true;
	UpdateVisuals();
}

void APickupBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Rotate pickup for visual appeal
	if (bIsAvailable)
	{
		FRotator NewRotation = GetActorRotation();
		NewRotation.Yaw += DeltaTime * 60.0f; // 60 degrees per second
		SetActorRotation(NewRotation);
	}

	// Debug visualization
	if (bShowDebugInfo)
	{
		FColor DebugColor = bIsAvailable ? FColor::Green : FColor::Red;
		FString DebugText = FString::Printf(
			TEXT("%s\n%s\nRespawn: %.1fs"),
			*UEnum::GetValueAsString(PickupType),
			bIsAvailable ? TEXT("Available") : TEXT("Collected"),
			GetRespawnTimeRemaining()
		);

		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 100), DebugText, nullptr, DebugColor, 0.0f, true);
		DrawDebugSphere(GetWorld(), GetActorLocation(), CollectionRadius, 16, DebugColor, false, -1.0f, 0, 2.0f);
	}
}

bool APickupBase::TryCollect(AActor* Collector)
{
	// Check if available
	if (!bIsAvailable)
	{
		return false;
	}

	// Check if collector can use this pickup
	if (!CanCollect(Collector))
	{
		return false;
	}

	// Apply effect
	ApplyPickupEffect(Collector);

	// Mark as collected
	OnCollected(Collector);

	return true;
}

void APickupBase::ApplyPickupEffect_Implementation(AActor* Collector)
{
	// Base implementation does nothing
	// Override in subclasses
}

bool APickupBase::CanCollect_Implementation(AActor* Collector) const
{
	// Base implementation allows all collectors
	// Override in subclasses for specific rules (e.g., only if health < max)
	return Collector != nullptr;
}

void APickupBase::OnCollected(AActor* Collector)
{
	bIsAvailable = false;
	UpdateVisuals();

	// Broadcast event
	OnPickupCollected.Broadcast(Collector, PickupType);

	// Log collection
	UE_LOG(LogTemp, Log, TEXT("Pickup %s collected by %s"), *GetName(), *Collector->GetName());

	// Start respawn timer if enabled
	if (bRespawns)
	{
		StartRespawnTimer();
	}
}

void APickupBase::StartRespawnTimer()
{
	if (RespawnTime <= 0.0f)
	{
		return;
	}

	// Store completion time
	RespawnCompleteTime = GetWorld()->GetTimeSeconds() + RespawnTime;

	// Set timer
	GetWorld()->GetTimerManager().SetTimer(
		RespawnTimerHandle,
		this,
		&APickupBase::OnRespawnTimerComplete,
		RespawnTime,
		false
	);
}

void APickupBase::OnRespawnTimerComplete()
{
	bIsAvailable = true;
	RespawnCompleteTime = 0.0f;
	UpdateVisuals();

	// Broadcast respawn event
	OnPickupRespawned.Broadcast(PickupType);

	UE_LOG(LogTemp, Log, TEXT("Pickup %s respawned"), *GetName());
}

float APickupBase::GetRespawnTimeRemaining() const
{
	if (bIsAvailable)
	{
		return 0.0f;
	}

	float CurrentTime = GetWorld()->GetTimeSeconds();
	return FMath::Max(0.0f, RespawnCompleteTime - CurrentTime);
}

void APickupBase::ForceRespawn()
{
	// Clear timer if active
	if (RespawnTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}

	// Respawn immediately
	OnRespawnTimerComplete();
}

void APickupBase::Reset()
{
	// Clear any active respawn timer
	if (RespawnTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(RespawnTimerHandle);
		RespawnTimerHandle.Invalidate();
	}

	// Reset to available state
	bIsAvailable = true;
	RespawnCompleteTime = 0.0f;

	// Update visuals
	UpdateVisuals();

	UE_LOG(LogTemp, Verbose, TEXT("[PickupBase] %s reset to available"), *GetName());
}

void APickupBase::UpdateVisuals()
{
	// Update mesh visibility
	PickupMesh->SetVisibility(bIsAvailable);

	// Disable collection sphere when consumed so agents cannot overlap-trigger it
	CollectionSphere->SetCollisionEnabled(bIsAvailable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);

	// Update particle effect
	if (VisualEffect)
	{
		if (bIsAvailable)
		{
			VisualEffect->SetVisibility(true);
			VisualEffect->Activate(true);
		}
		else
		{
			VisualEffect->DeactivateImmediate();
			VisualEffect->SetVisibility(false);
		}
	}

	// Update material parameters
	if (DynamicMaterial)
	{
		DynamicMaterial->SetScalarParameterValue(FName("IsAvailable"), bIsAvailable ? 1.0f : 0.0f);
		DynamicMaterial->SetScalarParameterValue(FName("RespawnProgress"),
			bIsAvailable ? 1.0f : (1.0f - (GetRespawnTimeRemaining() / RespawnTime)));
	}

	// Update through-wall visibility
	PickupMesh->SetRenderCustomDepth(bIsAvailable && bVisibleThroughWalls);
}

void APickupBase::OnCollectionSphereBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	// Only allow characters to collect
	if (!OtherActor || !OtherActor->IsA(ACharacter::StaticClass()))
	{
		return;
	}

	// EnvID isolation: reject agents from different environments
	if (AMocCharacter* MocChar = Cast<AMocCharacter>(OtherActor))
	{
		if (MocChar->GetEnvID_Implementation() != EnvID)
		{
			return;
		}
	}

	// Try to collect
	TryCollect(OtherActor);
}
