/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include <cstdlib>
#include <cstring> // memcpy
#include <set>

#include "xsimd/xsimd.hpp"
#include "ReadMap.h"
#include "MapDamage.h"
#include "MapInfo.h"
#include "MetalMap.h"
#include "Rendering/Env/MapRendering.h"
#include "SMF/SMFReadMap.h"
#include "Game/LoadScreen.h"
#include "System/EventHandler.h"
#include "System/Exceptions.h"
#include "System/SpringMath.h"
#include "System/Threading/ThreadPool.h"
#include "System/FileSystem/ArchiveScanner.h"
#include "System/FileSystem/FileHandler.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Log/ILog.h"
#include "System/SpringHash.h"
#include "System/SafeUtil.h"
#include "System/TimeProfiler.h"
#include "System/XSimdOps.hpp"
#include "Game/GlobalUnsynced.h"
#include "Sim/Misc/LosHandler.h"

#include "System/Misc/TracyDefs.h"

static constexpr size_t MAX_UHM_RECTS_PER_FRAME = 128;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////



CR_BIND(MapDimensions, ())
CR_REG_METADATA(MapDimensions, (
	CR_MEMBER(mapx),
	CR_MEMBER(mapxm1),
	CR_MEMBER(mapxp1),

	CR_MEMBER(mapy),
	CR_MEMBER(mapym1),
	CR_MEMBER(mapyp1),

	CR_MEMBER(mapSquares),

	CR_MEMBER(hmapx),
	CR_MEMBER(hmapy),
	CR_MEMBER(pwr2mapx),
	CR_MEMBER(pwr2mapy)
))

CR_BIND_INTERFACE(CReadMap)
CR_REG_METADATA(CReadMap, (
	CR_IGNORED(hmUpdated),
	CR_IGNORED(processingHeightBounds),
	CR_IGNORED(initHeightBounds),
	CR_IGNORED(tempHeightBounds),
	CR_IGNORED(currHeightBounds),
	CR_IGNORED(unsyncedHeightInfo),
	CR_IGNORED(boundingRadius),
	CR_IGNORED(mapChecksum),

	CR_IGNORED(heightMapSyncedPtr),
	CR_IGNORED(heightMapUnsyncedPtr),
	CR_IGNORED(originalHeightMapPtr),

	/*
	CR_IGNORED(originalHeightMap),
	CR_IGNORED(centerHeightMap),
	CR_IGNORED(mipCenterHeightMaps),
	*/
	CR_IGNORED(mipPointerHeightMaps),
	/*
	CR_IGNORED(visVertexNormals),
	CR_IGNORED(faceNormalsSynced),
	CR_IGNORED(faceNormalsUnsynced),
	CR_IGNORED(centerNormalsSynced),
	CR_IGNORED(centerNormalsUnsynced),
	CR_IGNORED(centerNormals2D),
	CR_IGNORED(slopeMap),
	CR_IGNORED(typeMap),
	*/

	CR_IGNORED(sharedCornerHeightMaps),
	CR_IGNORED(sharedCenterHeightMaps),
	CR_IGNORED(sharedFaceNormals),
	CR_IGNORED(sharedCenterNormals),
	CR_IGNORED(sharedSlopeMaps),

	CR_IGNORED(unsyncedHeightMapUpdates),

	/*
	CR_IGNORED(  syncedHeightMapDigests),
	CR_IGNORED(unsyncedHeightMapDigests),
	*/

	CR_POSTLOAD(PostLoad),
	CR_SERIALIZER(Serialize)
))



// initialized in CGame::LoadMap
CReadMap* readMap = nullptr;

MapDimensions mapDims;

std::vector<float> CReadMap::mapFileHeightMap;
std::vector<float> CReadMap::originalHeightMap;
std::vector<float> CReadMap::centerHeightMap;
std::vector<float> CReadMap::maxHeightMap;
std::array<std::vector<float>, CReadMap::numHeightMipMaps - 1> CReadMap::mipCenterHeightMaps;

std::vector<float3> CReadMap::faceNormalsSynced;
std::vector<float3> CReadMap::faceNormalsUnsynced;
std::vector<float3> CReadMap::centerNormalsSynced;
std::vector<float3> CReadMap::centerNormalsUnsynced;

std::vector<float> CReadMap::slopeMap;
std::vector<uint8_t> CReadMap::typeMap;
std::vector<float3> CReadMap::centerNormals2D;

std::vector<uint8_t> CReadMap::  syncedHeightMapDigests;
std::vector<uint8_t> CReadMap::unsyncedHeightMapDigests;


MapTexture::~MapTexture() {
	// do NOT delete a Lua-set texture here!
	glDeleteTextures(1, &texIDs[RAW_TEX_IDX]);

	texIDs[RAW_TEX_IDX] = 0;
	texIDs[LUA_TEX_IDX] = 0;
}



CReadMap* CReadMap::LoadMap(const std::string& mapName)
{
	RECOIL_DETAILED_TRACY_ZONE;
	CReadMap* rm = nullptr;

	if (FileSystem::GetExtension(mapName) == "sm3") {
		throw content_error("[CReadMap::LoadMap] SM3 maps are no longer supported as of Spring 95.0");
	} else {
		// assume SMF format by default; calls ::Initialize
		rm = new CSMFReadMap(mapName);
	}

	if (rm == nullptr)
		return nullptr;

	// read metal- and type-map
	MapBitmapInfo mbi;
	MapBitmapInfo tbi;

	unsigned char* metalmapPtr = rm->GetInfoMap("metal", &mbi);
	unsigned char* typemapPtr = rm->GetInfoMap("type", &tbi);

	assert(mbi.width == mapDims.hmapx);
	assert(mbi.height == mapDims.hmapy);
	metalMap.Init(metalmapPtr, mbi.width, mbi.height, mapInfo->map.maxMetal);

	if (metalmapPtr != nullptr)
		rm->FreeInfoMap("metal", metalmapPtr);

	if (typemapPtr != nullptr && tbi.width == mapDims.hmapx && tbi.height == mapDims.hmapy) {
		assert(!typeMap.empty());
		memcpy(typeMap.data(), typemapPtr, typeMap.size());
	} else {
		LOG_L(L_WARNING, "[CReadMap::%s] missing or illegal typemap for \"%s\" (dims=<%d,%d>)", __func__, mapName.c_str(), tbi.width, tbi.height);
	}

	if (typemapPtr != nullptr)
		rm->FreeInfoMap("type", typemapPtr);

	return rm;
}

