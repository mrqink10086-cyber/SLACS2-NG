#include "config/Settings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>

namespace slacs
{
	namespace
	{
		enum class JsonKind
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object,
		};

		struct JsonValue
		{
			JsonKind kind{ JsonKind::Null };
			bool boolean{};
			double number{};
			std::string string;
			std::vector<JsonValue> array;
			std::map<std::string, JsonValue, std::less<>> object;
		};

		class JsonParser
		{
		public:
			explicit JsonParser(std::string_view a_text) :
				text_(a_text)
			{}

			bool parse(JsonValue& a_out, std::string& a_error)
			{
				skipSpace();
				if (!parseValue(a_out, 0)) {
					a_error = error_;
					return false;
				}
				skipSpace();
				if (pos_ != text_.size()) {
					a_error = "trailing data";
					return false;
				}
				return true;
			}

		private:
			void skipSpace()
			{
				while (pos_ < text_.size()) {
					const auto c = text_[pos_];
					if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
						break;
					}
					++pos_;
				}
			}

			bool fail(std::string_view a_message)
			{
				if (error_.empty()) {
					error_ = std::string(a_message) + " at offset " + std::to_string(pos_);
				}
				return false;
			}

			bool consume(char a_expected)
			{
				skipSpace();
				if (pos_ >= text_.size() || text_[pos_] != a_expected) {
					return fail(std::string("expected '") + a_expected + "'");
				}
				++pos_;
				return true;
			}

