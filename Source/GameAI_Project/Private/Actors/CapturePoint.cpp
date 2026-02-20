// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/CapturePoint.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

ACapturePoint::ACapturePoint()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create root component
	RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = RootComp;

	// Create capture zone (cylindrical trigger)
	CaptureZone = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureZone"));
	CaptureZone->SetupAttachment(RootComponent);
	CaptureZone->SetSphereRadius(CaptureRadius);
	CaptureZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CaptureZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CaptureZone->SetGenerateOverlapEvents(true);

	// Create visual mesh
	PointMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PointMesh"));
	PointMesh->SetupAttachment(RootComponent);
	PointMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Create debug text
	DebugText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugText"));
	DebugText->SetupAttachment(RootComponent);
	DebugText->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
	DebugText->SetWorldSize(50.0f);
	DebugText->SetHorizontalAlignment(EHTA_Center);
	DebugText->SetVerticalAlignment(EVRTA_TextCenter);

	// Create Niagara VFX component
	TeamColorVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TeamColorVFX"));
	TeamColorVFX->SetupAttachment(RootComponent);
	TeamColorVFX->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	TeamColorVFX->bAutoActivate = true;
}

void ACapturePoint::BeginPlay()
{
	Super::BeginPlay();

	// Set initial ownership
	CurrentOwner = InitialOwner;

	// Bind overlap events
	CaptureZone->OnComponentBeginOverlap.AddDynamic(this, &ACapturePoint::OnCaptureZoneBeginOverlap);
	CaptureZone->OnComponentEndOverlap.AddDynamic(this, &ACapturePoint::OnCaptureZoneEndOverlap);

	// Create dynamic material for visual feedback
	if (PointMesh && PointMesh->GetMaterial(0))
	{
		DynamicMaterial = PointMesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	// Setup Niagara VFX
	if (TeamColorVFX && TeamColorVFXAsset)
	{
		TeamColorVFX->SetAsset(TeamColorVFXAsset);
		TeamColorVFX->Activate();
	}

	// Update initial visuals
	UpdateVisuals();
	UpdateNiagaraColor();
}

void ACapturePoint::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Clear just-captured flag
	bJustCaptured = false;

	// Update capture progress
	UpdateCaptureProgress(DeltaTime);

	// Update visuals
	UpdateVisuals();

	// Debug visualization
	if (bShowDebugInfo)
	{
		FString OwnerStr;
		switch (CurrentOwner)
		{
		case ECapturePointOwnership::RedTeam: OwnerStr = TEXT("RED"); break;
		case ECapturePointOwnership::BlueTeam: OwnerStr = TEXT("BLUE"); break;
		default: OwnerStr = TEXT("NEUTRAL"); break;
		}

		FString StatusStr = FString::Printf(
			TEXT("%s\nProgress: %.1f%%\nRed: %d | Blue: %d\n%s"),
			*UEnum::GetValueAsString(PointID),
			CaptureProgress * 100.0f,
			RedTeamAgents.Num(),
			BlueTeamAgents.Num(),
			*OwnerStr
		);

		DebugText->SetText(FText::FromString(StatusStr));

		// Draw capture zone — color reflects owning team when idle, active team when capturing
		FColor CylinderColor;
		if (IsContested())
		{
			CylinderColor = FColor::Yellow;
		}
		else if (CapturingTeam == 0)
		{
			CylinderColor = FColor::Red;
		}
		else if (CapturingTeam == 1)
		{
			CylinderColor = FColor::Blue;
		}
		else
		{
			switch (CurrentOwner)
			{
			case ECapturePointOwnership::RedTeam:  CylinderColor = FColor::Red;   break;
			case ECapturePointOwnership::BlueTeam: CylinderColor = FColor::Blue;  break;
			default:                               CylinderColor = FColor::White; break;
			}
		}

		DrawDebugCylinder(
			GetWorld(),
			GetActorLocation() - FVector(0, 0, CaptureHeight / 2),
			GetActorLocation() + FVector(0, 0, CaptureHeight / 2),
			CaptureRadius,
			32,
			CylinderColor,
			false,
			-1.0f,
			0,
			2.0f
		);
	}
}


int32 ACapturePoint::GetTeamID_Implementation() const
{
    // Enum -> Int 변환
    switch (CurrentOwner)
    {
        case ECapturePointOwnership::RedTeam: return 0;
        case ECapturePointOwnership::BlueTeam: return 1;
        default: return -1;
    }
}