#ifdef USING_CREG
void CReadMap::Serialize(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	SerializeMapChangesBeforeMatch(s);
	SerializeMapChangesDuringMatch(s);
	SerializeTypeMap(s);
	SerializePendingDerivedTerrain(s);
	s->SerializeObjectInstance(&metalMap, metalMap.GetClass());
}

void CReadMap::SerializeMapChangesBeforeMatch(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	SerializeMapChanges(s, GetMapFileHeightMapSynced(), const_cast<float*>(GetOriginalHeightMapSynced()));
}

void CReadMap::SerializeMapChangesDuringMatch(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	SerializeMapChanges(s, GetOriginalHeightMapSynced(), const_cast<float*>(GetCornerHeightMapSynced()));
}

void CReadMap::SerializeMapChanges(creg::ISerializer* s, const float* refHeightMap, float* modifiedHeightMap) {
	RECOIL_DETAILED_TRACY_ZONE;
	// using integers so we can xor the original heightmap with the
	// current one (affected by Lua, explosions, etc) - long runs of
	// zeros for unchanged squares should compress significantly better.
	      int32_t*  ichms = reinterpret_cast<      int32_t*>(modifiedHeightMap);
	const int32_t* iochms = reinterpret_cast<const int32_t*>(refHeightMap);

	int32_t height;

	if (s->IsWriting()) {
		for (uint32_t i = 0; i < (mapDims.mapxp1 * mapDims.mapyp1); i++) {
			height = ichms[i] ^ iochms[i];
			s->Serialize(&height, sizeof(int32_t));
		}
	} else {
		for (uint32_t i = 0; i < (mapDims.mapxp1 * mapDims.mapyp1); i++) {
			s->Serialize(&height, sizeof(int32_t));
			ichms[i] = height ^ iochms[i];
		}
	}
}

void CReadMap::SerializeTypeMap(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// LuaSynced can also touch the typemap, serialize it (manually)
	MapBitmapInfo tbi;

	uint8_t*  itm = typeMap.data();
	uint8_t* iotm = GetInfoMap("type", &tbi);

	assert(!typeMap.empty());
	assert(typeMap.size() == (tbi.width * tbi.height));

	if (iotm == nullptr)
		return;

	uint8_t type;

	if (s->IsWriting()) {
		for (uint32_t i = 0; i < (mapDims.hmapx * mapDims.hmapy); i++) {
			type = itm[i] ^ iotm[i];
			s->Serialize(&type, sizeof(uint8_t));
		}
	} else {
		for (uint32_t i = 0; i < (mapDims.hmapx * mapDims.hmapy); i++) {
			s->Serialize(&type, sizeof(uint8_t));
			itm[i] = type ^ iotm[i];
		}
	}

	FreeInfoMap("type", iotm);
}

