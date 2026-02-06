// File: Content/AI/EQS/EQ_TacticalMovement.uasset (Blueprint 설정)

/**
 * EQS Query: EQ_TacticalMovement
 * Generator: Circle (반경 1500 units, 48개 샘플 포인트)
 * 
 * 이 Query는 RL Policy가 제공한 가중치를 동적으로 적용하여
 * 최적의 이동 위치를 선택합니다.
 */


// Circle Generator Configuration
Generator: SimpleGrid or Circle
    - SearchRadius: 1500.0f          // 15m 탐색 반위
    - SpaceBetween: 150.0f           // 샘플 간격
    - NumberOfRings: 3               // 동심원 개수
    - PointsPerRing: 16              // 링당 포인트 수
    - TraceData:
        - TraceMode: Navigation       // NavMesh만 사용
        - ProjectDown: 500.0f         // 지면 투사

// Test 1: Distance to Enemy Objective
Test: Distance
    - DistanceTo: EnemyObjective (Context)
    - TestPurpose: Weight
    - ScoringEquation: InverseLinear  // 가까울수록 높은 점수
    - Weight: [DYNAMIC from RL]       // RL Policy의 EnemyObjectiveProximity
    - Clamp: 
        - Min: 0.0, Max: 3000.0

// Test 2: Distance to Ally Objective
Test: Distance
    - DistanceTo: AllyObjective (Context)
    - TestPurpose: Weight
    - ScoringEquation: InverseLinear
    - Weight: [DYNAMIC from RL]       // AllyObjectiveProximity
    - Clamp:
        - Min: 0.0, Max: 2000.0

// Test 3: Cover Density (Trace Test)
Test: Trace
    - TraceMode: VisibilityCollision
    - TraceFrom: ItemToContext (Self)
    - TraceTo: EnemyActors (Context)
    - TestPurpose: Weight
    - BoolMatch: Hit = High Score     // 차폐되면 좋은 점수
    - Weight: [DYNAMIC from RL]       // CoverDensity
    - Notes: "벽/장애물로 적 시야가 차단된 위치 선호"

// Test 4: Enemy Visibility (Dot Product Test)
Test: Dot
    - LineA: Self Forward Vector
    - LineB: Self to NearestEnemy
    - TestPurpose: Weight
    - ScoringEquation: Linear         // 정면을 향할수록 높은 점수
    - Weight: [DYNAMIC from RL]       // EnemyVisibility
    - Notes: "적을 정면으로 볼 수 있는 위치 선호"

// Test 5: Ally Proximity
Test: Distance
    - DistanceTo: TeamMembers (Context)
    - TestPurpose: Weight
    - ScoringEquation: InverseLinear
    - Weight: [DYNAMIC from RL]       // AllyProximity
    - Clamp:
        - Min: 300.0, Max: 1500.0     // 3-15m 범위

// Test 6: Combat Range (Distance to Nearest Enemy)
Test: Distance
    - DistanceTo: NearestEnemy (Context)
    - TestPurpose: Weight
    - ScoringEquation: Parabola       // 최적 거리에서 최고점
    - ParabolaPeak: 1000.0f           // 10m가 이상적
    - Weight: [DYNAMIC from RL]       // CombatRange
    - Notes: "무기 유효 사거리 기반 조정"

// Test 7: Pickup Proximity
Test: Distance
    - DistanceTo: NearestPickup (Context)
    - TestPurpose: Weight
    - ScoringEquation: InverseLinear
    - Weight: [DYNAMIC from RL]       // PickupProximity
    - Clamp:
        - Min: 0.0, Max: 2000.0

// Test 8: Height Advantage
Test: PathfindingBatch (Z-axis difference)
    - PathTo: NearestEnemy (Context)
    - TestPurpose: Weight
    - TestMode: HeightDifference
    - ScoringEquation: Linear         // 높을수록 좋음
    - Weight: [DYNAMIC from RL]       // HeightAdvantage
    - Clamp:
        - Min: -500.0, Max: 500.0     // -5m ~ +5m
