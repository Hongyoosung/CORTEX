// ObjectiveActor.cpp - Durability-based objective implementation

#include "Team/ObjectiveActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Team/FollowerAgentComponent.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

AObjectiveActor::AObjectiveActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create pillar mesh (root component)
	PillarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PillarMesh"));
	RootComponent = PillarMesh;
	PillarMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PillarMesh->SetCollisionObjectType(ECC_WorldStatic);

	// Create capture volume (spherical trigger)
	CaptureVolume = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureVolume"));
	CaptureVolume->SetupAttachment(RootComponent);
	CaptureVolume->SetSphereRadius(CaptureRadius);
	CaptureVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CaptureVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CaptureVolume->SetGenerateOverlapEvents(true);

	// Visual feedback for capture zone (visible in editor and debug builds)
	CaptureVolume->SetVisibility(true);
	CaptureVolume->SetHiddenInGame(false);
	CaptureVolume->ShapeColor = FColor::Green;

	CurrentDurability = MaxDurability;
}

void AObjectiveActor::BeginPlay()
{
	Super::BeginPlay();

	// Bind overlap events
	CaptureVolume->OnComponentBeginOverlap.AddDynamic(this, &AObjectiveActor::OnCaptureVolumeBeginOverlap);
	CaptureVolume->OnComponentEndOverlap.AddDynamic(this, &AObjectiveActor::OnCaptureVolumeEndOverlap);

	// Create dynamic material for visual feedback
	UpdateMaterial();

	// Start durability update timer (10Hz for performance)
	GetWorld()->GetTimerManager().SetTimer(
		DurabilityUpdateTimer,
		this,
		&AObjectiveActor::UpdateDurability,
		0.1f,  // 10Hz update rate
		true   // Loop
	);

	UE_LOG(LogTemp, Log, TEXT("[OBJECTIVE] '%s' initialized: TeamID=%d, Durability=%.1f, Radius=%.1f"),
		*GetName(), OwnerTeamID, CurrentDurability, CaptureRadius);
}

void AObjectiveActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Debug visualization
	if (bShowDebugVisualization)
	{
		FVector Location = GetActorLocation();

		// Draw durability text
		DrawDebugString(
			GetWorld(),
			Location + FVector(0, 0, 200),
			FString::Printf(TEXT("Team %d\nDurability: %.1f%%\nHostiles: %d"),
				OwnerTeamID,
				GetDurabilityPercent() * 100.0f,
				HostileAgentsInVolume.Num()),
			nullptr,
			FColor::White,
			0.0f,
			true
		);

		// Draw capture volume sphere
		DrawDebugSphere(
			GetWorld(),
			Location,
			CaptureRadius,
			16,
			FColor::Green.WithAlpha(50),
			false,
			0.0f,
			0,
			2.0f
		);
	}
}

void AObjectiveActor::OnCaptureVolumeBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this)
	{
		return;
	}

	// Check if agent is hostile
	UFollowerAgentComponent* Agent = OtherActor->FindComponentByClass<UFollowerAgentComponent>();
	if (Agent && IsHostileTo(Agent->GetTeamID()))
	{
		HostileAgentsInVolume.Add(OtherActor);

		UE_LOG(LogTemp, Display, TEXT("[OBJECTIVE] '%s': Hostile agent '%s' (Team %d) entered volume (Total hostiles: %d)"),
			*GetName(),
			*OtherActor->GetName(),
			Agent->GetTeamID(),
			HostileAgentsInVolume.Num());
	}
}

void AObjectiveActor::OnCaptureVolumeEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (!OtherActor)
	{
		return;
	}

	if (HostileAgentsInVolume.Contains(OtherActor))
	{
		HostileAgentsInVolume.Remove(OtherActor);

		UE_LOG(LogTemp, Display, TEXT("[OBJECTIVE] '%s': Hostile agent '%s' left volume (Remaining hostiles: %d)"),
			*GetName(),
			*OtherActor->GetName(),
			HostileAgentsInVolume.Num());
	}
}

