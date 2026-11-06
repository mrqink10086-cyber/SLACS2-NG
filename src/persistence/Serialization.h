#pragma once

#include "core/Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace SKSE
{
	class SerializationInterface;
}

namespace slacs
{
	struct PersistedActor
	{
		FormId formId{};
		std::array<RegionState, kRegionCount> regions{};
		GameHours lastSettledGameHours{};
		std::optional<double> timedClearRemainingSeconds;
	};

	struct SaveSnapshot
	{
		std::uint32_t schemaVersion{ 1 };
		SessionEpoch epoch{};
		GameTime capturedAt;
		std::vector<PersistedActor> actors;
	};

	enum class PersistenceError : std::uint8_t
	{
		None,
		Io,
		BadRecord,
		UnsupportedSchema,
		InvalidValue,
		RemapFailed,
	};

	struct PersistenceStatus
	{
		bool success{};
		std::uint32_t recordsWritten{};
		PersistenceError error{ PersistenceError::None };
	};

	enum class LoadStatus : std::uint8_t
	{
		Loaded,
		Empty,
		UnsupportedSchema,
		InvalidData,
		IoError,
	};

	struct LoadResult
	{
		LoadStatus status{};
		std::uint32_t schemaVersion{};
		std::vector<PersistedActor> actors;
		std::vector<PersistenceError> recordErrors;
	};

	struct Serialization
	{
		static SaveSnapshot makeSnapshot(const CoreState& a_state, const GameTime& a_now);
		static PersistenceStatus save(SKSE::SerializationInterface& a_skse, const SaveSnapshot& a_snapshot);
		static LoadResult load(SKSE::SerializationInterface& a_skse);
	};
}
