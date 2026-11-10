#pragma once

#include "core/Types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace slacs
{
	enum class LayerRemovalOrder : std::uint8_t
	{
		LegacyLifo,
	};

	enum class SwimCleaningPolicy : std::uint8_t
	{
		LegacyImmediateClear,
	};

	enum class TimedClearMode : std::uint8_t
	{
		Disabled,
		Legacy120ActiveSeconds,
	};

	enum class SourceClass : std::uint8_t
	{
		Male,
		Female,
		Futa,
		CreatureMale,
		CreatureFemale,
		Unknown,
	};

	enum class OverlayDomain : std::uint8_t
	{
		Body,
		Face,
		Hands,
		Feet,
	};

	enum class SettingsIssueCode : std::uint8_t
	{
		Parse,
		UnknownField,
		InvalidEnum,
		OutOfRange,
		DuplicateForm,
	};

	enum class SettingsLoadStatus : std::uint8_t
	{
		Loaded,
		MissingUsingDefaults,
		Invalid,
		IoError,
	};

	enum class SettingsSaveStatus : std::uint8_t
	{
		Saved,
		Invalid,
		IoError,
	};

	struct SettingsIssue;
	struct SettingsValidationResult;
	struct SettingsLoadResult;
	struct SettingsSaveResult;

	struct Settings
	{
		bool naturalDecay{ true };
		bool rainAccelerates{ true };
		double decayIntervalGameHours{ 1.0 };
		std::uint8_t maxLayersPerRegion{ 5 };
		LayerRemovalOrder layerRemovalOrder{ LayerRemovalOrder::LegacyLifo };
		SwimCleaningPolicy swimCleaning{ SwimCleaningPolicy::LegacyImmediateClear };
		TimedClearMode timedClear{ TimedClearMode::Legacy120ActiveSeconds };
		double timedClearSeconds{ 120.0 };
		float overlayOpacity{ 1.0F };
		bool overlaysEnabled{ true };
		bool effectsEnabled{ true };
		std::uint8_t spellLevel1Threshold{ 1 };
		std::uint8_t spellLevel2Threshold{ 3 };
		bool allowFemaleFluid{ true };
		bool femaleTargetsRequirePlayerOrFollower{ true };
		std::array<bool, 6> enabledSourceClasses{ true, true, true, true, false, false };
		bool megaCumForAll{ false };

		bool operator==(const Settings&) const = default;

		static SettingsValidationResult validate(const Settings& a_value);
		static SettingsLoadResult load(const std::filesystem::path& a_path);
		static SettingsSaveResult save(const std::filesystem::path& a_path, const Settings& a_value);
	};

	struct SettingsIssue
	{
		std::string key;
		SettingsIssueCode code{};
	};

	struct SettingsValidationResult
	{
		bool valid{};
		std::vector<SettingsIssue> issues;
	};

	struct SettingsLoadResult
	{
		SettingsLoadStatus status{};
		Settings value;
		std::vector<SettingsIssue> issues;
	};

	struct SettingsSaveResult
	{
		SettingsSaveStatus status{};
		std::vector<SettingsIssue> issues;
	};
}
