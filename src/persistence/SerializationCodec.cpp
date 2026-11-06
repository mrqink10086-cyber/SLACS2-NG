#include "persistence/SerializationCodec.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <utility>

namespace slacs::serialization_codec
{
	namespace
	{
		constexpr std::uint32_t kSchemaVersion = 1;
		constexpr std::uint32_t kMaxActors = 100000;
		constexpr std::uint32_t kMaxLayers = 5;
		constexpr std::size_t kMaxRecordBytes = 64 * 1024 * 1024;
		constexpr double kMaxGameHours = 1.0e9;
		constexpr double kMaxActiveSeconds = 1.0e9;
		// formId + seven regions (layer count, remainder, revision) + last
		// settled + timed-clear presence/value.
		constexpr std::size_t kMinActorBytes =
			sizeof(FormId) + kRegionCount * (sizeof(std::uint32_t) + sizeof(double) + sizeof(Revision)) +
			sizeof(GameHours) + sizeof(std::uint8_t) + sizeof(double);

		template <class T>
		void Append(std::vector<std::uint8_t>& a_data, const T& a_value)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(std::addressof(a_value));
			a_data.insert(a_data.end(), bytes, bytes + sizeof(T));
		}

		class Reader
		{
		public:
			Reader(const std::uint8_t* a_data, std::size_t a_size) :
				data_(a_data), size_(a_size)
			{}

			template <class T>
			bool read(T& a_value)
			{
				if (pos_ + sizeof(T) > size_) {
					return false;
				}
				std::memcpy(std::addressof(a_value), data_ + pos_, sizeof(T));
				pos_ += sizeof(T);
				return true;
			}

			[[nodiscard]] bool done() const
			{
				return pos_ == size_;
			}

			[[nodiscard]] std::size_t remaining() const
			{
				return size_ - pos_;
			}

		private:
			const std::uint8_t* data_{};
			std::size_t size_{};
			std::size_t pos_{};
		};

		bool ValidRange(double a_value, double a_max) noexcept
		{
			return std::isfinite(a_value) && a_value >= 0.0 && a_value <= a_max;
		}

		std::size_t TexturePoolSize(std::size_t a_regionIndex) noexcept
		{
			switch (static_cast<Region>(a_regionIndex)) {
			case Region::BodyLegs:
			case Region::Hands:
			case Region::Feet:
				return 4;
			default:
				return 12;
			}
		}

		bool ValidLayer(const LayerRecord& a_layer, std::size_t a_regionIndex) noexcept
		{
			return a_layer.id != 0 &&
				a_layer.id != std::numeric_limits<std::uint64_t>::max() &&
				a_layer.textureIndex != 0 &&
				a_layer.textureIndex <= TexturePoolSize(a_regionIndex) &&
				(a_layer.variant == TextureVariant::Normal || a_layer.variant == TextureVariant::Mega);
		}

		bool ValidRegion(const RegionState& a_region, std::size_t a_regionIndex) noexcept
		{
			if (a_region.layers.size() > kMaxLayers) {
				return false;
			}
			if (!ValidRange(a_region.decayRemainderGameHours, kMaxGameHours)) {
				return false;
			}
			std::unordered_set<std::uint64_t> ids;
			for (const auto& layer : a_region.layers) {
				if (!ValidLayer(layer, a_regionIndex) || !ids.insert(layer.id).second) {
					return false;
				}
			}
			return true;
		}

