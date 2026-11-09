#ifndef MONSTER_SDF_FROZEN_ORACLE_H
#define MONSTER_SDF_FROZEN_ORACLE_H

#include "MonsterSDF.h"

/* Implementación F congelada en b799f269e980a358161f880c5ed32c361da91943. */
MonsterSDFConfig MonsterSDF_Frozen_DefaultConfig(void);
MonsterSDF MonsterSDF_Frozen_Create(void);
void MonsterSDF_Frozen_Free(MonsterSDF* sdf);
bool MonsterSDF_Frozen_Build(MonsterSDF* sdf,const Monster* monster,MonsterSDFConfig config);
SDFSample MonsterSDF_Frozen_Evaluate(const MonsterSDF* sdf,Vector3 point);
SDFSample MonsterSDF_Frozen_EvaluateDebug(const MonsterSDF* sdf,Vector3 point,MonsterHeadDebugMode mode);
float MonsterSDF_Frozen_EvaluateDistance(const MonsterSDF* sdf,Vector3 point);

#endif
