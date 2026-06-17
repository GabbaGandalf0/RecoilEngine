/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef QTPFS_PATHMANAGER_HDR
#define QTPFS_PATHMANAGER_HDR

#include <vector>

#include "Sim/Misc/ModInfo.h"
#include "Sim/Path/IPathManager.h"
#include "System/creg/creg_cond.h"
#include "NodeLayer.h"
#include "PathCache.h"
#include "PathSearch.h"
#include "System/UnorderedMap.hpp"

struct MoveDef;
struct SRectangle;
class CSolidObject;


namespace QTPFS {
	struct LoadSavePathNode {
		CR_DECLARE_STRUCT(LoadSavePathNode)

		std::uint32_t nodeId = -1U;
		std::uint32_t nodeNumber = -1U;
		float netPointX = 0.0f;
		float netPointY = 0.0f;
		int pathPointIndex = -1;
		int xmin = 0;
		int zmin = 0;
		int xmax = 0;
		int zmax = 0;
		bool badNode = false;
	};

	struct LoadSavePathRecord {
		CR_DECLARE_STRUCT(LoadSavePathRecord)

		enum Kind {
			KIND_SYNCED = 0,
			KIND_EXTERNAL_SYNCED = 1,
		};

		int entityId = 0;
		int kind = KIND_SYNCED;
		int ownerId = -1;
		int ownerTeam = -1;
		int ownerCreationFrame = -1;
		int ownerMoveDefPathType = -1;
		int pathType = 0;

		unsigned int nextPointIndex = 0;
		unsigned int repathAtPointIndex = 0;
		unsigned int numPathUpdates = 0;
		unsigned int firstNodeIdOfCleanPath = 0;

		std::uint64_t hashLow = BAD_HASH_PART;
		std::uint64_t hashHigh = BAD_HASH_PART;
		std::uint64_t virtualHashLow = BAD_HASH_PART;
		std::uint64_t virtualHashHigh = BAD_HASH_PART;
		float radius = 0.0f;

		bool synced = true;
		bool haveFullPath = true;
		bool havePartialPath = false;
		bool boundingBoxOverride = false;
		bool isRawPath = false;
		bool hasRequeueComponent = false;
		bool requeueSearch = false;
		bool hasDirtyComponent = false;
		bool hasToBeUpdatedComponent = false;
		bool wasTempPath = false;
		bool hadSearchRef = false;
		bool hadUpdatedCounterIncrease = false;
		bool pendingSearchRawPathCheck = false;
		bool pendingSearchAllowPartialSearch = false;
		bool pendingSearchTryPathRepair = false;
		bool hasSharedPathChain = false;
		bool isSharedPathHead = false;
		int sharedPathPrevId = 0;
		int sharedPathNextId = 0;
		bool hasPartialSharedPathChain = false;
		bool isPartialSharedPathHead = false;
		int partialSharedPathPrevId = 0;
		int partialSharedPathNextId = 0;

		std::vector<float3> points;
		std::vector<LoadSavePathNode> nodes;

		float3 boundingBoxMins;
		float3 boundingBoxMaxs;
		float3 goalPosition;
	};

	struct LoadSaveMapChangeTrack {
		CR_DECLARE_STRUCT(LoadSaveMapChangeTrack)

		std::vector<std::uint8_t> damageMap;
		std::vector<int> damageQueue;
	};

	struct LoadSaveState {
		CR_DECLARE_STRUCT(LoadSaveState)

		std::vector<LoadSavePathRecord> paths;
		std::vector<LoadSaveNodeLayerState> nodeLayerStates;
		std::vector<LoadSaveMapChangeTrack> mapChangeTrackers;
		std::vector<std::uint8_t> registryEntitySnapshot;
		int damageTrackWidth = 0;
		int damageTrackHeight = 0;
		int damageTrackCellSize = 0;
		int updateDirtyPathRate = 0;
		int updateDirtyPathRemainder = 0;
		int refreshDirtyPathRateFrame = QTPFS_LAST_FRAME;
		std::uint32_t pfsChecksum = 0;
		std::uint32_t nodeLayerChecksum = 0;
		unsigned int skippedPendingPaths = 0;
		unsigned int skippedUnsyncedPaths = 0;
	};

