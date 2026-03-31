// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/DECapturePoint.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "Characters/DEAgent.h"
#include "Team/DEMatchManager.h"
#include "Data/DETeamData.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Player/DESpectatorController.h"

ADECapturePoint::ADECapturePoint()
{
	PrimaryActorTick.bCanEverTick = false;

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

	// Sky laser — sits at zone center, points upward
	TeamColorVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TeamColorVFX"));
	TeamColorVFX->SetupAttachment(RootComponent);
	TeamColorVFX->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	TeamColorVFX->bAutoActivate = true;

	// Donut ring — centered at origin, driven by CaptureProgress / TeamColor params
	DonutProgressVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("DonutProgressVFX"));
	DonutProgressVFX->SetupAttachment(RootComponent);
	DonutProgressVFX->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	DonutProgressVFX->bAutoActivate = true;
}

void ADECapturePoint::BeginPlay()
{
	Super::BeginPlay();

	CurrentOwner = InitialOwner;

	CaptureZone->OnComponentBeginOverlap.AddDynamic(this, &ADECapturePoint::OnCaptureZoneBeginOverlap);
	CaptureZone->OnComponentEndOverlap.AddDynamic(this, &ADECapturePoint::OnCaptureZoneEndOverlap);

	if (PointMesh && PointMesh->GetMaterial(0))
	{
		DynamicMaterial = PointMesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	if (TeamColorVFX && TeamColorVFXAsset)
	{
		TeamColorVFX->SetAsset(TeamColorVFXAsset);
		TeamColorVFX->Activate();
	}

	if (DonutProgressVFX && DonutProgressVFXAsset)
	{
		DonutProgressVFX->SetAsset(DonutProgressVFXAsset);
		DonutProgressVFX->Activate();
	}

	UpdateVisuals();
	UpdateNiagaraColor();
	UpdateDonutVFX();

	// Start timer-based capture update at 10Hz (replaces per-frame Tick)
	constexpr float CaptureUpdateInterval = 0.1f;
	GetWorld()->GetTimerManager().SetTimer(
		CaptureUpdateTimerHandle, this, &ADECapturePoint::CaptureUpdateTick,
		CaptureUpdateInterval, true);
}

void ADECapturePoint::CaptureUpdateTick()
{
	constexpr float DeltaTime = 0.1f;

	bJustCaptured = false;
	UpdateCaptureProgress(DeltaTime);
	UpdateVisuals();
	UpdateDonutVFX();

	if (bShowDebugInfo)
	{
		FColor CylinderColor = FColor::White;
		if (IsContested()) CylinderColor = FColor::Yellow;
		else if (CapturingTeam != -1) CylinderColor = GetTeamColor(CapturingTeam).ToFColor(true);
		else CylinderColor = GetTeamColor(CurrentOwner).ToFColor(true);

		// Lifetime slightly exceeds the 0.1s tick interval so the cylinder
		// stays visible continuously rather than blinking every tick.
		// Height halved, shifted down by 90 units.
		DrawDebugCylinder(
			GetWorld(),
			GetActorLocation() + FVector(0, 0, (-CaptureHeight / 8.0f) - 140.0f),
			GetActorLocation() + FVector(0, 0, (CaptureHeight / 4.0f) - 90.0f),
			CaptureRadius, 32, CylinderColor, false, 0.12f, 0, 2.0f
		);
	}
}

int32 ADECapturePoint::GetTeamID_Implementation() const { return CurrentOwner; }
int32 ADECapturePoint::GetEnvID_Implementation() const { return EnvID; }
void ADECapturePoint::SetTeamID_Implementation(int32 NewTeamID) { CurrentOwner = NewTeamID; }
void ADECapturePoint::SetEnvID_Implementation(int32 NewEnvID) { EnvID = NewEnvID; }

void ADECapturePoint::UpdateCaptureProgress(float DeltaTime)
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

		return;
	}

	// No one in zone
	CapturingTeam = -1;

	if (CurrentOwner == -1)
	{
		if (CaptureProgress > 0.0f) // Neutral point decaying
		{
			CaptureProgress = FMath::Max(0.0f, CaptureProgress - DecayRate * DeltaTime);

		}
	}
	else if (CaptureProgress < 1.0f)
	{
		// Owned point regenerating
		CaptureProgress = FMath::Min(1.0f, CaptureProgress + DecayRate * DeltaTime);

	}
}

void ADECapturePoint::CompleteCaptureSequence()
{
	int32 PreviousOwner = CurrentOwner;
	CurrentOwner = CapturingTeam;
	CaptureProgress = 1.0f;
	bJustCaptured = true;

	OnPointCaptured_Delegate.Broadcast(PreviousOwner, CurrentOwner);
	UpdateNiagaraColor();
	UpdateDonutVFX();

	UE_LOG(LogTemp, Log, TEXT("DECapturePoint %s captured! %d -> %d"),
		*UEnum::GetValueAsString(PointID), PreviousOwner, CurrentOwner);
}

