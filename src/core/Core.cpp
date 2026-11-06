#include "core/Core.h"

#include "config/Settings.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <unordered_set>

namespace slacs
{
	namespace
	{
		constexpr std::size_t kMaxSupportedLayers = 5;
		constexpr double kMaxGameHours = 1.0e9;
		constexpr std::uint8_t kEffectCombinationCount = 27;

		std::size_t EffectiveLayerCap(const Settings& a_settings)
		{
			return std::clamp<std::size_t>(a_settings.maxLayersPerRegion, 1, kMaxSupportedLayers);
		}

		std::size_t TexturePoolSize(Region a_region) noexcept
		{
			switch (a_region) {
			case Region::BodyAnal:
			case Region::BodyBreast:
			case Region::Face:
				return 12;
			case Region::BodyVaginal:
				return 12;
			case Region::BodyLegs:
			case Region::Hands:
			case Region::Feet:
				return 4;
			}
			return 1;
		}

		std::uint64_t Mix(std::uint64_t a_value) noexcept
		{
			a_value += 0x9E3779B97F4A7C15ull;
			a_value = (a_value ^ (a_value >> 30)) * 0xBF58476D1CE4E5B9ull;
			a_value = (a_value ^ (a_value >> 27)) * 0x94D049BB133111EBull;
			return a_value ^ (a_value >> 31);
		}

		bool RegionContainsTexture(const RegionState& a_region, std::uint16_t a_index) noexcept
		{
			return std::any_of(a_region.layers.begin(), a_region.layers.end(), [a_index](const LayerRecord& a_layer) {
				return a_layer.textureIndex == a_index;
			});
		}

		std::uint16_t ChooseTextureIndex(
			const RegionState& a_region,
			Region a_regionId,
			std::uint64_t a_seed,
			ActorId a_actor) noexcept
		{
			const auto pool = TexturePoolSize(a_regionId);
			const auto start = static_cast<std::size_t>(
				Mix(a_seed ^ (a_actor * 0x9E3779B97F4A7C15ull) ^ (RegionIndex(a_regionId) * 0xBF58476D1CE4E5B9ull)) % pool);
			for (std::size_t offset = 0; offset < pool; ++offset) {
				const auto candidate = static_cast<std::uint16_t>(((start + offset) % pool) + 1);
				if (!RegionContainsTexture(a_region, candidate)) {
					return candidate;
				}
			}
			return static_cast<std::uint16_t>(start + 1);
		}

		RegionTarget MakeTarget(const ActorState& a_actor, Region a_region)
		{
			RegionTarget target{};
			target.actor = a_actor.actor;
			target.region = a_region;
			const auto& region = a_actor.regions[RegionIndex(a_region)];
			target.revision = region.revision;
			target.layers.reserve(region.layers.size());
			for (const auto& layer : region.layers) {
				target.layers.push_back(LayerVisual{ layer.id, layer.textureIndex, layer.variant });
			}
			return target;
		}

		ActorState& EnsureActor(CoreState& a_state, ActorId a_actor, const GameTime& a_now)
		{
			auto [it, inserted] = a_state.actors.try_emplace(a_actor);
			if (inserted) {
				it->second.actor = a_actor;
				it->second.lastSettledGameHours = a_now.calendarHours;
			}
			return it->second;
		}

		void PushAllRegionTargets(ChangeSet& a_change, const ActorState& a_actor)
		{
			for (const auto a_region : kRegions) {
				a_change.regions.push_back(MakeTarget(a_actor, a_region));
			}
		}

		std::uint8_t EffectLevel(std::size_t a_layers, const Settings& a_settings) noexcept
		{
			if (a_layers >= static_cast<std::size_t>(a_settings.spellLevel2Threshold)) {
				return 2;
			}
			if (a_layers >= static_cast<std::size_t>(a_settings.spellLevel1Threshold)) {
				return 1;
			}
			return 0;
		}

		void PushSpellTarget(ChangeSet& a_change, const ActorState& a_actor, const Settings& a_settings)
		{
			const auto oral = EffectLevel(a_actor.regions[RegionIndex(Region::Face)].layers.size(), a_settings);
			const auto vaginal = EffectLevel(a_actor.regions[RegionIndex(Region::BodyVaginal)].layers.size(), a_settings);
			const auto anal = EffectLevel(a_actor.regions[RegionIndex(Region::BodyAnal)].layers.size(), a_settings);
			const auto combination = static_cast<std::uint8_t>(anal * 9 + vaginal * 3 + oral);
			if (combination >= kEffectCombinationCount) {
				return;
			}
			SpellTarget target{};
			target.actor = a_actor.actor;
			target.desired.combinationIndex = combination;
			target.desired.enabled = a_settings.effectsEnabled;
			target.revision = a_actor.revision;
			a_change.spells.push_back(target);
		}

