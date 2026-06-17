/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// #undef NDEBUG

#include "GroundMoveSystem.h"

#include "Map/Ground.h"
#include "Sim/Ecs/Registry.h"
#include "Sim/Features/Feature.h"
#include "Sim/Misc/GlobalSynced.h"
#include "Sim/Misc/QuadField.h"
#include "Sim/MoveTypes/Components/MoveTypesComponents.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitHandler.h"

#include "System/Config/ConfigHandler.h"
#include "System/EventHandler.h"
#include "System/Log/ILog.h"
#include "System/TimeProfiler.h"
#include "System/Threading/ThreadPool.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

using namespace MoveTypes;

void GroundMoveSystem::Init() {}

namespace {
	struct RewindMovePhaseDigest {
		uint64_t kinematics = 1469598103934665603ull;
		uint64_t planning = 1469598103934665603ull;
		uint64_t transient = 1469598103934665603ull;
		uint64_t headingEvents = 1469598103934665603ull;
		uint64_t mainHeadingEvents = 1469598103934665603ull;
		uint64_t headingInputs = 1469598103934665603ull;
		uint64_t movementInputs = 1469598103934665603ull;
		uint64_t terrainInputs = 1469598103934665603ull;
	};

	static uint32_t RewindMoveFloatBits(const float value)
	{
		uint32_t bits = 0;
		static_assert(sizeof(bits) == sizeof(value));
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	static void RewindMoveMix(uint64_t& digest, const uint64_t value)
	{
		digest ^= value;
		digest *= 1099511628211ull;
	}

	static void RewindMoveMixFloat3(uint64_t& digest, const float3& value)
	{
		RewindMoveMix(digest, RewindMoveFloatBits(value.x));
		RewindMoveMix(digest, RewindMoveFloatBits(value.y));
		RewindMoveMix(digest, RewindMoveFloatBits(value.z));
	}

	static RewindMovePhaseDigest BuildRewindMovePhaseDigest()
	{
		RewindMovePhaseDigest digest;
		auto view = Sim::registry.view<GroundMoveType>();

		for (std::size_t i = 0; i < view.size(); ++i) {
			const auto entity = view.storage<GroundMoveType>()[i];
			const auto& unitId = view.get<GroundMoveType>(entity);
			const CUnit* unit = unitHandler.GetUnit(unitId.value);
			const auto* moveType = (unit != nullptr) ? static_cast<const CGroundMoveType*>(unit->moveType) : nullptr;

			RewindMoveMix(digest.kinematics, static_cast<uint64_t>(unitId.value));
			if (unit == nullptr || moveType == nullptr)
				continue;

			RewindMoveMixFloat3(digest.kinematics, unit->pos);
			RewindMoveMix(digest.kinematics, RewindMoveFloatBits(unit->speed.x));
			RewindMoveMix(digest.kinematics, RewindMoveFloatBits(unit->speed.y));
			RewindMoveMix(digest.kinematics, RewindMoveFloatBits(unit->speed.z));
			RewindMoveMix(digest.kinematics, RewindMoveFloatBits(unit->speed.w));
			RewindMoveMixFloat3(digest.kinematics, unit->frontdir);
			RewindMoveMixFloat3(digest.kinematics, unit->rightdir);
			RewindMoveMixFloat3(digest.kinematics, unit->updir);
			RewindMoveMix(digest.kinematics, static_cast<uint64_t>(unit->heading));

			RewindMoveMix(digest.planning, static_cast<uint64_t>(unitId.value));
			RewindMoveMix(digest.planning, static_cast<uint64_t>(moveType->GetPathID()));
			RewindMoveMixFloat3(digest.planning, moveType->goalPos);
			RewindMoveMixFloat3(digest.planning, moveType->GetCurrWayPoint());
			RewindMoveMixFloat3(digest.planning, moveType->GetNextWayPoint());
			RewindMoveMixFloat3(digest.planning, moveType->GetWaypointDir());
			RewindMoveMixFloat3(digest.planning, moveType->GetFlatFrontDir());
			RewindMoveMixFloat3(digest.planning, moveType->GetLastAvoidanceDir());
			RewindMoveMix(digest.planning, RewindMoveFloatBits(moveType->GetCurrWayPointDist()));
			RewindMoveMix(digest.planning, RewindMoveFloatBits(moveType->GetPrevWayPointDist()));
			RewindMoveMix(digest.planning, static_cast<uint64_t>(moveType->GetWantedHeading()));
			RewindMoveMix(digest.planning, static_cast<uint64_t>(moveType->GetSetHeadingState()));
			RewindMoveMix(digest.planning, static_cast<uint64_t>(moveType->GetSetHeadingDir()));
			RewindMoveMix(digest.planning, static_cast<uint64_t>(moveType->GetLimitSpeedForTurning()));
			RewindMoveMix(digest.planning, static_cast<uint64_t>(moveType->IsReversing()));
			RewindMoveMix(digest.planning, static_cast<uint64_t>(moveType->IsAtGoal()));

			RewindMoveMix(digest.transient, static_cast<uint64_t>(unitId.value));
			RewindMoveMix(digest.transient, RewindMoveFloatBits(moveType->GetWantedSpeed()));
			RewindMoveMix(digest.transient, RewindMoveFloatBits(moveType->GetCurrentSpeed()));
			RewindMoveMix(digest.transient, RewindMoveFloatBits(moveType->GetDeltaSpeed()));
			RewindMoveMix(digest.transient, RewindMoveFloatBits(moveType->GetOldSpeed()));
			RewindMoveMix(digest.transient, RewindMoveFloatBits(moveType->GetNewSpeed()));
			RewindMoveMixFloat3(digest.transient, moveType->GetResultantForces());
			RewindMoveMixFloat3(digest.transient, moveType->GetMovingCollisionForces());
			RewindMoveMixFloat3(digest.transient, moveType->GetStaticCollisionForces());
			RewindMoveMix(digest.transient, static_cast<uint64_t>(moveType->IsPositionStuck()));
			RewindMoveMix(digest.transient, static_cast<uint64_t>(moveType->IsAvoidingUnits()));

			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unitId.value));
			RewindMoveMix(digest.movementInputs, RewindMoveFloatBits(moveType->GetMyGravity()));
			RewindMoveMix(digest.movementInputs, RewindMoveFloatBits(moveType->GetTurnRate()));
			RewindMoveMix(digest.movementInputs, RewindMoveFloatBits(moveType->GetTurnSpeed()));
			RewindMoveMix(digest.movementInputs, RewindMoveFloatBits(moveType->GetTurnAccel()));
			RewindMoveMix(digest.movementInputs, RewindMoveFloatBits(moveType->GetMaxWantedSpeed()));
			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unit->IsOnGround()));
			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unit->IsInAir()));
			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unit->IsInWater()));
			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unit->IsFlying()));
			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unit->IsSkidding()));
			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unit->beingBuilt));
			RewindMoveMix(digest.movementInputs, static_cast<uint64_t>(unit->UnderFirstPersonControl()));
			RewindMoveMix(
				digest.movementInputs,
				static_cast<uint64_t>((unit->GetTransporter() != nullptr) ? unit->GetTransporter()->id : -1)
			);

			const float3 nextSamplePos = unit->pos + unit->speed;
			RewindMoveMix(digest.terrainInputs, static_cast<uint64_t>(unitId.value));
			RewindMoveMix(digest.terrainInputs, RewindMoveFloatBits(CGround::GetHeightReal(unit->pos.x, unit->pos.z)));
			RewindMoveMix(digest.terrainInputs, RewindMoveFloatBits(CGround::GetHeightReal(nextSamplePos.x, nextSamplePos.z)));
			RewindMoveMixFloat3(digest.terrainInputs, CGround::GetNormal(unit->pos.x, unit->pos.z));
			RewindMoveMixFloat3(digest.terrainInputs, CGround::GetNormal(nextSamplePos.x, nextSamplePos.z));
		}

		{
			auto headingView = Sim::registry.view<ChangeHeadingEvent>();
			RewindMoveMix(digest.headingEvents, static_cast<uint64_t>(headingView.size()));

			for (std::size_t i = 0; i < headingView.size(); ++i) {
				const auto entity = headingView.storage<ChangeHeadingEvent>()[i];
				const auto& event = headingView.get<ChangeHeadingEvent>(entity);

				RewindMoveMix(digest.headingEvents, static_cast<uint64_t>(entt::to_integral(entity)));
				RewindMoveMix(digest.headingEvents, static_cast<uint64_t>(event.unitId));
				RewindMoveMix(digest.headingEvents, static_cast<uint64_t>(static_cast<uint16_t>(event.deltaHeading)));
				RewindMoveMix(digest.headingEvents, static_cast<uint64_t>(event.changed));

				if (!event.changed)
					continue;

				const CUnit* unit = unitHandler.GetUnit(event.unitId);
				const auto* moveType = (unit != nullptr) ? static_cast<const CGroundMoveType*>(unit->moveType) : nullptr;
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(entt::to_integral(entity)));
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(event.unitId));

				if (unit == nullptr || moveType == nullptr) {
					RewindMoveMix(digest.headingInputs, 0xffffffffffffffffull);
					continue;
				}

				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(static_cast<uint16_t>(unit->heading)));
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(unit->IsFlying()));
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(unit->IsInAir()));
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(unit->IsOnGround()));
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(unit->upright));
				RewindMoveMix(
					digest.headingInputs,
					static_cast<uint64_t>((unit->GetTransporter() != nullptr) ? unit->GetTransporter()->id : -1)
				);
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(moveType->GetPathID()));
				RewindMoveMix(digest.headingInputs, RewindMoveFloatBits(moveType->GetTurnRate()));
				RewindMoveMix(digest.headingInputs, RewindMoveFloatBits(moveType->GetTurnSpeed()));
				RewindMoveMix(digest.headingInputs, RewindMoveFloatBits(moveType->GetTurnAccel()));
				RewindMoveMix(digest.headingInputs, static_cast<uint64_t>(static_cast<uint16_t>(moveType->GetWantedHeading())));
			}
		}

		{
			auto mainHeadingView = Sim::registry.view<ChangeMainHeadingEvent>();
			RewindMoveMix(digest.mainHeadingEvents, static_cast<uint64_t>(mainHeadingView.size()));

			for (std::size_t i = 0; i < mainHeadingView.size(); ++i) {
				const auto entity = mainHeadingView.storage<ChangeMainHeadingEvent>()[i];
				const auto& event = mainHeadingView.get<ChangeMainHeadingEvent>(entity);

				RewindMoveMix(digest.mainHeadingEvents, static_cast<uint64_t>(entt::to_integral(entity)));
				RewindMoveMix(digest.mainHeadingEvents, static_cast<uint64_t>(event.unitId));
				RewindMoveMix(digest.mainHeadingEvents, static_cast<uint64_t>(event.changed));
			}
		}

		return digest;
	}

	static void AuditRewindMovePhase(const unsigned int phase, const char* phaseName)
	{
		if (!configHandler->GetBool("RewindAudit"))
			return;

		static std::unordered_map<uint64_t, RewindMovePhaseDigest> references;
		static int firstDivergentFrame = -1;

		const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(gs->frameNum)) << 8) | phase;
		const RewindMovePhaseDigest current = BuildRewindMovePhaseDigest();
		const auto [it, inserted] = references.emplace(key, current);
		if (inserted)
			return;

		const RewindMovePhaseDigest& reference = it->second;
		if (
			reference.kinematics == current.kinematics &&
			reference.planning == current.planning &&
			reference.transient == current.transient &&
			reference.headingEvents == current.headingEvents &&
			reference.mainHeadingEvents == current.mainHeadingEvents &&
			reference.headingInputs == current.headingInputs &&
			reference.movementInputs == current.movementInputs &&
			reference.terrainInputs == current.terrainInputs
		) {
			return;
		}

		if (firstDivergentFrame < 0)
			firstDivergentFrame = gs->frameNum;
		if (gs->frameNum > firstDivergentFrame + 1)
			return;

		LOG(
			"[RewindMovePhaseAudit] frame=%d phase=%s kinematics=%llu/%llu planning=%llu/%llu transient=%llu/%llu headingEvents=%llu/%llu mainHeadingEvents=%llu/%llu headingInputs=%llu/%llu movementInputs=%llu/%llu terrainInputs=%llu/%llu",
			gs->frameNum,
			phaseName,
			static_cast<unsigned long long>(reference.kinematics),
			static_cast<unsigned long long>(current.kinematics),
			static_cast<unsigned long long>(reference.planning),
			static_cast<unsigned long long>(current.planning),
			static_cast<unsigned long long>(reference.transient),
			static_cast<unsigned long long>(current.transient),
			static_cast<unsigned long long>(reference.headingEvents),
			static_cast<unsigned long long>(current.headingEvents),
			static_cast<unsigned long long>(reference.mainHeadingEvents),
			static_cast<unsigned long long>(current.mainHeadingEvents),
			static_cast<unsigned long long>(reference.headingInputs),
			static_cast<unsigned long long>(current.headingInputs),
			static_cast<unsigned long long>(reference.movementInputs),
			static_cast<unsigned long long>(current.movementInputs),
			static_cast<unsigned long long>(reference.terrainInputs),
			static_cast<unsigned long long>(current.terrainInputs)
		);
	}
}

