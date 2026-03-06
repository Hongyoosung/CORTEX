// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/CapturePoint.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "Characters/MocCharacter.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

ACapturePoint::ACapturePoint()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = RootComp;

	CaptureZone = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureZone"));
	CaptureZone->SetupAttachment(RootComponent);
	CaptureZone->SetSphereRadius(CaptureRadius);
	CaptureZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CaptureZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CaptureZone->SetGenerateOverlapEvents(true);

	PointMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PointMesh"));
	PointMesh->SetupAttachment(RootComponent);
	PointMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DebugText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugText"));
	DebugText->SetupAttachment(RootComponent);
	DebugText->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
	DebugText->SetWorldSize(50.0f);
	DebugText->SetHorizontalAlignment(EHTA_Center);
	DebugText->SetVerticalAlignment(EVRTA_TextCenter);

	TeamColorVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TeamColorVFX"));
	TeamColorVFX->SetupAttachment(RootComponent);
	TeamColorVFX->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	TeamColorVFX->bAutoActivate = true;
}

void ACapturePoint::BeginPlay()
{
	Super::BeginPlay();

	CurrentOwner = InitialOwner;

	CaptureZone->OnComponentBeginOverlap.AddDynamic(this, &ACapturePoint::OnCaptureZoneBeginOverlap);
	CaptureZone->OnComponentEndOverlap.AddDynamic(this, &ACapturePoint::OnCaptureZoneEndOverlap);

	if (PointMesh && PointMesh->GetMaterial(0))
	{
		DynamicMaterial = PointMesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	if (TeamColorVFX && TeamColorVFXAsset)
	{
		TeamColorVFX->SetAsset(TeamColorVFXAsset);
		TeamColorVFX->Activate();
	}

	UpdateVisuals();
	UpdateNiagaraColor();
}

void ACapturePoint::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	bJustCaptured = false;
	UpdateCaptureProgress(DeltaTime);
	UpdateVisuals();

	if (bShowDebugInfo)
	{
		FString OwnerStr = CurrentOwner == -1 ? TEXT("NEUTRAL") : FString::Printf(TEXT("TEAM %d"), CurrentOwner);

		FString DebugStr = FString::Printf(
			TEXT("%s\nProgress: %.1f%%\n%s\n"),
			*UEnum::GetValueAsString(PointID),
			CaptureProgress * 100.0f,
			*OwnerStr
		);

		for (const auto& Pair : AgentsInZoneByTeam)
		{
			DebugStr += FString::Printf(TEXT("T%d: %d | "), Pair.Key, Pair.Value.Agents.Num());
		}

		DebugText->SetText(FText::FromString(DebugStr));

		FColor CylinderColor = FColor::White;
		if (IsContested()) CylinderColor = FColor::Yellow;
		else if (CapturingTeam != -1) CylinderColor = (CapturingTeam == 0) ? FColor::Red : FColor::Blue; // Adjust fallback colors as needed
		else CylinderColor = (CurrentOwner == 0) ? FColor::Red : (CurrentOwner == 1) ? FColor::Blue : FColor::White;

		DrawDebugCylinder(
			GetWorld(),
			GetActorLocation() - FVector(0, 0, CaptureHeight / 2),
			GetActorLocation() + FVector(0, 0, CaptureHeight / 2),
			CaptureRadius, 32, CylinderColor, false, -1.0f, 0, 2.0f
		);
	}
}

int32 ACapturePoint::GetTeamID_Implementation() const { return CurrentOwner; }
int32 ACapturePoint::GetEnvID_Implementation() const { return EnvID; }
void ACapturePoint::SetTeamID_Implementation(int32 NewTeamID) { CurrentOwner = NewTeamID; }
void ACapturePoint::SetEnvID_Implementation(int32 NewEnvID) { EnvID = NewEnvID; }

