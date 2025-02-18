#ifndef ASYNC_MQTT5_RECONNECT_OP_HPP
#define ASYNC_MQTT5_RECONNECT_OP_HPP

#include <boost/asio/deferred.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/prepend.hpp>

#include <boost/asio/experimental/parallel_group.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <async_mqtt5/types.hpp>

#include <async_mqtt5/detail/async_traits.hpp>

#include <async_mqtt5/impl/connect_op.hpp>

namespace async_mqtt5::detail {

namespace asio = boost::asio;

template <typename Owner, typename Handler>
class reconnect_op {
	struct on_locked {};
	struct on_next_endpoint {};
	struct on_connect {};
	struct on_backoff {};

	Owner& _owner;
	Handler _handler;
	std::unique_ptr<std::string> _buffer_ptr;

	using endpoint = asio::ip::tcp::endpoint;
	using epoints = asio::ip::tcp::resolver::results_type;

public:
	reconnect_op(Owner& owner, Handler&& handler) :
		_owner(owner),
		_handler(std::move(handler))
	{}

	reconnect_op(reconnect_op&&) noexcept = default;
	reconnect_op(const reconnect_op&) = delete;

	using executor_type = typename Owner::executor_type;
	executor_type get_executor() const noexcept {
		return _owner.get_executor();
	}

	using allocator_type = asio::associated_allocator_t<Handler>;
	allocator_type get_allocator() const noexcept {
		return asio::get_associated_allocator(_handler);
	}

	using cancellation_slot_type =
		asio::associated_cancellation_slot_t<Handler>;
	cancellation_slot_type get_cancellation_slot() const noexcept {
		return asio::get_associated_cancellation_slot(_handler);
	}

	void perform(typename Owner::stream_ptr s) {
		_owner._conn_mtx.lock(
			asio::prepend(std::move(*this), on_locked {}, s)
		);
	}

	void operator()(on_locked, typename Owner::stream_ptr s, error_code ec) {
		if (ec == asio::error::operation_aborted || !_owner.is_open())
			return complete(asio::error::operation_aborted);

		if (s != _owner._stream_ptr)
			return complete(asio::error::try_again);

		do_reconnect();
	}

	void do_reconnect() {
		_owner._endpoints.async_next_endpoint(
			asio::prepend(std::move(*this), on_next_endpoint {})
		);
	}

	void backoff_and_reconnect() {
		_owner._connect_timer.expires_from_now(std::chrono::seconds(5));
		_owner._connect_timer.async_wait(
			asio::prepend(std::move(*this), on_backoff {})
		);
	}

	void operator()(on_backoff, error_code ec) {
		if (ec == asio::error::operation_aborted || !_owner.is_open())
			return complete(asio::error::operation_aborted);

		do_reconnect();
	}

	void operator()(
		on_next_endpoint, error_code ec,
		epoints eps, authority_path ap
	) {
		namespace asioex = boost::asio::experimental;

		// the three error codes below are the only possible codes
		// that may be returned from async_next_endpont

		if (ec == asio::error::operation_aborted || !_owner.is_open())
			return complete(asio::error::operation_aborted);

		if (ec == asio::error::try_again)
			return backoff_and_reconnect();

		if (ec == asio::error::host_not_found)
			return complete(asio::error::no_recovery);

		auto sptr = _owner.construct_next_layer();

		if constexpr (has_tls_context<typename Owner::stream_context_type>)
			setup_tls_sni(
				ap, _owner._stream_context.tls_context(), *sptr
			);

		// wait max 5 seconds for the connect (handshake) op to finish
		_owner._connect_timer.expires_from_now(std::chrono::seconds(5));

		auto init_connect = [this, sptr](
			auto handler, const auto& eps, auto ap
		) {
			connect_op {
				*sptr, std::move(handler),
				_owner._stream_context.mqtt_context()
			}.perform(eps, std::move(ap));
		};

		auto timed_connect = asioex::make_parallel_group(
			asio::async_initiate<const asio::deferred_t, void (error_code)>(
				std::move(init_connect), asio::deferred,
				std::move(eps), std::move(ap)
			),
			_owner._connect_timer.async_wait(asio::deferred)
		);

		timed_connect.async_wait(
			asioex::wait_for_one(),
			asio::prepend(std::move(*this), on_connect {}, std::move(sptr))
		);
	}

	void operator()(
		on_connect, typename Owner::stream_ptr sptr,
		auto ord, error_code connect_ec, error_code timer_ec
	) {
		// connect_ec may be any of stream.async_connect() error codes
		// plus access_denied, connection_refused and
		// client::error::malformed_packet
		if (
			ord[0] == 0 && connect_ec == asio::error::operation_aborted ||
			ord[0] == 1 && timer_ec == asio::error::operation_aborted ||
			!_owner.is_open()
		)
			return complete(asio::error::operation_aborted);

		// operation timed out so retry
		if (ord[0] == 1)
			return do_reconnect();

		if (connect_ec == asio::error::access_denied)
			return complete(asio::error::no_recovery);

		// retry for any other stream.async_connect() error or
		// connection_refused, client::error::malformed_packet
		if (connect_ec)
			return do_reconnect();

		_owner.replace_next_layer(std::move(sptr));
		complete(error_code {});
	}

private:
	void complete(error_code ec) {
		get_cancellation_slot().clear();
		_owner._conn_mtx.unlock();

		asio::dispatch(
			get_executor(),
			asio::prepend(std::move(_handler), ec)
		);
	}

};


} // end namespace async_mqtt5::detail

#endif // !ASYNC_MQTT5_RECONNECT_OP_HPP
