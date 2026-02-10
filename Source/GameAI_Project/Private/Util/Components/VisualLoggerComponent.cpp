// VisualLoggerComponent.cpp
// Phase 3: Debug visualization (extracted from TeamLeaderComponent and FollowerAgentComponent)

#include "Util/Components/VisualLoggerComponent.h"
#include "DrawDebugHelpers.h"
#include "Types/StrategyTypes.h"


UVisualLoggerComponent::UVisualLoggerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UVisualLoggerComponent::IsDebugDrawingEnabled() const
{
#if UE_BUILD_SHIPPING
	return false;  // Never draw debug in shipping builds
#else
	return bEnableDebugDrawing;
#endif
}

FLinearColor UVisualLoggerComponent::GetStrategyColor(EStrategyType Strategy) const
{
	switch (Strategy)
	{
	case EStrategyType::Assault:
		return FLinearColor::Red;
	case EStrategyType::Defend:
		return FLinearColor::Blue;
	case EStrategyType::Support:
		return FLinearColor::Green;
	default:
		return FLinearColor::White;
	}
}

void UVisualLoggerComponent::DrawCombatState(
	const FVector& Location,
	AActor* Target,
	float DistanceToTarget,
	bool bIsAiming,
	bool bIsFiring
)
{
	if (!IsDebugDrawingEnabled() || !bDrawCombatInfo)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return;
	}

	// Combat status text
	FString CombatText = FString::Printf(
		TEXT("Target: %.0fm\n%s%s"),
		DistanceToTarget / 100.0f,  // Convert to meters
		bIsAiming ? TEXT("AIM ") : TEXT(""),
		bIsFiring ? TEXT("FIRE") : TEXT("")
	);

	FColor CombatColor = bIsFiring ? FColor::Orange : (bIsAiming ? FColor::Yellow : FColor::White);

	DrawDebugString(
		World,
		Location + FVector(0, 0, TextHeightOffset + 60.0f),
		CombatText,
		nullptr,
		CombatColor,
		0.0f,
		true
	);

	// Draw line to target
	DrawLineToTarget(Location, Target->GetActorLocation(), FLinearColor(1.0f, 0.5f, 0.0f));  // Orange
}

void UVisualLoggerComponent::DrawLineToTarget(
	const FVector& Start,
	const FVector& End,
	FLinearColor Color,
	bool bDrawArrow
)
{
	if (!IsDebugDrawingEnabled())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bDrawArrow)
	{
		DrawDebugDirectionalArrow(
			World,
			Start,
			End,
			50.0f,  // Arrow size
			Color.ToFColor(true),
			false,  // Persistent lines
			0.0f,   // Lifetime (0 = single frame)
			0,      // Depth priority
			LineThickness
		);
	}
	else
	{
		DrawDebugLine(
			World,
			Start,
			End,
			Color.ToFColor(true),
			false,
			0.0f,
			0,
			LineThickness
		);
	}
}

void UVisualLoggerComponent::DrawTeamLeaderState(
	const FVector& Location,
	int32 FollowerCount,
	int32 AliveFollowerCount,
	bool bIsMCTSRunning,
	float LastMCTSTime
)
{
	if (!IsDebugDrawingEnabled() || !bDrawTeamInfo)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Team leader info
	FString LeaderText = FString::Printf(
		TEXT("TEAM LEADER\nFollowers: %d/%d\nMCTS: %s\nLast: %.1fs ago"),
		AliveFollowerCount,
		FollowerCount,
		bIsMCTSRunning ? TEXT("RUNNING") : TEXT("IDLE"),
		LastMCTSTime
	);

	FColor LeaderColor = bIsMCTSRunning ? FColor::Cyan : FColor::White;

	DrawDebugString(
		World,
		Location + FVector(0, 0, TextHeightOffset),
		LeaderText,
		nullptr,
		LeaderColor,
		0.0f,
		true
	);

	// Draw sphere around leader
	DrawDebugSphere(
		World,
		Location,
		SphereRadius,
		16,  // Segments
		LeaderColor,
		false,
		0.0f,
		0,
		LineThickness
	);
}