	struct QTNode;
	class PathManager: public IPathManager {
	public:
		// must not be larger than the smallest evenly divisible size of maps.
		static constexpr unsigned int DAMAGE_MAP_BLOCK_SIZE = 16;

		struct MapChangeTrack {
			std::vector<bool> damageMap;
			std::deque<int> damageQueue;
		};
		struct NodeLayersChangeTrack {
			std::vector<MapChangeTrack> mapChangeTrackers;
			int width = 0;
			int height = 0;
			int cellSize = 0;
		};

		PathManager();
		~PathManager();

		static void InitStatic();

		std::int32_t GetPathFinderType() const override { return QTPFS_TYPE; }
		std::uint32_t GetPathCheckSum() const override { return pfsCheckSum; }

		std::int64_t Finalize() override;
		std::int64_t PostFinalizeRefresh() override;

		bool PathUpdated(unsigned int pathID) override;
		void ClearPathUpdated(unsigned int pathID) override;

		bool AllowShortestPath() override { return true; }

		void TerrainChange(unsigned int x1, unsigned int z1,  unsigned int x2, unsigned int z2, unsigned int type) override;
		void Update() override;
		void UpdatePath(const CSolidObject* owner, unsigned int pathID) override;
		void DeletePath(unsigned int pathID, bool force = false) override;
		void DeletePathEntity(QTPFS::entity pathEntity);

		unsigned int RequestPath(
			CSolidObject* object,
			const MoveDef* moveDef,
			float3 sourcePos,
			float3 targetPos,
			float radius,
			bool synced,
			bool immediateResult = false
		) override;

		float3 NextWayPoint(
			const CSolidObject*, // owner
			unsigned int pathID,
			unsigned int, // numRetries
			float3 point,
			float radius,
			bool synced
		) override;

		bool CurrentWaypointIsUnreachable(unsigned int pathID) override;
		bool NextWayPointIsUnreachable(unsigned int pathID) override;

		void GetPathWayPoints(
			unsigned int pathID,
			std::vector<float3>& points,
			std::vector<int>& starts
		) const override;

		int2 GetNumQueuedUpdates() const override;


		const NodeLayer& GetNodeLayer(unsigned int pathType) const { return nodeLayers[pathType]; }
		const NodeLayersChangeTrack& GetMapDamageTrack() const { return nodeLayersMapDamageTrack; };

		const spring::unordered_map<unsigned int, PathSearchTrace::Execution*>& GetPathTraces() const { return pathTraces; }
		unsigned int GetNumPendingLoadSavePaths() const;
		void CaptureLoadSaveState(LoadSaveState& outState) const;
		void QueueLoadSaveRestore(const LoadSaveState& state);
		bool HasQueuedFullLoadSaveState() const;
		bool ConsumeLoadSavePostLoadRefreshSkip();
		void ApplyLoadSaveRestore();
		bool HasLivePath(unsigned int pathID, bool requireSynced = false) const;
		bool HasLivePathForOwner(unsigned int pathID, const CSolidObject* owner, bool requireSynced = false) const;
		bool HasCompatibleLivePath(unsigned int pathID, const CSolidObject* owner, bool requireSynced = false) const;
		bool HadLoadSaveRestoreNodeLayerChecksumMismatch() const { return loadSaveRestoreNodeLayerChecksumMismatch; }
		bool NeedsLoadSaveGroundPathRebuild() const { return loadSaveRestoreNeedsGroundPathRebuild; }

		void RemovePathFromShared(QTPFS::entity entity);
		void RemovePathFromPartialShared(QTPFS::entity entity);

	private:
		void MapChanged(int x1, int z1, int x2, int z2);

		void ThreadUpdate();
		void Load();

		std::uint64_t GetMemFootPrint() const;

