#pragma once

#include "persistence/Serialization.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace slacs::serialization_codec
{
	// Pure record codec used by Serialization and by the automated checks. It
	// owns no SKSE interface and keeps no engine pointers. The resolver maps a
	// saved FormID to the current load order; returning false drops that actor.
	SaveSnapshot makeSnapshot(const CoreState& a_state, const GameTime& a_now);
	PersistenceError encode(const SaveSnapshot& a_snapshot, std::vector<std::uint8_t>& a_data);
	LoadResult decode(
		const std::uint8_t* a_data,
		std::size_t a_size,
		const std::function<bool(FormId, FormId&)>& a_resolve);
}