void UVisualLoggerComponent::DrawFormationInfo(
	const FVector& LeaderLocation,
	const TArray<AActor*>& Followers
)
{
	if (!IsDebugDrawingEnabled() || !bDrawFormation)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Draw lines from leader to each follower
	for (AActor* Follower : Followers)
	{
		if (!Follower)
		{
			continue;
		}

		FVector FollowerLocation = Follower->GetActorLocation();
		float Distance = FVector::Dist(LeaderLocation, FollowerLocation);

		// Color based on distance (green = close, yellow = medium, red = far)
		FLinearColor LineColor;
		if (Distance < 1000.0f)
		{
			LineColor = FLinearColor::Green;
		}
		else if (Distance < 2000.0f)
		{
			LineColor = FLinearColor::Yellow;
		}
		else
		{
			LineColor = FLinearColor::Red;
		}

		DrawDebugLine(
			World,
			LeaderLocation,
			FollowerLocation,
			LineColor.ToFColor(true),
			false,
			0.0f,
			0,
			LineThickness * 0.5f  // Thinner lines for formation
		);

		// Draw distance label at midpoint
		FVector MidPoint = (LeaderLocation + FollowerLocation) * 0.5f;
		FString DistanceText = FString::Printf(TEXT("%.1fm"), Distance / 100.0f);
		DrawDebugString(
			World,
			MidPoint,
			DistanceText,
			nullptr,
			FColor::White,
			0.0f,
			false  // No shadow for distance labels
		);
	}
}

void UVisualLoggerComponent::DrawObjectiveMarkers(
	AActor* FriendlyObjective,
	AActor* HostileObjective
)
{
	if (!IsDebugDrawingEnabled() || !bDrawObjectives)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Draw friendly objective (blue)
	if (FriendlyObjective)
	{
		FVector FriendlyLoc = FriendlyObjective->GetActorLocation();
		DrawDebugSphere(
			World,
			FriendlyLoc,
			SphereRadius * 2.0f,
			16,
			FColor::Blue,
			false,
			0.0f,
			0,
			LineThickness * 2.0f
		);

		DrawDebugString(
			World,
			FriendlyLoc + FVector(0, 0, TextHeightOffset * 2.0f),
			TEXT("FRIENDLY OBJECTIVE"),
			nullptr,
			FColor::Blue,
			0.0f,
			true
		);
	}

	// Draw hostile objective (red)
	if (HostileObjective)
	{
		FVector HostileLoc = HostileObjective->GetActorLocation();
		DrawDebugSphere(
			World,
			HostileLoc,
			SphereRadius * 2.0f,
			16,
			FColor::Red,
			false,
			0.0f,
			0,
			LineThickness * 2.0f
		);

		DrawDebugString(
			World,
			HostileLoc + FVector(0, 0, TextHeightOffset * 2.0f),
			TEXT("HOSTILE OBJECTIVE"),
			nullptr,
			FColor::Red,
			0.0f,
			true
		);
	}
}

void UVisualLoggerComponent::DrawDebugText(
	const FVector& Location,
	const FString& Text,
	FLinearColor Color,
	float Duration
)
{
	if (!IsDebugDrawingEnabled())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugString(
		World,
		Location,
		Text,
		nullptr,
		Color.ToFColor(true),
		Duration,
		true
	);
}

void UVisualLoggerComponent::DrawSphere(
	const FVector& Location,
	float Radius,
	FLinearColor Color,
	float Duration
)
{
	if (!IsDebugDrawingEnabled())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugSphere(
		World,
		Location,
		Radius,
		16,
		Color.ToFColor(true),
		false,
		Duration,
		0,
		LineThickness
	);
}

void UVisualLoggerComponent::DrawLine(
	const FVector& Start,
	const FVector& End,
	FLinearColor Color,
	float Duration,
	float Thickness
)
{
	if (!IsDebugDrawingEnabled())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugLine(
		World,
		Start,
		End,
		Color.ToFColor(true),
		false,
		Duration,
		0,
		Thickness
	);
}

void UVisualLoggerComponent::DrawArrow(
	const FVector& Start,
	const FVector& End,
	FLinearColor Color,
	float ArrowSize,
	float Duration,
	float Thickness
)
{
	if (!IsDebugDrawingEnabled())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugDirectionalArrow(
		World,
		Start,
		End,
		ArrowSize,
		Color.ToFColor(true),
		false,
		Duration,
		0,
		Thickness
	);
}