void CReadMap::SerializePendingDerivedTerrain(creg::ISerializer* s)
{
	RECOIL_DETAILED_TRACY_ZONE;

	static constexpr int TERRAIN_TILE_SIZE = 32;
	std::vector<SRectangle> pendingRects;
	std::set<int> tileIds;

	if (s->IsWriting()) {
		mapDamage->GetPendingTerrainRecalcRects(pendingRects);

		for (const SRectangle& rect: pendingRects) {
			const int firstTileX = rect.x1 / TERRAIN_TILE_SIZE;
			const int firstTileZ = rect.z1 / TERRAIN_TILE_SIZE;
			const int lastTileX = std::max(rect.x1, rect.x2 - 1) / TERRAIN_TILE_SIZE;
			const int lastTileZ = std::max(rect.z1, rect.z2 - 1) / TERRAIN_TILE_SIZE;
			const int tilesX = (mapDims.mapx + TERRAIN_TILE_SIZE - 1) / TERRAIN_TILE_SIZE;

			for (int tileZ = firstTileZ; tileZ <= lastTileZ; ++tileZ) {
				for (int tileX = firstTileX; tileX <= lastTileX; ++tileX) {
					tileIds.insert(tileZ * tilesX + tileX);
				}
			}
		}
	}

	uint32_t tileCount = static_cast<uint32_t>(tileIds.size());
	s->Serialize(tileCount);

	if (s->IsWriting()) {
		loadSaveTerrainTiles.clear();
		loadSaveTerrainTiles.reserve(tileCount);

		const int tilesX = (mapDims.mapx + TERRAIN_TILE_SIZE - 1) / TERRAIN_TILE_SIZE;
		for (const int tileId: tileIds) {
			LoadSaveTerrainTile tile;
			tile.tileX = tileId % tilesX;
			tile.tileZ = tileId / tilesX;
			loadSaveTerrainTiles.emplace_back(std::move(tile));
		}
	} else {
		loadSaveTerrainTiles.clear();
		loadSaveTerrainTiles.resize(tileCount);
	}

	auto serializeFloatVector = [s](std::vector<float>& values, size_t valueCount) {
		if (!s->IsWriting())
			values.resize(valueCount);
		if (!values.empty())
			s->Serialize(values.data(), static_cast<int>(values.size() * sizeof(float)));
	};
	auto serializeFloat3Vector = [s](std::vector<float3>& values, size_t valueCount) {
		if (!s->IsWriting())
			values.resize(valueCount);
		if (!values.empty())
			s->Serialize(values.data(), static_cast<int>(values.size() * sizeof(float3)));
	};

	for (LoadSaveTerrainTile& tile: loadSaveTerrainTiles) {
		s->Serialize(tile.tileX);
		s->Serialize(tile.tileZ);

		const int x1 = tile.tileX * TERRAIN_TILE_SIZE;
		const int z1 = tile.tileZ * TERRAIN_TILE_SIZE;
		const int x2 = std::min(x1 + TERRAIN_TILE_SIZE, mapDims.mapx);
		const int z2 = std::min(z1 + TERRAIN_TILE_SIZE, mapDims.mapy);
		const size_t squareCount = static_cast<size_t>(x2 - x1) * (z2 - z1);
		const int slopeX1 = x1 / 2;
		const int slopeZ1 = z1 / 2;
		const int slopeX2 = std::min((x2 + 1) / 2, mapDims.hmapx);
		const int slopeZ2 = std::min((z2 + 1) / 2, mapDims.hmapy);
		const size_t slopeCount = static_cast<size_t>(slopeX2 - slopeX1) * (slopeZ2 - slopeZ1);

		if (s->IsWriting()) {
			tile.centerHeights.clear();
			tile.maxHeights.clear();
			tile.faceNormals.clear();
			tile.centerNormals.clear();
			tile.centerNormals2DValues.clear();
			tile.slopes.clear();

			tile.centerHeights.reserve(squareCount);
			tile.maxHeights.reserve(squareCount);
			tile.faceNormals.reserve(squareCount * 2);
			tile.centerNormals.reserve(squareCount);
			tile.centerNormals2DValues.reserve(squareCount);
			tile.slopes.reserve(slopeCount);

			for (int z = z1; z < z2; ++z) {
				for (int x = x1; x < x2; ++x) {
					const size_t squareIndex = static_cast<size_t>(z) * mapDims.mapx + x;
					tile.centerHeights.push_back(centerHeightMap[squareIndex]);
					tile.maxHeights.push_back(maxHeightMap[squareIndex]);
					tile.faceNormals.push_back(faceNormalsSynced[squareIndex * 2]);
					tile.faceNormals.push_back(faceNormalsSynced[squareIndex * 2 + 1]);
					tile.centerNormals.push_back(centerNormalsSynced[squareIndex]);
					tile.centerNormals2DValues.push_back(centerNormals2D[squareIndex]);
				}
			}

			for (int z = slopeZ1; z < slopeZ2; ++z) {
				for (int x = slopeX1; x < slopeX2; ++x) {
					tile.slopes.push_back(slopeMap[static_cast<size_t>(z) * mapDims.hmapx + x]);
				}
			}
		}

		serializeFloatVector(tile.centerHeights, squareCount);
		serializeFloatVector(tile.maxHeights, squareCount);
		serializeFloat3Vector(tile.faceNormals, squareCount * 2);
		serializeFloat3Vector(tile.centerNormals, squareCount);
		serializeFloat3Vector(tile.centerNormals2DValues, squareCount);
		serializeFloatVector(tile.slopes, slopeCount);
	}

	if (s->IsWriting()) {
		loadSaveCenterHeightMap = centerHeightMap;
		loadSaveMaxHeightMap = maxHeightMap;
		loadSaveFaceNormalsSynced = faceNormalsSynced;
		loadSaveCenterNormalsSynced = centerNormalsSynced;
		loadSaveCenterNormals2D = centerNormals2D;
		loadSaveSlopeMap = slopeMap;
	}

	serializeFloatVector(loadSaveCenterHeightMap, centerHeightMap.size());
	serializeFloatVector(loadSaveMaxHeightMap, maxHeightMap.size());
	serializeFloat3Vector(loadSaveFaceNormalsSynced, faceNormalsSynced.size());
	serializeFloat3Vector(loadSaveCenterNormalsSynced, centerNormalsSynced.size());
	serializeFloat3Vector(loadSaveCenterNormals2D, centerNormals2D.size());
	serializeFloatVector(loadSaveSlopeMap, slopeMap.size());

	for (int mip = 1; mip < numHeightMipMaps; ++mip) {
		const size_t mipSize = static_cast<size_t>(mapDims.mapx >> mip) * (mapDims.mapy >> mip);

		if (s->IsWriting())
			loadSaveMipCenterHeightMaps[mip - 1] = mipCenterHeightMaps[mip - 1];

		serializeFloatVector(loadSaveMipCenterHeightMaps[mip - 1], mipSize);
	}

	LOG("[ReplayRewind] %s exact derived terrain: rects=%u tiles=%u center=%u normals=%u slopes=%u",
		s->IsWriting() ? "captured" : "restored",
		static_cast<unsigned int>(pendingRects.size()),
		tileCount,
		static_cast<unsigned int>(loadSaveCenterHeightMap.size()),
		static_cast<unsigned int>(loadSaveCenterNormalsSynced.size()),
		static_cast<unsigned int>(loadSaveSlopeMap.size())
	);

	if (s->IsWriting()) {
		std::vector<LoadSaveTerrainTile>().swap(loadSaveTerrainTiles);
		std::vector<float>().swap(loadSaveCenterHeightMap);
		std::vector<float>().swap(loadSaveMaxHeightMap);
		std::vector<float3>().swap(loadSaveFaceNormalsSynced);
		std::vector<float3>().swap(loadSaveCenterNormalsSynced);
		std::vector<float3>().swap(loadSaveCenterNormals2D);
		std::vector<float>().swap(loadSaveSlopeMap);
		for (std::vector<float>& mipMap: loadSaveMipCenterHeightMaps)
			std::vector<float>().swap(mipMap);
	}
}