template<typename T, typename F>
void issue_events(F func)
{
    auto view = Sim::registry.view<T>();
    view.each([&](T& comp){
        std::for_each(comp.value.begin(), comp.value.end(), func);
        comp.value.clear();
    });
}

void GroundMoveSystem::Update() {
	AuditRewindMovePhase(0, "start");

    // TODO: GroundMove could become a component (or series of components) and then the extra indirection wouldn't be
    // needed. Though that will be a bigger change.
	{
		SCOPED_TIMER("Sim::Unit::MoveType::1::UpdateTraversalPlan");
        auto view = Sim::registry.view<GroundMoveType>();
        for_mt(0, view.size(), [&view](const int i){
            auto entity = view.storage<GroundMoveType>()[i];
            auto unitId = view.get<GroundMoveType>(entity);

            CUnit* unit = unitHandler.GetUnit(unitId.value);
			CGroundMoveType* moveType = static_cast<CGroundMoveType*>(unit->moveType);
            assert(moveType != nullptr);

            #ifndef NDEBUG
			unit->SanityCheck();
            #endif

			moveType->UpdateTraversalPlan();
		});
	}
	AuditRewindMovePhase(1, "after-traversal");
	{
		SCOPED_TIMER("Sim::Unit::MoveType::2::UpdatePreCollisions");

        // These two sections are ST due to the numerous synced vars being changed.
        {
            auto view = Sim::registry.view<ChangeHeadingEvent>();
            view.each([](ChangeHeadingEvent& event){
                if (event.changed) {
                    CUnit* unit = unitHandler.GetUnit(event.unitId);
                    CGroundMoveType* moveType = static_cast<CGroundMoveType*>(unit->moveType);
                    moveType->ChangeHeading(event.deltaHeading);
                    event.changed = false;
                }
            });
        }
        {
            auto view = Sim::registry.view<ChangeMainHeadingEvent>();
            view.each([](ChangeMainHeadingEvent& event){
                if (event.changed) {
                    CUnit* unit = unitHandler.GetUnit(event.unitId);
                    CGroundMoveType* moveType = static_cast<CGroundMoveType*>(unit->moveType);
                    moveType->SetMainHeading();
                    event.changed = false;
                }
            });
        }
    }
	AuditRewindMovePhase(2, "after-heading");
	{
        auto view = Sim::registry.view<GroundMoveType>();
        for_mt(0, view.size(), [&view](const int i){
            auto entity = view.storage<GroundMoveType>()[i];
            auto unitId = view.get<GroundMoveType>(entity);

            CUnit* unit = unitHandler.GetUnit(unitId.value);
			CGroundMoveType* moveType = static_cast<CGroundMoveType*>(unit->moveType);
            assert(moveType != nullptr);

			moveType->UpdateUnitPosition();
		});

		view.each([](GroundMoveType& unitId){
			CUnit* unit = unitHandler.GetUnit(unitId.value);
			CGroundMoveType* moveType = static_cast<CGroundMoveType*>(unit->moveType);
            assert(moveType != nullptr);

			moveType->UpdatePreCollisions();

            // this unit is not coming back, kill it now without any death
            // sequence (s.t. deathScriptFinished becomes true immediately)
            if (!unit->pos.IsInBounds() && (unit->speed.w > MAX_UNIT_SPEED))
                unit->ForcedKillUnit(nullptr, false, true, -CSolidObject::DAMAGE_KILLED_OOB);
		});
	}
	AuditRewindMovePhase(3, "after-position");
    {
        SCOPED_TIMER("Sim::Unit::MoveType::3::CollisionDetection");
        auto view = Sim::registry.view<GroundMoveType>();
        //size_t count = view.storage<GroundMoveType>().size();
        for_mt(0, view.size(), [&view](const int i){
            auto entity = view.storage<GroundMoveType>()[i];
            assert( Sim::registry.valid(entity) );
            assert( Sim::registry.all_of<GroundMoveType>(entity) );
            assert( !Sim::registry.all_of<GeneralMoveType>(entity) );

            auto unitId = view.get<GroundMoveType>(entity);

            CUnit* unit = unitHandler.GetUnit(unitId.value);
            CGroundMoveType* moveType = static_cast<CGroundMoveType*>(unit->moveType);
            assert(moveType != nullptr);

            moveType->SetMtJobId(i);
            moveType->UpdateCollisionDetections();
        });
    }
	AuditRewindMovePhase(4, "after-collision-detection");
	{
        SCOPED_TIMER("Sim::Unit::MoveType::4::ProcessCollisionEvents");

        issue_events<UnitCrushEvents>([](const UnitCrushEvent& event) {
            event.collidee->Kill(event.collider, event.crushImpulse, true);
        });
        issue_events<FeatureCrushEvents>([](const FeatureCrushEvent& event) {
            event.collidee->Kill(event.collider, event.crushImpulse, true);
        });
        issue_events<UnitCollisionEvents>([&](const UnitCollisionEvent& event) {
            eventHandler.UnitUnitCollision(event.collider, event.collidee);
        });
        issue_events<FeatureCollisionEvents>([](const FeatureCollisionEvent& event) {
            eventHandler.UnitFeatureCollision(event.collider, event.collidee);
        });
        issue_events<FeatureMoveEvents>([](const FeatureMoveEvent& event) {
            quadField.RemoveFeature(event.collidee);
            event.collidee->Move(event.moveImpulse, true);
            quadField.AddFeature(event.collidee);
        });
	}
	{
        // TODO: the vars are synced and that's what is stopping this being MT'ed.
        // Need an alternative method to support sync values that doesn't stop MT.
        // Same for change heading above as well.
        SCOPED_TIMER("Sim::Unit::MoveType::5::Update");
        auto view = Sim::registry.view<GroundMoveType>();
        view.each([&view](GroundMoveType& unitId){
            CUnit* unit = unitHandler.GetUnit(unitId.value);
            CGroundMoveType* moveType = static_cast<CGroundMoveType*>(unit->moveType);
            assert(moveType != nullptr);
            if (moveType->Update()) 
                eventHandler.UnitMoved(unit);

            #ifndef NDEBUG
            unit->SanityCheck();
            #endif
        });
    }
	AuditRewindMovePhase(5, "after-final-update");
}

void GroundMoveSystem::Shutdown() {}
