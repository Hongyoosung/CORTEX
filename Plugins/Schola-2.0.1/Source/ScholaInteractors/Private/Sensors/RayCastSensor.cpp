// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Sensors/RayCastSensor.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Common/LogSchola.h"

void URayCastSensor::GetObservationSpace_Implementation(FInstancedStruct& OutObservationSpace) const
{
	OutObservationSpace.InitializeAs<FBoxSpace>();
	FBoxSpace& SpaceDefinition = OutObservationSpace.GetMutable<FBoxSpace>();

	for (int i = 0; i < NumRays; i++)
	{
		// First # of tag entries correspond to each tag
		for (const FName& Tag : TrackedTags)
		{
			SpaceDefinition.Dimensions.Add(FBoxSpaceDimension(0.0, 1.0));
		}
		// Did we hit anything at all
		SpaceDefinition.Dimensions.Add(FBoxSpaceDimension(0.0, 1.0));
		// How far away was the thing we hit
		SpaceDefinition.Dimensions.Add(FBoxSpaceDimension(0.0, 1.0));
	}

}

void URayCastSensor::GenerateRayEndpoints(int32 InNumRays, float InRayDegrees, FVector InBaseEnd, FVector InStart, FTransform InTransform, FVector InEndOffset, TArray<FVector>& OutRayEndpoints)
{

	OutRayEndpoints.Init(FVector(), InNumRays);

	float Delta;
	// Special case to avoid 2 rays ontop of each other or a divide by zero
	if (InRayDegrees >= 360.0 || InNumRays <= 1)
	{
		// For 360 degrees, we should have 2 rays -> 180 deg, 3 rays -> 120 deg, 4 rays -> 90 deg, 5 rays -> 72 deg
		Delta = InRayDegrees / InNumRays;
	}
	else
	{
		// Normal Case where we put rays up to the edges of the range
		// For 90 degrees, we should have 2 rays -> 90 deg, 3 rays -> 45 deg, 4 rays -> 30 deg, 5 rays -> 17.5 deg
		Delta = InRayDegrees / (InNumRays - 1);
	}

	for (int32 Index = 0; Index < InNumRays; Index += 1)
	{
		// We want to start on the far left and move to the far right so for 90 degrees first ray is at -45 degrees.
		FRotator Rotator(0.0f, Delta * Index - (InRayDegrees / 2), 0);
		OutRayEndpoints[Index] = Rotator.RotateVector(InBaseEnd);
	}

	for (int i = 0; i < OutRayEndpoints.Num(); i++)
	{
		// Apply the combined transform that includes the pawn's orientation
		OutRayEndpoints[i] = InTransform.TransformVector(OutRayEndpoints[i]);
		OutRayEndpoints[i] = OutRayEndpoints[i] + InEndOffset + InStart;
	}

}

void URayCastSensor::AppendEmptyTags(FBoxPoint& OutObservations)
{
	for (FName& TrackedTag : TrackedTags)
	{
		OutObservations.Values.Emplace(0.0);
	}
}

void URayCastSensor::HandleRayMiss(FBoxPoint& OutObservations, const FVector& Start, const FVector& RayEndpoint)
{
	AppendEmptyTags(OutObservations);
	// Tack on the did I hit and hit distance variables
	OutObservations.Values.Emplace(0.0);
	OutObservations.Values.Emplace(0.0);
	if (bDrawDebugLines)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola: RaySensor Debug Lines");
		DrawDebugLine(
			GetWorld(),
			Start,
			RayEndpoint,
			DebugMissColor,
			false,
			0,
			0,
			kLineGirth);
	}
}

