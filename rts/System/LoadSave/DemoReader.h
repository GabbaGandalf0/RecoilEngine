/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef DEMO_READER
#define DEMO_READER

#include <fstream>
#include <vector>

#include "Demo.h"

#include "Game/Players/PlayerStatistics.h"
#include "Sim/Misc/TeamStatistics.h"

namespace netcode { class RawPacket; }
class CFileHandler;

struct DemoReaderStreamState
{
	bool valid = false;
	int filePos = 0;
	int bytesRemaining = 0;
	float demoTimeOffset = 0.0f;
	float nextDemoReadTime = 0.0f;
	float chunkModGameTime = 0.0f;
	unsigned int chunkLength = 0;
};

/**
 * @brief Utility class for reading demofiles
 */
class CDemoReader : public CDemo
{
public:
	/**
	@brief Open a demofile for reading
	@throw std::runtime_error Demofile not found / header corrupt / outdated
	*/
	CDemoReader(const std::string& filename, float curTime, int startFrame = -1, float startTime = -1.0f);
	virtual ~CDemoReader();

	/**
	@brief read from demo file
	@return The data read (or 0 if no data), don't forget to delete it
	*/
	netcode::RawPacket* GetData(const float curTime);

	/**
	@brief Whether the demo has reached the end
	@return true when end reached, false when there is still stuff to read
	*/
	bool ReachedEnd();

	float GetModGameTime() const { return chunkHeader.modGameTime; }
	float GetDemoTimeOffset() const { return demoTimeOffset; }
	float GetNextDemoReadTime() const { return nextDemoReadTime; }
	bool CaptureStreamState(DemoReaderStreamState& state);
	bool CaptureStreamStateForFrame(int startFrame, float curTime, DemoReaderStreamState& state);
	bool RestoreStreamState(const DemoReaderStreamState& state);

	const std::string& GetSetupScript() const
	{
		return setupScript;
	};

	const std::vector<PlayerStatistics>& GetPlayerStats() const { return playerStats; }
	const std::vector< std::vector<TeamStatistics> >& GetTeamStats() const { return teamStats; }
	const std::vector< unsigned char >& GetWinningAllyTeams() const { return winningAllyTeams; }

	/// Not needed for normal demo watching
	void LoadStats();

private:
	void SeekToCheckpointStart(int startFrame, float startTime, float curTime);

	CFileHandler* playbackDemo;

	float demoTimeOffset;
	float nextDemoReadTime;
	int bytesRemaining;
	int playbackDemoSize;
	int streamStartFilePos;
	int streamStartBytesRemaining;

	DemoStreamChunkHeader chunkHeader;
	DemoStreamChunkHeader streamStartChunkHeader;

	std::string setupScript;	// the original, unaltered version from script

	std::vector<PlayerStatistics> playerStats; // one stat per player
	std::vector< std::vector<TeamStatistics> > teamStats; // many stats per team
	std::vector<unsigned char> winningAllyTeams;
};

#endif
