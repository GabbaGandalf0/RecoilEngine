/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "SimObjectIDPool.h"
#include "GlobalConstants.h"
#include "GlobalSynced.h"
#include "Sim/Objects/SolidObject.h"
#include "System/Cpp11Compat.hpp"
#include "System/creg/STL_Map.h"

#include "System/Misc/TracyDefs.h"

#include <algorithm>
#include <numeric>
#include <vector>


CR_BIND(SimObjectIDPool, )
CR_REG_METADATA(SimObjectIDPool, (
	CR_MEMBER(poolIDs),
	CR_MEMBER(freeIDs),
	CR_MEMBER(tempIDs),
	CR_MEMBER(freeIDOrder),
	CR_MEMBER(tempIDOrder),
	CR_MEMBER(freeIDOrderCursor),
	CR_POSTLOAD(PostLoad)
))


void SimObjectIDPool::Expand(uint32_t baseID, uint32_t numIDs) {
	RECOIL_DETAILED_TRACY_ZONE;
	std::vector<uint32_t> newIDs(numIDs);

	// allocate new batch of (randomly shuffled) id's
	std::iota(newIDs.begin(), newIDs.end(), baseID);

	// randomize so that Lua widgets can not easily determine counts
	spring::random_shuffle(newIDs.begin(), newIDs.end(), gsRNG);
	spring::random_shuffle(newIDs.begin(), newIDs.end(), gsRNG);

	// NOTE:
	//   any randomization would be undone by a sorted std::container
	//   instead create a bi-directional mapping from indices to ID's
	//   (where the ID's are a random permutation of the index range)
	//   such that ID's can be assigned and returned to the pool with
	//   their original index, e.g.
	//
	//     freeIDs<idx, uid> = {<0, 13>, < 1, 27>, < 2, 54>, < 3, 1>, ...}
	//     poolIDs<uid, idx> = {<1,  3>, <13,  0>, <27,  1>, <54, 2>, ...}
	//
	//   (the ID --> index map is never changed at runtime!)
	for (uint32_t offsetID = 0; offsetID < numIDs; offsetID++) {
		freeIDs.emplace(baseID + offsetID, newIDs[offsetID]);
		poolIDs.emplace(newIDs[offsetID], baseID + offsetID);
	}

	RefreshFreeIDOrder();
}

void SimObjectIDPool::RefreshFreeIDOrder()
{
	RECOIL_DETAILED_TRACY_ZONE;
	freeIDOrder.clear();
	freeIDOrder.reserve(freeIDs.size());

	for (const auto& entry: freeIDs) {
		freeIDOrder.emplace_back(entry.second);
	}

	freeIDOrderCursor = 0;
}

void SimObjectIDPool::RefreshTempIDOrder()
{
	RECOIL_DETAILED_TRACY_ZONE;
	tempIDOrder.clear();
	tempIDOrder.reserve(tempIDs.size());

	for (const auto& entry: tempIDs) {
		tempIDOrder.emplace_back(entry.second);
	}
}

void SimObjectIDPool::PostLoad()
{
	RECOIL_DETAILED_TRACY_ZONE;

	// Older checkpoint files do not contain explicit extraction orders.
	// New checkpoints keep the serialized UID order so newly created objects
	// receive the same IDs after loading a rewind checkpoint.
	if (freeIDOrder.empty() && !freeIDs.empty())
		RefreshFreeIDOrder();
	if (tempIDOrder.empty() && !tempIDs.empty())
		RefreshTempIDOrder();

	if (freeIDOrderCursor > freeIDOrder.size())
		freeIDOrderCursor = 0;
}



void SimObjectIDPool::AssignID(CSolidObject* object) {
	RECOIL_DETAILED_TRACY_ZONE;
	if (object->id < 0) {
		object->id = ExtractID();
	} else {
		ReserveID(object->id);
	}
}

uint32_t SimObjectIDPool::ExtractID() {
	RECOIL_DETAILED_TRACY_ZONE;
	// extract a random ID from the pool
	//
	// should be unreachable, UnitHandler
	// and FeatureHandler have safeguards
	assert(!IsEmpty());

	if (freeIDOrderCursor >= freeIDOrder.size())
		RefreshFreeIDOrder();

	auto it = freeIDs.end();
	uint32_t uid = 0;
	while (freeIDOrderCursor < freeIDOrder.size()) {
		uid = freeIDOrder[freeIDOrderCursor++];
		const auto poolIt = poolIDs.find(uid);

		if (poolIt == poolIDs.end())
			continue;

		it = freeIDs.find(poolIt->second);
		if (it != freeIDs.end())
			break;
	}

	if (it == freeIDs.end()) {
		RefreshFreeIDOrder();
		assert(!freeIDOrder.empty());

		while (freeIDOrderCursor < freeIDOrder.size()) {
			uid = freeIDOrder[freeIDOrderCursor++];
			const auto poolIt = poolIDs.find(uid);

			if (poolIt == poolIDs.end())
				continue;

			it = freeIDs.find(poolIt->second);
			if (it != freeIDs.end())
				break;
		}
	}

	assert(it != freeIDs.end());
	assert(it->second == uid);

	freeIDs.erase(it);

	if (IsEmpty()) {
		RecycleIDs();
	}

	return uid;
}

