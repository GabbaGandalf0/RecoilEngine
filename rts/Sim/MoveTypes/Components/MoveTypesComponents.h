/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef MOVE_TYPE_COMPONENTS_H__
#define MOVE_TYPE_COMPONENTS_H__

#include "MoveTypesEvents.h"
#include "System/Ecs/Components/BaseComponents.h"
#include <System/Threading/ThreadPool.h>

struct CUnit;
struct CFeature;

namespace MoveTypes {

// For move types that need to be handled single threaded.
ALIAS_COMPONENT(GeneralMoveType, int);

// Special multi-thread ground move type.
ALIAS_COMPONENT(GroundMoveType, int);

// Used by units that have updated the ground collision map and may have trapped units as a result.
// This is used to allow such a situation to be detected immediately. The fall-back checks are too
// slow in practice.
enum UnitTrapCheckType {
    TRAPPER_IS_UNIT,
    TRAPPER_IS_FEATURE
};
struct UnitTrapCheck {
    UnitTrapCheckType type;
    int id;
};

struct YardmapTrapCheckSystemComponent {
	static constexpr std::size_t page_size = 1;
    static constexpr std::size_t INITIAL_TRAP_UNIT_LIST_ALLOC_SIZE = 8;

	std::array<std::vector<CUnit*>, ThreadPool::MAX_THREADS> trappedUnitLists;
};

constexpr size_t UNIT_EVENT_VECTOR_RESERVE = 4;

ALIAS_COMPONENT_LIST_RESERVE(FeatureCollisionEvents, std::vector<FeatureCollisionEvent>, UNIT_EVENT_VECTOR_RESERVE);
ALIAS_COMPONENT_LIST_RESERVE(UnitCollisionEvents, std::vector<UnitCollisionEvent>, UNIT_EVENT_VECTOR_RESERVE);
ALIAS_COMPONENT_LIST_RESERVE(FeatureCrushEvents, std::vector<FeatureCrushEvent>, UNIT_EVENT_VECTOR_RESERVE);
ALIAS_COMPONENT_LIST_RESERVE(UnitCrushEvents, std::vector<UnitCrushEvent>, UNIT_EVENT_VECTOR_RESERVE);
ALIAS_COMPONENT_LIST_RESERVE(FeatureMoveEvents, std::vector<FeatureMoveEvent>, UNIT_EVENT_VECTOR_RESERVE);

template<class Archive>
void serialize(Archive &ar, UnitTrapCheck &c) { ar(c.type, c.id); }

int GetLoadSaveUnitID(const CUnit* unit);
int GetLoadSaveFeatureID(const CFeature* feature);
void ResolveLoadSaveEventPointers();
void ResetLoadSaveTransientEvents();

template<class Archive>
void save(Archive &ar, const FeatureCollisionEvent &c)
{
    const int colliderId = GetLoadSaveUnitID(c.collider);
    const int collideeId = GetLoadSaveFeatureID(c.collidee);
    ar(colliderId, collideeId);
}

template<class Archive>
void load(Archive &ar, FeatureCollisionEvent &c)
{
    ar(c.colliderId, c.collideeId);
    c.collider = nullptr;
    c.collidee = nullptr;
}

template<class Archive>
void save(Archive &ar, const FeatureCrushEvent &c)
{
    const int colliderId = GetLoadSaveUnitID(c.collider);
    const int collideeId = GetLoadSaveFeatureID(c.collidee);
    ar(colliderId, collideeId, c.crushImpulse);
}

template<class Archive>
void load(Archive &ar, FeatureCrushEvent &c)
{
    ar(c.colliderId, c.collideeId, c.crushImpulse);
    c.collider = nullptr;
    c.collidee = nullptr;
}

template<class Archive>
void save(Archive &ar, const FeatureMoveEvent &c)
{
    const int colliderId = GetLoadSaveUnitID(c.collider);
    const int collideeId = GetLoadSaveFeatureID(c.collidee);
    ar(colliderId, collideeId, c.moveImpulse);
}

template<class Archive>
void load(Archive &ar, FeatureMoveEvent &c)
{
    ar(c.colliderId, c.collideeId, c.moveImpulse);
    c.collider = nullptr;
    c.collidee = nullptr;
}

template<class Archive>
void save(Archive &ar, const UnitCollisionEvent &c)
{
    const int colliderId = GetLoadSaveUnitID(c.collider);
    const int collideeId = GetLoadSaveUnitID(c.collidee);
    ar(colliderId, collideeId);
}

template<class Archive>
void load(Archive &ar, UnitCollisionEvent &c)
{
    ar(c.colliderId, c.collideeId);
    c.collider = nullptr;
    c.collidee = nullptr;
}

template<class Archive>
void save(Archive &ar, const UnitCrushEvent &c)
{
    const int colliderId = GetLoadSaveUnitID(c.collider);
    const int collideeId = GetLoadSaveUnitID(c.collidee);
    ar(colliderId, collideeId, c.crushImpulse);
}

template<class Archive>
void load(Archive &ar, UnitCrushEvent &c)
{
    ar(c.colliderId, c.collideeId, c.crushImpulse);
    c.collider = nullptr;
    c.collidee = nullptr;
}

template<class Archive>
void save(Archive &ar, const UnitMovedEvent &c)
{
    const int unitId = GetLoadSaveUnitID(c.unit);
    ar(unitId, c.moved);
}

template<class Archive>
void load(Archive &ar, UnitMovedEvent &c)
{
    ar(c.unitId, c.moved);
    c.unit = nullptr;
}

template<class Archive>
void serialize(Archive &ar, FeatureCollisionEvents &c) { ar(c.value); }

template<class Archive>
void serialize(Archive &ar, UnitCollisionEvents &c) { ar(c.value); }

template<class Archive>
void serialize(Archive &ar, FeatureCrushEvents &c) { ar(c.value); }

template<class Archive>
void serialize(Archive &ar, UnitCrushEvents &c) { ar(c.value); }

template<class Archive>
void serialize(Archive &ar, FeatureMoveEvents &c) { ar(c.value); }

template<class Archive>
void serialize(Archive &ar, ChangeHeadingEvent &c) { ar(c.unitId, c.deltaHeading, c.changed); }

template<class Archive>
void serialize(Archive &ar, ChangeMainHeadingEvent &c) { ar(c.unitId, c.changed); }

template<class Archive, class Snapshot>
void serializeComponents(Archive &archive, Snapshot &snapshot) {
    snapshot.template component
        < GeneralMoveType, GroundMoveType, UnitTrapCheck,
          FeatureCollisionEvents, UnitCollisionEvents, FeatureCrushEvents, UnitCrushEvents, FeatureMoveEvents,
          UnitMovedEvent, ChangeHeadingEvent, ChangeMainHeadingEvent
        >(archive);
}

}

#endif