void AObjectiveActor::UpdateDurability()
{
	// Remove invalid/destroyed actors from set
	HostileAgentsInVolume.Remove(nullptr);
	for (auto It = HostileAgentsInVolume.CreateIterator(); It; ++It)
	{
		if (!IsValid(*It))
		{
			It.RemoveCurrent();
		}
	}

	int32 HostileCount = HostileAgentsInVolume.Num();

	if (HostileCount > 0)
	{
		// DECLINE: Hostile agents present
		float DeclineRate = HostileCount * DamagePerAgentPerSecond;
		float DeltaTime = 0.1f; // Timer interval
		CurrentDurability -= DeclineRate * DeltaTime;
		CurrentDurability = FMath::Max(CurrentDurability, 0.0f);

		UE_LOG(LogTemp, Verbose, TEXT("[OBJECTIVE] '%s': Durability declining: %.1f%% (-%d agents × %.1f/s)"),
			*GetName(),
			GetDurabilityPercent() * 100.0f,
			HostileCount,
			DamagePerAgentPerSecond);

		// Check for defeat
		if (CurrentDurability <= 0.0f && !bWasDefeated)
		{
			OnDefeat();
			bWasDefeated = true;
		}
	}
	else if (CurrentDurability < MaxDurability)
	{
		// RECOVERY: No hostiles, regenerate durability
		float DeltaTime = 0.1f; // Timer interval
		CurrentDurability += RecoveryPerSecond * DeltaTime;
		CurrentDurability = FMath::Min(CurrentDurability, MaxDurability);

		UE_LOG(LogTemp, Verbose, TEXT("[OBJECTIVE] '%s': Durability recovering: %.1f%% (+%.1f/s)"),
			*GetName(),
			GetDurabilityPercent() * 100.0f,
			RecoveryPerSecond);
	}

	// Update material to reflect durability changes
	UpdateMaterial();
}

void AObjectiveActor::OnDefeat()
{
	UE_LOG(LogTemp, Warning, TEXT("[OBJECTIVE] '%s' DEFEATED! Team %d loses. Broadcasting defeat event..."),
		*GetName(), OwnerTeamID);

	// Broadcast defeat event to game mode for episode reset
	AGameModeBase* GameMode = UGameplayStatics::GetGameMode(this);
	if (GameMode)
	{
		// Game mode should handle episode reset logic
		// TODO: Implement GameMode->OnObjectiveDefeated(OwnerTeamID);
		UE_LOG(LogTemp, Warning, TEXT("[OBJECTIVE] Game mode found, should trigger episode reset"));
	}

	// Visual feedback (disable mesh or change material)
	if (PillarMesh)
	{
		PillarMesh->SetVisibility(false);
	}

	// Stop durability timer
	GetWorld()->GetTimerManager().ClearTimer(DurabilityUpdateTimer);
}

void AObjectiveActor::ResetDurability()
{
	CurrentDurability = MaxDurability;
	bWasDefeated = false;
	HostileAgentsInVolume.Empty();

	// Re-enable mesh if it was hidden
	if (PillarMesh)
	{
		PillarMesh->SetVisibility(true);
	}

	// Restart timer if it was stopped
	if (!GetWorld()->GetTimerManager().IsTimerActive(DurabilityUpdateTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(
			DurabilityUpdateTimer,
			this,
			&AObjectiveActor::UpdateDurability,
			0.1f,
			true
		);
	}

	UpdateMaterial();

	UE_LOG(LogTemp, Log, TEXT("[OBJECTIVE] '%s' reset: Durability=%.1f%%"),
		*GetName(), GetDurabilityPercent() * 100.0f);
}

bool AObjectiveActor::IsAgentInVolume(AActor* Agent) const
{
	if (!Agent || !CaptureVolume)
	{
		return false;
	}

	return CaptureVolume->IsOverlappingActor(Agent);
}

void AObjectiveActor::UpdateMaterial()
{
	if (!PillarMesh)
	{
		return;
	}

	// Create dynamic material instance on first call
	if (!DynamicMaterial)
	{
		UMaterialInterface* BaseMaterial = PillarMesh->GetMaterial(0);
		if (BaseMaterial)
		{
			DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			PillarMesh->SetMaterial(0, DynamicMaterial);
		}
	}

	if (DynamicMaterial)
	{
		// Set team color (Team 0 = Blue, Team 1 = Red)
		FLinearColor TeamColor = (OwnerTeamID == 0) ? FLinearColor::Blue : FLinearColor::Red;
		DynamicMaterial->SetVectorParameterValue(FName("TeamColor"), TeamColor);

		// Set durability-based emission (pulses red when low, bright when healthy)
		float DurabilityPct = GetDurabilityPercent();
		float EmissionStrength = FMath::Lerp(2.0f, 0.2f, DurabilityPct);
		DynamicMaterial->SetScalarParameterValue(FName("EmissionStrength"), EmissionStrength);

		// Optional: Lerp color from team color to red as durability drops
		FLinearColor CurrentColor = FMath::Lerp(FLinearColor::Red, TeamColor, DurabilityPct);
		DynamicMaterial->SetVectorParameterValue(FName("BaseColor"), CurrentColor);
	}
}