		LoadResult Invalid(PersistenceError a_error)
		{
			LoadResult result{};
			result.status = LoadStatus::InvalidData;
			result.recordErrors.push_back(a_error);
			return result;
		}
	}

	PersistenceError encode(const SaveSnapshot& a_snapshot, std::vector<std::uint8_t>& a_data)
	{
		a_data.clear();
		if (a_snapshot.schemaVersion != kSchemaVersion) {
			return PersistenceError::UnsupportedSchema;
		}
		if (!std::isfinite(a_snapshot.capturedAt.calendarHours) ||
			!std::isfinite(a_snapshot.capturedAt.activeSeconds) ||
			a_snapshot.capturedAt.calendarHours < -kMaxGameHours ||
			a_snapshot.capturedAt.calendarHours > kMaxGameHours ||
			!ValidRange(a_snapshot.capturedAt.activeSeconds, kMaxActiveSeconds)) {
			return PersistenceError::InvalidValue;
		}
		if (a_snapshot.actors.size() > kMaxActors) {
			return PersistenceError::InvalidValue;
		}

		std::unordered_set<FormId> formIds;
		for (const auto& actor : a_snapshot.actors) {
			if (actor.formId == kInvalidFormId || !formIds.insert(actor.formId).second) {
				return PersistenceError::InvalidValue;
			}
			if (!ValidRange(actor.lastSettledGameHours, kMaxGameHours)) {
				return PersistenceError::InvalidValue;
			}
			for (std::size_t index = 0; index < actor.regions.size(); ++index) {
				if (!ValidRegion(actor.regions[index], index)) {
					return PersistenceError::InvalidValue;
				}
			}
			if (actor.timedClearRemainingSeconds &&
				!ValidRange(*actor.timedClearRemainingSeconds, kMaxActiveSeconds)) {
				return PersistenceError::InvalidValue;
			}
		}

		a_data.reserve(32 + a_snapshot.actors.size() * 128);
		Append(a_data, kSchemaVersion);
		Append(a_data, a_snapshot.capturedAt.calendarHours);
		Append(a_data, a_snapshot.capturedAt.activeSeconds);
		const auto actorCount = static_cast<std::uint32_t>(a_snapshot.actors.size());
		Append(a_data, actorCount);

		for (const auto& actor : a_snapshot.actors) {
			Append(a_data, actor.formId);
			for (const auto& region : actor.regions) {
				const auto layerCount = static_cast<std::uint32_t>(region.layers.size());
				Append(a_data, layerCount);
				for (const auto& layer : region.layers) {
					Append(a_data, layer.id);
					Append(a_data, layer.textureIndex);
					Append(a_data, layer.variant);
				}
				Append(a_data, region.decayRemainderGameHours);
				Append(a_data, region.revision);
			}
			Append(a_data, actor.lastSettledGameHours);
			const std::uint8_t hasTimedClear = actor.timedClearRemainingSeconds.has_value() ? 1 : 0;
			Append(a_data, hasTimedClear);
			const double timedClear = actor.timedClearRemainingSeconds.value_or(0.0);
			Append(a_data, timedClear);
		}

		if (a_data.size() > kMaxRecordBytes || a_data.size() > std::numeric_limits<std::uint32_t>::max()) {
			a_data.clear();
			return PersistenceError::InvalidValue;
		}
		return PersistenceError::None;
	}

	LoadResult decode(
		const std::uint8_t* a_data,
		std::size_t a_size,
		const std::function<bool(FormId, FormId&)>& a_resolve)
	{
		if (!a_data || a_size > kMaxRecordBytes) {
			return Invalid(PersistenceError::BadRecord);
		}

		Reader reader(a_data, a_size);
		std::uint32_t schema = 0;
		GameTime capturedAt;
		std::uint32_t actorCount = 0;
		if (!reader.read(schema) ||
			!reader.read(capturedAt.calendarHours) || !reader.read(capturedAt.activeSeconds) ||
			!reader.read(actorCount)) {
			return Invalid(PersistenceError::BadRecord);
		}
		if (schema != kSchemaVersion) {
			LoadResult result{};
			result.status = LoadStatus::UnsupportedSchema;
			result.schemaVersion = schema;
			return result;
		}
		if (!std::isfinite(capturedAt.calendarHours) ||
			capturedAt.calendarHours < -kMaxGameHours ||
			capturedAt.calendarHours > kMaxGameHours ||
			!ValidRange(capturedAt.activeSeconds, kMaxActiveSeconds)) {
			return Invalid(PersistenceError::InvalidValue);
		}
		if (actorCount > kMaxActors || actorCount > reader.remaining() / kMinActorBytes) {
			return Invalid(PersistenceError::BadRecord);
		}

		LoadResult result{};
		result.schemaVersion = schema;
		result.actors.reserve(actorCount);
		std::unordered_set<FormId> resolvedForms;
		for (std::uint32_t i = 0; i < actorCount; ++i) {
			PersistedActor actor{};
			if (!reader.read(actor.formId)) {
				return Invalid(PersistenceError::BadRecord);
			}
			for (std::size_t regionIndex = 0; regionIndex < actor.regions.size(); ++regionIndex) {
				auto& region = actor.regions[regionIndex];
				std::uint32_t layerCount = 0;
				if (!reader.read(layerCount) || layerCount > kMaxLayers) {
					return Invalid(PersistenceError::BadRecord);
				}
				region.layers.resize(layerCount);
				for (auto& layer : region.layers) {
					if (!reader.read(layer.id) || !reader.read(layer.textureIndex) || !reader.read(layer.variant)) {
						return Invalid(PersistenceError::BadRecord);
					}
				}
				if (!reader.read(region.decayRemainderGameHours) || !reader.read(region.revision)) {
					return Invalid(PersistenceError::BadRecord);
				}
				if (!ValidRegion(region, regionIndex)) {
					return Invalid(PersistenceError::InvalidValue);
				}
			}
			std::uint8_t hasTimedClear = 0;
			double timedClear = 0.0;
			if (!reader.read(actor.lastSettledGameHours) || !reader.read(hasTimedClear) || !reader.read(timedClear)) {
				return Invalid(PersistenceError::BadRecord);
			}
			if (!ValidRange(actor.lastSettledGameHours, kMaxGameHours)) {
				return Invalid(PersistenceError::InvalidValue);
			}
			if (hasTimedClear > 1 || !ValidRange(timedClear, kMaxActiveSeconds)) {
				return Invalid(PersistenceError::InvalidValue);
			}
			if (hasTimedClear == 1) {
				actor.timedClearRemainingSeconds = timedClear;
			}
			if (actor.formId == kInvalidFormId) {
				result.recordErrors.push_back(PersistenceError::RemapFailed);
				continue;
			}
			FormId resolved = 0;
			if (a_resolve) {
				if (!a_resolve(actor.formId, resolved) || resolved == kInvalidFormId) {
					result.recordErrors.push_back(PersistenceError::RemapFailed);
					continue;
				}
			} else {
				resolved = actor.formId;
			}
			actor.formId = static_cast<FormId>(resolved);
			if (!resolvedForms.insert(actor.formId).second) {
				result.recordErrors.push_back(PersistenceError::RemapFailed);
				continue;
			}
			result.actors.push_back(std::move(actor));
		}

		if (!reader.done()) {
			return Invalid(PersistenceError::BadRecord);
		}

		result.status = LoadStatus::Loaded;
		return result;
	}

	SaveSnapshot makeSnapshot(const CoreState& a_state, const GameTime& a_now)
	{
		SaveSnapshot snapshot{};
		snapshot.schemaVersion = kSchemaVersion;
		snapshot.capturedAt = a_now;
		snapshot.actors.reserve(std::min<std::size_t>(a_state.actors.size(), kMaxActors));

		for (const auto& [actorId, state] : a_state.actors) {
			(void)actorId;
			if (state.formId == kInvalidFormId || snapshot.actors.size() >= kMaxActors) {
				continue;
			}

			PersistedActor persisted{};
			persisted.formId = state.formId;
			persisted.lastSettledGameHours = ValidRange(state.lastSettledGameHours, kMaxGameHours) ?
				state.lastSettledGameHours :
				a_now.calendarHours;
			bool hasLayers = false;
			for (std::size_t index = 0; index < kRegionCount; ++index) {
				const auto& source = state.regions[index];
				auto& target = persisted.regions[index];
				target.revision = source.revision;
				target.decayRemainderGameHours = ValidRange(source.decayRemainderGameHours, kMaxGameHours) ?
					source.decayRemainderGameHours :
					0.0;
				target.layers.reserve(std::min<std::size_t>(source.layers.size(), kMaxLayers));
				std::unordered_set<std::uint64_t> ids;
				for (const auto& layer : source.layers) {
					if (target.layers.size() >= kMaxLayers) {
						break;
					}
					if (ValidLayer(layer, index) && ids.insert(layer.id).second) {
						target.layers.push_back(layer);
						hasLayers = true;
					}
				}
				if (target.layers.empty()) {
					target.decayRemainderGameHours = 0.0;
				}
			}
			if (!hasLayers && !state.timedClearAt) {
				continue;
			}
			if (state.timedClearAt) {
				const auto remaining = *state.timedClearAt - a_now.activeSeconds;
				if (std::isfinite(remaining)) {
					persisted.timedClearRemainingSeconds = std::clamp(remaining, 0.0, kMaxActiveSeconds);
				} else {
					persisted.timedClearRemainingSeconds = 0.0;
				}
			}
			snapshot.actors.push_back(std::move(persisted));
		}
		std::sort(snapshot.actors.begin(), snapshot.actors.end(), [](const PersistedActor& a_left, const PersistedActor& a_right) {
			return a_left.formId < a_right.formId;
		});
		return snapshot;
	}
}