void URayCastSensor::HandleRayHit(const FHitResult& InHitResult, FBoxPoint& OutObservations, const FVector& InStart)
{
	// A Precondition is that the HitResult Suceeded, thus Hit.GetActor is always valid

	const AActor* HitObject = InHitResult.GetActor();

	const TArray<FName>& AttachedTags = HitObject->Tags;

	// Fast Path where we just slap a bunch of zeroes and call it a day
	if (AttachedTags.Num() == 0)
	{
		AppendEmptyTags(OutObservations);
	}
	else
	{
		for (const FName& TrackedTag : TrackedTags)
		{
			bool bIsTrackedTagFound = false;
			for (int i = 0; i < AttachedTags.Num() && !bIsTrackedTagFound; i++)
			{
				bIsTrackedTagFound = AttachedTags[i] == TrackedTag;
			}

			OutObservations.Values.Emplace(static_cast<float>(bIsTrackedTagFound));
		}
	}

	if (bDrawDebugLines)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola: RaySensor Debug Lines");
		DrawDebugLine(
			GetWorld(),
			InStart,
			InHitResult.ImpactPoint,
			DebugHitColor,
			false,
			0,
			0,
			kLineGirth);

		DrawDebugSphere(
			this->GetWorld(),
			InHitResult.ImpactPoint - InHitResult.ImpactNormal * kSphereRadius,
			kSphereRadius,
			12,
			DebugHitColor);
	}

	// At this point we have a hit and it has some tags
	// Loop through each tracked tag and check if we find some
	// Always adds 0/1 to the OutObservations for each tag
	OutObservations.Values.Emplace(1.0f);
	OutObservations.Values.Emplace(InHitResult.Time);
}

void URayCastSensor::CollectObservations_Implementation(FInstancedStruct& OutObservations)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola: RaySensor Observation Collection");
    OutObservations.InitializeAs<FBoxPoint>();
	FBoxPoint& BoxPoint = OutObservations.GetMutable<FBoxPoint>();

    
    TArray<FVector> Endpoints;
	GenerateRayEndpoints(NumRays, RayDegrees, this->GetForwardVector() * RayLength, this->GetComponentLocation(), this->GetRelativeTransform(), RayEndOffset, Endpoints);

	FCollisionQueryParams TraceParams = FCollisionQueryParams(FName(*FString("RayCastSensor")), this->bTraceComplex, this->GetOwner());
        
        
	for (auto RayEndpoint : Endpoints)
	{
		FHitResult Hit;
		bool	   bHasHit = GetWorld()->LineTraceSingleByChannel(
				Hit,
				this->GetComponentLocation(),
				RayEndpoint,
				this->CollisionChannel,
				TraceParams,
				FCollisionResponseParams::DefaultResponseParam);

		if (bHasHit)
		{
			HandleRayHit(Hit, BoxPoint, this->GetComponentLocation());
		}
		else
		{
			HandleRayMiss(BoxPoint, this->GetComponentLocation(), RayEndpoint);
		}
	}
}

FString URayCastSensor::GenerateId() const
{
	FString Output = FString("Ray");
	// Add the number of rays
	Output.Appendf(TEXT("_Num_%d"), NumRays);
	// Add the angle of the rays
	Output.Appendf(TEXT("_Deg_%.2f"), RayDegrees);
	//Add the Max distance
	Output.Appendf(TEXT("_Max_%.2f"), RayLength);
	Output.Append("_").Append(UEnum::GetValueAsString<ECollisionChannel>(CollisionChannel));
	// Add the tags
	if (TrackedTags.Num() > 0)
	{
		Output.Append("_Tags");
		for (const FName& Tag : TrackedTags)
		{
			Output.Appendf(TEXT("_%s"), *Tag.ToString());
		}
	}
	return Output;
}

#if WITH_EDITOR
void URayCastSensor::DrawDebugLines()
{
	FlushPersistentDebugLines(GetWorld());

	AActor* Owner = this->GetOwner();

	if (Owner)
	{
		FVector			ActorLocation = Owner->GetActorLocation();
		FVector			ForwardVector = Owner->GetActorForwardVector();
		FVector			Start = this->GetComponentLocation();
		TArray<FVector> Endpoints;
		GenerateRayEndpoints(NumRays, RayDegrees, ForwardVector * RayLength, this->GetComponentLocation(), this->GetRelativeTransform(), RayEndOffset, Endpoints);

		for (auto RayEndpoint : Endpoints)
		{
			float DirectionLengthAndSphereRadius = (RayEndpoint - Start).Length();
			float ScaledDirectionLengthAndSphereRadius = RayLength > 0
				? kSphereRadius * DirectionLengthAndSphereRadius / RayLength
				: kSphereRadius;

			DrawDebugLine(
				GetWorld(),
				Start,
				RayEndpoint,
				FColor::MakeRandomColor(),
				true,
				0,
				0,
				kLineGirth);
		}
	}
}

void URayCastSensor::ToggleDebugLines()
{
	if (bDebugLinesEnabled)
	{
		FlushPersistentDebugLines(GetWorld());
	}
	else
	{
		this->DrawDebugLines();
	}

	bDebugLinesEnabled = !bDebugLinesEnabled;
}
#endif
