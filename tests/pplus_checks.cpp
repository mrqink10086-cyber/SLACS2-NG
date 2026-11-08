#include "integration/PPlusMapping.h"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
	using namespace slacs;
	int failures{};
	void Expect(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			++failures;
			std::cerr << "FAIL: " << a_message << '\n';
		}
	}
}

int main()
{
	const PPlusInteractionFlags allTags{ true, true, true, true, true, true, true };
	const std::array<std::int32_t, 1> vaginal{ 1 };
	const auto dyad = ResolvePPlusInteractions(allTags, vaginal, true, true);
	Expect(dyad.vaginal && dyad.breast && !dyad.anal && !dyad.oral && !dyad.hand && !dyad.foot && !dyad.trib,
		"a unique pair keeps breast fallback while directed acts suppress unrelated stage tags");
	const auto group = ResolvePPlusInteractions(allTags, vaginal, true, false);
	Expect(group.vaginal && !group.breast && !group.anal && !group.oral && !group.hand && !group.foot && !group.trib,
		"group pairs never inherit unrelated whole-stage tags");
	Expect(!ResolvePPlusInteractions({}, vaginal, true, true).breast, "breast fallback requires a stage tag");
	const auto fallback = ResolvePPlusInteractions(allTags, {}, false, true);
	Expect(fallback.vaginal && fallback.anal && fallback.oral && fallback.trib && fallback.hand && fallback.foot && fallback.breast,
		"missing directed data uses stage fallback");
	for (std::int32_t type = -1; type <= 13; ++type) {
		const std::array value{ type };
		const auto flags = ResolvePPlusInteractions({}, value, true, false);
		Expect(flags.vaginal == (type == 1) && flags.anal == (type == 2) &&
			flags.oral == (type == 3 || type == 5 || type == 6 || type == 7 || type == 11) &&
			flags.trib == (type == 4) && flags.hand == (type == 9) && flags.foot == (type == 8) && !flags.breast,
			"directed CTYPE mapping preserves the supported region rules");
	}
	if (failures == 0) std::cout << "SLACS2 NG PPlus mapping checks passed\n";
	return failures == 0 ? 0 : 1;
}
