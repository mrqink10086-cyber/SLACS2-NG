#include "persistence/SerializationCodec.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void Expect(bool a_condition, const char* a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "FAIL line " << a_line << ": " << a_expression << '\n';
			++g_failures;
		}
	}

	slacs::LayerRecord Layer(std::uint64_t a_id, std::uint16_t a_texture, slacs::TextureVariant a_variant)
	{
		return slacs::LayerRecord{ a_id, a_texture, a_variant };
	}

	slacs::PersistedActor Actor(slacs::FormId a_formId, slacs::GameHours a_lastSettled)
	{
		slacs::PersistedActor actor{};
		actor.formId = a_formId;
		actor.lastSettledGameHours = a_lastSettled;
		return actor;
	}

	slacs::SaveSnapshot BasicSnapshot()
	{
		slacs::SaveSnapshot snapshot{};
		snapshot.schemaVersion = 1;
		snapshot.epoch = 7;
		snapshot.capturedAt = slacs::GameTime{ 123.5, 456.25 };

		auto first = Actor(0x100, 120.0);
		first.regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers = {
			Layer(5, 9, slacs::TextureVariant::Normal),
			Layer(6, 10, slacs::TextureVariant::Mega),
		};
		first.regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].decayRemainderGameHours = 0.25;
		first.regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].revision = 3;
		first.regions[slacs::RegionIndex(slacs::Region::BodyAnal)].layers = {
			Layer(7, 1, slacs::TextureVariant::Normal),
		};
		first.regions[slacs::RegionIndex(slacs::Region::BodyAnal)].decayRemainderGameHours = 0.5;
		first.regions[slacs::RegionIndex(slacs::Region::BodyAnal)].revision = 4;
		first.timedClearRemainingSeconds = 42.5;
		snapshot.actors.push_back(first);

		auto second = Actor(0x200, 121.0);
		second.regions[slacs::RegionIndex(slacs::Region::Face)].layers = {
			Layer(8, 11, slacs::TextureVariant::Mega),
		};
		second.regions[slacs::RegionIndex(slacs::Region::Face)].revision = 9;
		snapshot.actors.push_back(second);
		return snapshot;
	}

	std::vector<std::uint8_t> EncodeOrFail(const slacs::SaveSnapshot& a_snapshot)
	{
		std::vector<std::uint8_t> data;
		const auto error = slacs::serialization_codec::encode(a_snapshot, data);
		Expect(error == slacs::PersistenceError::None, "snapshot encodes", __LINE__);
		return data;
	}

	void WriteAt(std::vector<std::uint8_t>& a_data, std::size_t a_offset, const void* a_value, std::size_t a_size)
	{
		Expect(a_offset + a_size <= a_data.size(), "mutation inside record", __LINE__);
		if (a_offset + a_size <= a_data.size()) {
			std::memcpy(a_data.data() + a_offset, a_value, a_size);
		}
	}

	void CheckRoundTripAndRemap()
	{
		const auto snapshot = BasicSnapshot();
		const auto data = EncodeOrFail(snapshot);
		const auto decoded = slacs::serialization_codec::decode(
			data.data(),
			data.size(),
			[](slacs::FormId a_old, slacs::FormId& a_new) {
				a_new = a_old + 0x1000;
				return true;
			});
		Expect(decoded.status == slacs::LoadStatus::Loaded, "roundtrip loads", __LINE__);
		Expect(decoded.schemaVersion == 1, "roundtrip schema", __LINE__);
		Expect(decoded.actors.size() == 2, "roundtrip actor count", __LINE__);
		Expect(decoded.recordErrors.empty(), "roundtrip no record errors", __LINE__);
		if (decoded.actors.size() == 2) {
			const auto& first = decoded.actors[0];
			Expect(first.formId == 0x1100, "first FormID remapped", __LINE__);
			Expect(first.lastSettledGameHours == 120.0, "last settlement restored", __LINE__);
			Expect(first.timedClearRemainingSeconds && *first.timedClearRemainingSeconds == 42.5, "timer restored", __LINE__);
			const auto& layers = first.regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers;
			Expect(layers.size() == 2, "layer count restored", __LINE__);
			if (layers.size() == 2) {
				Expect(layers[0].id == 5 && layers[0].textureIndex == 9 &&
						 layers[0].variant == slacs::TextureVariant::Normal,
					"first layer restored in order", __LINE__);
				Expect(layers[1].id == 6 && layers[1].textureIndex == 10 &&
						 layers[1].variant == slacs::TextureVariant::Mega,
					"second layer restored in order", __LINE__);
			}
			Expect(std::abs(first.regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].decayRemainderGameHours - 0.25) < 1e-12,
				"decay remainder restored", __LINE__);
			Expect(decoded.actors[1].formId == 0x1200, "second FormID remapped", __LINE__);
			Expect(!decoded.actors[1].timedClearRemainingSeconds, "absent timer restored as absent", __LINE__);
		}

		const auto failed = slacs::serialization_codec::decode(
			data.data(),
			data.size(),
			[](slacs::FormId a_old, slacs::FormId& a_new) {
				if (a_old == 0x100) {
					return false;
				}
				a_new = a_old;
				return true;
			});
		Expect(failed.status == slacs::LoadStatus::Loaded, "partial remap still loads", __LINE__);
		Expect(failed.actors.size() == 1, "failed remap drops actor", __LINE__);
		Expect(failed.recordErrors.size() == 1 &&
				 failed.recordErrors[0] == slacs::PersistenceError::RemapFailed,
			"failed remap is recorded", __LINE__);

		const auto duplicate = slacs::serialization_codec::decode(
			data.data(),
			data.size(),
			[](slacs::FormId, slacs::FormId& a_new) {
				a_new = 0x777;
				return true;
			});
		Expect(duplicate.status == slacs::LoadStatus::Loaded, "duplicate remap still loads", __LINE__);
		Expect(duplicate.actors.size() == 1, "duplicate remap drops later actor", __LINE__);
		Expect(duplicate.recordErrors.size() == 1, "duplicate remap is recorded", __LINE__);
	}

	void CheckZeroFormAndInvalidDecode()
	{
		auto snapshot = BasicSnapshot();
		auto data = EncodeOrFail(snapshot);

		// Header = schema (4) + calendar (8) + active (8) + actor count (4).
		constexpr std::size_t kFirstFormId = 24;
		const slacs::FormId zero = 0;
		WriteAt(data, kFirstFormId, &zero, sizeof(zero));
		const auto zeroForm = slacs::serialization_codec::decode(
			data.data(),
			data.size(),
			[](slacs::FormId a_old, slacs::FormId& a_new) {
				a_new = a_old;
				return true;
			});
		Expect(zeroForm.status == slacs::LoadStatus::Loaded, "zero form record skips", __LINE__);
		Expect(zeroForm.actors.size() == 1, "zero form actor dropped", __LINE__);
		Expect(zeroForm.recordErrors.size() == 1, "zero form error recorded", __LINE__);

		auto truncated = EncodeOrFail(snapshot);
		truncated.pop_back();
		const auto badLength = slacs::serialization_codec::decode(truncated.data(), truncated.size(), {});
		Expect(badLength.status == slacs::LoadStatus::InvalidData, "truncated record rejected", __LINE__);

		auto badLayerCount = EncodeOrFail(snapshot);
		// First actor FormID ends at 28; BodyVaginal layer count starts there.
		const std::uint32_t tooManyLayers = 6;
		WriteAt(badLayerCount, 28, &tooManyLayers, sizeof(tooManyLayers));
		const auto badCount = slacs::serialization_codec::decode(badLayerCount.data(), badLayerCount.size(), {});
		Expect(badCount.status == slacs::LoadStatus::InvalidData, "oversized layer count rejected", __LINE__);

		auto duplicateLayer = EncodeOrFail(snapshot);
		const std::uint64_t duplicateLayerId = 5;
		constexpr auto firstLayerOffset = kFirstFormId + sizeof(slacs::FormId) + sizeof(std::uint32_t);
		constexpr auto encodedLayerSize = sizeof(std::uint64_t) + sizeof(std::uint16_t) + sizeof(slacs::TextureVariant);
		WriteAt(duplicateLayer, firstLayerOffset + encodedLayerSize, &duplicateLayerId, sizeof(duplicateLayerId));
		const auto duplicateLayerResult = slacs::serialization_codec::decode(
			duplicateLayer.data(), duplicateLayer.size(), {});
		Expect(duplicateLayerResult.status == slacs::LoadStatus::InvalidData, "duplicate layer ID rejected on decode", __LINE__);

		auto unsupported = EncodeOrFail(snapshot);
		const std::uint32_t schemaTwo = 2;
		WriteAt(unsupported, 0, &schemaTwo, sizeof(schemaTwo));
		const auto badSchema = slacs::serialization_codec::decode(unsupported.data(), unsupported.size(), {});
		Expect(badSchema.status == slacs::LoadStatus::UnsupportedSchema, "payload schema rejected", __LINE__);

		auto badVariant = EncodeOrFail(snapshot);
		const std::uint8_t variant = 0x7F;
		// First region: count at 28, layer id 32..39, texture 40..41, variant 42.
		WriteAt(badVariant, 42, &variant, sizeof(variant));
		const auto badVariantResult = slacs::serialization_codec::decode(badVariant.data(), badVariant.size(), {});
		Expect(badVariantResult.status == slacs::LoadStatus::InvalidData, "invalid texture variant rejected", __LINE__);

		auto badRemainder = EncodeOrFail(snapshot);
		const double nan = std::numeric_limits<double>::quiet_NaN();
		// Two layers end at 54; the first region remainder occupies 54..61.
		WriteAt(badRemainder, 54, &nan, sizeof(nan));
		const auto badRemainderResult = slacs::serialization_codec::decode(badRemainder.data(), badRemainder.size(), {});
		Expect(badRemainderResult.status == slacs::LoadStatus::InvalidData, "NaN remainder rejected", __LINE__);

		slacs::SaveSnapshot emptyBody{};
		emptyBody.capturedAt = slacs::GameTime{ 1.0, 2.0 };
		emptyBody.actors.push_back(Actor(0x300, 3.0));
		auto badTimer = EncodeOrFail(emptyBody);
		// Header 24 + FormID 4 + seven empty regions (20 bytes each) + last settled 8.
		constexpr std::size_t kTimerFlag = 24 + 4 + 7 * 20 + 8;
		const std::uint8_t invalidFlag = 2;
		WriteAt(badTimer, kTimerFlag, &invalidFlag, sizeof(invalidFlag));
		const auto badTimerResult = slacs::serialization_codec::decode(badTimer.data(), badTimer.size(), {});
		Expect(badTimerResult.status == slacs::LoadStatus::InvalidData, "invalid timer flag rejected", __LINE__);
	}

	void CheckEncodeValidation()
	{
		auto duplicate = BasicSnapshot();
		duplicate.actors[1].formId = duplicate.actors[0].formId;
		std::vector<std::uint8_t> data;
		Expect(slacs::serialization_codec::encode(duplicate, data) == slacs::PersistenceError::InvalidValue,
			"duplicate FormID rejected on encode", __LINE__);

		auto badSchema = BasicSnapshot();
		badSchema.schemaVersion = 2;
		Expect(slacs::serialization_codec::encode(badSchema, data) == slacs::PersistenceError::UnsupportedSchema,
			"unsupported schema rejected on encode", __LINE__);

		auto badTime = BasicSnapshot();
		badTime.capturedAt.calendarHours = std::numeric_limits<double>::quiet_NaN();
		Expect(slacs::serialization_codec::encode(badTime, data) == slacs::PersistenceError::InvalidValue,
			"NaN captured time rejected on encode", __LINE__);

		auto badTexture = BasicSnapshot();
		badTexture.actors[0].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers[0].textureIndex = 0;
		Expect(slacs::serialization_codec::encode(badTexture, data) == slacs::PersistenceError::InvalidValue,
			"invalid texture index rejected on encode", __LINE__);

		auto duplicateLayer = BasicSnapshot();
		duplicateLayer.actors[0].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers[1].id =
			duplicateLayer.actors[0].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers[0].id;
		Expect(slacs::serialization_codec::encode(duplicateLayer, data) == slacs::PersistenceError::InvalidValue,
			"duplicate layer ID rejected on encode", __LINE__);

		auto badVariant = BasicSnapshot();
		badVariant.actors[0].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers[0].variant =
			static_cast<slacs::TextureVariant>(9);
		Expect(slacs::serialization_codec::encode(badVariant, data) == slacs::PersistenceError::InvalidValue,
			"invalid texture variant rejected on encode", __LINE__);

		auto badTimer = BasicSnapshot();
		badTimer.actors[0].timedClearRemainingSeconds = -1.0;
		Expect(slacs::serialization_codec::encode(badTimer, data) == slacs::PersistenceError::InvalidValue,
			"negative timer rejected on encode", __LINE__);

		auto badSettlement = BasicSnapshot();
		badSettlement.actors[0].lastSettledGameHours = std::numeric_limits<double>::infinity();
		Expect(slacs::serialization_codec::encode(badSettlement, data) == slacs::PersistenceError::InvalidValue,
			"infinite settlement rejected on encode", __LINE__);
	}

	void CheckMakeSnapshot()
	{
		slacs::CoreState state{};
		slacs::ActorState invalid{};
		invalid.actor = 1;
		invalid.formId = 0;
		state.actors.emplace(1, invalid);

		slacs::ActorState empty{};
		empty.actor = 2;
		empty.formId = 0x100;
		empty.lastSettledGameHours = 10.0;
		state.actors.emplace(2, empty);

		slacs::ActorState timerOnly{};
		timerOnly.actor = 3;
		timerOnly.formId = 0x200;
		timerOnly.lastSettledGameHours = 11.0;
		timerOnly.timedClearAt = 150.0;
		state.actors.emplace(3, timerOnly);

		slacs::ActorState hugeTimer{};
		hugeTimer.actor = 5;
		hugeTimer.formId = 0x400;
		hugeTimer.lastSettledGameHours = 12.0;
		hugeTimer.timedClearAt = 2.0e9;
		state.actors.emplace(5, hugeTimer);

		slacs::ActorState layered{};
		layered.actor = 4;
		layered.formId = 0x300;
		layered.lastSettledGameHours = 12.0;
		auto& region = layered.regions[slacs::RegionIndex(slacs::Region::BodyVaginal)];
		region.decayRemainderGameHours = -5.0;
		region.revision = 12;
		for (std::uint64_t i = 0; i < 7; ++i) {
			region.layers.push_back(Layer(100 + i, static_cast<std::uint16_t>(1 + i), slacs::TextureVariant::Normal));
		}
		state.actors.emplace(4, layered);

		const auto snapshot = slacs::serialization_codec::makeSnapshot(state, slacs::GameTime{ 20.0, 30.0 });
		Expect(snapshot.schemaVersion == 1, "snapshot schema", __LINE__);
		Expect(snapshot.capturedAt.calendarHours == 20.0 && snapshot.capturedAt.activeSeconds == 30.0,
			"snapshot captures time", __LINE__);
		Expect(snapshot.actors.size() == 3, "snapshot filters empty and invalid actors", __LINE__);
		if (snapshot.actors.size() == 3) {
			Expect(snapshot.actors[0].formId == 0x200, "timer-only actor retained", __LINE__);
			Expect(snapshot.actors[0].timedClearRemainingSeconds &&
					 *snapshot.actors[0].timedClearRemainingSeconds == 120.0,
				"timer converted to remaining seconds", __LINE__);
			Expect(snapshot.actors[1].formId == 0x300, "layered actor retained", __LINE__);
			Expect(snapshot.actors[1].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers.size() == 5,
				"snapshot caps layers", __LINE__);
			Expect(snapshot.actors[1].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].decayRemainderGameHours == 0.0,
				"snapshot sanitizes remainder", __LINE__);
			Expect(snapshot.actors[1].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].revision == 12,
				"snapshot keeps region revision", __LINE__);
			Expect(snapshot.actors[2].formId == 0x400, "actors sorted by FormID", __LINE__);
			Expect(snapshot.actors[2].timedClearRemainingSeconds &&
					 *snapshot.actors[2].timedClearRemainingSeconds == 1.0e9,
				"huge timer clamped on snapshot", __LINE__);
		}
	}

	void CheckSnapshotLayerFilteringAndOrder()
	{
		slacs::CoreState state{};
		slacs::ActorState actor{};
		actor.actor = 1;
		actor.formId = 0x500;
		actor.lastSettledGameHours = 1.0;
		auto& layers = actor.regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers;
		layers = {
			Layer(0, 1, slacs::TextureVariant::Normal),
			Layer(10, 1, slacs::TextureVariant::Normal),
			Layer(10, 2, slacs::TextureVariant::Mega),
			Layer(11, 0, slacs::TextureVariant::Normal),
			Layer(11, 3, slacs::TextureVariant::Mega),
			Layer(12, 4, slacs::TextureVariant::Normal),
			Layer(13, 5, slacs::TextureVariant::Normal),
			Layer(14, 6, slacs::TextureVariant::Normal),
			Layer(15, 7, slacs::TextureVariant::Mega),
		};
		state.actors.emplace(actor.actor, actor);

		const auto snapshot = slacs::serialization_codec::makeSnapshot(state, { 2.0, 3.0 });
		Expect(snapshot.actors.size() == 1, "snapshot retains actor with filtered layers", __LINE__);
		if (snapshot.actors.size() == 1) {
			const auto& result = snapshot.actors[0].regions[slacs::RegionIndex(slacs::Region::BodyVaginal)].layers;
			Expect(result.size() == 5, "snapshot fills layer cap from valid unique layers", __LINE__);
			if (result.size() == 5) {
				Expect(result[0].id == 10 && result[0].textureIndex == 1 && result[0].variant == slacs::TextureVariant::Normal,
					"snapshot keeps first duplicate and its order", __LINE__);
				Expect(result[1].id == 11 && result[1].textureIndex == 3 && result[1].variant == slacs::TextureVariant::Mega,
					"snapshot skips invalid layer before valid occurrence", __LINE__);
				Expect(result[2].id == 12 && result[3].id == 13 && result[4].id == 14,
					"snapshot preserves accepted layer order and truncates at five", __LINE__);
			}
		}
	}
}

int main()
{
	CheckRoundTripAndRemap();
	CheckZeroFormAndInvalidDecode();
	CheckEncodeValidation();
	CheckMakeSnapshot();
	CheckSnapshotLayerFilteringAndOrder();

	if (g_failures != 0) {
		std::cerr << g_failures << " S5 persistence check(s) failed\n";
		return 1;
	}
	std::cout << "SLACS2 NG S5 persistence checks passed\n";
	return 0;
}