void SimObjectIDPool::ReserveID(uint32_t uid) {
	RECOIL_DETAILED_TRACY_ZONE;
	// reserve a chosen ID from the pool
	assert(HasID(uid));
	assert(!IsEmpty());

	const auto it = poolIDs.find(uid);
	const uint32_t idx = it->second;

	freeIDs.erase(idx);

	if (IsEmpty())
		RecycleIDs();
}

void SimObjectIDPool::FreeID(uint32_t uid, bool delayed) {
	RECOIL_DETAILED_TRACY_ZONE;
	// put an ID back into the pool either immediately
	// or after all remaining free ID's run out (which
	// is better iff the object count never gets close
	// to the maximum)
	assert(!HasID(uid));

	const uint32_t idx = poolIDs[uid];

	if (delayed) {
		tempIDs.emplace(idx, uid);
		tempIDOrder.emplace_back(uid);
	} else {
		freeIDs.emplace(idx, uid);
		freeIDOrder.emplace_back(uid);
	}

	//handle the corner case of maximum allocation
	if (IsEmpty())
		RecycleIDs();
}

bool SimObjectIDPool::RecycleID(uint32_t uid) {
	RECOIL_DETAILED_TRACY_ZONE;
	assert(poolIDs.find(uid) != poolIDs.end());

	const uint32_t idx = poolIDs[uid];
	const auto it = tempIDs.find(idx);

	if (it == tempIDs.end())
		return false;

	tempIDs.erase(idx);
	freeIDs.emplace(idx, uid);
	tempIDOrder.erase(std::remove(tempIDOrder.begin(), tempIDOrder.end(), uid), tempIDOrder.end());
	freeIDOrder.emplace_back(uid);

	return true;
}

void SimObjectIDPool::RecycleIDs() {
	RECOIL_DETAILED_TRACY_ZONE;

	freeIDOrder.clear();
	freeIDOrderCursor = 0;

	// throw each ID recycled up until now back into the pool
	for (const uint32_t uid: tempIDOrder) {
		const auto poolIt = poolIDs.find(uid);

		if (poolIt == poolIDs.end())
			continue;

		const auto it = tempIDs.find(poolIt->second);
		if (it != tempIDs.end()) {
			freeIDs.emplace(it->first, it->second);
			freeIDOrder.emplace_back(it->second);
		}
	}
	for (const auto& [idx, uid]: tempIDs) {
		const auto inserted = freeIDs.emplace(idx, uid);
		if (inserted.second)
			freeIDOrder.emplace_back(uid);
	}
	tempIDs.clear();
	tempIDOrder.clear();
}


bool SimObjectIDPool::HasID(uint32_t uid) const {
	RECOIL_DETAILED_TRACY_ZONE;
	assert(poolIDs.find(uid) != poolIDs.end());

	// check if given ID is available (to be assigned) in this pool
	const auto it = poolIDs.find(uid);
	const uint32_t idx = it->second;

	return (freeIDs.find(idx) != freeIDs.end());
}

uint64_t SimObjectIDPool::GetDebugDigest() const
{
	RECOIL_DETAILED_TRACY_ZONE;

	uint64_t digest = 1469598103934665603ull;
	const auto mix = [&digest](const uint64_t value) {
		digest ^= value;
		digest *= 1099511628211ull;
	};
	const auto mixMap = [&mix](const IDMap& map) {
		std::vector<std::pair<uint32_t, uint32_t>> entries;
		entries.reserve(map.size());

		for (const auto& entry: map) {
			entries.emplace_back(entry.first, entry.second);
		}

		std::sort(entries.begin(), entries.end());
		mix(entries.size());

		for (const auto& [key, value]: entries) {
			mix(key);
			mix(value);
		}
	};
	const auto mixVector = [&mix](const std::vector<uint32_t>& values) {
		mix(values.size());

		for (const uint32_t value: values) {
			mix(value);
		}
	};

	mixMap(poolIDs);
	mixMap(freeIDs);
	mixMap(tempIDs);
	mixVector(freeIDOrder);
	mixVector(tempIDOrder);
	mix(freeIDOrderCursor);

	return digest;
}
