#include "runtime/Ingress.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace slacs
{
	Ingress::Ingress(WakeCallback a_wake) :
		wake_(std::move(a_wake))
	{}

	Ingress::Ingress(std::size_t a_capacity, WakeCallback a_wake) :
		capacity_(a_capacity == 0 ? 1 : a_capacity),
		wake_(std::move(a_wake))
	{}

	void Ingress::setWakeCallback(WakeCallback a_wake)
	{
		std::lock_guard lock(mutex_);
		wake_ = std::move(a_wake);
	}

	void Ingress::setOpen(bool a_open)
	{
		std::lock_guard lock(mutex_);
		open_ = a_open;
	}

	bool Ingress::isOpen() const
	{
		std::lock_guard lock(mutex_);
		return open_;
	}

	IngressResult Ingress::post(RuntimeCommand a_command)
	{
		WakeCallback wake;
		IngressResult result{};
		{
			std::lock_guard lock(mutex_);
			wake = wake_;
			result = enqueueLocked(std::move(a_command));
		}
		if (wake) {
			try {
				wake();
			} catch (...) {
				std::fputs("Ingress wake callback threw\n", stderr);
			}
		}
		return result;
	}

	IngressResult Ingress::postOrgasm(FormId a_sourceFormId, Tid a_tid, SessionEpoch a_epoch)
	{
		WakeCallback wake;
		IngressResult result{};
		{
			std::lock_guard lock(mutex_);
			if (!open_) {
				return { IngressStatus::SessionClosed, std::nullopt };
			}
			if (queue_.size() + reserved_ >= capacity_) {
				return { IngressStatus::Overloaded, std::nullopt };
			}
			wake = wake_;
			const auto sequence = nextSequence_++;
			NativeOrgasm command{ a_sourceFormId, a_tid, sequence, a_epoch };
			result = enqueueLocked(std::move(command));
			result.sequence = sequence;
		}
		if (wake) {
			try {
				wake();
			} catch (...) {
				std::fputs("Ingress wake callback threw\n", stderr);
			}
		}
		return result;
	}

	bool Ingress::reserve(std::size_t a_count)
	{
		if (a_count == 0) {
			return true;
		}
		std::lock_guard lock(mutex_);
		if (!open_ || a_count > capacity_ - std::min(capacity_, queue_.size() + reserved_)) {
			return false;
		}
		reserved_ += a_count;
		return true;
	}

	void Ingress::release(std::size_t a_count) noexcept
	{
		std::lock_guard lock(mutex_);
		reserved_ = a_count >= reserved_ ? 0 : reserved_ - a_count;
	}

	std::size_t Ingress::reserved() const
	{
		std::lock_guard lock(mutex_);
		return reserved_;
	}

	IngressResult Ingress::postReserved(RuntimeCommand a_command)
	{
		WakeCallback wake;
		IngressResult result{};
		{
			std::lock_guard lock(mutex_);
			wake = wake_;
			result = enqueueLocked(std::move(a_command), true);
		}
		if (wake) {
			try {
				wake();
			} catch (...) {
				std::fputs("Ingress wake callback threw\n", stderr);
			}
		}
		return result;
	}

	std::optional<RuntimeCommand> Ingress::pop()
	{
		std::lock_guard lock(mutex_);
		if (queue_.empty()) {
			return std::nullopt;
		}
		auto command = std::move(queue_.front());
		queue_.pop_front();
		return command;
	}

	std::size_t Ingress::size() const
	{
		std::lock_guard lock(mutex_);
		return queue_.size();
	}

	void Ingress::clear()
	{
		std::lock_guard lock(mutex_);
		queue_.clear();
		reserved_ = 0;
	}

	IngressResult Ingress::enqueueLocked(RuntimeCommand a_command, bool a_consumeReservation)
	{
		if (!open_) {
			return { IngressStatus::SessionClosed, std::nullopt };
		}
		if (a_consumeReservation) {
			if (reserved_ == 0 || queue_.size() >= capacity_) {
				return { IngressStatus::Overloaded, std::nullopt };
			}
			--reserved_;
		} else if (queue_.size() + reserved_ >= capacity_) {
			return { IngressStatus::Overloaded, std::nullopt };
		}
		queue_.push_back(std::move(a_command));
		return { IngressStatus::Accepted, std::nullopt };
	}
}