void CReadMap::RestorePendingDerivedTerrain()
{
	static constexpr int TERRAIN_TILE_SIZE = 32;

	for (const LoadSaveTerrainTile& tile: loadSaveTerrainTiles) {
		const int x1 = tile.tileX * TERRAIN_TILE_SIZE;
		const int z1 = tile.tileZ * TERRAIN_TILE_SIZE;
		const int x2 = std::min(x1 + TERRAIN_TILE_SIZE, mapDims.mapx);
		const int z2 = std::min(z1 + TERRAIN_TILE_SIZE, mapDims.mapy);
		const int slopeX1 = x1 / 2;
		const int slopeZ1 = z1 / 2;
		const int slopeX2 = std::min((x2 + 1) / 2, mapDims.hmapx);
		const int slopeZ2 = std::min((z2 + 1) / 2, mapDims.hmapy);
		size_t squareOffset = 0;
		size_t slopeOffset = 0;

		for (int z = z1; z < z2; ++z) {
			for (int x = x1; x < x2; ++x, ++squareOffset) {
				const size_t squareIndex = static_cast<size_t>(z) * mapDims.mapx + x;
				centerHeightMap[squareIndex] = tile.centerHeights[squareOffset];
				maxHeightMap[squareIndex] = tile.maxHeights[squareOffset];
				faceNormalsSynced[squareIndex * 2] = tile.faceNormals[squareOffset * 2];
				faceNormalsSynced[squareIndex * 2 + 1] = tile.faceNormals[squareOffset * 2 + 1];
				centerNormalsSynced[squareIndex] = tile.centerNormals[squareOffset];
				centerNormals2D[squareIndex] = tile.centerNormals2DValues[squareOffset];
			}
		}

		for (int z = slopeZ1; z < slopeZ2; ++z) {
			for (int x = slopeX1; x < slopeX2; ++x, ++slopeOffset) {
				slopeMap[static_cast<size_t>(z) * mapDims.hmapx + x] = tile.slopes[slopeOffset];
			}
		}
	}

	auto restoreExactVector = [](auto& destination, auto& source) {
		if (source.empty())
			return;
		if (destination.size() != source.size())
			throw content_error("[CReadMap::RestorePendingDerivedTerrain] incompatible derived-terrain vector size");

		std::copy(source.begin(), source.end(), destination.begin());
		source.clear();
		source.shrink_to_fit();
	};

	// Keep the original allocations alive: LOS, pathing, and rendering systems
	// retain pointers into these vectors after map initialization.
	restoreExactVector(centerHeightMap, loadSaveCenterHeightMap);
	restoreExactVector(maxHeightMap, loadSaveMaxHeightMap);
	restoreExactVector(faceNormalsSynced, loadSaveFaceNormalsSynced);
	restoreExactVector(centerNormalsSynced, loadSaveCenterNormalsSynced);
	restoreExactVector(centerNormals2D, loadSaveCenterNormals2D);
	restoreExactVector(slopeMap, loadSaveSlopeMap);

	for (int mip = 1; mip < numHeightMipMaps; ++mip) {
		restoreExactVector(mipCenterHeightMaps[mip - 1], loadSaveMipCenterHeightMaps[mip - 1]);
	}

	sharedCenterHeightMaps[0] = centerHeightMap.data();
	sharedCenterHeightMaps[1] = centerHeightMap.data();
	sharedFaceNormals[0] = faceNormalsUnsynced.data();
	sharedFaceNormals[1] = faceNormalsSynced.data();
	sharedCenterNormals[0] = centerNormalsUnsynced.data();
	sharedCenterNormals[1] = centerNormalsSynced.data();
	sharedSlopeMaps[0] = slopeMap.data();
	sharedSlopeMaps[1] = slopeMap.data();

	mipPointerHeightMaps[0] = centerHeightMap.data();
	for (int mip = 1; mip < numHeightMipMaps; ++mip)
		mipPointerHeightMaps[mip] = mipCenterHeightMaps[mip - 1].data();

	LOG("[ReplayRewind] reapplied exact derived terrain after map derivation: tiles=%u center=%u normals=%u slopes=%u",
		static_cast<unsigned int>(loadSaveTerrainTiles.size()),
		static_cast<unsigned int>(centerHeightMap.size()),
		static_cast<unsigned int>(centerNormalsSynced.size()),
		static_cast<unsigned int>(slopeMap.size())
	);
	loadSaveTerrainTiles.clear();
}


void CReadMap::PostLoad()
{
	RECOIL_DETAILED_TRACY_ZONE;
	sharedCornerHeightMaps[0] = &(*heightMapUnsyncedPtr)[0];
	sharedCornerHeightMaps[1] = &(*heightMapSyncedPtr)[0];

	sharedCenterHeightMaps[0] = &centerHeightMap[0]; // NO UNSYNCED VARIANT
	sharedCenterHeightMaps[1] = &centerHeightMap[0];

	sharedFaceNormals[0] = &faceNormalsUnsynced[0];
	sharedFaceNormals[1] = &faceNormalsSynced[0];

	sharedCenterNormals[0] = &centerNormalsUnsynced[0];
	sharedCenterNormals[1] = &centerNormalsSynced[0];

	sharedSlopeMaps[0] = &slopeMap[0]; // NO UNSYNCED VARIANT
	sharedSlopeMaps[1] = &slopeMap[0];

	mipPointerHeightMaps.fill(nullptr);
	mipPointerHeightMaps[0] = &centerHeightMap[0];

	for (int i = 1; i < numHeightMipMaps; i++) {
		mipCenterHeightMaps[i - 1].clear();
		mipCenterHeightMaps[i - 1].resize((mapDims.mapx >> i) * (mapDims.mapy >> i));

		mipPointerHeightMaps[i] = &mipCenterHeightMaps[i - 1][0];
	}

	hmUpdated = true;

	mapDamage->RecalcArea(0, mapDims.mapx, 0, mapDims.mapy);
	RestorePendingDerivedTerrain();
}
#endif //USING_CREG


CReadMap::~CReadMap()
{
	RECOIL_DETAILED_TRACY_ZONE;
	metalMap.Kill();
}