		bool ClearRegionLayers(ChangeSet& a_change, ActorState& a_actor, Region a_region)
		{
			auto& region = a_actor.regions[RegionIndex(a_region)];
			if (region.layers.empty()) {
				region.decayRemainderGameHours = 0.0;
				return false;
			}
			region.layers.clear();
			region.decayRemainderGameHours = 0.0;
			++region.revision;
			++a_actor.revision;
			a_change.regions.push_back(MakeTarget(a_actor, a_region));
			return true;
		}

		bool HasLayers(const ActorState& a_actor) noexcept
		{
			return std::any_of(a_actor.regions.begin(), a_actor.regions.end(), [](const RegionState& a_region) {
				return !a_region.layers.empty();
			});
		}

		double TimedClearSeconds(const Settings& a_settings) noexcept
		{
			return std::isfinite(a_settings.timedClearSeconds) && a_settings.timedClearSeconds > 0.0 ?
				a_settings.timedClearSeconds :
				120.0;
		}

		void ArmTimedClear(ActorState& a_actor, const GameTime& a_now, const Settings& a_settings)
		{
			if (a_settings.timedClear == TimedClearMode::Disabled) {
				if (a_actor.timedClearAt) {
					a_actor.timedClearAt.reset();
					++a_actor.revision;
				}
				return;
			}
			if (HasLayers(a_actor) && !a_actor.timedClearAt) {
				a_actor.timedClearAt = a_now.activeSeconds + TimedClearSeconds(a_settings);
				++a_actor.revision;
			}
		}

		void NormalizeRegion(ActorState& a_actor, Region a_regionId, const Settings& a_settings)
		{
			auto& region = a_actor.regions[RegionIndex(a_regionId)];
			bool changed = false;
			if (!std::isfinite(region.decayRemainderGameHours) ||
				region.decayRemainderGameHours < 0.0 ||
				region.decayRemainderGameHours > kMaxGameHours) {
				region.decayRemainderGameHours = std::isfinite(region.decayRemainderGameHours) ?
					std::clamp(region.decayRemainderGameHours, 0.0, kMaxGameHours) :
					0.0;
				changed = true;
			}
			const auto cap = EffectiveLayerCap(a_settings);
			if (region.layers.size() > cap) {
				region.layers.resize(cap);
				changed = true;
			}

			std::unordered_set<std::uint64_t> ids;
			std::vector<LayerRecord> layers;
			layers.reserve(region.layers.size());
			const auto pool = TexturePoolSize(a_regionId);
			for (const auto& layer : region.layers) {
				const bool valid = layer.id != 0 &&
					layer.id != std::numeric_limits<std::uint64_t>::max() &&
					ids.insert(layer.id).second &&
					layer.textureIndex != 0 &&
					layer.textureIndex <= pool &&
					(layer.variant == TextureVariant::Normal || layer.variant == TextureVariant::Mega);
				if (valid) {
					layers.push_back(layer);
				} else {
					changed = true;
				}
			}
			if (layers.size() != region.layers.size()) {
				region.layers = std::move(layers);
			}
			if (region.layers.empty() && region.decayRemainderGameHours != 0.0) {
				region.decayRemainderGameHours = 0.0;
				changed = true;
			}
			if (changed) {
				++region.revision;
				++a_actor.revision;
			}
		}

		void EmitDeadlines(ChangeSet& a_change, const ActorState& a_actor, const GameTime& a_now, const Settings& a_settings)
		{
			const auto interval = a_settings.decayIntervalGameHours;
			const bool naturalUsable = a_settings.naturalDecay && std::isfinite(interval) && interval > 0.0;
			for (const auto a_region : kRegions) {
				const auto& region = a_actor.regions[RegionIndex(a_region)];
				DeadlineIntent intent{};
				intent.actor = a_actor.actor;
				intent.kind = DeadlineKind::NaturalDecay;
				intent.clock = ClockDomain::CalendarGameHours;
				intent.region = a_region;
				intent.ownerRevision = region.revision;
				if (naturalUsable && !region.layers.empty()) {
					const auto remainder = std::clamp(region.decayRemainderGameHours, 0.0, interval);
					intent.action = DeadlineIntent::Action::Upsert;
					intent.due = a_now.calendarHours + std::max(0.0, interval - remainder);
				} else {
					intent.action = DeadlineIntent::Action::Cancel;
				}
				a_change.deadlines.push_back(intent);
			}

			DeadlineIntent timed{};
			timed.actor = a_actor.actor;
			timed.kind = DeadlineKind::TimedClear;
			timed.clock = ClockDomain::ActiveSeconds;
			timed.ownerRevision = a_actor.revision;
			if (a_settings.timedClear != TimedClearMode::Disabled && a_actor.timedClearAt) {
				timed.action = DeadlineIntent::Action::Upsert;
				timed.due = *a_actor.timedClearAt;
			} else {
				timed.action = DeadlineIntent::Action::Cancel;
			}
			a_change.deadlines.push_back(timed);
		}