		typedef void (PathManager::*MemberFunc)(
			unsigned int threadNum,
			unsigned int numThreads,
			const SRectangle& rect
		);
		typedef spring::unordered_map<unsigned int, unsigned int> PathTypeMap;
		typedef spring::unordered_map<unsigned int, unsigned int>::iterator PathTypeMapIt;
		typedef spring::unordered_map<unsigned int, PathSearchTrace::Execution*> PathTraceMap;
		typedef spring::unordered_map<unsigned int, PathSearchTrace::Execution*>::iterator PathTraceMapIt;
		typedef spring::unordered_map<PathHashType, QTPFS::entity> SharedPathMap;
		typedef spring::unordered_map<PathHashType, QTPFS::entity>::iterator SharedPathMapIt;
		typedef spring::unordered_map<PathHashType, QTPFS::entity> PartialSharedPathMap;
		typedef spring::unordered_map<PathHashType, QTPFS::entity>::iterator PartialSharedPathMapIt;

		typedef std::vector<PathSearch*> PathSearchVect;
		typedef std::vector<PathSearch*>::iterator PathSearchVectIt;

		void InitNodeLayersThreaded(const SRectangle& rect);
		void InitNodeLayer(unsigned int layerNum, const SRectangle& r);
		void InitRootSize(const SRectangle& r);
		void UpdateNodeLayer(unsigned int layerNum, const SRectangle& r, int currentThread);

		bool InitializeSearch(QTPFS::entity searchEntity);
		void RemovePathSearch(QTPFS::entity pathEntity);

		void ReadyQueuedSearches();
		void ProcessPathSearch(int i, bool shouldBeRaw);
		void ExecuteQueuedSearches();
		void QueueDeadPathSearches();

		unsigned int QueueSearch(
			const CSolidObject* object,
			const MoveDef* moveDef,
			const float3& sourcePoint,
			const float3& targetPoint,
			const float radius,
			const bool synced,
			const bool externalRequest,
			const bool allowRawSearch
		);

	public:
		unsigned int RequeueSearch(
			IPath* oldPath,
			const bool allowRawSearch,
			const bool allowPartialSearch,
			const bool allowRepair
		);

	private:
		bool ExecuteSearch(
			PathSearch* search,
			NodeLayer& nodeLayer,
			unsigned int pathType,
			bool immediateSearch
		);

		unsigned int ExecuteImmediateSearch(unsigned int pathId);
		std::uint32_t CalculateNodeLayerChecksum() const;

		bool IsFinalized() const { return isFinalized; }

	public:
		std::vector<NodeLayer> nodeLayers;

	private:
		PathCache pathCache;

		// per thread data
		std::vector<SearchThreadData> searchThreadData;
		std::vector<UpdateThreadData> updateThreadData;
		std::vector<unsigned char> nodeLayerUpdatePriorityOrder;

		PathTraceMap pathTraces;
		SharedPathMap sharedPaths;
		PartialSharedPathMap partialSharedPaths;

		// std::vector<unsigned int> numCurrExecutedSearches;
		// std::vector<unsigned int> numPrevExecutedSearches;

		NodeLayersChangeTrack nodeLayersMapDamageTrack;

		int deadPathsToUpdatePerFrame = 1;
		int recalcDeadPathUpdateRateOnFrame = 0;
		int rootSize = 0;

		static unsigned int LAYERS_PER_UPDATE;
		static unsigned int MAX_TEAM_SEARCHES;

		unsigned int searchStateOffset;
		unsigned int numPathRequests;

		std::int32_t refreshDirtyPathRateFrame = QTPFS_LAST_FRAME;
		std::int32_t updateDirtyPathRate = 0;
		std::int32_t updateDirtyPathRemainder = 0;

		std::uint32_t pfsCheckSum;

		QTPFS::entity systemEntity = entt::null;

		bool isFinalized = false;
		bool loadSaveRestoreNodeLayerChecksumMismatch = false;
		bool loadSaveRestoreNeedsGroundPathRebuild = false;
		bool loadSaveRestoreSkipPostLoadRefresh = false;

		static constexpr size_t INITIAL_PATH_RESERVE = 256;
	};
}

#endif