void CReadMap::Initialize()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// set global map info
	mapDims.Initialize();

	float3::maxxpos = mapDims.mapx * SQUARE_SIZE - 1;
	float3::maxzpos = mapDims.mapy * SQUARE_SIZE - 1;

	boundingRadius = math::sqrt(Square(mapDims.mapx * SQUARE_SIZE) + Square(mapDims.mapy * SQUARE_SIZE)) * 0.5f;

	{
		char loadMsg[512];
		const char* fmtString = "Loading Map (%u MB)";
		uint32_t reqMemFootPrintKB =
			((( mapDims.mapxp1)   * mapDims.mapyp1  * 2     * sizeof(float))         / 1024) +   // cornerHeightMap{Synced, Unsynced}
			((( mapDims.mapxp1)   * mapDims.mapyp1  *         sizeof(float))         / 1024) +   // originalHeightMap
			((  mapDims.mapx      * mapDims.mapy    * 2 * 2 * sizeof(float3))        / 1024) +   // faceNormals{Synced, Unsynced}
			((  mapDims.mapx      * mapDims.mapy    * 2     * sizeof(float3))        / 1024) +   // centerNormals{Synced, Unsynced}
			((( mapDims.mapxp1)   * mapDims.mapyp1          * sizeof(float3))        / 1024) +   // VisVertexNormals
			((  mapDims.mapx      * mapDims.mapy            * sizeof(float))         / 1024) +   // centerHeightMap
			((  mapDims.mapx      * mapDims.mapy            * sizeof(float))         / 1024) +   // maxHeightMap
			((  mapDims.hmapx     * mapDims.hmapy           * sizeof(float))         / 1024) +   // slopeMap
			((  mapDims.hmapx     * mapDims.hmapy           * sizeof(uint8_t))       / 1024) +   // typeMap
			((  mapDims.hmapx     * mapDims.hmapy           * sizeof(float))         / 1024) +   // MetalMap::extractionMap
			((  mapDims.hmapx     * mapDims.hmapy           * sizeof(unsigned char)) / 1024);    // MetalMap::metalMap

		// mipCenterHeightMaps[i]
		for (int i = 1; i < numHeightMipMaps; i++) {
			reqMemFootPrintKB += ((((mapDims.mapx >> i) * (mapDims.mapy >> i)) * sizeof(float)) / 1024);
		}

		sprintf(loadMsg, fmtString, reqMemFootPrintKB / 1024);
		loadscreen->SetLoadMessage(loadMsg);
	}

	mapFileHeightMap.clear();
	mapFileHeightMap.resize(mapDims.mapxp1 * mapDims.mapyp1);
	originalHeightMap.clear();
	originalHeightMap.resize(mapDims.mapxp1 * mapDims.mapyp1);
	faceNormalsSynced.clear();
	faceNormalsSynced.resize(mapDims.mapx * mapDims.mapy * 2);
	faceNormalsUnsynced.clear();
	faceNormalsUnsynced.resize(mapDims.mapx * mapDims.mapy * 2);
	centerNormalsSynced.clear();
	centerNormalsSynced.resize(mapDims.mapx * mapDims.mapy);
	centerNormalsUnsynced.clear();
	centerNormalsUnsynced.resize(mapDims.mapx * mapDims.mapy);
	centerNormals2D.clear();
	centerNormals2D.resize(mapDims.mapx * mapDims.mapy);
	centerHeightMap.clear();
	centerHeightMap.resize(mapDims.mapx * mapDims.mapy);
	maxHeightMap.clear();
	maxHeightMap.resize(mapDims.mapx * mapDims.mapy);

	mipPointerHeightMaps.fill(nullptr);
	mipPointerHeightMaps[0] = &centerHeightMap[0];

	originalHeightMapPtr = &originalHeightMap;

	for (int i = 1; i < numHeightMipMaps; i++) {
		mipCenterHeightMaps[i - 1].clear();
		mipCenterHeightMaps[i - 1].resize((mapDims.mapx >> i) * (mapDims.mapy >> i));

		mipPointerHeightMaps[i] = &mipCenterHeightMaps[i - 1][0];
	}

	slopeMap.clear();
	slopeMap.resize(mapDims.hmapx * mapDims.hmapy);

	// by default, all squares are set to terrain-type 0
	typeMap.clear();
	typeMap.resize(mapDims.hmapx * mapDims.hmapy, 0);

	assert(heightMapSyncedPtr != nullptr);
	assert(heightMapUnsyncedPtr != nullptr);
	assert(originalHeightMapPtr != nullptr);

	{
		sharedCornerHeightMaps[0] = &(*heightMapUnsyncedPtr)[0];
		sharedCornerHeightMaps[1] = &(*heightMapSyncedPtr)[0];

		sharedCenterHeightMaps[0] = &centerHeightMap[0]; // NO UNSYNCED VARIANT
		sharedCenterHeightMaps[1] = &centerHeightMap[0];

		sharedFaceNormals[0] = &faceNormalsUnsynced[0];
		sharedFaceNormals[1] = &faceNormalsSynced[0];

		sharedCenterNormals[0] = &centerNormalsUnsynced[0];
		sharedCenterNormals[1] = &centerNormalsSynced[0];

		sharedSlopeMaps[0] = &slopeMap[0]; // NO UNSYNCED VARIANT
		sharedSlopeMaps[1] = &slopeMap[0];
	}

	InitHeightBounds();

	syncedHeightMapDigests.clear();
	unsyncedHeightMapDigests.clear();

	// not callable here because losHandler is still uninitialized, deferred to Game::PostLoadSim
	// InitHeightMapDigestVectors();
	UpdateHeightMapSynced({0, 0, mapDims.mapx, mapDims.mapy});

	unsyncedHeightInfo.resize(
		(mapDims.mapx / PATCH_SIZE) * (mapDims.mapy / PATCH_SIZE),
		float3{
			initHeightBounds.x,
			initHeightBounds.y,
			(initHeightBounds.y + initHeightBounds.x) * 0.5f
		}
	);
	// FIXME: sky & skyLight aren't created yet (crashes in SMFReadMap.cpp)
	// UpdateDraw(true);
}

void CReadMap::InitHeightBounds()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float* heightmap = GetCornerHeightMapSynced();
	for (int i = 0; i < (mapDims.mapxp1 * mapDims.mapyp1); ++i) {
		mapFileHeightMap[i] = heightmap[i];
	}

	LoadOriginalHeightMapAndChecksum();
}

void CReadMap::LoadOriginalHeightMapAndChecksum()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float* heightmap = GetCornerHeightMapSynced();

	initHeightBounds.x = std::numeric_limits<float>::max();
	initHeightBounds.y = std::numeric_limits<float>::lowest();

	tempHeightBounds = initHeightBounds;

	uint32_t checksum = 0;

	for (int i = 0; i < (mapDims.mapxp1 * mapDims.mapyp1); ++i) {
		originalHeightMap[i] = heightmap[i];

		initHeightBounds.x = std::min(initHeightBounds.x, heightmap[i]);
		initHeightBounds.y = std::max(initHeightBounds.y, heightmap[i]);

		checksum = spring::LiteHash(&heightmap[i], sizeof(heightmap[i]), checksum);
	}

	mapChecksum = spring::LiteHash(mapInfo->map.name.c_str(), mapInfo->map.name.size(), checksum);

	currHeightBounds.x = initHeightBounds.x;
	currHeightBounds.y = initHeightBounds.y;
}





uint32_t CReadMap::CalcHeightmapChecksum()
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float* heightmap = GetCornerHeightMapSynced();

	uint32_t checksum = 0;

	for (int i = 0; i < (mapDims.mapxp1 * mapDims.mapyp1); ++i) {
		checksum = spring::LiteHash(&heightmap[i], sizeof(heightmap[i]), checksum);
	}

	return spring::LiteHash(mapInfo->map.name.c_str(), mapInfo->map.name.size(), checksum);
}


uint32_t CReadMap::CalcTypemapChecksum()
{
	RECOIL_DETAILED_TRACY_ZONE;
	uint32_t checksum = spring::LiteHash(&typeMap[0], typeMap.size() * sizeof(typeMap[0]), 0);

	for (const CMapInfo::TerrainType& tt : mapInfo->terrainTypes) {
		checksum = spring::LiteHash(tt.name.c_str(), tt.name.size(), checksum);
		checksum = spring::LiteHash(&tt.hardness, offsetof(CMapInfo::TerrainType, receiveTracks) - offsetof(CMapInfo::TerrainType, hardness), checksum);
	}

	return checksum;
}


