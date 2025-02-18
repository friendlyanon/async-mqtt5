#ifndef ASYNC_MQTT5_TEST_DELAYED_OP_HPP
#define ASYNC_MQTT5_TEST_DELAYED_OP_HPP

#include <chrono>

#include <boost/asio/append.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/prepend.hpp>
#include <boost/asio/recycling_allocator.hpp>
#include <boost/asio/steady_timer.hpp>


namespace async_mqtt5::test {

namespace asio = boost::asio;

using error_code = boost::system::error_code;
using time_stamp = std::chrono::time_point<std::chrono::steady_clock>;
using duration = time_stamp::duration;

template <typename ...BoundArgs>
class delayed_op {
	struct on_timer {};

	std::unique_ptr<asio::steady_timer> _timer;
	time_stamp::duration _delay;
	asio::cancellation_slot _cancel_slot;

	std::tuple<BoundArgs...> _args;

public:
	template <typename Executor, typename ...Args>
	delayed_op(
		const Executor& ex, time_stamp::duration delay, Args&& ...args
	) :
		_timer(new asio::steady_timer(ex)), _delay(delay),
		_args(std::move(args)...)
	{}

	delayed_op(delayed_op&&) noexcept = default;
	delayed_op(const delayed_op&) = delete;

	using executor_type = asio::steady_timer::executor_type;
	executor_type get_executor() const noexcept {
		return _timer->get_executor();
	}

	using allocator_type = asio::recycling_allocator<void>;
	allocator_type get_allocator() const noexcept {
		return allocator_type {};
	}

	using cancellation_slot_type = asio::cancellation_slot;
	asio::cancellation_slot get_cancellation_slot() const noexcept {
		return _cancel_slot;
	}

	template <typename CompletionHandler>
	void perform(CompletionHandler&& handler) {
		_cancel_slot = asio::get_associated_cancellation_slot(handler);

		_timer->expires_from_now(_delay);
		_timer->async_wait(
			asio::prepend(std::move(*this), on_timer {}, std::move(handler))
		);
	}

	template <typename CompletionHandler>
	void operator()(on_timer, CompletionHandler&& h, error_code ec) {
		get_cancellation_slot().clear();

		auto bh = std::apply(
			[h = std::move(h)](auto&&... args) mutable {
				return asio::append(std::move(h), std::move(args)...);
			},
			_args
		);

		asio::dispatch(asio::prepend(std::move(bh), ec));
	}
};

template <typename CompletionToken, typename ...BoundArgs>
decltype(auto) async_delay(
	asio::cancellation_slot cancel_slot,
	delayed_op<BoundArgs...>&& op,
	CompletionToken&& token
) {
	using Signature = void (error_code, std::remove_cvref_t<BoundArgs>...);

	auto initiation = [](
		auto handler, asio::cancellation_slot cancel_slot,
		delayed_op<BoundArgs...> op
	) {
		op.perform(
			asio::bind_cancellation_slot(cancel_slot, std::move(handler))
		);
	};

	return asio::async_initiate<CompletionToken, Signature>(
		std::move(initiation), token, cancel_slot, std::move(op)
	);
}


} // end namespace async_mqtt5::test

#endif // ASYNC_MQTT5_TEST_DELAYED_OP_HPP
