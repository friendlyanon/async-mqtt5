#ifndef ASYNC_MQTT5_ASSEMBLE_OP_HPP
#define ASYNC_MQTT5_ASSEMBLE_OP_HPP

#include <string>

#include <boost/asio/append.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/prepend.hpp>

#include <async_mqtt5/error.hpp>

#include <async_mqtt5/detail/control_packet.hpp>
#include <async_mqtt5/detail/internal_types.hpp>

#include <async_mqtt5/impl/internal/codecs/base_decoders.hpp>
#include <async_mqtt5/impl/internal/codecs/message_decoders.hpp>


namespace async_mqtt5::detail {

namespace asio = boost::asio;

class data_span : private std::pair<byte_citer, byte_citer> {
	using base = std::pair<byte_citer, byte_citer>;
public:
	using base::base;

	auto first() const {
		return base::first;
	}
	auto last() const {
		return base::second;
	}
	void expand_suffix(size_t num_chars) {
		base::second += num_chars;
	}
	void remove_prefix(size_t num_chars) {
		base::first += num_chars;
	}
	size_t size() const {
		return std::distance(base::first, base::second);
	}
};


template <typename ClientService, typename Handler>
class assemble_op {
	using client_service = ClientService;
	struct on_read {};

	static constexpr size_t max_packet_size = 65536;

	client_service& _svc;
	Handler _handler;

	std::string& _read_buff;
	data_span& _data_span;

public:
	assemble_op(
		client_service& svc, Handler&& handler,
		std::string& read_buff, data_span& active_span
	) :
		_svc(svc),
		_handler(std::move(handler)),
		_read_buff(read_buff), _data_span(active_span)
	{}

	assemble_op(assemble_op&&) noexcept = default;
	assemble_op(const assemble_op&) = delete;

	using executor_type = typename client_service::executor_type;
	executor_type get_executor() const noexcept {
		return _svc.get_executor();
	}

	using allocator_type = asio::associated_allocator_t<Handler>;
	allocator_type get_allocator() const noexcept {
		return asio::get_associated_allocator(_handler);
	}

	template <typename CompletionCondition>
	void perform(duration wait_for, CompletionCondition cc) {
		_read_buff.erase(
			_read_buff.cbegin(), _data_span.first()
		);
		// TODO: respect max packet size from CONNACK
		_read_buff.resize(max_packet_size);
		_data_span = {
			_read_buff.cbegin(),
			_read_buff.cbegin() + _data_span.size()
		};

		if (cc(error_code {}, 0) == 0 && _data_span.size()) {
			/* TODO clear read buffer on reconnect
			* OR use dispatch instead of post here
			*/
			return asio::post(
				asio::prepend(
					std::move(*this), on_read {}, error_code {},
					0, wait_for, std::move(cc)
				)
			);
		}

		// Must be evaluated before this is moved
		auto store_begin = _read_buff.data() + _data_span.size();
		auto store_size = std::distance(_data_span.last(), _read_buff.cend());

		_svc._stream.async_read_some(
			asio::buffer(store_begin, store_size), wait_for,
			asio::prepend(
				asio::append(std::move(*this), wait_for, std::move(cc)),
				on_read {}
			)
		);
	}

	template <typename CompletionCondition>
	void operator()(
		on_read, error_code ec, size_t bytes_read,
		duration wait_for, CompletionCondition cc
	) {
		if (ec == asio::error::try_again) {
			_svc.update_session_state();
			_svc._async_sender.resend();
			_data_span = { _read_buff.cend(), _read_buff.cend() };
			return perform(wait_for, std::move(cc));
		}

		if (ec)
			return complete(ec, 0, {}, {});

		_data_span.expand_suffix(bytes_read);
		assert(_data_span.size());

		auto control_byte = uint8_t(*_data_span.first());

		if ((control_byte & 0b11110000) == 0)
			// close the connection, cancel
			return complete(client::error::malformed_packet, 0, {}, {});

		auto first = _data_span.first() + 1;
		auto varlen = decoders::type_parse(
			first, _data_span.last(), decoders::basic::varint_
		);

		if (!varlen) {
			if (_data_span.size() < 5)
				return perform(wait_for, asio::transfer_at_least(1));
			return complete(client::error::malformed_packet, 0, {}, {});
		}

		// TODO: respect max packet size which could be dinamically set by the broker
		if (*varlen > max_packet_size - std::distance(_data_span.first(), first))
			return complete(client::error::malformed_packet, 0, {}, {});

		if (std::distance(first, _data_span.last()) < *varlen)
			return perform(wait_for, asio::transfer_at_least(1));

		_data_span.remove_prefix(
			std::distance(_data_span.first(), first) + *varlen
		);

		dispatch(wait_for, control_byte, first, first + *varlen);
	}

private:
	static bool valid_header(uint8_t control_byte) {
		using enum control_code_e;

		auto code = control_code_e(control_byte & 0b11110000);

		if (code == publish)
			return true;

		auto res = control_byte & 0b00001111;
		if (code == pubrel)
			return res == 0b00000010;
		return res == 0b00000000;
	}

	void dispatch(
		duration wait_for,
		uint8_t control_byte, byte_citer first, byte_citer last
	) {
		using namespace decoders;
		using enum control_code_e;

		if (!valid_header(control_byte))
			return complete(client::error::malformed_packet, 0, {}, {});

		auto code = control_code_e(control_byte & 0b11110000);

		if (code == pingresp)
			return perform(wait_for, asio::transfer_at_least(0));

		bool is_reply = code != publish && code != auth && code != disconnect;
		if (is_reply) {
			auto packet_id = decoders::decode_packet_id(first).value();
			_svc._replies.dispatch(error_code {}, code, packet_id, first, last);
			return perform(wait_for, asio::transfer_at_least(0));
		}

		complete(error_code {}, control_byte, first, last);
	}

	void complete(
		error_code ec, uint8_t control_code,
		byte_citer first, byte_citer last
	) {
		asio::dispatch(
			get_executor(),
			asio::prepend(
				std::move(_handler), ec, control_code,
				first, last
			)
		);
	}
};

} // end namespace async_mqtt5::detail

#endif // !ASYNC_MQTT5_ASSEMBLE_OP_HPP
