#include "integration/PPlusMapping.h"
#include "runtime/PPlusScenes.h"

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

	void CheckSceneEndAndTidReuse()
	{
		PPlusScenes scenes;
		constexpr Tid tid = 3;
		constexpr SessionEpoch epoch = 1;
		std::vector<RequestId> dispatched;
		const auto emit = [&](const PPlusCompleted& completion) { dispatched.push_back(completion.requestId); };
		const auto result = [&](IngressSequence sequence) {
			ResolvedOrgasm outcome{};
			outcome.tid = tid;
			outcome.epoch = epoch;
			outcome.sequence = sequence;
			return PPlusCompleted{ sequence, epoch, std::move(outcome) };
		};

		Expect(scenes.start(tid, epoch).admitted && scenes.isActive(tid, epoch), "Start opens the scene");
		scenes.trackAccepted(tid, 10);
		scenes.trackAccepted(tid, 11);
		Expect(scenes.end(tid) && !scenes.isActive(tid, epoch), "End closes admission for new requests");
		Expect(scenes.complete(tid, 11, result(11), emit) && dispatched.empty(), "End preserves ordering for in-flight completions");
		Expect(scenes.complete(tid, 10, result(10), emit) && dispatched == std::vector<RequestId>{ 10, 11 },
			"queries accepted before End still commit in ingress order");
		Expect(!scenes.complete(tid, 11, result(11), emit), "a completion cannot commit twice");

		Expect(!scenes.start(tid, epoch).replaced, "completed scenes retain no pending entries");
		scenes.trackAccepted(tid, 12);
		scenes.end(tid);
		const auto next = scenes.start(tid, epoch);
		Expect(next.admitted && next.replaced, "Start on a reused tid invalidates queries still draining");
		scenes.trackAccepted(tid, 13);
		Expect(!scenes.complete(tid, 12, result(12), emit), "an old result cannot reach a new scene with the same tid");
		Expect(scenes.complete(tid, 13, result(13), emit) && dispatched == std::vector<RequestId>{ 10, 11, 13 },
			"a new scene can commit its own result on the reused tid");

		scenes.trackAccepted(tid, 14);
		scenes.end(tid);
		scenes.clear();
		Expect(!scenes.complete(tid, 14, result(14), emit), "a session reset drops old queries");
	}
}

int main()
{
	CheckSceneEndAndTidReuse();
	Expect(ShouldIncludePPlusTarget(true, 1, true), "male source can target an adult female NPC without player or follower status");
	Expect(ShouldIncludePPlusTarget(true, 2, true), "male source can target a futa NPC");
	Expect(ShouldIncludePPlusTarget(true, 0, false) && ShouldIncludePPlusTarget(true, 3, false),
		"male source keeps existing male target eligibility");
	Expect(!ShouldIncludePPlusTarget(false, 0, true), "female source still cannot target a male actor");
	Expect(ShouldIncludePPlusTarget(false, 1, true), "female source can target an adult female NPC");
	Expect(!ShouldIncludePPlusTarget(true, 1, false), "child or creature female targets remain excluded");
	Expect(!ShouldIncludePPlusTarget(true, 5, true), "unknown target sex is excluded");

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
