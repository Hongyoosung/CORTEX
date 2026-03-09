#include "Team/DETeamInterface.h"

// Add default functionality here for any IDETeamInterface functions that are not pure virtual.

bool IDETeamInterface::IsEnemy(AActor* Other) const
{
    if (!Other || !Other->Implements<UDETeamInterface>())
    {
        return false;
    }

    // 인터페이스 함수 호출 (Execute_ 접두사 사용은 블루프린트 호환성 위함)
    int32 MyTeam = IDETeamInterface::Execute_GetTeamID(Cast<UObject>(this));
    int32 OtherTeam = IDETeamInterface::Execute_GetTeamID(Other);

    return (MyTeam != -1 && OtherTeam != -1 && MyTeam != OtherTeam);
}

bool IDETeamInterface::IsAlly(AActor* Other) const
{
    if (!Other || !Other->Implements<UDETeamInterface>())
    {
        return false;
    }

    int32 MyTeam = IDETeamInterface::Execute_GetTeamID(Cast<UObject>(this));
    int32 OtherTeam = IDETeamInterface::Execute_GetTeamID(Other);

    return (MyTeam != -1 && OtherTeam != -1 && MyTeam == OtherTeam);
}