void CReadMap::UpdateDraw(bool firstCall)
{
	SCOPED_TIMER("Update::ReadMap::UHM");

	if (unsyncedHeightMapUpdates.empty())
		return;

	//optimize layout
	unsyncedHeightMapUpdates.Process(firstCall);

	const int N = static_cast<int>(std::min(MAX_UHM_RECTS_PER_FRAME, unsyncedHeightMapUpdates.size()));

	for (int i = 0; i < N; i++) {
		UpdateHeightMapUnsynced(*(unsyncedHeightMapUpdates.begin() + i));
	};
	UpdateHeightMapUnsyncedPost();

	for (int i = 0; i < N; i++) {
		eventHandler.UnsyncedHeightMapUpdate(*(unsyncedHeightMapUpdates.begin() + i));
	}

	for (int i = 0; i < N; i++) {
		unsyncedHeightMapUpdates.pop_front();
	}
}


void CReadMap::UpdateHeightMapSynced(const SRectangle& hgtMapRect)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const bool initialize = (hgtMapRect == SRectangle{ 0, 0, mapDims.mapx, mapDims.mapy });

	const int2 mins = {hgtMapRect.x1 - 1, hgtMapRect.z1 - 1};
	const int2 maxs = {hgtMapRect.x2 + 1, hgtMapRect.z2 + 1};

	// NOTE:
	//   rectangles are clamped to map{x,y}m1 which are the proper inclusive bounds for center heightmaps
	//   parts of UpdateHeightMapUnsynced() (vertex normals, normal texture) however inclusively clamp to
	//   map{x,y} since they index corner heightmaps, while UnsyncedHeightMapUpdate() EventClients should
	//   already expect {x,z}2 <= map{x,y} and do internal clamping as well
	const SRectangle centerRect = {std::max(mins.x, 0), std::max(mins.y, 0),  std::min(maxs.x, mapDims.mapxm1),  std::min(maxs.y, mapDims.mapym1)};
	const SRectangle cornerRect = {std::max(mins.x, 0), std::max(mins.y, 0),  std::min(maxs.x, mapDims.mapx  ),  std::min(maxs.y, mapDims.mapy  )};

	UpdateCenterHeightmap(centerRect, initialize);
	UpdateMipHeightmaps(centerRect, initialize);
	UpdateFaceNormals(centerRect, initialize);
	UpdateSlopemap(centerRect, initialize); // must happen after UpdateFaceNormals()!

	// push the unsynced update; initial one without LOS check
	if (initialize) {
		unsyncedHeightMapUpdates.push_back(cornerRect);
	} else {
		#ifdef USE_HEIGHTMAP_DIGESTS
		// convert heightmap rectangle to LOS-map space
		const       int2 losMapSize = losHandler->los.size;
		const SRectangle losMapRect = centerRect * (SQUARE_SIZE * losHandler->los.invDiv);

		// heightmap updated, increment digests (byte-overflow is intentional!)
		for (int lmz = losMapRect.z1; lmz <= losMapRect.z2; ++lmz) {
			for (int lmx = losMapRect.x1; lmx <= losMapRect.x2; ++lmx) {
				const int losMapIdx = lmx + lmz * (losMapSize.x + 1);

				assert(losMapIdx < syncedHeightMapDigests.size());

				syncedHeightMapDigests[losMapIdx]++;
			}
		}
		#endif

		HeightMapUpdateLOSCheck(cornerRect);
	}
}


void CReadMap::UpdateHeightBounds(int syncFrame)
{
	RECOIL_DETAILED_TRACY_ZONE;
	constexpr int PACING_PERIOD = GAME_SPEED; //tune if needed
	int dataChunk = syncFrame % PACING_PERIOD;

	if (dataChunk == 0) {
		if (processingHeightBounds)
			currHeightBounds = tempHeightBounds;

		processingHeightBounds = hmUpdated;
		hmUpdated = false;
	}

	if (!processingHeightBounds)
		return;

	if (dataChunk == 0) {
		tempHeightBounds.x = std::numeric_limits<float>::max();
		tempHeightBounds.y = std::numeric_limits<float>::lowest();
	}

	const int idxBeg = (dataChunk + 0) * mapDims.mapxp1 * mapDims.mapyp1 / PACING_PERIOD;
	const int idxEnd = (dataChunk + 1) * mapDims.mapxp1 * mapDims.mapyp1 / PACING_PERIOD;
	UpdateTempHeightBoundsSIMD(idxBeg, idxEnd);
}

void CReadMap::UpdateTempHeightBoundsSIMD(size_t idxBeg, size_t idxEnd)
{
	RECOIL_DETAILED_TRACY_ZONE;
	tempHeightBounds.xy = xsimd::reduce(
		heightMapSyncedPtr->begin() + idxBeg,
		heightMapSyncedPtr->begin() + idxEnd,
		tempHeightBounds.xy,
		MinOp{}, MaxOp{}
	);
}

void CReadMap::UpdateHeightBounds()
{
	RECOIL_DETAILED_TRACY_ZONE;
	tempHeightBounds.x = std::numeric_limits<float>::max();
	tempHeightBounds.y = std::numeric_limits<float>::lowest();

	UpdateTempHeightBoundsSIMD(0, mapDims.mapxp1 * mapDims.mapyp1);

	currHeightBounds.x = tempHeightBounds.x;
	currHeightBounds.y = tempHeightBounds.y;
}

void CReadMap::UpdateCenterHeightmap(const SRectangle& rect, bool initialize) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float* heightmapSynced = GetCornerHeightMapSynced();

	for_mt_chunk(rect.z1, rect.z2 + 1, [heightmapSynced, &rect](const int y) {
		for (int x = rect.x1; x <= rect.x2; x++) {
			const int idxTL = (y + 0) * mapDims.mapxp1 + x + 0;
			const int idxTR = (y + 0) * mapDims.mapxp1 + x + 1;
			const int idxBL = (y + 1) * mapDims.mapxp1 + x + 0;
			const int idxBR = (y + 1) * mapDims.mapxp1 + x + 1;

			const int index = y * mapDims.mapx + x;
			const float height =
				heightmapSynced[idxTL] +
				heightmapSynced[idxTR] +
				heightmapSynced[idxBL] +
				heightmapSynced[idxBR];
			centerHeightMap[index] = height * 0.25f;
			maxHeightMap[index] = std::max
					( std::max(heightmapSynced[idxTL], heightmapSynced[idxTR])
					, std::max(heightmapSynced[idxBL], heightmapSynced[idxBR])
					);
		}
	}, 256);
}


