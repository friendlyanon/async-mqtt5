#ifndef ASYNC_MQTT5_READ_OP_HPP
#define ASYNC_MQTT5_READ_OP_HPP

#include <boost/asio/deferred.hpp>
#include <boost/asio/prepend.hpp>

#include <boost/asio/experimental/parallel_group.hpp>

#include <async_mqtt5/detail/internal_types.hpp>

namespace async_mqtt5::detail {

namespace asio = boost::asio;
namespace asioex = boost::asio::experimental;

template <typename Owner, typename Handler>
class read_op {
	struct on_read {};
	struct on_reconnect {};

	Owner& _owner;
	Handler _handler;

public:
	read_op(Owner& owner, Handler&& handler) :
		_owner(owner),
		_handler(std::move(handler))
	{}

	read_op(read_op&&) noexcept = default;
	read_op(const read_op&) = delete;

	using executor_type = typename Owner::executor_type;
	executor_type get_executor() const noexcept {
		return _owner.get_executor();
	}

	using allocator_type = asio::associated_allocator_t<Handler>;
	allocator_type get_allocator() const noexcept {
		return asio::get_associated_allocator(_handler);
	}

	template <typename BufferType>
	void perform(
		const BufferType& buffer, duration wait_for
	) {
		auto stream_ptr = _owner._stream_ptr;

		if (_owner.was_connected()) {
			_owner._read_timer.expires_from_now(wait_for);

			auto timed_read = asioex::make_parallel_group(
				stream_ptr->async_read_some(buffer, asio::deferred),
				_owner._read_timer.async_wait(asio::deferred)
			);

			timed_read.async_wait(
				asioex::wait_for_one(),
				asio::prepend(std::move(*this), on_read {}, stream_ptr)
			);
		}
		else
			(*this)(
				on_read {}, stream_ptr,
				{ 0, 1 }, asio::error::not_connected, 0, {}
			);
	}

	void operator()(
		on_read, typename Owner::stream_ptr stream_ptr,
		std::array<std::size_t, 2> ord, error_code read_ec, size_t bytes_read,
		error_code timer_ec
	) {
		if (!_owner.is_open())
			return complete(asio::error::operation_aborted, bytes_read);

		error_code ec = ord[0] == 1 ? asio::error::timed_out : read_ec;
		bytes_read = ord[0] == 0 ? bytes_read : 0;

		if (!ec)
			return complete(ec, bytes_read);

		// websocket returns operation_aborted if disconnected
		if (should_reconnect(ec) || ec == asio::error::operation_aborted)
			return _owner.async_reconnect(
				stream_ptr, asio::prepend(std::move(*this), on_reconnect {})
			);

		return complete(asio::error::no_recovery, bytes_read);
	}

	void operator()(on_reconnect, error_code ec) {
		if ((ec == asio::error::operation_aborted && _owner.is_open()) || !ec)
			ec = asio::error::try_again;

		return complete(ec, 0);
	}

private:
	void complete(error_code ec, size_t bytes_read) {
		asio::dispatch(
			get_executor(),
			asio::prepend(std::move(_handler), ec, bytes_read)
		);
	}

	static bool should_reconnect(error_code ec) {
		using namespace asio::error;
		return ec == connection_aborted || ec == not_connected ||
			ec == timed_out || ec == connection_reset || ec == broken_pipe;
	}
};


} // end namespace async_mqtt5::detail

#endif // !ASYNC_MQTT5_READ_OP_HPP
