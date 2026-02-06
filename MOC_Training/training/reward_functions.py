# File: training/reward_functions.py

class EQSAwareRewards:
    """
    EQS가 선택한 위치의 품질을 RL 보상에 반영
    """
    
    @staticmethod
    def compute_position_quality_reward(state, next_state, eqs_weights):
        """
        선택된 위치가 전략적 목표를 달성했는지 평가
        """
        reward = 0.0
        
        # Attack Strategy 보상
        if eqs_weights['EnemyObjectiveProximity'] > 0.5:
            # 적 거점에 실제로 접근했는가?
            distance_reduction = (
                state['enemy_obj_distance'] - next_state['enemy_obj_distance']
            )
            reward += 0.5 * distance_reduction / 100.0  # 1m당 0.005 보상
            
            # 시야 확보 성공
            if next_state['enemy_visible'] and not state['enemy_visible']:
                reward += 1.0
        
        # Defend Strategy 보상
        if eqs_weights['AllyObjectiveProximity'] > 0.5:
            # 아군 거점 방어 위치 점유
            if next_state['on_ally_objective']:
                reward += 2.0
            
            # 엄폐 상태 유지
            if next_state['in_cover']:
                reward += 0.5
        
        # 페널티: 비효율적 이동
        movement_distance = np.linalg.norm(
            next_state['position'] - state['position']
        )
        if movement_distance < 50.0:  # 너무 짧은 이동
            reward -= 0.2
        
        return reward
