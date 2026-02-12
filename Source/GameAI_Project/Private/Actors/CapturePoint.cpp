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

		// Draw capture zone
		DrawDebugCylinder(
			GetWorld(),
			GetActorLocation() - FVector(0, 0, CaptureHeight / 2),
			GetActorLocation() + FVector(0, 0, CaptureHeight / 2),
			CaptureRadius,
			32,
			IsContested() ? FColor::Yellow : (CapturingTeam == 0 ? FColor::Red : (CapturingTeam == 1 ? FColor::Blue : FColor::White)),
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
	// Store previous progress
	PreviousProgress = CaptureProgress;

	// Determine which team has majority
	DetermineMajorityTeam();

	// Check if contested (both teams present)
	if (IsContested())
	{
		// Contestation: Pause progress, no decay
		CapturingTeam = -1;
		return;
	}

	// No one capturing
	if (CapturingTeam == -1)
	{
		// Progress decay when capturing team withdraws
		if (CaptureProgress > 0.0f)
		{
			CaptureProgress = FMath::Max(0.0f, CaptureProgress - (DecayRate * DeltaTime));
		}
		return;
	}

	// Check if capturing team is trying to capture enemy/neutral point
	int32 OwnerTeamID = GetOwningTeamID();
	if (CapturingTeam == OwnerTeamID)
	{
		// Same team, no capture needed
		CaptureProgress = 0.0f;
		return;
	}

	// Capture in progress
	float ProgressRate = 1.0f / CaptureTime; // Progress per second
	CaptureProgress = FMath::Clamp(CaptureProgress + (ProgressRate * DeltaTime), 0.0f, 1.0f);

	// Check if capture completed
	if (CaptureProgress >= 1.0f)
	{
		CompleteCaptureSequence();
	}

	// Broadcast progress change if significant
	if (FMath::Abs(CaptureProgress - PreviousProgress) > 0.01f)
	{
		OnCaptureProgressChanged.Broadcast(PointID, CaptureProgress);
	}
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

	// Update ownership
	CurrentOwner = NewOwner;
	CaptureProgress = 0.0f;
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
