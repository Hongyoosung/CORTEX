Phase 4: EQS Asset Creation Guide

  Step 1: Enable EQS Plugin

  1. Go to Edit > Plugins
  2. Search for "Environment Query"
  3. Enable the Environment Query Editor plugin
  4. Restart Unreal Editor

  Step 2: Create EQS Query Assets

  1. In Content Browser, navigate to Content/Game/Blueprints/AI/EQS/ (or create this folder structure)
  2. Right-click > Artificial Intelligence > Environment Query
  3. Create 5 assets with these exact names:
    - EQS_ForwardCover
    - EQS_RetreatCover
    - EQS_FlankLeft
    - EQS_FlankRight
    - EQS_Advance

  Step 3: Configure Each EQS Query

  Below are detailed configurations for each tactical query. Open each asset and follow the instructions.

  ---
  1. EQS_ForwardCover (Cover points toward objective)

  Purpose: Find cover positions that move the agent closer to the objective while providing protection.

  Configuration:

  1. Generator: Simple Grid
    - Right-click graph > Add Generator > Simple Grid
    - Connect to Root node
    - Settings:
        - Grid Size: 1500 units (15m radius)
      - Space Between: 200 units (2m spacing)
      - Generate Around: Querier
  2. Tests to Add (right-click the Generator > Add Test):

  2. a. Trace Test (Check for cover)
    - Test Purpose: Filter Only
    - Trace Mode: Geometry by Channel
    - Context: Querier
    - Trace From Context: ✅ Enabled
    - Item Height Offset: 150 (chest-high cover)
    - Filter: Keep positions that provide cover

  b. Distance Test (Closer to objective)
    - Test Purpose: Score Only
    - Distance To: EQS_ObjectiveContext (you'll need to create this - see Step 4)
    - Scoring Equation: Inverse Linear
    - Higher score = closer to objective

  c. Distance Test (Not too close to enemies)
    - Test Purpose: Score Only
    - Distance To: EQS_EnemiesContext (you'll need to create this)
    - Clamp Min Type: Absolute
    - Clamp Min: 500 (5m minimum)
    - Scoring Equation: Linear
    - Higher score = safer distance from enemies

  d. Dot Product Test (Forward direction toward objective)
    - Line A: Querier to Position
    - Line B: Querier to EQS_ObjectiveContext
    - Test Mode: Dot 3D
    - Absolute Value: ❌ Disabled
    - Scoring: Higher dot = more forward movement

  ---
  2. EQS_RetreatCover (Cover points away from enemies)

  Purpose: Find safe cover positions away from visible enemies.

  Configuration:

  1. Generator: Simple Grid
    - Grid Size: 2000 units (20m radius)
    - Space Between: 200 units
    - Generate Around: Querier
  2. Tests:

  2. a. Trace Test (Verify cover exists)
    - Same as ForwardCover

  b. Distance Test (Farther from enemies)
    - Distance To: EQS_EnemiesContext
    - Scoring Equation: Linear
    - Higher score = farther from enemies

  c. Dot Product Test (Backward direction from enemies)
    - Line A: Querier to Position
    - Line B: EQS_EnemiesContext to Querier (reversed)
    - Absolute Value: ❌ Disabled
    - Scoring: Higher dot = moving away from threat

  d. Path Finding Test (Ensure reachable)
    - Test Purpose: Filter Only
    - Path From Context: Querier
    - Filter: Remove unreachable positions

  ---
  3. EQS_FlankLeft (Left flank positions)

  Purpose: Find positions to the left of current position relative to objective/enemies.    

  Configuration:

  1. Generator: Points: Circle
    - Circle Radius: 1000 units
    - Space Between: 150 units
    - Arc Direction: Querier Rotation (or use custom context)
    - Arc Angle: 90 degrees (left quadrant only)
    - Angle Offset: 90 (rotate to left side)
    - Generate Around: Querier
  2. Tests:

  2. a. Trace Test (Optional - only if flanking with cover)
    - Same as above, or disable for aggressive flanking

  b. Distance Test (Maintain tactical distance)
    - Distance To: EQS_EnemiesContext
    - Clamp Min: 400, Clamp Max: 1200
    - Scoring: Constant (filter only)

  c. Dot Product Test (Verify left-side positioning)
    - Line A: Querier to Position
    - Line B: Querier Right Vector (perpendicular left)
    - Scoring: Higher dot = more left

  d. Path Finding Test
    - Filter unreachable positions

  ---
  4. EQS_FlankRight (Right flank positions)

  Purpose: Mirror of FlankLeft for right-side flanking.

  Configuration:

  1. Generator: Points: Circle
    - Same as FlankLeft
    - Angle Offset: -90 (rotate to right side)
  2. Tests:
    - Same as FlankLeft, but reverse the dot product or use Right Vector with negative scoring

  ---
  5. EQS_Advance (Forward positions, no cover required)

  Purpose: Aggressive forward movement toward objective without cover requirement.

  Configuration:

  1. Generator: Points: Circle or Simple Grid
    - Circle Radius: 800 units
    - Space Between: 150 units
    - Arc Direction: Querier Rotation
    - Arc Angle: 120 degrees (forward arc)
    - Generate Around: Querier
  2. Tests:

  2. a. Distance Test (Closer to objective)
    - Distance To: EQS_ObjectiveContext
    - Scoring: Inverse Linear
    - Higher score = closer

  b. Dot Product Test (Forward direction)
    - Line A: Querier to Position
    - Line B: Querier to EQS_ObjectiveContext
    - Higher dot = more forward

  c. Path Finding Test (Must be reachable)
    - Filter only

  d. Trace Test (Optional - check line of sight to enemies)
    - Test Purpose: Score Only
    - Higher score if visible to enemies (aggressive play)

  ---
  Step 4: Create Custom EQS Contexts

  Your queries need custom contexts to reference objectives and enemies. Create Blueprint contexts:

  1. EQS_ObjectiveContext (returns current objective location)
    - Right-click > Blueprint Class > EnvQueryContext_BlueprintBase
    - Name: EQS_ObjectiveContext
    - Override Provide Single Location or Provide Single Actor
    - Get objective from your SharedContext or game state
    - Return objective actor or location
  2. EQS_EnemiesContext (returns visible enemy locations)
    - Blueprint Class > EnvQueryContext_BlueprintBase
    - Name: EQS_EnemiesContext
    - Override Provide Locations Set or Provide Actors Set
    - Get SharedContext.VisibleEnemies array
    - Return enemy actors or their locations

  Example Blueprint for EQS_ObjectiveContext:
  ProvideLocationsSet:
    Get Controlled Pawn > Cast to FollowerAgent
    > Get Current Objective Location
    > Make Array > Set Resulting Locations

  ---
  Step 5: Testing EQS Queries in Editor

  1. Enable EQS Debugging:
    - Press ` (backtick) in PIE
    - Type: eqs debug [AgentName]
    - Or use: Gameplay Debugger (' apostrophe key) > EQS category
  2. Visual Debugging:
    - Enabled EQS queries show:
        - Green spheres = high-scoring positions
      - Red spheres = low-scoring positions
      - Yellow lines = test results
  3. Check Logs:
    - Your RunEQSQuery() implementation logs errors for:
        - Missing EQS assets
      - Query execution failures
      - No valid results

  ---
  Step 6: Common Issues & Solutions

  | Issue                      | Solution                                                   
     |
  |----------------------------|-----------------------------------------------------------------|
  | "Failed to load EQS asset" | Verify asset path: /Game/AI/EQS/{QueryName}.uasset matches code |
  | No positions returned      | Check generator radius, grid density, and filter tests          |
  | Agent doesn't move         | Ensure PathFinding test doesn't over-filter positions           |
  | Crashes on query           | Verify contexts return valid actors/locations              
     |
  | Poor positioning           | Adjust test weights in Details panel (Score Normalization)      |

  ---
  Step 7: Advanced Configuration

  Test Scoring Weights:
  - Select each test node
  - Details panel > Score section
  - Adjust Scoring Factor (default 1.0)
  - Higher factor = more influence on final score

  Normalization:
  - Absolute Filter - Hard pass/fail (use for critical tests like PathFinding)
  - Score Only - Influences ranking
  - Filter and Score - Both filter and rank

  Performance Tips:
  - Keep Grid Size under 2000 units
  - Use Space Between > 150 to reduce test points
  - Put expensive tests (PathFinding, Trace) as filters early
  - Cache context results when possible

  ---
  Step 8: Integration Verification

  After creating all assets, verify your C++ integration:

  1. Check Asset Paths:
  Your code loads from: Source/GameAI_Project/Private/StateTree/Tasks/STTask_ExecuteObjective.cpp:520
  FString AssetPath = FString::Printf(TEXT("/Game/AI/EQS/%s.%s"), *QueryName, *QueryName);  

  1. Assets must be at: Content/Game/AI/EQS/{QueryName}.uasset
  2. Compile and Test:
    - Compile C++ project (Build > Compile)
    - PIE with TacticalActuator agent
    - Watch logs for EQS execution
    - Enable EQS debugging to visualize queries
  3. Expected Behavior:
    - Agent executes macro actions from neural network
    - EQS queries return positions
    - Agent moves to highest-scored position
    - Logs show successful query execution

  ---
  Next Steps After Asset Creation

  Once all 5 EQS assets are created and tested:

  1. ✅ Run PIE and verify no "Failed to load EQS asset" errors
  2. ✅ Test each tactical position manually (use console commands or test behavior tree)   
  3. ✅ Enable EQS debugging and verify positions are sensible
  4. ✅ Proceed to Python RLlib training with v4.0 MultiDiscrete actions
  5. ✅ Monitor training for movement quality and convergence
