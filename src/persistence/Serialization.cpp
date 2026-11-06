#include "persistence/Serialization.h"

#include "persistence/SerializationCodec.h"

#include <SKSE/SKSE.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace slacs
{
	namespace
	{
		constexpr std::uint32_t kRecordType = 'SLNG';
		constexpr std::uint32_t kSchemaVersion = 1;
		constexpr std::size_t kMaxRecordBytes = 64 * 1024 * 1024;
	}

	SaveSnapshot Serialization::makeSnapshot(const CoreState& a_state, const GameTime& a_now)
	{
		return serialization_codec::makeSnapshot(a_state, a_now);
	}

	PersistenceStatus Serialization::save(SKSE::SerializationInterface& a_skse, const SaveSnapshot& a_snapshot)
	{
		PersistenceStatus status{};
		std::vector<std::uint8_t> data;
		const auto error = serialization_codec::encode(a_snapshot, data);
		if (error != PersistenceError::None) {
			status.error = error;
			return status;
		}
		if (data.size() > kMaxRecordBytes || data.size() > std::numeric_limits<std::uint32_t>::max()) {
			status.error = PersistenceError::InvalidValue;
			return status;
		}
		if (!a_skse.WriteRecord(kRecordType, kSchemaVersion, data.data(), static_cast<std::uint32_t>(data.size()))) {
			status.error = PersistenceError::Io;
			return status;
		}

		status.success = true;
		status.recordsWritten = 1;
		return status;
	}

	LoadResult Serialization::load(SKSE::SerializationInterface& a_skse)
	{
		LoadResult result{};
		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;

		bool found = false;
		for (std::size_t record = 0; record < 64; ++record) {
			if (!a_skse.GetNextRecordInfo(type, version, length)) {
				break;
			}
			if (type == kRecordType) {
				found = true;
				break;
			}
		}
		if (!found) {
			result.status = LoadStatus::Empty;
			return result;
		}
		if (version != kSchemaVersion) {
			result.status = LoadStatus::UnsupportedSchema;
			result.schemaVersion = version;
			return result;
		}
		if (length > kMaxRecordBytes) {
			result.status = LoadStatus::InvalidData;
			result.recordErrors.push_back(PersistenceError::BadRecord);
			return result;
		}

		std::vector<std::uint8_t> data(length);
		if (a_skse.ReadRecordData(data.data(), length) != length) {
			result.status = LoadStatus::IoError;
			result.recordErrors.push_back(PersistenceError::Io);
			return result;
		}

		return serialization_codec::decode(
			data.data(),
			data.size(),
			[&a_skse](FormId a_old, FormId& a_new) {
				RE::FormID resolved = 0;
				if (!a_skse.ResolveFormID(static_cast<RE::FormID>(a_old), resolved)) {
					return false;
				}
				a_new = static_cast<FormId>(resolved);
				return a_new != kInvalidFormId;
			});
	}
}
