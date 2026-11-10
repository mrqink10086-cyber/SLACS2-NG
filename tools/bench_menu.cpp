#include "menu/MenuState.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>

namespace
{
	std::size_t allocations{};
	bool countAllocations{};

	const slacs::MenuSnapshot& View(const slacs::MenuSnapshot& a_snapshot) { return a_snapshot; }
	const slacs::MenuSnapshot& View(const std::shared_ptr<const slacs::MenuSnapshot>& a_snapshot) { return *a_snapshot; }
}

void* operator new(std::size_t a_size)
{
	if (countAllocations) ++allocations;
	if (auto* memory = std::malloc(a_size == 0 ? 1 : a_size)) return memory;
	throw std::bad_alloc{};
}

void operator delete(void* a_memory) noexcept { std::free(a_memory); }
void operator delete(void* a_memory, std::size_t) noexcept { std::free(a_memory); }
void* operator new[](std::size_t a_size) { return ::operator new(a_size); }
void operator delete[](void* a_memory) noexcept { ::operator delete(a_memory); }
void operator delete[](void* a_memory, std::size_t) noexcept { ::operator delete(a_memory); }

int main()
{
	constexpr std::size_t iterations = 5000;
	std::size_t checksum{};
	for (const std::size_t count : { 16, 256, 1024 }) {
		slacs::MenuSnapshot input{};
		input.actors.resize(count);
		for (std::size_t i = 0; i < count; ++i) {
			input.actors[i].actor = i + 1;
			input.actors[i].displayName = std::string(64, 'a');
		}
		slacs::MenuState::publish(std::move(input));
		allocations = 0;
		countAllocations = true;
		const auto started = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i < iterations; ++i) {
			const auto snapshot = slacs::MenuState::snapshot();
			checksum += View(snapshot).actors.size();
		}
		const auto elapsed = std::chrono::steady_clock::now() - started;
		countAllocations = false;
		std::cout << "actors=" << count << " reads=" << iterations << " allocations=" << allocations
			<< " elapsedUs=" << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << '\n';
	}
	std::cout << "checksum=" << checksum << '\n';
}
