/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef MOVE_TYPE_EVENTS_H__
#define MOVE_TYPE_EVENTS_H__

#include "System/float3.h"

class CUnit;
class CFeature;
class CGroundMoveType;

namespace MoveTypes {

struct FeatureCollisionEvent {
    CUnit* collider = nullptr;
    CFeature* collidee = nullptr;
    int colliderId = -1;
    int collideeId = -1;

    FeatureCollisionEvent() = default;
    FeatureCollisionEvent(CUnit* _collider, CFeature* _collidee)
    : collider(_collider)
    , collidee(_collidee)
    {}
};

struct FeatureCrushEvent {
    CUnit* collider = nullptr;
    CFeature* collidee = nullptr;
    float3 crushImpulse;
    int colliderId = -1;
    int collideeId = -1;

    FeatureCrushEvent() = default;
    FeatureCrushEvent(CUnit* _collider, CFeature* _collidee, float3 _crushImpulse)
    : collider(_collider)
    , collidee(_collidee)
    , crushImpulse(_crushImpulse)
    {}
};

struct FeatureMoveEvent {
    CUnit* collider = nullptr;
    CFeature* collidee = nullptr;
    float3 moveImpulse;
    int colliderId = -1;
    int collideeId = -1;

    FeatureMoveEvent() = default;
    FeatureMoveEvent(CUnit* _collider, CFeature* _collidee, float3 _moveImpulse)
    : collider(_collider)
    , collidee(_collidee)
    , moveImpulse(_moveImpulse)
    {}
};

struct UnitCollisionEvent {
    CUnit* collider = nullptr;
    CUnit* collidee = nullptr;
    int colliderId = -1;
    int collideeId = -1;

    UnitCollisionEvent() = default;
    UnitCollisionEvent(CUnit* _collider, CUnit* _collidee)
    : collider(_collider)
    , collidee(_collidee)
    {}
};

struct UnitCrushEvent {
    CUnit* collider = nullptr;
    CUnit* collidee = nullptr;
    float3 crushImpulse;
    int colliderId = -1;
    int collideeId = -1;

    UnitCrushEvent() = default;
    UnitCrushEvent(CUnit* _collider, CUnit* _collidee, float3 _crushImpulse)
    : collider(_collider)
    , collidee(_collidee)
    , crushImpulse(_crushImpulse)
    {}
};

struct UnitMovedEvent {
    CUnit* unit = nullptr;
    int unitId = -1;
    bool moved = false;
};

struct ChangeHeadingEvent {
    int unitId = -1;
    short deltaHeading = 0;
    bool changed = false;

    ChangeHeadingEvent() = default;
    ChangeHeadingEvent(int _unitId)
    : unitId(_unitId)
    {}
};

struct ChangeMainHeadingEvent {
    int unitId = -1;
    bool changed = false;

    ChangeMainHeadingEvent() = default;
    ChangeMainHeadingEvent(int _unitId)
    : unitId(_unitId)
    {}
};

}

#endif