void ACapturePoint::UpdateCaptureProgress(float DeltaTime)
{
	PreviousProgress = CaptureProgress;

	const int32 RedCount = RedTeamAgents.Num();
	const int32 BlueCount = BlueTeamAgents.Num();
	const int32 OwnerTeamID = GetOwningTeamID();

	// Both teams present: pause all progress
	if (RedCount > 0 && BlueCount > 0)
	{
		CapturingTeam = -1;
		return;
	}

	// Red team only in zone
	if (RedCount > 0)
	{
		if (OwnerTeamID == 0)
		{
			// Red owns it — no action needed
			CapturingTeam = -1;
			return;
		}

		CapturingTeam = 0;
		const float Rate = (1.0f / CaptureTime) * static_cast<float>(RedCount);

		if (OwnerTeamID == 1)
		{
			// Blue owns it — Red erodes ownership proportional to agent count
			CaptureProgress = FMath::Max(0.0f, CaptureProgress - Rate * DeltaTime);
			if (CaptureProgress <= 0.0f)
			{
				const ECapturePointOwnership PreviousOwner = CurrentOwner;
				CurrentOwner = ECapturePointOwnership::Neutral;
				OnPointCaptured.Broadcast(PointID, PreviousOwner, ECapturePointOwnership::Neutral);
				UpdateNiagaraColor();
			}
		}
		else
		{
			// Neutral — Red captures, rate proportional to agent count
			CaptureProgress = FMath::Min(1.0f, CaptureProgress + Rate * DeltaTime);
			if (CaptureProgress >= 1.0f)
			{
				CompleteCaptureSequence();
			}
		}

		if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
		{
			OnCaptureProgressChanged.Broadcast(PointID, CaptureProgress);
		}
		return;
	}

	// Blue team only in zone
	if (BlueCount > 0)
	{
		if (OwnerTeamID == 1)
		{
			// Blue owns it — no action needed
			CapturingTeam = -1;
			return;
		}

		CapturingTeam = 1;
		const float Rate = (1.0f / CaptureTime) * static_cast<float>(BlueCount);

		if (OwnerTeamID == 0)
		{
			// Red owns it — Blue erodes ownership proportional to agent count
			CaptureProgress = FMath::Max(0.0f, CaptureProgress - Rate * DeltaTime);
			if (CaptureProgress <= 0.0f)
			{
				const ECapturePointOwnership PreviousOwner = CurrentOwner;
				CurrentOwner = ECapturePointOwnership::Neutral;
				OnPointCaptured.Broadcast(PointID, PreviousOwner, ECapturePointOwnership::Neutral);
				UpdateNiagaraColor();
			}
		}
		else
		{
			// Neutral — Blue captures, rate proportional to agent count
			CaptureProgress = FMath::Min(1.0f, CaptureProgress + Rate * DeltaTime);
			if (CaptureProgress >= 1.0f)
			{
				CompleteCaptureSequence();
			}
		}

		if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
		{
			OnCaptureProgressChanged.Broadcast(PointID, CaptureProgress);
		}
		return;
	}

	// No one in zone
	CapturingTeam = -1;

	if (OwnerTeamID == -1)
	{
		// Neutral point with partial progress: decay toward 0
		if (CaptureProgress > 0.0f)
		{
			CaptureProgress = FMath::Max(0.0f, CaptureProgress - DecayRate * DeltaTime);

			if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
			{
				OnCaptureProgressChanged.Broadcast(PointID, CaptureProgress);
			}
		}
	}
	else if (CaptureProgress < 1.0f)
	{
		// Owned point where the enemy had eroded progress: restore toward 1.0
		CaptureProgress = FMath::Min(1.0f, CaptureProgress + DecayRate * DeltaTime);

		if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
		{
			OnCaptureProgressChanged.Broadcast(PointID, CaptureProgress);
		}
	}
	// Owned point at full progress (1.0) with no one present: no change
}

void ACapturePoint::DetermineMajorityTeam()
{
	int32 RedCount = RedTeamAgents.Num();
	int32 BlueCount = BlueTeamAgents.Num();

	// Both teams present = contested
	if (RedCount > 0 && BlueCount > 0)
	{
		CapturingTeam = -1;
		return;
	}

	// Red has majority
	if (RedCount > 0)
	{
		CapturingTeam = 0;
		return;
	}

	// Blue has majority
	if (BlueCount > 0)
	{
		CapturingTeam = 1;
		return;
	}

	// No one present
	CapturingTeam = -1;
}

void ACapturePoint::CompleteCaptureSequence()
{
	ECapturePointOwnership PreviousOwner = CurrentOwner;
	ECapturePointOwnership NewOwner;

	// Set new owner based on capturing team
	if (CapturingTeam == 0)
	{
		NewOwner = ECapturePointOwnership::RedTeam;
	}
	else if (CapturingTeam == 1)
	{
		NewOwner = ECapturePointOwnership::BlueTeam;
	}
	else
	{
		return; // Should not happen
	}

	// Update ownership — keep progress at 1.0 so visuals stay at full capture
	CurrentOwner = NewOwner;
	CaptureProgress = 1.0f;
	bJustCaptured = true;

	// Broadcast capture event
	OnPointCaptured.Broadcast(PointID, PreviousOwner, NewOwner);

	// Update Niagara color for new owner
	UpdateNiagaraColor();

	UE_LOG(LogTemp, Log, TEXT("CapturePoint %s captured! %s -> %s"),
		*UEnum::GetValueAsString(PointID),
		*UEnum::GetValueAsString(PreviousOwner),
		*UEnum::GetValueAsString(NewOwner)
	);
}

