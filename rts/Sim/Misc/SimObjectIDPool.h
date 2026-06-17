/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <vector>

#include "System/creg/creg_cond.h"
#include "System/UnorderedMap.hpp"

class CSolidObject;
class SimObjectIDPool {
	CR_DECLARE_STRUCT(SimObjectIDPool)

public:
	SimObjectIDPool() {} // FIXME: creg, needs PostLoad
	SimObjectIDPool(uint32_t maxObjects) {
		// pools are reused as part of object handlers, internal table sizes must be
		// constant at runtime to prevent desyncs between fresh and reloaded clients
		// (both must execute Expand since it touches the RNG)
		poolIDs.reserve(maxObjects);
		freeIDs.reserve(maxObjects);
		tempIDs.reserve(maxObjects);
	}

	void Expand(uint32_t baseID, uint32_t numIDs);
	void Clear() {
		freeIDs.clear();
		poolIDs.clear();
		tempIDs.clear();
		freeIDOrder.clear();
		tempIDOrder.clear();
		freeIDOrderCursor = 0;
	}

	void AssignID(CSolidObject* object);
	void FreeID(uint32_t uid, bool delayed);

	bool RecycleID(uint32_t uid);
	bool HasID(uint32_t uid) const;
	bool IsEmpty() const { return (freeIDs.empty()); }

	uint32_t GetSize() const { return (freeIDs.size()); } // number of ID's still unused
	uint32_t MaxSize() const { return (poolIDs.size()); } // number of ID's this pool owns
	uint64_t GetDebugDigest() const;

private:
	using IDMap = spring::unordered_map<uint32_t, uint32_t>;

	uint32_t ExtractID();

	void ReserveID(uint32_t uid);
	void RecycleIDs();
	void PostLoad();
	void RefreshFreeIDOrder();
	void RefreshTempIDOrder();

private:
	IDMap poolIDs; // uid to idx
	IDMap freeIDs; // idx to uid
	IDMap tempIDs; // idx to uid
	std::vector<uint32_t> freeIDOrder; // explicit UID extraction order, required for deterministic load/save
	std::vector<uint32_t> tempIDOrder; // explicit delayed-recycle UID order
	uint32_t freeIDOrderCursor = 0;
};
