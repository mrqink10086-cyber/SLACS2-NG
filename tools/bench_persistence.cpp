#include "persistence/SerializationCodec.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>

namespace
{
	std::size_t g_allocationCount = 0;
	bool g_countAllocations = false;
}

void* operator new(std::size_t a_size)
{
	if (void* memory = std::malloc(a_size == 0 ? 1 : a_size)) {
		if (g_countAllocations) {
			++g_allocationCount;
		}
		return memory;
	}
	throw std::bad_alloc();
}

void* operator new[](std::size_t a_size)
{
	if (void* memory = std::malloc(a_size == 0 ? 1 : a_size)) {
		if (g_countAllocations) {
			++g_allocationCount;
		}
		return memory;
	}
	throw std::bad_alloc();
}

void operator delete(void* a_memory) noexcept
{
	std::free(a_memory);
}

void operator delete[](void* a_memory) noexcept
{
	std::free(a_memory);
}

void operator delete(void* a_memory, std::size_t) noexcept
{
	std::free(a_memory);
}

void operator delete[](void* a_memory, std::size_t) noexcept
{
	std::free(a_memory);
}

int main()
{
	slacs::CoreState state{};
	for (std::uint64_t actorId = 0; actorId < 1024; ++actorId) {
		slacs::ActorState actor{};
		actor.actor = actorId + 1;
		actor.formId = static_cast<slacs::FormId>(actorId + 1);
		actor.lastSettledGameHours = 100.0;
		for (std::size_t regionIndex = 0; regionIndex < slacs::kRegionCount; ++regionIndex) {
			auto& layers = actor.regions[regionIndex].layers;
			const std::uint64_t texturePoolSize = regionIndex == slacs::RegionIndex(slacs::Region::BodyLegs) ||
					regionIndex == slacs::RegionIndex(slacs::Region::Hands) ||
					regionIndex == slacs::RegionIndex(slacs::Region::Feet) ?
					4 :
					12;
			for (std::uint64_t layerIndex = 0; layerIndex < 5; ++layerIndex) {
				layers.push_back({ actorId * 16 + layerIndex + 1,
					static_cast<std::uint16_t>(layerIndex % texturePoolSize + 1),
					slacs::TextureVariant::Normal });
			}
		}
		state.actors.emplace(actor.actor, std::move(actor));
	}

	for (int warmup = 0; warmup < 3; ++warmup) {
		auto snapshot = slacs::serialization_codec::makeSnapshot(state, { 123.0, 456.0 });
	}

	constexpr int kRounds = 15;
	constexpr int kIterationsPerRound = 8;
	std::vector<double> microsPerSnapshot;
	std::vector<double> allocationsPerSnapshot;
	microsPerSnapshot.reserve(kRounds);
	allocationsPerSnapshot.reserve(kRounds);
	std::size_t actorCountSink = 0;

	for (int round = 0; round < kRounds; ++round) {
		g_allocationCount = 0;
		const auto start = std::chrono::steady_clock::now();
		g_countAllocations = true;
		for (int iteration = 0; iteration < kIterationsPerRound; ++iteration) {
			auto snapshot = slacs::serialization_codec::makeSnapshot(state, { 123.0, 456.0 });
			actorCountSink += snapshot.actors.size();
		}
		g_countAllocations = false;
		const auto stop = std::chrono::steady_clock::now();
		microsPerSnapshot.push_back(
			std::chrono::duration<double, std::micro>(stop - start).count() / kIterationsPerRound);
		allocationsPerSnapshot.push_back(
			static_cast<double>(g_allocationCount) / kIterationsPerRound);
	}

	std::sort(microsPerSnapshot.begin(), microsPerSnapshot.end());
	std::sort(allocationsPerSnapshot.begin(), allocationsPerSnapshot.end());
	std::printf("actors=1024 regions=7 layers_per_region=5 rounds=%d iterations_per_round=%d\n",
		kRounds, kIterationsPerRound);
	std::printf("snapshot_us median=%.1f p10=%.1f p90=%.1f allocations_per_snapshot median=%.1f sink=%zu\n",
		microsPerSnapshot[microsPerSnapshot.size() / 2],
		microsPerSnapshot[1],
		microsPerSnapshot[microsPerSnapshot.size() - 2],
		allocationsPerSnapshot[allocationsPerSnapshot.size() / 2],
		actorCountSink);
}
