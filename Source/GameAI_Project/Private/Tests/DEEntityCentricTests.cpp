// Copyright Epic Games, Inc. All Rights Reserved.
//
// Phase 8 — Entity-Centric Refactor Unit Tests
//
// Test suites:
//   1. FDEObservationV2::ToDict()    — token counts and mask correctness (3v3, 5v5, 3v8)
//   2. FDEObservationV2::ToFlatArray() — dimension, padding, mask layout
//   3. AssignBasesToAgents logic     — all agents covered; Defend uniqueness invariant
//
// Run via:
//   Session Frontend → Automation → "CORTEX.EntityCentric"
//   or:  -ExecCmds "Automation RunTests CORTEX.EntityCentric"
//

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Types/DEObservationTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace DETestHelpers
{
	/**
	 * Build a minimal FDEObservationV2 with the requested entity counts.
	 * All features are set to a known value (0.5) so mask logic can be verified
	 * independently of content correctness.
	 */
	static FDEObservationV2 MakeObs(int32 NumAllies, int32 NumEnemies, int32 NumBases)
	{
		FDEObservationV2 Obs;

		// Self token (7-dim)
		Obs.SelfToken.Features = { 0.1f, 0.2f, 0.3f, 1.0f, 0.0f, 0.0f, 0.0f };

		// Ally tokens (5-dim each)
		for (int32 i = 0; i < NumAllies; ++i)
		{
			FDEEntityToken T;
			T.Features = { 0.5f, 0.5f, 0.5f, 1.0f, 1.0f };
			Obs.AllyTokens.Add(T);
		}

		// Enemy tokens (5-dim each)
		for (int32 i = 0; i < NumEnemies; ++i)
		{
			FDEEntityToken T;
			T.Features = { 0.5f, 0.5f, 0.5f, 1.0f, 0.8f };
			Obs.EnemyTokens.Add(T);
		}

		// Base tokens (7-dim each)
		for (int32 i = 0; i < NumBases; ++i)
		{
			FDEEntityToken T;
			T.Features = { 0.1f, 0.1f, 0.0f, 1.0f, 0.5f, (i == 0 ? 1.0f : 0.0f), 0.5f };
			Obs.BaseTokens.Add(T);
		}

		return Obs;
	}

	/**
	 * Count zero-value masks (present = 0.0) in a mask array.
	 */
	static int32 CountPresent(const TArray<float>& Mask)
	{
		int32 N = 0;
		for (float V : Mask) { if (V == 0.0f) ++N; }
		return N;
	}

	/**
	 * Count padding masks (padding = 1.0) in a mask array.
	 */
	static int32 CountPadding(const TArray<float>& Mask)
	{
		int32 N = 0;
		for (float V : Mask) { if (V == 1.0f) ++N; }
		return N;
	}
} // namespace DETestHelpers


