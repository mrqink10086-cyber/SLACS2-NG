#pragma once

#include "runtime/ActorRegistry.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <variant>

using skee_i32 = std::int32_t;

class IOverlayInterface
{
public:
	using InstallCallback = void(*)(RE::TESObjectREFR*, RE::NiAVObject*);

	enum class OverlayType { Normal, Spell };
	enum class OverlayLocation { Body, Hand, Feet, Face };
	std::array<unsigned, 4> counts{ 6, 3, 3, 3 };
	unsigned countCalls{};
	unsigned formatCalls{};
	unsigned addOverlaysCalls{};
	std::mutex callbackMutex;
	std::mutex callbackStateMutex;
	std::condition_variable callbackStateChanged;
	std::string callbackKey;
	InstallCallback installCallback{};
	std::atomic<unsigned> registerCalls{};
	std::atomic<unsigned> unregisterAttempts{};
	std::atomic<unsigned> unregisterCalls{};
	unsigned GetOverlayCount(OverlayType, OverlayLocation a_location)
	{
		++countCalls;
		switch (a_location) {
		case OverlayLocation::Body: return counts[0];
		case OverlayLocation::Hand: return counts[1];
		case OverlayLocation::Feet: return counts[2];
		case OverlayLocation::Face: return counts[3];
		}
		return 0;
	}
	const char* GetOverlayFormat(OverlayType, OverlayLocation a_location)
	{
		++formatCalls;
		switch (a_location) {
		case OverlayLocation::Body: return "Body [Ovl{}]";
		case OverlayLocation::Hand: return "Hands [Ovl{}]";
		case OverlayLocation::Feet: return "Feet [Ovl{}]";
		case OverlayLocation::Face: return "Face [Ovl{}]";
		}
		return "";
	}
	bool RegisterInstallCallback(const char* a_key, InstallCallback a_callback)
	{
		std::lock_guard lock(callbackMutex);
		++registerCalls;
		callbackKey = a_key ? a_key : "";
		installCallback = a_callback;
		return true;
	}
	bool UnregisterInstallCallback(const char* a_key)
	{
		{
			std::lock_guard lock(callbackStateMutex);
			++unregisterAttempts;
		}
		callbackStateChanged.notify_all();
		std::lock_guard lock(callbackMutex);
		if (!a_key || callbackKey != a_key) return false;
		callbackKey.clear();
		installCallback = nullptr;
		++unregisterCalls;
		return true;
	}
	InstallCallback SnapshotInstallCallback()
	{
		std::lock_guard lock(callbackMutex);
		return installCallback;
	}
	void InvokeInstallCallback(RE::TESObjectREFR* a_reference, RE::NiAVObject* a_node)
	{
		std::lock_guard lock(callbackMutex);
		if (installCallback) installCallback(a_reference, a_node);
	}
	bool WaitForUnregisterAttempt(std::chrono::milliseconds a_timeout)
	{
		std::unique_lock lock(callbackStateMutex);
		return callbackStateChanged.wait_for(lock, a_timeout, [&] { return unregisterAttempts.load() != 0; });
	}
	void ResetCallbacksForTesting()
	{
		std::lock_guard lock(callbackMutex);
		callbackKey.clear();
		installCallback = nullptr;
		registerCalls = 0;
		unregisterAttempts = 0;
		unregisterCalls = 0;
	}
	void AddOverlays(RE::TESObjectREFR*, bool) { ++addOverlaysCalls; }
};

class IOverrideInterface
{
public:
	class SetVariant
	{
	public:
		enum class Type { None, Int, Float, String, Bool, TextureSet };
		virtual Type GetType() { return Type::None; }
		virtual float Float() { return 0; }
		virtual const char* String() { return nullptr; }
	};
	class GetVariant
	{
	public:
		virtual void Int(skee_i32) = 0;
		virtual void Float(float) = 0;
		virtual void String(const char*) = 0;
		virtual void Bool(bool) = 0;
		virtual void TextureSet(const BGSTextureSet*) = 0;
	};
	using Key = std::tuple<std::string, unsigned, unsigned>;
	std::map<Key, std::variant<std::string, float>> data;
	unsigned writes{};
	unsigned getCalls{};
	unsigned hasCalls{};
	unsigned setNodePropertiesCalls{};
	bool HasNodeOverride(RE::TESObjectREFR*, bool, const char* a_node, std::uint16_t a_key, std::uint8_t a_index)
	{
		++hasCalls;
		return data.contains({ a_node, a_key, a_index });
	}
	bool GetNodeOverride(RE::TESObjectREFR*, bool, const char* a_node, std::uint16_t a_key, std::uint8_t a_index, GetVariant& a_value)
	{
		++getCalls;
		const auto it = data.find({ a_node, a_key, a_index });
		if (it == data.end()) {
			return false;
		}
		if (const auto* value = std::get_if<std::string>(&it->second)) {
			a_value.String(value->c_str());
		} else {
			a_value.Float(std::get<float>(it->second));
		}
		return true;
	}
	void AddNodeOverride(RE::TESObjectREFR*, bool, const char* a_node, std::uint16_t a_key, std::uint8_t a_index, SetVariant& a_value)
	{
		++writes;
		if (a_value.GetType() == SetVariant::Type::String) {
			data[{ a_node, a_key, a_index }] = std::string(a_value.String());
		} else {
			data[{ a_node, a_key, a_index }] = a_value.Float();
		}
	}
	void SetNodeProperties(RE::TESObjectREFR*, bool) { ++setNodePropertiesCalls; }
};
