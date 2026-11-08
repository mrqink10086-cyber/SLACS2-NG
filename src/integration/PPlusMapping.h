#pragma once

#include <cstdint>
#include <span>

namespace slacs
{
	struct PPlusInteractionFlags
	{
		bool vaginal{};
		bool anal{};
		bool oral{};
		bool trib{};
		bool hand{};
		bool foot{};
		bool breast{};
	};

	inline PPlusInteractionFlags ResolvePPlusInteractions(
		const PPlusInteractionFlags& a_stageTags,
		std::span<const std::int32_t> a_types,
		bool a_hasDirected,
		bool a_uniquePair)
	{
		if (!a_hasDirected) {
			return a_stageTags;
		}
		PPlusInteractionFlags flags{};
		// SexLab P+/Source/Scripts/SexLabThread.psc defines no breast CTYPE;
		// its stage tag identifies this pair only in a two-actor scene.
		flags.breast = a_uniquePair && a_stageTags.breast;
		for (const auto type : a_types) {
			switch (type) {
			case 1: flags.vaginal = true; break;
			case 2: flags.anal = true; break;
			case 3:
			case 5:
			case 6:
			case 7:
			case 11: flags.oral = true; break;
			case 4: flags.trib = true; break;
			case 8: flags.foot = true; break;
			case 9: flags.hand = true; break;
			default: break;
			}
		}
		return flags;
	}
}