		void SettleActor(
			ChangeSet& a_change,
			ActorState& a_actor,
			const GameTime& a_now,
			const Settings& a_settings,
			const EnvironmentSample* a_sample)
		{
			if (!std::isfinite(a_actor.lastSettledGameHours)) {
				a_actor.lastSettledGameHours = a_now.calendarHours;
			}
			auto elapsed = a_now.calendarHours - a_actor.lastSettledGameHours;
			if (!std::isfinite(elapsed) || elapsed < 0.0) {
				elapsed = 0.0;
			} else {
				// Keep the baseline monotonic. A save load or debug clock jump
				// may report an earlier calendar value; moving the baseline
				// backwards would count the same interval twice.
				a_actor.lastSettledGameHours = a_now.calendarHours;
			}

			const bool swimming = a_sample && a_sample->actor == a_actor.actor &&
				a_sample->swimming && a_settings.swimCleaning == SwimCleaningPolicy::LegacyImmediateClear;
			if (swimming) {
				for (const auto a_region : kRegions) {
					ClearRegionLayers(a_change, a_actor, a_region);
				}
				if (a_actor.timedClearAt) {
					a_actor.timedClearAt.reset();
					++a_actor.revision;
				}
			} else if (a_settings.naturalDecay) {
				const auto interval = a_settings.decayIntervalGameHours;
				if (std::isfinite(interval) && interval > 0.0) {
					double rainHours = 0.0;
					// A sample only describes the interval ending at its
					// timestamp. If the clock did not advance, this is a
					// duplicate/stale sample and its rain increment has
					// already been consumed (or never covered an interval).
					if (elapsed > 0.0 && a_sample && a_sample->actor == a_actor.actor &&
						a_settings.rainAccelerates && !a_sample->interior && a_sample->raining &&
						std::isfinite(a_sample->observedRainHours)) {
						rainHours = std::max(0.0, a_sample->observedRainHours);
					}
					for (const auto a_region : kRegions) {
						auto& region = a_actor.regions[RegionIndex(a_region)];
						if (region.layers.empty()) {
							region.decayRemainderGameHours = 0.0;
							continue;
						}
						const auto total = std::max(0.0, region.decayRemainderGameHours) + elapsed + rainHours;
						const auto stepsRaw = std::floor(total / interval + 1e-9);
						if (std::isnan(stepsRaw)) {
							region.decayRemainderGameHours = 0.0;
							continue;
						}
						if (stepsRaw <= 0.0) {
							region.decayRemainderGameHours = std::isfinite(total) ?
								std::min(total, kMaxGameHours) :
								0.0;
							continue;
						}
						const auto remove = !std::isfinite(stepsRaw) ||
							stepsRaw >= static_cast<double>(region.layers.size()) ?
							region.layers.size() :
							static_cast<std::size_t>(stepsRaw);
						region.layers.resize(region.layers.size() - remove);
						region.decayRemainderGameHours = region.layers.empty() ? 0.0 : std::max(0.0, total - static_cast<double>(remove) * interval);
						++region.revision;
						++a_actor.revision;
						a_change.regions.push_back(MakeTarget(a_actor, a_region));
					}
				}
			} else {
				// Disabled natural decay must not bank partial progress. When it
				// is enabled again, decay starts from the new configuration time
				// instead of consuming time that elapsed while it was off.
				for (const auto a_region : kRegions) {
					auto& region = a_actor.regions[RegionIndex(a_region)];
					if (region.decayRemainderGameHours != 0.0) {
						region.decayRemainderGameHours = 0.0;
						++region.revision;
						++a_actor.revision;
					}
				}
			}

			if (a_settings.timedClear == TimedClearMode::Disabled) {
				if (a_actor.timedClearAt) {
					a_actor.timedClearAt.reset();
					++a_actor.revision;
				}
			} else if (a_actor.timedClearAt && a_now.activeSeconds >= *a_actor.timedClearAt) {
				for (const auto a_region : kRegions) {
					ClearRegionLayers(a_change, a_actor, a_region);
				}
				a_actor.timedClearAt.reset();
				++a_actor.revision;
			}
		}