void ADECapturePoint::UpdateVisuals()
{
	if (!DynamicMaterial) return;

	static const FName ParamTeamColor(TEXT("TeamColor"));
	static const FName ParamCaptureProgress(TEXT("CaptureProgress"));
	static const FName ParamIsContested(TEXT("IsContested"));

	const FLinearColor TeamCol = GetTeamColor(CurrentOwner);

	DynamicMaterial->SetVectorParameterValue(ParamTeamColor, TeamCol);
	DynamicMaterial->SetScalarParameterValue(ParamCaptureProgress, CaptureProgress);
	DynamicMaterial->SetScalarParameterValue(ParamIsContested, IsContested() ? 1.0f : 0.0f);
}

void ADECapturePoint::UpdateNiagaraColor()
{
	const FLinearColor Color = GetTeamColor(CurrentOwner);

	if (TeamColorVFX)
	{
		TeamColorVFX->SetVariableLinearColor(VFXColorParameterName, Color);
	}

	if (DonutProgressVFX)
	{
		DonutProgressVFX->SetVariableLinearColor(VFXColorParameterName, Color);
	}
}

void ADECapturePoint::UpdateDonutVFX()
{
	if (!DonutProgressVFX) return;

	// Use capturing team color when actively being captured, otherwise owner color
	const int32 DisplayTeam = (CapturingTeam != -1) ? CapturingTeam : CurrentOwner;
	DonutProgressVFX->SetVariableLinearColor(VFXColorParameterName, GetTeamColor(DisplayTeam));
	DonutProgressVFX->SetVariableFloat(VFXProgressParameterName, CaptureProgress);
}

bool ADECapturePoint::IsContested() const
{
	return AgentsInZoneByTeam.Num() > 1;
}

void ADECapturePoint::ResetPoint()
{
	CurrentOwner = InitialOwner;
	CaptureProgress = 0.0f;
	CapturingTeam = -1;
	bJustCaptured = false;
	AgentsInZoneByTeam.Empty();
	UpdateVisuals();
	UpdateNiagaraColor();
	UpdateDonutVFX();
}

void ADECapturePoint::SetOwnership(int32 NewOwnerTeamID)
{
	int32 PreviousOwner = CurrentOwner;
	CurrentOwner = NewOwnerTeamID;
	CaptureProgress = 0.0f;
	OnPointCaptured_Delegate.Broadcast(PreviousOwner, CurrentOwner);
	UpdateVisuals();
	UpdateNiagaraColor();
	UpdateDonutVFX();
}

void ADECapturePoint::SetMatchManager(ADEMatchManager* InMatchManager)
{
	OwnerMatchManager = TWeakObjectPtr<ADEMatchManager>(InMatchManager);
}

int32 ADECapturePoint::GetTeamCountInZone(int32 TeamID) const
{
	if (const FTeamAgentSet* TeamSet = AgentsInZoneByTeam.Find(TeamID))
	{
		return TeamSet->Agents.Num();
	}
	return 0;
}

TArray<AActor*> ADECapturePoint::GetAgentsInZone() const
{
	TArray<AActor*> AllAgents;
	for (const auto& Pair : AgentsInZoneByTeam)
	{
		AllAgents.Append(Pair.Value.Agents.Array());
	}
	return AllAgents;
}

void ADECapturePoint::OnCaptureZoneBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || !OtherActor->IsA(ACharacter::StaticClass())) return;

	if (ADEAgent* DEChar = Cast<ADEAgent>(OtherActor))
	{
		if (DEChar->GetEnvID_Implementation() != EnvID) return;
	}

	int32 TeamID = GetAgentTeamID(OtherActor);
	if (TeamID != -1)
	{
		AgentsInZoneByTeam.FindOrAdd(TeamID).Agents.Add(OtherActor);
	}
}

void ADECapturePoint::OnCaptureZoneEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor) return;

	if (ADEAgent* DEChar = Cast<ADEAgent>(OtherActor))
	{
		if (DEChar->GetEnvID_Implementation() != EnvID) return;
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

FLinearColor ADECapturePoint::GetTeamColor(int32 TeamID) const
{
	if (TeamID == -1 || !OwnerMatchManager.IsValid()) return FLinearColor::Gray;
	const FDETeamConfiguration Config = OwnerMatchManager->GetTeamConfiguration(TeamID);
	return Config.GetTeamColor();
}

int32 ADECapturePoint::GetAgentTeamID(AActor* Agent) const
{
	if (Agent && Agent->Implements<UDETeamInterface>())
	{
		return IDETeamInterface::Execute_GetTeamID(Agent);
	}
	return -1;
}