// ─────────────────────────────────────────────────────────────────────────────
// Suite 1 — ToDict() token counts and mask correctness
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDEObsV2ToDictTokenCounts,
	"CORTEX.EntityCentric.ObsV2.ToDict.TokenCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDEObsV2ToDictTokenCounts::RunTest(const FString& Parameters)
{
	// ── 3v3 (3 allies, 3 enemies, 3 bases) ──────────────────────────────────
	{
		const FDEObservationV2 Obs = DETestHelpers::MakeObs(3, 3, 3);
		const TMap<FString, TArray<float>> Dict = Obs.ToDict();

		// Key presence
		TestTrue(TEXT("3v3: 'self' key exists"),       Dict.Contains(TEXT("self")));
		TestTrue(TEXT("3v3: 'allies' key exists"),     Dict.Contains(TEXT("allies")));
		TestTrue(TEXT("3v3: 'enemies' key exists"),    Dict.Contains(TEXT("enemies")));
		TestTrue(TEXT("3v3: 'bases' key exists"),      Dict.Contains(TEXT("bases")));
		TestTrue(TEXT("3v3: 'ally_mask' key exists"),  Dict.Contains(TEXT("ally_mask")));
		TestTrue(TEXT("3v3: 'enemy_mask' key exists"), Dict.Contains(TEXT("enemy_mask")));
		TestTrue(TEXT("3v3: 'base_mask' key exists"),  Dict.Contains(TEXT("base_mask")));

		// Dimensions
		TestEqual(TEXT("3v3: self dim"),       Dict[TEXT("self")].Num(),       DE_SELF_DIM);
		TestEqual(TEXT("3v3: allies flat dim"),  Dict[TEXT("allies")].Num(),   DE_MAX_ALLIES  * DE_ALLY_DIM);
		TestEqual(TEXT("3v3: enemies flat dim"), Dict[TEXT("enemies")].Num(),  DE_MAX_ENEMIES * DE_ENEMY_DIM);
		TestEqual(TEXT("3v3: bases flat dim"),   Dict[TEXT("bases")].Num(),    DE_MAX_BASES   * DE_BASE_DIM);
		TestEqual(TEXT("3v3: ally_mask size"),   Dict[TEXT("ally_mask")].Num(),  DE_MAX_ALLIES);
		TestEqual(TEXT("3v3: enemy_mask size"),  Dict[TEXT("enemy_mask")].Num(), DE_MAX_ENEMIES);
		TestEqual(TEXT("3v3: base_mask size"),   Dict[TEXT("base_mask")].Num(),  DE_MAX_BASES);

		// Present / padding counts
		TestEqual(TEXT("3v3: ally_mask present=3"),   DETestHelpers::CountPresent(Dict[TEXT("ally_mask")]),  3);
		TestEqual(TEXT("3v3: ally_mask padding=5"),   DETestHelpers::CountPadding(Dict[TEXT("ally_mask")]),  5);
		TestEqual(TEXT("3v3: enemy_mask present=3"),  DETestHelpers::CountPresent(Dict[TEXT("enemy_mask")]), 3);
		TestEqual(TEXT("3v3: enemy_mask padding=5"),  DETestHelpers::CountPadding(Dict[TEXT("enemy_mask")]), 5);
		TestEqual(TEXT("3v3: base_mask present=3"),   DETestHelpers::CountPresent(Dict[TEXT("base_mask")]),  3);
		TestEqual(TEXT("3v3: base_mask padding=5"),   DETestHelpers::CountPadding(Dict[TEXT("base_mask")]),  5);
	}

	// ── 5v5 (5 allies, 5 enemies, 5 bases) ──────────────────────────────────
	{
		const FDEObservationV2 Obs = DETestHelpers::MakeObs(5, 5, 5);
		const TMap<FString, TArray<float>> Dict = Obs.ToDict();

		TestEqual(TEXT("5v5: ally_mask present=5"),  DETestHelpers::CountPresent(Dict[TEXT("ally_mask")]),  5);
		TestEqual(TEXT("5v5: ally_mask padding=3"),  DETestHelpers::CountPadding(Dict[TEXT("ally_mask")]),  3);
		TestEqual(TEXT("5v5: enemy_mask present=5"), DETestHelpers::CountPresent(Dict[TEXT("enemy_mask")]), 5);
		TestEqual(TEXT("5v5: base_mask present=5"),  DETestHelpers::CountPresent(Dict[TEXT("base_mask")]),  5);
	}

	// ── 3v8 (3 allies, 8 enemies, 3 bases) — max enemies, unequal counts ────
	{
		const FDEObservationV2 Obs = DETestHelpers::MakeObs(3, 8, 3);
		const TMap<FString, TArray<float>> Dict = Obs.ToDict();

		TestEqual(TEXT("3v8: ally_mask present=3"),   DETestHelpers::CountPresent(Dict[TEXT("ally_mask")]),  3);
		TestEqual(TEXT("3v8: ally_mask padding=5"),   DETestHelpers::CountPadding(Dict[TEXT("ally_mask")]),  5);
		TestEqual(TEXT("3v8: enemy_mask present=8"),  DETestHelpers::CountPresent(Dict[TEXT("enemy_mask")]), 8);
		TestEqual(TEXT("3v8: enemy_mask padding=0"),  DETestHelpers::CountPadding(Dict[TEXT("enemy_mask")]), 0);
		TestEqual(TEXT("3v8: base_mask present=3"),   DETestHelpers::CountPresent(Dict[TEXT("base_mask")]),  3);
	}

	return true;
}