		ChangeSet ApplyAdvance(CoreState& a_state, const Advance& a_op, const Settings& a_settings)
		{
			ChangeSet change{};
			const auto found = a_state.actors.find(a_op.actor);
			if (found == a_state.actors.end()) {
				return change;
			}

			const EnvironmentSample* sample = nullptr;
			if (a_op.environment && a_op.environment->actor == a_op.actor) {
				sample = &*a_op.environment;
			}
			SettleActor(change, found->second, a_op.now, a_settings, sample);
			EmitDeadlines(change, found->second, a_op.now, a_settings);
			PushSpellTarget(change, found->second, a_settings);
			return change;
		}

		ChangeSet ApplyAddLayers(CoreState& a_state, const AddLayers& a_op, const Settings& a_settings, const GameTime& a_now)
		{
			ChangeSet change{};
			const auto cap = EffectiveLayerCap(a_settings);
			if (a_settings.maxLayersPerRegion < 1 || a_settings.maxLayersPerRegion > kMaxSupportedLayers) {
				change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::InvalidSettings, 0, std::nullopt });
			}

			std::set<std::pair<ActorId, std::size_t>> claimed;
			std::unordered_set<ActorId> settled;
			std::unordered_set<ActorId> rearmed;
			for (const auto& requested : a_op.targets) {
				if (requested.actor == kInvalidActorId) {
					change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::InvalidTarget, requested.actor, std::nullopt });
					continue;
				}
				if (a_op.source != kInvalidActorId && requested.actor == a_op.source) {
					continue;
				}

				std::vector<Region> targetRegions;
				std::unordered_set<std::size_t> seenRegions;
				for (const auto a_region : requested.regions) {
					const auto index = RegionIndex(a_region);
					if (index >= kRegionCount) {
						change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::InvalidRegion, requested.actor, a_region });
						continue;
					}
					if (seenRegions.insert(index).second) {
						targetRegions.push_back(a_region);
					}
				}
				if (targetRegions.empty()) {
					continue;
				}

				auto& actor = EnsureActor(a_state, requested.actor, a_now);
				if (settled.insert(actor.actor).second) {
					SettleActor(change, actor, a_now, a_settings, nullptr);
				}

				for (const auto a_region : targetRegions) {
					const auto index = RegionIndex(a_region);
					if (!claimed.insert({ requested.actor, index }).second) {
						continue;
					}
					if (a_state.nextLayerId == std::numeric_limits<std::uint64_t>::max()) {
						change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::AtCapacity, requested.actor, a_region });
						continue;
					}

					auto& region = actor.regions[index];
					if (region.layers.size() >= cap) {
						change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::AtCapacity, requested.actor, a_region });
						continue;
					}

					LayerRecord layer{};
					layer.id = a_state.nextLayerId++;
					layer.textureIndex = ChooseTextureIndex(region, a_region, a_op.selectionSeed, requested.actor);
					layer.variant = requested.variant;
					region.layers.push_back(layer);
					++region.revision;
					++actor.revision;
					change.regions.push_back(MakeTarget(actor, a_region));
					rearmed.insert(actor.actor);
				}
			}

			for (const auto actorId : settled) {
				auto& actor = a_state.actors.at(actorId);
				if (rearmed.contains(actorId)) {
					if (a_settings.timedClear != TimedClearMode::Disabled) {
						actor.timedClearAt = a_now.activeSeconds + TimedClearSeconds(a_settings);
						++actor.revision;
					} else {
						actor.timedClearAt.reset();
					}
				}
				EmitDeadlines(change, actor, a_now, a_settings);
				PushSpellTarget(change, actor, a_settings);
			}
			return change;
		}

		ChangeSet ApplyClearRegions(CoreState& a_state, const ClearRegions& a_op, const Settings& a_settings, const GameTime& a_now)
		{
			ChangeSet change{};
			const auto found = a_state.actors.find(a_op.actor);
			if (found == a_state.actors.end()) {
				return change;
			}

			auto& actor = found->second;
			SettleActor(change, actor, a_now, a_settings, nullptr);
			std::unordered_set<std::size_t> seen;
			for (const auto a_region : a_op.regions) {
				const auto index = RegionIndex(a_region);
				if (index >= kRegionCount) {
					change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::InvalidRegion, a_op.actor, a_region });
					continue;
				}
				if (!seen.insert(index).second) {
					continue;
				}
				auto& region = actor.regions[index];
				if (!region.layers.empty()) {
					region.layers.clear();
					region.decayRemainderGameHours = 0.0;
					++region.revision;
					++actor.revision;
				}
				change.regions.push_back(MakeTarget(actor, a_region));
			}
			EmitDeadlines(change, actor, a_now, a_settings);
			PushSpellTarget(change, actor, a_settings);
			return change;
		}

		ChangeSet ApplySettingsChanged(CoreState& a_state, const Settings& a_settings, const GameTime& a_now)
		{
			ChangeSet change{};
			const auto cap = EffectiveLayerCap(a_settings);
			if (a_settings.maxLayersPerRegion < 1 || a_settings.maxLayersPerRegion > kMaxSupportedLayers) {
				change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::InvalidSettings, 0, std::nullopt });
			}
			for (auto& [actorId, actor] : a_state.actors) {
				for (const auto a_region : kRegions) {
					auto& region = actor.regions[RegionIndex(a_region)];
					if (region.layers.size() > cap) {
						region.layers.resize(cap);
						++region.revision;
						++actor.revision;
					}
					if (region.layers.empty()) {
						region.decayRemainderGameHours = 0.0;
					}
				}
				ArmTimedClear(actor, a_now, a_settings);
				PushAllRegionTargets(change, actor);
				EmitDeadlines(change, actor, a_now, a_settings);
				PushSpellTarget(change, actor, a_settings);
			}
			return change;
		}

		void Sanitize(
			CoreState& a_state,
			std::vector<ActorState> a_actors,
			const Settings& a_settings,
			const GameTime& a_now,
			ChangeSet& a_change)
		{
			const auto cap = EffectiveLayerCap(a_settings);
			std::unordered_set<ActorId> seen;
			for (auto& actor : a_actors) {
				if (actor.actor == kInvalidActorId || !seen.insert(actor.actor).second) {
					continue;
				}
				if (!std::isfinite(actor.lastSettledGameHours) || actor.lastSettledGameHours < 0.0 ||
					actor.lastSettledGameHours > kMaxGameHours) {
					actor.lastSettledGameHours = a_now.calendarHours;
				}
				for (const auto a_region : kRegions) {
					const bool overCapacity = actor.regions[RegionIndex(a_region)].layers.size() > cap;
					NormalizeRegion(actor, a_region, a_settings);
					if (overCapacity) {
						a_change.diagnostics.push_back(CoreDiagnostic{ CoreDiagnosticCode::AtCapacity, actor.actor, a_region });
					}
					for (const auto& layer : actor.regions[RegionIndex(a_region)].layers) {
						a_state.nextLayerId = std::max(a_state.nextLayerId, layer.id + 1);
					}
				}
				ArmTimedClear(actor, a_now, a_settings);
				a_state.actors.emplace(actor.actor, std::move(actor));
			}
		}
	}

	ChangeSet Core::apply(CoreState& a_state, const CoreOp& a_op, const Settings& a_settings, const GameTime& a_now)
	{
		return std::visit(
			[&](const auto& op) -> ChangeSet {
				using T = std::decay_t<decltype(op)>;
				if constexpr (std::is_same_v<T, AddLayers>) {
					return ApplyAddLayers(a_state, op, a_settings, a_now);
				} else if constexpr (std::is_same_v<T, ClearRegions>) {
					return ApplyClearRegions(a_state, op, a_settings, a_now);
				} else if constexpr (std::is_same_v<T, Advance>) {
					return ApplyAdvance(a_state, op, a_settings);
				} else if constexpr (std::is_same_v<T, SettingsChanged>) {
					return ApplySettingsChanged(a_state, a_settings, a_now);
				}
			},
			a_op);
	}

	ChangeSet Core::replaceState(CoreState& a_state, std::vector<ActorState> a_actors, const Settings& a_settings, const GameTime& a_now)
	{
		ChangeSet change{};
		a_state = {};
		Sanitize(a_state, std::move(a_actors), a_settings, a_now, change);
		for (const auto& [actorId, actor] : a_state.actors) {
			PushAllRegionTargets(change, actor);
			EmitDeadlines(change, actor, a_now, a_settings);
			PushSpellTarget(change, actor, a_settings);
		}
		return change;
	}
}