void ACapturePoint::UpdateVisuals()
{
	if (!DynamicMaterial)
	{
		return;
	}

	// Set material color based on ownership
	FLinearColor TeamColor;
	switch (CurrentOwner)
	{
	case ECapturePointOwnership::RedTeam:
		TeamColor = FLinearColor::Red;
		break;
	case ECapturePointOwnership::BlueTeam:
		TeamColor = FLinearColor::Blue;
		break;
	default:
		TeamColor = FLinearColor::Gray;
		break;
	}

	// Set color parameter
	DynamicMaterial->SetVectorParameterValue(FName("TeamColor"), TeamColor);

	// Set capture progress parameter (for visual effects)
	DynamicMaterial->SetScalarParameterValue(FName("CaptureProgress"), CaptureProgress);

	// Set contested parameter
	DynamicMaterial->SetScalarParameterValue(FName("IsContested"), IsContested() ? 1.0f : 0.0f);
}

void ACapturePoint::UpdateNiagaraColor()
{
	if (!TeamColorVFX)
	{
		return;
	}

	// Determine color based on current ownership
	FLinearColor TeamColor;
	switch (CurrentOwner)
	{
	case ECapturePointOwnership::RedTeam:
		TeamColor = FLinearColor::Red;
		break;
	case ECapturePointOwnership::BlueTeam:
		TeamColor = FLinearColor::Blue;
		break;
	default:
		TeamColor = FLinearColor::Gray;
		break;
	}

	// Update Niagara color parameter
	TeamColorVFX->SetVariableLinearColor(VFXColorParameterName, TeamColor);

	UE_LOG(LogTemp, Verbose, TEXT("CapturePoint %s VFX color updated to: R=%.2f, G=%.2f, B=%.2f"),
		*UEnum::GetValueAsString(PointID), TeamColor.R, TeamColor.G, TeamColor.B);
}

bool ACapturePoint::IsContested() const
{
	return RedTeamAgents.Num() > 0 && BlueTeamAgents.Num() > 0;
}

int32 ACapturePoint::GetCapturingTeam() const
{
	return CapturingTeam;
}

int32 ACapturePoint::GetOwningTeamID() const
{
	switch (CurrentOwner)
	{
	case ECapturePointOwnership::RedTeam: return 0;
	case ECapturePointOwnership::BlueTeam: return 1;
	default: return -1; // Neutral
	}
}

void ACapturePoint::ResetPoint()
{
	CurrentOwner = InitialOwner;
	CaptureProgress = 0.0f;
	CapturingTeam = -1;
	bJustCaptured = false;
	RedTeamAgents.Empty();
	BlueTeamAgents.Empty();
	UpdateVisuals();
	UpdateNiagaraColor();
}

void ACapturePoint::SetOwnership(ECapturePointOwnership NewOwner)
{
	ECapturePointOwnership PreviousOwner = CurrentOwner;
	CurrentOwner = NewOwner;
	CaptureProgress = 0.0f;
	OnPointCaptured.Broadcast(PointID, PreviousOwner, NewOwner);
	UpdateVisuals();
	UpdateNiagaraColor();
}

TArray<AActor*> ACapturePoint::GetAgentsInZone() const
{
	TArray<AActor*> AllAgents;
	AllAgents.Append(RedTeamAgents.Array());
	AllAgents.Append(BlueTeamAgents.Array());
	return AllAgents;
}

void ACapturePoint::OnCaptureZoneBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	// Only track characters (agents)
	if (!OtherActor || !OtherActor->IsA(ACharacter::StaticClass()))
	{
		return;
	}

	// Get team ID
	int32 TeamID = GetAgentTeamID(OtherActor);

	// Add to appropriate team set
	if (TeamID == 0)
	{
		RedTeamAgents.Add(OtherActor);
	}
	else if (TeamID == 1)
	{
		BlueTeamAgents.Add(OtherActor);
	}
}

void ACapturePoint::OnCaptureZoneEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (!OtherActor)
	{
		return;
	}

	// Remove from both sets (safe if not present)
	RedTeamAgents.Remove(OtherActor);
	BlueTeamAgents.Remove(OtherActor);
}

int32 ACapturePoint::GetAgentTeamID(AActor* Agent) const
{
	if (!Agent)
	{
		return -1;
	}

	// Use team interface if available
	if (Agent->Implements<UMocTeamInterface>())
	{
		return IMocTeamInterface::Execute_GetTeamID(Agent);
	}

	return -1;
}
