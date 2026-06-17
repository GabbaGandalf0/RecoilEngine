/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef SIM_ECS_REGISTRY_H__
#define SIM_ECS_REGISTRY_H__

#include "SaveLoadUtils.h"
#include "System/Ecs/EcsMain.h"
#include "System/Ecs/Utils/SystemGlobalUtils.h"
#include "System/Ecs/Utils/SystemUtils.h"

namespace Sim {
    extern entt::registry registry;
    extern SystemGlobals::SystemGlobal<> systemGlobals;
    extern SystemUtils::SystemUtils systemUtils;
    extern SaveLoadUtils saveLoadUtils;
}

#endif