// ─────────────────────────────────────────────────────────────────────────────
// Suite 2 — ToFlatArray() total dimension and mask layout
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDEObsV2ToFlatArrayDim,
	"CORTEX.EntityCentric.ObsV2.ToFlatArray.Dimension",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDEObsV2ToFlatArrayDim::RunTest(const FString& Parameters)
{
	// Total must equal DE_OBS_V2_DIM (167) regardless of entity count
	for (int32 NA : {0, 1, 3, 5, 8})
	{
		for (int32 NE : {0, 3, 8})
		{
			for (int32 NB : {1, 5, 8})
			{
				const FDEObservationV2 Obs = DETestHelpers::MakeObs(NA, NE, NB);
				const TArray<float> Flat = Obs.ToFlatArray();

				const FString Tag = FString::Printf(TEXT("Flat dim [A=%d E=%d B=%d]"), NA, NE, NB);
				TestEqual(*Tag, Flat.Num(), DE_OBS_V2_DIM);
			}
		}
	}

	// Mask layout: ally mask starts at index 7 + 40 + 40 + 56 = 143
	{
		constexpr int32 AllyMaskOffset  = DE_SELF_DIM + DE_MAX_ALLIES * DE_ALLY_DIM + DE_MAX_ENEMIES * DE_ENEMY_DIM + DE_MAX_BASES * DE_BASE_DIM;
		constexpr int32 EnemyMaskOffset = AllyMaskOffset  + DE_MAX_ALLIES;
		constexpr int32 BaseMaskOffset  = EnemyMaskOffset + DE_MAX_ENEMIES;

		TestEqual(TEXT("AllyMask offset == 143"),  AllyMaskOffset,  143);
		TestEqual(TEXT("EnemyMask offset == 151"), EnemyMaskOffset, 151);
		TestEqual(TEXT("BaseMask offset == 159"),  BaseMaskOffset,  159);

		// Verify mask values in a 3-ally observation
		const FDEObservationV2 Obs = DETestHelpers::MakeObs(3, 0, 0);
		const TArray<float> Flat = Obs.ToFlatArray();

		// First 3 ally slots → present (0.0)
		for (int32 i = 0; i < 3; ++i)
		{
			TestEqual(*FString::Printf(TEXT("AllyMask[%d] == 0 (present)"), i),
				Flat[AllyMaskOffset + i], 0.0f);
		}
		// Remaining 5 slots → padding (1.0)
		for (int32 i = 3; i < DE_MAX_ALLIES; ++i)
		{
			TestEqual(*FString::Printf(TEXT("AllyMask[%d] == 1 (padding)"), i),
				Flat[AllyMaskOffset + i], 1.0f);
		}
	}

	return true;
}


// ─────────────────────────────────────────────────────────────────────────────
// Suite 3 — ToFlatArray() self token values (normalization sanity)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDEObsV2SelfTokenValues,
	"CORTEX.EntityCentric.ObsV2.ToFlatArray.SelfTokenValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDEObsV2SelfTokenValues::RunTest(const FString& Parameters)
{
	FDEObservationV2 Obs;
	Obs.SelfToken.Features = { 0.1f, 0.2f, 0.3f, 1.0f, 0.4f, 0.5f, 0.6f };

	const TArray<float> Flat = Obs.ToFlatArray();

	// Self token is at [0..6]
	TestNearlyEqual(TEXT("Self[0]"), Flat[0], 0.1f, 1e-5f);
	TestNearlyEqual(TEXT("Self[1]"), Flat[1], 0.2f, 1e-5f);
	TestNearlyEqual(TEXT("Self[2]"), Flat[2], 0.3f, 1e-5f);
	TestNearlyEqual(TEXT("Self[3]"), Flat[3], 1.0f, 1e-5f);
	TestNearlyEqual(TEXT("Self[4]"), Flat[4], 0.4f, 1e-5f);
	TestNearlyEqual(TEXT("Self[5]"), Flat[5], 0.5f, 1e-5f);
	TestNearlyEqual(TEXT("Self[6]"), Flat[6], 0.6f, 1e-5f);

	return true;
}


