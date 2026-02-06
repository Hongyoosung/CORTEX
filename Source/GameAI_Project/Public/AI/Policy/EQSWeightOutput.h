// File: AI/Policy/EQSWeightOutput.h

/**
 * RL Policy가 출력하는 EQS 가중치 파라미터
 * 각 가중치는 [-1, 1] 범위로 정규화되며, EQS Test의 중요도를 결정
 */
struct FEQSWeightParameters {
    // === 전술적 포지셔닝 (Tactical Positioning) ===
    
    /** 적 거점 접근성 - 공격성의 핵심 지표 */
    float EnemyObjectiveProximity;  // +1.0 = 적극 접근, -1.0 = 회피
    
    /** 아군 거점 접근성 - 방어성의 핵심 지표 */
    float AllyObjectiveProximity;   // +1.0 = 거점 방어, -1.0 = 이탈
    
    /** 엄폐물 선호도 - 생존성 지표 */
    float CoverDensity;             // +1.0 = 엄폐 우선, -1.0 = 개활지 선호
    
    /** 적 가시성 제어 */
    float EnemyVisibility;          // +1.0 = 적 시야 확보, -1.0 = 은신
    
    /** 팀원 근접성 - 협동 행동 */
    float AllyProximity;            // +1.0 = 팀 밀집, -1.0 = 분산
    
    /** 교전 거리 선호도 */
    float CombatRange;              // +1.0 = 근거리, -1.0 = 원거리
    
    // === 유틸리티 행동 (Utility Behaviors) ===
    
    /** 체력팩/탄약 접근성 */
    float PickupProximity;          // +1.0 = 픽업 우선, -1.0 = 무시
    
    /** 고지대 선호도 */
    float HeightAdvantage;          // +1.0 = 높은 곳 선호, -1.0 = 저지대
    
    // 총 8-dim 출력
};

/**
 * 전략별 가중치 프로파일 예시
 */
namespace TacticalProfiles {
    // Attack Strategy
    constexpr FEQSWeightParameters AGGRESSIVE = {
        .EnemyObjectiveProximity = +0.9f,  // 적극 진출
        .AllyObjectiveProximity  = -0.3f,  // 아군 거점 무시
        .CoverDensity            = +0.2f,  // 최소한의 엄폐
        .EnemyVisibility         = +0.8f,  // 적 시야 확보
        .AllyProximity           = +0.4f,  // 팀과 함께 공격
        .CombatRange             = +0.6f,  // 중-근거리 교전
        .PickupProximity         = -0.5f,  // 픽업 무시
        .HeightAdvantage         = +0.5f   // 고지 장악
    };
    
    // Defend Strategy
    constexpr FEQSWeightParameters DEFENSIVE = {
        .EnemyObjectiveProximity = -0.7f,  // 적 거점 회피
        .AllyObjectiveProximity  = +0.9f,  // 아군 거점 고수
        .CoverDensity            = +0.8f,  // 엄폐 우선
        .EnemyVisibility         = +0.3f,  // 적 경계
        .AllyProximity           = +0.7f,  // 팀 결속
        .CombatRange             = -0.4f,  // 원거리 교전
        .PickupProximity         = +0.2f,  // 여유시 픽업
        .HeightAdvantage         = +0.7f   // 방어적 고지
    };
}