void ACapturePoint::UpdateCaptureProgress(float DeltaTime)
{
	PreviousProgress = CaptureProgress;

	// Clean up empty sets just in case
	for (auto It = AgentsInZoneByTeam.CreateIterator(); It; ++It)
	{
		if (It.Value().Agents.Num() == 0) It.RemoveCurrent();
	}

	const int32 TeamsPresent = AgentsInZoneByTeam.Num();

	if (TeamsPresent > 1)
	{
		// Contested
		CapturingTeam = -1;
		return;
	}

	if (TeamsPresent == 1)
	{
		// Extract the single team attempting capture
		int32 ActiveTeamID = -1;
		int32 AgentCount = 0;
		for (const auto& Pair : AgentsInZoneByTeam)
		{
			ActiveTeamID = Pair.Key;
			AgentCount = Pair.Value.Agents.Num();
		}

		if (CurrentOwner == ActiveTeamID)
		{
			// Owning team is sitting on it; heal progress if it was eroded
			CapturingTeam = -1;
			if (CaptureProgress < 1.0f)
			{
				CaptureProgress = FMath::Min(1.0f, CaptureProgress + DecayRate * DeltaTime);
				if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
				{
					OnCaptureProgressChanged_Delegate.Broadcast(PointID, CaptureProgress);
				}
			}
			return;
		}

		CapturingTeam = ActiveTeamID;
		const float Rate = (1.0f / CaptureTime) * static_cast<float>(AgentCount);

		if (CurrentOwner != -1)
		{
			// Erosion phase (Enemy owns it)
			CaptureProgress = FMath::Max(0.0f, CaptureProgress - Rate * DeltaTime);
			if (CaptureProgress <= 0.0f)
			{
				const int32 PreviousOwner = CurrentOwner;
				CurrentOwner = -1; // Goes Neutral first
				OnPointCaptured_Delegate.Broadcast(PreviousOwner, -1);
				UpdateNiagaraColor();
			}
		}
		else
		{
			// Capture phase (Neutral)
			CaptureProgress = FMath::Min(1.0f, CaptureProgress + Rate * DeltaTime);
			if (CaptureProgress >= 1.0f)
			{
				CompleteCaptureSequence();
			}
		}

		if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
		{
			OnCaptureProgressChanged_Delegate.Broadcast(PointID, CaptureProgress);
		}
		return;
	}

	// No one in zone
	CapturingTeam = -1;

	if (CurrentOwner == -1)
	{
		if (CaptureProgress > 0.0f) // Neutral point decaying
		{
			CaptureProgress = FMath::Max(0.0f, CaptureProgress - DecayRate * DeltaTime);
			if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
			{
				OnCaptureProgressChanged_Delegate.Broadcast(PointID, CaptureProgress);
			}
		}
	}
	else if (CaptureProgress < 1.0f)
	{
		// Owned point regenerating
		CaptureProgress = FMath::Min(1.0f, CaptureProgress + DecayRate * DeltaTime);
		if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
		{
			OnCaptureProgressChanged_Delegate.Broadcast(PointID, CaptureProgress);
		}
	}
}

void ACapturePoint::CompleteCaptureSequence()
{
	int32 PreviousOwner = CurrentOwner;
	CurrentOwner = CapturingTeam;
	CaptureProgress = 1.0f;
	bJustCaptured = true;

	OnPointCaptured_Delegate.Broadcast(PreviousOwner, CurrentOwner);
	UpdateNiagaraColor();

	UE_LOG(LogTemp, Log, TEXT("CapturePoint %s captured! %d -> %d"),
		*UEnum::GetValueAsString(PointID), PreviousOwner, CurrentOwner);
}