void CReadMap::UpdateMipHeightmaps(const SRectangle& rect, bool initialize)
{
	RECOIL_DETAILED_TRACY_ZONE;
	for (int i = 0; i < numHeightMipMaps - 1; i++) {
		const int hmapx = mapDims.mapx >> i;

		const int sx = (rect.x1 >> i) & (~1);
		const int ex = (rect.x2 >> i);
		const int sy = (rect.z1 >> i) & (~1);
		const int ey = (rect.z2 >> i);

		float* topMipMap = mipPointerHeightMaps[i    ];
		float* subMipMap = mipPointerHeightMaps[i + 1];

		for (int y = sy; y < ey; y += 2) {
			for (int x = sx; x < ex; x += 2) {
				const float height =
					topMipMap[(x    ) + (y    ) * hmapx] +
					topMipMap[(x    ) + (y + 1) * hmapx] +
					topMipMap[(x + 1) + (y    ) * hmapx] +
					topMipMap[(x + 1) + (y + 1) * hmapx];
				subMipMap[(x / 2) + (y / 2) * hmapx / 2] = height * 0.25f;
			}
		}
	}
}


void CReadMap::UpdateFaceNormals(const SRectangle& rect, bool initialize)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const float* heightmapSynced = GetCornerHeightMapSynced();

	const int z1 = std::max(             0, rect.z1 - 1);
	const int x1 = std::max(             0, rect.x1 - 1);
	const int z2 = std::min(mapDims.mapym1, rect.z2 + 1);
	const int x2 = std::min(mapDims.mapxm1, rect.x2 + 1);

	for_mt_chunk(z1, z2 + 1, [&](const int y) {
		float3 fnTL;
		float3 fnBR;

		for (int x = x1; x <= x2; x++) {
			const int idxTL = (y    ) * mapDims.mapxp1 + x; // TL
			const int idxBL = (y + 1) * mapDims.mapxp1 + x; // BL

			const float& hTL = heightmapSynced[idxTL    ];
			const float& hTR = heightmapSynced[idxTL + 1];
			const float& hBL = heightmapSynced[idxBL    ];
			const float& hBR = heightmapSynced[idxBL + 1];

			// normal of top-left triangle (face) in square
			//
			//  *---> e1
			//  |
			//  |
			//  v
			//  e2
			//const float3 e1( SQUARE_SIZE, hTR - hTL,           0);
			//const float3 e2(           0, hBL - hTL, SQUARE_SIZE);
			//const float3 fnTL = (e2.cross(e1)).Normalize();
			fnTL.y = SQUARE_SIZE;
			fnTL.x = - (hTR - hTL);
			fnTL.z = - (hBL - hTL);
			fnTL.Normalize();

			// normal of bottom-right triangle (face) in square
			//
			//         e3
			//         ^
			//         |
			//         |
			//  e4 <---*
			//const float3 e3(-SQUARE_SIZE, hBL - hBR,           0);
			//const float3 e4(           0, hTR - hBR,-SQUARE_SIZE);
			//const float3 fnBR = (e4.cross(e3)).Normalize();
			fnBR.y = SQUARE_SIZE;
			fnBR.x = (hBL - hBR);
			fnBR.z = (hTR - hBR);
			fnBR.Normalize();

			faceNormalsSynced[(y * mapDims.mapx + x) * 2    ] = fnTL;
			faceNormalsSynced[(y * mapDims.mapx + x) * 2 + 1] = fnBR;
			// square-normal
			centerNormalsSynced[y * mapDims.mapx + x] = (fnTL + fnBR).Normalize();
			centerNormals2D[y * mapDims.mapx + x] = (fnTL + fnBR).Normalize2D();

			if (initialize) {
				faceNormalsUnsynced[(y * mapDims.mapx + x) * 2    ] = faceNormalsSynced[(y * mapDims.mapx + x) * 2    ];
				faceNormalsUnsynced[(y * mapDims.mapx + x) * 2 + 1] = faceNormalsSynced[(y * mapDims.mapx + x) * 2 + 1];
				centerNormalsUnsynced[y * mapDims.mapx + x] = centerNormalsSynced[y * mapDims.mapx + x];
			}
		}
	}, 64);
}


void CReadMap::UpdateSlopemap(const SRectangle& rect, bool initialize)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const int sx = std::max(0,                 (rect.x1 / 2) - 1);
	const int ex = std::min(mapDims.hmapx - 1, (rect.x2 / 2) + 1);
	const int sy = std::max(0,                 (rect.z1 / 2) - 1);
	const int ey = std::min(mapDims.hmapy - 1, (rect.z2 / 2) + 1);

	for_mt_chunk(sy, ey + 1, [sx, ex](const int y) {
		for (int x = sx; x <= ex; x++) {
			const int idx0 = (y*2    ) * (mapDims.mapx) + x*2;
			const int idx1 = (y*2 + 1) * (mapDims.mapx) + x*2;

			float avgslope = 0.0f;
			avgslope += faceNormalsSynced[(idx0    ) * 2    ].y;
			avgslope += faceNormalsSynced[(idx0    ) * 2 + 1].y;
			avgslope += faceNormalsSynced[(idx0 + 1) * 2    ].y;
			avgslope += faceNormalsSynced[(idx0 + 1) * 2 + 1].y;
			avgslope += faceNormalsSynced[(idx1    ) * 2    ].y;
			avgslope += faceNormalsSynced[(idx1    ) * 2 + 1].y;
			avgslope += faceNormalsSynced[(idx1 + 1) * 2    ].y;
			avgslope += faceNormalsSynced[(idx1 + 1) * 2 + 1].y;
			avgslope *= 0.125f;

			float maxslope =              faceNormalsSynced[(idx0    ) * 2    ].y;
			maxslope = std::min(maxslope, faceNormalsSynced[(idx0    ) * 2 + 1].y);
			maxslope = std::min(maxslope, faceNormalsSynced[(idx0 + 1) * 2    ].y);
			maxslope = std::min(maxslope, faceNormalsSynced[(idx0 + 1) * 2 + 1].y);
			maxslope = std::min(maxslope, faceNormalsSynced[(idx1    ) * 2    ].y);
			maxslope = std::min(maxslope, faceNormalsSynced[(idx1    ) * 2 + 1].y);
			maxslope = std::min(maxslope, faceNormalsSynced[(idx1 + 1) * 2    ].y);
			maxslope = std::min(maxslope, faceNormalsSynced[(idx1 + 1) * 2 + 1].y);

			// smooth it a bit, so small holes don't block huge tanks
			const float lerp = maxslope / avgslope;
			const float slope = mix(maxslope, avgslope, lerp);

			slopeMap[y * mapDims.hmapx + x] = 1.0f - slope;
		}
	}, 128);
}