			bool parseValue(JsonValue& a_out, int a_depth)
			{
				if (a_depth > 32) {
					return fail("nesting too deep");
				}
				skipSpace();
				if (pos_ >= text_.size()) {
					return fail("unexpected end");
				}
				switch (text_[pos_]) {
				case 'n':
					return parseLiteral("null", a_out, JsonKind::Null);
				case 't':
					return parseLiteral("true", a_out, JsonKind::Bool, true);
				case 'f':
					return parseLiteral("false", a_out, JsonKind::Bool, false);
				case '"':
					a_out.kind = JsonKind::String;
					return parseString(a_out.string);
				case '[':
					return parseArray(a_out, a_depth + 1);
				case '{':
					return parseObject(a_out, a_depth + 1);
				default:
					if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
						return parseNumber(a_out);
					}
					return fail("invalid value");
				}
			}

			bool parseLiteral(std::string_view a_literal, JsonValue& a_out, JsonKind a_kind, bool a_bool = false)
			{
				if (text_.substr(pos_, a_literal.size()) != a_literal) {
					return fail("invalid literal");
				}
				pos_ += a_literal.size();
				a_out.kind = a_kind;
				a_out.boolean = a_bool;
				return true;
			}

			static int hexDigit(char a_char)
			{
				if (a_char >= '0' && a_char <= '9') {
					return a_char - '0';
				}
				if (a_char >= 'a' && a_char <= 'f') {
					return a_char - 'a' + 10;
				}
				if (a_char >= 'A' && a_char <= 'F') {
					return a_char - 'A' + 10;
				}
				return -1;
			}

			bool parseString(std::string& a_out)
			{
				if (!consume('"')) {
					return false;
				}
				a_out.clear();
				while (pos_ < text_.size()) {
					const char c = text_[pos_++];
					if (c == '"') {
						return true;
					}
					if (static_cast<unsigned char>(c) < 0x20) {
						return fail("control character in string");
					}
					if (c != '\\') {
						a_out.push_back(c);
						continue;
					}
					if (pos_ >= text_.size()) {
						return fail("unfinished escape");
					}
					switch (text_[pos_++]) {
					case '"':
						a_out.push_back('"');
						break;
					case '\\':
						a_out.push_back('\\');
						break;
					case '/':
						a_out.push_back('/');
						break;
					case 'b':
						a_out.push_back('\b');
						break;
					case 'f':
						a_out.push_back('\f');
						break;
					case 'n':
						a_out.push_back('\n');
						break;
					case 'r':
						a_out.push_back('\r');
						break;
					case 't':
						a_out.push_back('\t');
						break;
					case 'u': {
						if (pos_ + 4 > text_.size()) {
							return fail("truncated unicode escape");
						}
						std::uint32_t code = 0;
						for (int i = 0; i < 4; ++i) {
							const int digit = hexDigit(text_[pos_++]);
							if (digit < 0) {
								return fail("invalid unicode escape");
							}
							code = (code << 4) | static_cast<std::uint32_t>(digit);
						}
						if (code <= 0x7F) {
							a_out.push_back(static_cast<char>(code));
						} else if (code <= 0x7FF) {
							a_out.push_back(static_cast<char>(0xC0 | (code >> 6)));
							a_out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
						} else {
							a_out.push_back(static_cast<char>(0xE0 | (code >> 12)));
							a_out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
							a_out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
						}
						break;
					}
					default:
						return fail("invalid escape");
					}
				}
				return fail("unterminated string");
			}

			bool parseNumber(JsonValue& a_out)
			{
				const std::size_t begin = pos_;
				if (text_[pos_] == '-') {
					++pos_;
				}
				while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
					++pos_;
				}
				if (pos_ < text_.size() && text_[pos_] == '.') {
					++pos_;
					while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
						++pos_;
					}
				}
				if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
					++pos_;
					if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
						++pos_;
					}
					while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
						++pos_;
					}
				}
				if (begin == pos_) {
					return fail("invalid number");
				}
				double value = 0.0;
				const auto result = std::from_chars(text_.data() + begin, text_.data() + pos_, value);
				if (result.ec != std::errc{} || !std::isfinite(value)) {
					return fail("invalid number");
				}
				a_out.kind = JsonKind::Number;
				a_out.number = value;
				return true;
			}

			bool parseArray(JsonValue& a_out, int a_depth)
			{
				if (!consume('[')) {
					return false;
				}
				a_out.kind = JsonKind::Array;
				skipSpace();
				if (pos_ < text_.size() && text_[pos_] == ']') {
					++pos_;
					return true;
				}
				for (;;) {
					JsonValue item;
					if (!parseValue(item, a_depth)) {
						return false;
					}
					a_out.array.push_back(std::move(item));
					skipSpace();
					if (pos_ >= text_.size()) {
						return fail("unterminated array");
					}
					if (text_[pos_] == ']') {
						++pos_;
						return true;
					}
					if (text_[pos_] != ',') {
						return fail("expected ',' or ']'");
					}
					++pos_;
				}
			}

			bool parseObject(JsonValue& a_out, int a_depth)
			{
				if (!consume('{')) {
					return false;
				}
				a_out.kind = JsonKind::Object;
				skipSpace();
				if (pos_ < text_.size() && text_[pos_] == '}') {
					++pos_;
					return true;
				}
				for (;;) {
					skipSpace();
					if (pos_ >= text_.size() || text_[pos_] != '"') {
						return fail("expected object key");
					}
					std::string key;
					if (!parseString(key)) {
						return false;
					}
					if (!consume(':')) {
						return false;
					}
					JsonValue value;
					if (!parseValue(value, a_depth)) {
						return false;
					}
					if (!a_out.object.emplace(std::move(key), std::move(value)).second) {
						return fail("duplicate key");
					}
					skipSpace();
					if (pos_ >= text_.size()) {
						return fail("unterminated object");
					}
					if (text_[pos_] == '}') {
						++pos_;
						return true;
					}
					if (text_[pos_] != ',') {
						return fail("expected ',' or '}'");
					}
					++pos_;
				}
			}

			std::string_view text_;
			std::size_t pos_{};
			std::string error_;
		};

		void AddIssue(std::vector<SettingsIssue>& a_issues, std::string a_key, SettingsIssueCode a_code)
		{
			a_issues.push_back({ std::move(a_key), a_code });
		}

		const JsonValue* Find(const JsonValue& a_object, std::string_view a_key)
		{
			const auto it = a_object.object.find(a_key);
			return it == a_object.object.end() ? nullptr : std::addressof(it->second);
		}

		bool ReadBool(const JsonValue& a_object, std::string_view a_key, bool& a_out, std::vector<SettingsIssue>& a_issues)
		{
			const auto* value = Find(a_object, a_key);
			if (!value) {
				return true;
			}
			if (value->kind != JsonKind::Bool) {
				AddIssue(a_issues, std::string(a_key), SettingsIssueCode::Parse);
				return false;
			}
			a_out = value->boolean;
			return true;
		}

		bool ReadNumber(const JsonValue& a_object, std::string_view a_key, double& a_out, std::vector<SettingsIssue>& a_issues)
		{
			const auto* value = Find(a_object, a_key);
			if (!value) {
				return true;
			}
			if (value->kind != JsonKind::Number || !std::isfinite(value->number)) {
				AddIssue(a_issues, std::string(a_key), SettingsIssueCode::Parse);
				return false;
			}
			a_out = value->number;
			return true;
		}

		bool ReadInteger(const JsonValue& a_object, std::string_view a_key, std::uint32_t& a_out, std::vector<SettingsIssue>& a_issues)
		{
			double number = 0.0;
			if (!ReadNumber(a_object, a_key, number, a_issues)) {
				return false;
			}
			if (!Find(a_object, a_key)) {
				return true;
			}
			if (std::floor(number) != number || number < 0.0 || number > 255.0) {
				AddIssue(a_issues, std::string(a_key), SettingsIssueCode::OutOfRange);
				return false;
			}
			a_out = static_cast<std::uint32_t>(number);
			return true;
		}

		bool ReadString(const JsonValue& a_object, std::string_view a_key, std::string& a_out, std::vector<SettingsIssue>& a_issues)
		{
			const auto* value = Find(a_object, a_key);
			if (!value) {
				return true;
			}
			if (value->kind != JsonKind::String) {
				AddIssue(a_issues, std::string(a_key), SettingsIssueCode::Parse);
				return false;
			}
			a_out = value->string;
			return true;
		}

		bool ReadBoolArray(const JsonValue& a_object, std::string_view a_key, bool* a_out, std::size_t a_count, std::vector<SettingsIssue>& a_issues)
		{
			const auto* value = Find(a_object, a_key);
			if (!value) {
				return true;
			}
			if (value->kind != JsonKind::Array || value->array.size() != a_count) {
				AddIssue(a_issues, std::string(a_key), SettingsIssueCode::Parse);
				return false;
			}
			for (std::size_t i = 0; i < a_count; ++i) {
				if (value->array[i].kind != JsonKind::Bool) {
					AddIssue(a_issues, std::string(a_key), SettingsIssueCode::Parse);
					return false;
				}
				a_out[i] = value->array[i].boolean;
			}
			return true;
		}

		bool KnownKey(std::string_view a_key)
		{
			return a_key == "schemaVersion" ||
			       a_key == "naturalDecay" ||
			       a_key == "rainAccelerates" ||
			       a_key == "decayIntervalGameHours" ||
			       a_key == "maxLayersPerRegion" ||
			       a_key == "firstOwnedSlot" ||
			       a_key == "layerRemovalOrder" ||
			       a_key == "swimCleaning" ||
			       a_key == "timedClear" ||
			       a_key == "timedClearSeconds" ||
			       a_key == "overlayOpacity" ||
			       a_key == "overlaysEnabled" ||
			       a_key == "effectsEnabled" ||
			       a_key == "spellLevel1Threshold" ||
			       a_key == "spellLevel2Threshold" ||
			       a_key == "allowFemaleFluid" ||
			       a_key == "femaleTargetsRequirePlayerOrFollower" ||
			       a_key == "enabledSourceClasses" ||
			       a_key == "megaCumForAll";
		}

		bool ParseSettings(const JsonValue& a_root, Settings& a_out, std::vector<SettingsIssue>& a_issues)
		{
			if (a_root.kind != JsonKind::Object) {
				AddIssue(a_issues, "$", SettingsIssueCode::Parse);
				return false;
			}

			for (const auto& [key, unused] : a_root.object) {
				if (!KnownKey(key)) {
					AddIssue(a_issues, key, SettingsIssueCode::UnknownField);
				}
			}

			Settings value;
			double number = 0.0;
			std::uint32_t integer = 0;
			std::string text;

			const auto* schema = Find(a_root, "schemaVersion");
			if (!schema || schema->kind != JsonKind::Number ||
				schema->number != 1.0 || std::floor(schema->number) != schema->number) {
				AddIssue(a_issues, "schemaVersion", SettingsIssueCode::Parse);
			}

			ReadBool(a_root, "naturalDecay", value.naturalDecay, a_issues);
			ReadBool(a_root, "rainAccelerates", value.rainAccelerates, a_issues);

			number = value.decayIntervalGameHours;
			if (ReadNumber(a_root, "decayIntervalGameHours", number, a_issues)) {
				value.decayIntervalGameHours = number;
			}

			integer = value.maxLayersPerRegion;
			if (ReadInteger(a_root, "maxLayersPerRegion", integer, a_issues)) {
				value.maxLayersPerRegion = static_cast<std::uint8_t>(integer);
			}

			if (ReadString(a_root, "layerRemovalOrder", text, a_issues) && Find(a_root, "layerRemovalOrder")) {
				if (text == "LegacyLifo") {
					value.layerRemovalOrder = LayerRemovalOrder::LegacyLifo;
				} else {
					AddIssue(a_issues, "layerRemovalOrder", SettingsIssueCode::InvalidEnum);
				}
			}
			text.clear();

			if (ReadString(a_root, "swimCleaning", text, a_issues) && Find(a_root, "swimCleaning")) {
				if (text == "LegacyImmediateClear") {
					value.swimCleaning = SwimCleaningPolicy::LegacyImmediateClear;
				} else {
					AddIssue(a_issues, "swimCleaning", SettingsIssueCode::InvalidEnum);
				}
			}
			text.clear();

			if (ReadString(a_root, "timedClear", text, a_issues) && Find(a_root, "timedClear")) {
				if (text == "Disabled") {
					value.timedClear = TimedClearMode::Disabled;
				} else if (text == "Legacy120ActiveSeconds") {
					value.timedClear = TimedClearMode::Legacy120ActiveSeconds;
				} else {
					AddIssue(a_issues, "timedClear", SettingsIssueCode::InvalidEnum);
				}
			}

			number = value.timedClearSeconds;
			if (ReadNumber(a_root, "timedClearSeconds", number, a_issues)) {
				value.timedClearSeconds = number;
			}

			number = value.overlayOpacity;
			if (ReadNumber(a_root, "overlayOpacity", number, a_issues)) {
				value.overlayOpacity = static_cast<float>(number);
			}

			ReadBool(a_root, "overlaysEnabled", value.overlaysEnabled, a_issues);
			ReadBool(a_root, "effectsEnabled", value.effectsEnabled, a_issues);

			integer = value.spellLevel1Threshold;
			if (ReadInteger(a_root, "spellLevel1Threshold", integer, a_issues)) {
				value.spellLevel1Threshold = static_cast<std::uint8_t>(integer);
			}
			integer = value.spellLevel2Threshold;
			if (ReadInteger(a_root, "spellLevel2Threshold", integer, a_issues)) {
				value.spellLevel2Threshold = static_cast<std::uint8_t>(integer);
			}

			ReadBool(a_root, "allowFemaleFluid", value.allowFemaleFluid, a_issues);
			ReadBoolArray(a_root, "enabledSourceClasses", value.enabledSourceClasses.data(), value.enabledSourceClasses.size(), a_issues);
			ReadBool(a_root, "megaCumForAll", value.megaCumForAll, a_issues);

			if (a_issues.empty()) {
				a_out = value;
				return true;
			}
			return false;
		}

		std::string Escape(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size() + 2);
			for (const char c : a_text) {
				switch (c) {
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					out.push_back(c);
					break;
				}
			}
			return out;
		}
	}

	SettingsValidationResult Settings::validate(const Settings& a_value)
	{
		SettingsValidationResult result{};
		const auto add = [&result](std::string key, SettingsIssueCode code) {
			result.issues.push_back({ std::move(key), code });
		};

		if (!std::isfinite(a_value.decayIntervalGameHours) || a_value.decayIntervalGameHours <= 0.0) {
			add("decayIntervalGameHours", SettingsIssueCode::OutOfRange);
		}
		if (a_value.maxLayersPerRegion != 5) {
			add("maxLayersPerRegion", SettingsIssueCode::OutOfRange);
		}
		if (!std::isfinite(a_value.timedClearSeconds) || a_value.timedClearSeconds <= 0.0) {
			add("timedClearSeconds", SettingsIssueCode::OutOfRange);
		}
		if (!std::isfinite(a_value.overlayOpacity) || a_value.overlayOpacity < 0.0F || a_value.overlayOpacity > 1.0F) {
			add("overlayOpacity", SettingsIssueCode::OutOfRange);
		}
		if (a_value.spellLevel1Threshold < 1 || a_value.spellLevel1Threshold > a_value.maxLayersPerRegion) {
			add("spellLevel1Threshold", SettingsIssueCode::OutOfRange);
		}
		if (a_value.spellLevel2Threshold < 1 || a_value.spellLevel2Threshold > a_value.maxLayersPerRegion ||
			a_value.spellLevel1Threshold > a_value.spellLevel2Threshold) {
			add("spellLevel2Threshold", SettingsIssueCode::OutOfRange);
		}
		result.valid = result.issues.empty();
		return result;
	}

	SettingsLoadResult Settings::load(const std::filesystem::path& a_path)
	{
		SettingsLoadResult result{};
		std::error_code ec;
		if (!std::filesystem::exists(a_path, ec)) {
			if (ec) {
				result.status = SettingsLoadStatus::IoError;
			} else {
				result.status = SettingsLoadStatus::MissingUsingDefaults;
			}
			return result;
		}

		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			result.status = SettingsLoadStatus::IoError;
			return result;
		}
		std::ostringstream stream;
		stream << file.rdbuf();
		if (!file.good() && !file.eof()) {
			result.status = SettingsLoadStatus::IoError;
			return result;
		}

		const auto text = stream.str();
		std::size_t offset = 0;
		if (text.size() >= 3 &&
			static_cast<unsigned char>(text[0]) == 0xEF &&
			static_cast<unsigned char>(text[1]) == 0xBB &&
			static_cast<unsigned char>(text[2]) == 0xBF) {
			offset = 3;
		}
		JsonValue root;
		std::string error;
		JsonParser parser(std::string_view(text).substr(offset));
		if (!parser.parse(root, error)) {
			result.status = SettingsLoadStatus::Invalid;
			result.issues.push_back({ "$", SettingsIssueCode::Parse });
			return result;
		}

		Settings parsed;
		if (!ParseSettings(root, parsed, result.issues)) {
			result.status = SettingsLoadStatus::Invalid;
			return result;
		}

		auto validation = validate(parsed);
		if (!validation.valid) {
			result.status = SettingsLoadStatus::Invalid;
			result.issues = std::move(validation.issues);
			return result;
		}

		result.value = parsed;
		result.status = SettingsLoadStatus::Loaded;
		return result;
	}

	SettingsSaveResult Settings::save(const std::filesystem::path& a_path, const Settings& a_value)
	{
		SettingsSaveResult result{};
		auto validation = validate(a_value);
		if (!validation.valid) {
			result.status = SettingsSaveStatus::Invalid;
			result.issues = std::move(validation.issues);
			return result;
		}

		std::error_code ec;
		if (const auto parent = a_path.parent_path(); !parent.empty()) {
			std::filesystem::create_directories(parent, ec);
			if (ec) {
				result.status = SettingsSaveStatus::IoError;
				return result;
			}
		}

		auto temporary = a_path;
		temporary += ".tmp";
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file) {
			result.status = SettingsSaveStatus::IoError;
			return result;
		}
		file.imbue(std::locale::classic());

		file << "{\n";
		file << "  \"schemaVersion\": 1,\n";
		file << "  \"naturalDecay\": " << (a_value.naturalDecay ? "true" : "false") << ",\n";
		file << "  \"rainAccelerates\": " << (a_value.rainAccelerates ? "true" : "false") << ",\n";
		file << "  \"decayIntervalGameHours\": " << a_value.decayIntervalGameHours << ",\n";
		file << "  \"maxLayersPerRegion\": " << static_cast<std::uint32_t>(a_value.maxLayersPerRegion) << ",\n";
		file << "  \"layerRemovalOrder\": \"LegacyLifo\",\n";
		file << "  \"swimCleaning\": \"LegacyImmediateClear\",\n";
		file << "  \"timedClear\": \"" << (a_value.timedClear == TimedClearMode::Disabled ? "Disabled" : "Legacy120ActiveSeconds") << "\",\n";
		file << "  \"timedClearSeconds\": " << a_value.timedClearSeconds << ",\n";
		file << "  \"overlayOpacity\": " << a_value.overlayOpacity << ",\n";
		file << "  \"overlaysEnabled\": " << (a_value.overlaysEnabled ? "true" : "false") << ",\n";
		file << "  \"effectsEnabled\": " << (a_value.effectsEnabled ? "true" : "false") << ",\n";
		file << "  \"spellLevel1Threshold\": " << static_cast<std::uint32_t>(a_value.spellLevel1Threshold) << ",\n";
		file << "  \"spellLevel2Threshold\": " << static_cast<std::uint32_t>(a_value.spellLevel2Threshold) << ",\n";
		file << "  \"allowFemaleFluid\": " << (a_value.allowFemaleFluid ? "true" : "false") << ",\n";
		file << "  \"enabledSourceClasses\": [";
		for (std::size_t i = 0; i < a_value.enabledSourceClasses.size(); ++i) {
			if (i != 0) {
				file << ", ";
			}
			file << (a_value.enabledSourceClasses[i] ? "true" : "false");
		}
		file << "],\n";
		file << "  \"megaCumForAll\": " << (a_value.megaCumForAll ? "true" : "false") << "\n";
		file << "}\n";

		file.flush();
		if (!file.good()) {
			file.close();
			std::error_code removeEc;
			std::filesystem::remove(temporary, removeEc);
			result.status = SettingsSaveStatus::IoError;
			return result;
		}
		file.close();
		if (file.fail()) {
			std::error_code removeEc;
			std::filesystem::remove(temporary, removeEc);
			result.status = SettingsSaveStatus::IoError;
			return result;
		}

		ec.clear();
		std::filesystem::rename(temporary, a_path, ec);
		if (ec) {
			std::error_code fallbackEc;
			std::filesystem::remove(a_path, fallbackEc);
			std::filesystem::rename(temporary, a_path, fallbackEc);
			if (fallbackEc) {
				std::error_code removeEc;
				std::filesystem::remove(temporary, removeEc);
				result.status = SettingsSaveStatus::IoError;
				return result;
			}
		}
		result.status = SettingsSaveStatus::Saved;
		return result;
	}
}