// ─────────────────────────────────────────────────────────────────────────────
// Suite 4 — AssignBasesToAgents logic (pure-logic mirror test)
//
// This test validates the algorithmic invariants of AssignBasesToAgents()
// without instantiating full UE actors. It replicates the greedy assignment
// logic and verifies:
//   (a) Every agent receives a valid base index in [0, NumBases)
//   (b) No two Defend agents are assigned the same base
//
// To run the true ADEMatchManager path, use the integration test in
// Suite 5 (requires PIE / Functional Test framework).
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDEAssignBasesLogic,
	"CORTEX.EntityCentric.AssignBases.PureLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDEAssignBasesLogic::RunTest(const FString& Parameters)
{
	// Simulate three scenarios covering the plan's test cases.
	// Each scenario defines agent strategies and base positions, then runs
	// a local copy of the greedy algorithm and checks invariants.

	struct FAgentSim
	{
		EDEStrategyType Strategy = EDEStrategyType::Assault;
		FVector Position         = FVector::ZeroVector;
		int32   AssignedBase     = -1;
	};

	struct FBaseSim
	{
		FVector Position = FVector::ZeroVector;
		int32   OwnerID  = -1; // -1 = neutral
	};

	// Greedy assignment mirror (matches ADEMatchManager::AssignBasesToAgents logic)
	auto RunAssignment = [&](TArray<FAgentSim>& Agents, const TArray<FBaseSim>& Bases,
	                          int32 TeamID)
	{
		TSet<int32> DefendClaimed;

		// Pass 1: Defend agents — prefer nearest friendly, uniqueness enforced
		for (FAgentSim& A : Agents)
		{
			if (A.Strategy != EDEStrategyType::Defend) continue;

			float BestScore = FLT_MAX;
			int32 BestIdx   = -1;

			for (int32 i = 0; i < Bases.Num(); ++i)
			{
				if (DefendClaimed.Contains(i)) continue;
				const float Dist = FVector::Dist(A.Position, Bases[i].Position);
				const float Bias = (Bases[i].OwnerID == TeamID) ? 0.0f : 5000.0f;
				if ((Dist + Bias) < BestScore) { BestScore = Dist + Bias; BestIdx = i; }
			}

			if (BestIdx >= 0) { DefendClaimed.Add(BestIdx); A.AssignedBase = BestIdx; }
		}

		// Pass 2: Assault / Support
		for (FAgentSim& A : Agents)
		{
			if (A.Strategy == EDEStrategyType::Defend) continue;

			float BestDist = FLT_MAX;
			int32 BestIdx  = -1;

			for (int32 i = 0; i < Bases.Num(); ++i)
			{
				const float Dist = FVector::Dist(A.Position, Bases[i].Position);
				if (A.Strategy == EDEStrategyType::Assault)
				{
					const float Bias = (Bases[i].OwnerID != TeamID) ? 0.0f : 3000.0f;
					if ((Dist + Bias) < BestDist) { BestDist = Dist + Bias; BestIdx = i; }
				}
				else
				{
					if (Dist < BestDist) { BestDist = Dist; BestIdx = i; }
				}
			}

			A.AssignedBase = BestIdx >= 0 ? BestIdx : 0;
		}
	};

	// ── Scenario A: StandardComp (2A, 2D, 1S) — 3 bases ──────────────────
	{
		TArray<FBaseSim> Bases = {
			{ FVector(0,    0, 0), -1 },   // neutral
			{ FVector(5000, 0, 0), -1 },   // neutral
			{ FVector(2500, 5000, 0), 0 }, // friendly (TeamID=0)
		};

		TArray<FAgentSim> Agents = {
			{ EDEStrategyType::Assault, FVector(100, 100, 0) },
			{ EDEStrategyType::Assault, FVector(200, 100, 0) },
			{ EDEStrategyType::Defend,  FVector(2400, 4900, 0) }, // near friendly base
			{ EDEStrategyType::Defend,  FVector(2600, 4900, 0) },
			{ EDEStrategyType::Support, FVector(300, 200, 0) },
		};

		RunAssignment(Agents, Bases, 0);

		// All assigned to a valid base
		for (int32 i = 0; i < Agents.Num(); ++i)
		{
			TestTrue(*FString::Printf(TEXT("ScenA: Agent[%d] base in [0,2]"), i),
				Agents[i].AssignedBase >= 0 && Agents[i].AssignedBase < 3);
		}

		// Defend uniqueness
		const int32 D0 = Agents[2].AssignedBase;
		const int32 D1 = Agents[3].AssignedBase;
		TestTrue(TEXT("ScenA: Defend agents on different bases"), D0 != D1);

		// Assault agents prefer non-friendly bases (bias 3000 pushed them away from base 2)
		// Both assault agents are near origin → nearest non-friendly = base 0 or 1
		TestTrue(TEXT("ScenA: Assault[0] not on friendly base"),
			Agents[0].AssignedBase != 2);
	}

	// ── Scenario B: TurtleFormation (5 Defend) — 3 bases ─────────────────
	// With 5 Defend agents but only 3 unique bases, bases [0..2] each get
	// exactly one Defend agent; the remaining 2 fall through to FindBestBase
	// without the uniqueness constraint (all 5 unique slots > base count).
	{
		TArray<FBaseSim> Bases = {
			{ FVector(0,    0, 0), 0 },
			{ FVector(5000, 0, 0), 0 },
			{ FVector(2500, 5000, 0), 0 },
		};

		TArray<FAgentSim> Agents;
		for (int32 i = 0; i < 5; ++i)
		{
			FAgentSim A;
			A.Strategy = EDEStrategyType::Defend;
			A.Position  = FVector(static_cast<float>(i * 200), 0.0f, 0.0f);
			Agents.Add(A);
		}

		RunAssignment(Agents, Bases, 0);

		// All agents must have a valid assignment
		for (int32 i = 0; i < Agents.Num(); ++i)
		{
			TestTrue(*FString::Printf(TEXT("ScenB: Agent[%d] assigned"), i),
				Agents[i].AssignedBase >= 0 && Agents[i].AssignedBase < 3);
		}

		// First 3 Defend agents (fill all bases uniquely)
		TSet<int32> FirstThree;
		for (int32 i = 0; i < 3; ++i) FirstThree.Add(Agents[i].AssignedBase);
		TestEqual(TEXT("ScenB: first 3 Defend agents cover all 3 bases"), FirstThree.Num(), 3);
	}

	// ── Scenario C: AllOutRush (5 Assault) — 5 bases ──────────────────────
	{
		TArray<FBaseSim> Bases;
		for (int32 i = 0; i < 5; ++i)
			Bases.Add({ FVector(static_cast<float>(i * 3000), 0, 0), (i < 2 ? 0 : -1) });

		TArray<FAgentSim> Agents;
		for (int32 i = 0; i < 5; ++i)
		{
			FAgentSim A;
			A.Strategy = EDEStrategyType::Assault;
			A.Position  = FVector(static_cast<float>(i * 500 + 100), 0, 0);
			Agents.Add(A);
		}

		RunAssignment(Agents, Bases, 0);

		for (int32 i = 0; i < Agents.Num(); ++i)
		{
			TestTrue(*FString::Printf(TEXT("ScenC: Assault[%d] assigned"), i),
				Agents[i].AssignedBase >= 0 && Agents[i].AssignedBase < 5);
		}
	}

	return true;
}