/// split the update into multiple invididual (los-square) chunks
void CReadMap::HeightMapUpdateLOSCheck(const SRectangle& hgtMapRect)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// size of LOS square in heightmap coords; divisor is SQUARE_SIZE * 2^mipLevel
	const        int losSqrSize = losHandler->los.mipDiv / SQUARE_SIZE;
	const SRectangle losMapRect = hgtMapRect * (SQUARE_SIZE * losHandler->los.invDiv); // LOS space

	const float* ctrHgtMap = readMap->GetCenterHeightMapSynced();

	const auto PushRect = [&](SRectangle& subRect, int hmx, int hmz) {
		if (subRect.GetArea() > 0) {
			subRect.ClampIn(hgtMapRect);
			unsyncedHeightMapUpdates.push_back(subRect);

			subRect = {hmx + losSqrSize, hmz,  hmx + losSqrSize, hmz + losSqrSize};
		} else {
			subRect.x1 = hmx + losSqrSize;
			subRect.x2 = hmx + losSqrSize;
		}
	};

	for (int lmz = losMapRect.z1; lmz <= losMapRect.z2; ++lmz) {
		const int hmz = lmz * losSqrSize;
		      int hmx = losMapRect.x1 * losSqrSize;

		SRectangle subRect = {hmx, hmz,  hmx, hmz + losSqrSize};

		for (int lmx = losMapRect.x1; lmx <= losMapRect.x2; ++lmx) {
			hmx = lmx * losSqrSize;

			// NB:
			//   LosHandler expects positions in center-heightmap bounds, but hgtMapRect is a corner-rectangle
			//   as such hmx and hmz have to be clamped by CenterSqrToPos before the center-height is accessed
			if (!(gu->spectatingFullView || losHandler->InLos(CenterSqrToPos(ctrHgtMap, hmx, hmz), gu->myAllyTeam))) {
				PushRect(subRect, hmx, hmz);
				continue;
			}

			if (!HasHeightMapViewChanged({lmx, lmz})) {
				PushRect(subRect, hmx, hmz);
				continue;
			}

			// update rectangle size
			subRect.x2 = hmx + losSqrSize;
		}

		PushRect(subRect, hmx, hmz);
	}
}


void CReadMap::InitHeightMapDigestVectors(const int2 losMapSize)
{
	RECOIL_DETAILED_TRACY_ZONE;
#if defined(USE_HEIGHTMAP_DIGESTS)
	assert(losHandler != nullptr);
	assert(syncedHeightMapDigests.empty());

	const int xsize = losMapSize.x + 1;
	const int ysize = losMapSize.y + 1;

	syncedHeightMapDigests.clear();
	syncedHeightMapDigests.resize(xsize * ysize, 0);
	unsyncedHeightMapDigests.clear();
	unsyncedHeightMapDigests.resize(xsize * ysize, 0);
#endif
}


bool CReadMap::HasHeightMapViewChanged(const int2 losMapPos)
{
	RECOIL_DETAILED_TRACY_ZONE;
#if defined(USE_HEIGHTMAP_DIGESTS)
	const int2 losMapSize = losHandler->los.size;
	const int losMapIdx = losMapPos.x + losMapPos.y * (losMapSize.x + 1);

	assert(losMapIdx < syncedHeightMapDigests.size() && losMapIdx >= 0);

	if (unsyncedHeightMapDigests[losMapIdx] != syncedHeightMapDigests[losMapIdx]) {
		unsyncedHeightMapDigests[losMapIdx] = syncedHeightMapDigests[losMapIdx];
		return true;
	}

	return false;
#else
	return true;
#endif
}

void CReadMap::UpdateLOS(const SRectangle& hgtMapRect)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (gu->spectatingFullView)
		return;

	// currently we use the LOS for view updates (alternatives are AirLOS and/or radar)
	// the other maps use different resolutions, must check size here for safety
	// (if another source is used, change the res. of syncedHeightMapDigests etc)
	assert(hgtMapRect.GetWidth() <= (losHandler->los.mipDiv / SQUARE_SIZE));
	assert(losHandler != nullptr);

	SRectangle hgtMapPoint = hgtMapRect;
	//HACK: UpdateLOS() is called for single LOS squares, but we use <= in HeightMapUpdateLOSCheck().
	// This would make our update area 4x as large, so we need to make the rectangle a point. Better
	// would be to use < instead of <= everywhere.
	//FIXME: this actually causes spikes in the UHM
	// hgtMapPoint.x2 = hgtMapPoint.x1;
	// hgtMapPoint.z2 = hgtMapPoint.z1;

	HeightMapUpdateLOSCheck(hgtMapPoint);
}

void CReadMap::BecomeSpectator()
{
	RECOIL_DETAILED_TRACY_ZONE;
	HeightMapUpdateLOSCheck({0, 0, mapDims.mapx, mapDims.mapy});
}

namespace {
	template<typename T>
	void CopySyncedToUnsyncedImpl(const std::vector<T>& src, std::vector<T>& dst) {
		std::copy(src.begin(), src.end(), dst.begin());
	};
}

void CReadMap::CopySyncedToUnsynced()
{
	RECOIL_DETAILED_TRACY_ZONE;
	CopySyncedToUnsyncedImpl(*heightMapSyncedPtr, *heightMapUnsyncedPtr);
	CopySyncedToUnsyncedImpl(faceNormalsSynced, faceNormalsUnsynced);
	CopySyncedToUnsyncedImpl(centerNormalsSynced, centerNormalsUnsynced);
	eventHandler.UnsyncedHeightMapUpdate(SRectangle{ 0, 0, mapDims.mapx, mapDims.mapy });
}

bool CReadMap::HasVisibleWater()  const { return (!mapRendering->voidWater && !IsAboveWater()); }
bool CReadMap::HasOnlyVoidWater() const { return ( mapRendering->voidWater &&  IsUnderWater()); }
