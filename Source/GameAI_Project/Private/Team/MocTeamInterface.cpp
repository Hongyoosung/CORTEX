#include "Team/MocTeamInterface.h"

// Add default functionality here for any IMocTeamInterface functions that are not pure virtual.

bool IMocTeamInterface::IsEnemy(AActor* Other) const
{
    if (!Other || !Other->Implements<UMocTeamInterface>())
    {
        return false;
    }

    // 인터페이스 함수 호출 (Execute_ 접두사 사용은 블루프린트 호환성 위함)
    int32 MyTeam = IMocTeamInterface::Execute_GetTeamID(Cast<UObject>(this));
    int32 OtherTeam = IMocTeamInterface::Execute_GetTeamID(Other);

    return (MyTeam != -1 && OtherTeam != -1 && MyTeam != OtherTeam);
}

bool IMocTeamInterface::IsAlly(AActor* Other) const
{
    if (!Other || !Other->Implements<UMocTeamInterface>())
    {
        return false;
    }

    int32 MyTeam = IMocTeamInterface::Execute_GetTeamID(Cast<UObject>(this));
    int32 OtherTeam = IMocTeamInterface::Execute_GetTeamID(Other);

    return (MyTeam != -1 && OtherTeam != -1 && MyTeam == OtherTeam);
}