void ACapturePoint::UpdateVisuals()
{
	if (!DynamicMaterial) return;

	// Note: For fully dynamic colors, consider pulling FTeamConfiguration from MatchManager via an Interface or Subsystem.
	FLinearColor TeamColor = FLinearColor::Gray;
	if (CurrentOwner == 0) TeamColor = FLinearColor::Red;
	else if (CurrentOwner == 1) TeamColor = FLinearColor::Blue;
	else if (CurrentOwner > 1) TeamColor = FLinearColor::Green;

	DynamicMaterial->SetVectorParameterValue(FName("TeamColor"), TeamColor);
	DynamicMaterial->SetScalarParameterValue(FName("CaptureProgress"), CaptureProgress);
	DynamicMaterial->SetScalarParameterValue(FName("IsContested"), IsContested() ? 1.0f : 0.0f);
}

void ACapturePoint::UpdateNiagaraColor()
{
	if (!TeamColorVFX) return;

	FLinearColor TeamColor = FLinearColor::Gray;
	if (CurrentOwner == 0) TeamColor = FLinearColor::Red;
	else if (CurrentOwner == 1) TeamColor = FLinearColor::Blue;
	else if (CurrentOwner > 1) TeamColor = FLinearColor::Green;

	TeamColorVFX->SetVariableLinearColor(VFXColorParameterName, TeamColor);
}

bool ACapturePoint::IsContested() const
{
	return AgentsInZoneByTeam.Num() > 1;
}

void ACapturePoint::ResetPoint()
{
	CurrentOwner = InitialOwner;
	CaptureProgress = 0.0f;
	CapturingTeam = -1;
	bJustCaptured = false;
	AgentsInZoneByTeam.Empty();
	UpdateVisuals();
	UpdateNiagaraColor();
}

void ACapturePoint::SetOwnership(int32 NewOwnerTeamID)
{
	int32 PreviousOwner = CurrentOwner;
	CurrentOwner = NewOwnerTeamID;
	CaptureProgress = 0.0f;
	OnPointCaptured_Delegate.Broadcast(PreviousOwner, CurrentOwner);
	UpdateVisuals();
	UpdateNiagaraColor();
}

int32 ACapturePoint::GetTeamCountInZone(int32 TeamID) const
{
	if (const FTeamAgentSet* TeamSet = AgentsInZoneByTeam.Find(TeamID))
	{
		return TeamSet->Agents.Num();
	}
	return 0;
}

TArray<AActor*> ACapturePoint::GetAgentsInZone() const
{
	TArray<AActor*> AllAgents;
	for (const auto& Pair : AgentsInZoneByTeam)
	{
		AllAgents.Append(Pair.Value.Agents.Array());
	}
	return AllAgents;
}

void ACapturePoint::OnCaptureZoneBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || !OtherActor->IsA(ACharacter::StaticClass())) return;

	if (AMocCharacter* MocChar = Cast<AMocCharacter>(OtherActor))
	{
		if (MocChar->GetEnvID_Implementation() != EnvID) return;
	}

	int32 TeamID = GetAgentTeamID(OtherActor);
	if (TeamID != -1)
	{
		AgentsInZoneByTeam.FindOrAdd(TeamID).Agents.Add(OtherActor);
	}
}

void ACapturePoint::OnCaptureZoneEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor) return;

	if (AMocCharacter* MocChar = Cast<AMocCharacter>(OtherActor))
	{
		if (MocChar->GetEnvID_Implementation() != EnvID) return;
	}

	int32 TeamID = GetAgentTeamID(OtherActor);
	if (TeamID != -1 && AgentsInZoneByTeam.Contains(TeamID))
	{
		AgentsInZoneByTeam[TeamID].Agents.Remove(OtherActor);
		if (AgentsInZoneByTeam[TeamID].Agents.Num() == 0)
		{
			AgentsInZoneByTeam.Remove(TeamID);
		}
	}
}

int32 ACapturePoint::GetAgentTeamID(AActor* Agent) const
{
	if (Agent && Agent->Implements<UMocTeamInterface>())
	{
		return IMocTeamInterface::Execute_GetTeamID(Agent);
	}
	return -1;
}