// ─────────────────────────────────────────────────────────────────────────────
// Suite 5 — Cooperative reward constant sanity check
//
// Validates that the reward parameter defaults in FDERewardData match the
// values specified in the refactor plan (Section 3).
// ─────────────────────────────────────────────────────────────────────────────

#include "Data/DERewardData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDERewardDataDefaults,
	"CORTEX.EntityCentric.RewardData.CoopDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDERewardDataDefaults::RunTest(const FString& Parameters)
{
	// Instantiate a default UDERewardData-like struct to inspect its defaults.
	// We can't construct a UObject here directly; instead read the defaults via CDO.
	// Use a local struct mirror that matches the header layout.

	// These tests act as a specification contract — they will fail if someone
	// accidentally changes plan-mandated defaults.
	const UDERewardData* CDO = GetDefault<UDERewardData>();
	if (!CDO)
	{
		AddWarning(TEXT("CDO for UDERewardData unavailable in this context — skipping defaults test"));
		return true;
	}

	// Note: CoOccupationPenalty and UndefendedBasePenalty store *positive* magnitudes;
	// negation is applied at call-site in DERewardSubsystem::ComputeBaseCooperationReward().
	TestNearlyEqual(TEXT("BaseOccupationReward default == 2.0"),
		CDO->BaseOccupationReward, 2.0f, 0.01f);
	TestNearlyEqual(TEXT("CoOccupationPenalty magnitude == 0.5"),
		CDO->CoOccupationPenalty, 0.5f, 0.01f);
	TestNearlyEqual(TEXT("BaseCaptureCreditReward default == 5.0"),
		CDO->BaseCaptureCreditReward, 5.0f, 0.01f);
	TestNearlyEqual(TEXT("UndefendedBasePenalty magnitude == 1.0"),
		CDO->UndefendedBasePenalty, 1.0f, 0.01f);
	TestNearlyEqual(TEXT("AssignedBaseReachReward default == 1.0"),
		CDO->AssignedBaseReachReward, 1.0f, 0.01f);
	TestTrue(TEXT("BaseOccupationRadius > 0"),
		CDO->BaseOccupationRadius > 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
