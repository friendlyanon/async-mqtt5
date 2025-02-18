#ifndef ASYNC_MQTT5_ENDPOINTS_HPP
#define ASYNC_MQTT5_ENDPOINTS_HPP

#include <boost/asio/append.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/prepend.hpp>

#include <boost/asio/experimental/parallel_group.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/spirit/home/x3.hpp>

#include <async_mqtt5/types.hpp>

namespace async_mqtt5::detail {

namespace asio = boost::asio;

using epoints = asio::ip::tcp::resolver::results_type;

template <typename Owner, typename Handler>
class resolve_op {
	struct on_resolve {};

	Owner& _owner;
	Handler _handler;

public:
	resolve_op(
		Owner& owner, Handler&& handler) :
		_owner(owner),
		_handler(std::move(handler))
	{}

	resolve_op(resolve_op&&) noexcept = default;
	resolve_op(const resolve_op&) = delete;

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

	void perform() {
		namespace asioex = boost::asio::experimental;

		if (_owner._servers.empty())
			return complete_post(asio::error::host_not_found, {}, {});

		_owner._current_host++;

		if (_owner._current_host + 1 > _owner._servers.size()) {
			_owner._current_host = -1;
			return complete_post(asio::error::try_again, {}, {});
		}

		authority_path ap = _owner._servers[_owner._current_host];

		_owner._connect_timer.expires_from_now(std::chrono::seconds(5));

		auto timed_resolve = asioex::make_parallel_group(
			_owner._resolver.async_resolve(ap.host, ap.port, asio::deferred),
			_owner._connect_timer.async_wait(asio::deferred)
		);

		timed_resolve.async_wait(
			asioex::wait_for_one(),
			asio::append(
				asio::prepend(std::move(*this), on_resolve {}),
				std::move(ap)
			)
		);
	}

	void operator()(
		on_resolve, auto ord,
		error_code resolve_ec, epoints epts,
		error_code timer_ec, authority_path ap
	) {
		if (
			ord[0] == 0 && resolve_ec == asio::error::operation_aborted ||
			ord[0] == 1 && timer_ec == asio::error::operation_aborted
		)
			return complete(asio::error::operation_aborted, {}, {});

		if (!resolve_ec)
			return complete(error_code {}, std::move(epts), std::move(ap));

		perform();
	}

private:
	void complete(error_code ec, epoints eps, authority_path ap) {
		get_cancellation_slot().clear();

		asio::dispatch(
			get_executor(),
			asio::prepend(
				std::move(_handler), ec,
				std::move(eps), std::move(ap)
			)
		);
	}

	void complete_post(error_code ec, epoints eps, authority_path ap) {
		get_cancellation_slot().clear();

		asio::post(
			get_executor(),
			asio::prepend(
				std::move(_handler), ec,
				std::move(eps), std::move(ap)
			)
		);

	}
};


class endpoints {
	asio::ip::tcp::resolver _resolver;
	asio::steady_timer& _connect_timer;

	std::vector<authority_path> _servers;

	int _current_host { -1 };

	template <typename Owner, typename Handler>
	friend class resolve_op;

	template <typename T>
	static constexpr auto to_(T& arg) {
		return [&](auto& ctx) { arg = boost::spirit::x3::_attr(ctx); };
	}

	template <typename T, typename Parser>
	static constexpr auto as_(Parser&& p){
		return boost::spirit::x3::rule<struct _, T>{} = std::forward<Parser>(p);
	}

public:
	template <typename Executor>
	endpoints(Executor ex, asio::steady_timer& timer)
		: _resolver(ex), _connect_timer(timer)
	{}

	using executor_type = asio::ip::tcp::resolver::executor_type;
	// NOTE: asio::ip::basic_resolver returns executor by value
	executor_type get_executor() {
		return _resolver.get_executor();
	}

	template <typename CompletionToken>
	decltype(auto) async_next_endpoint(CompletionToken&& token) {
		auto initiation = [this](auto handler) {
			resolve_op { *this, std::move(handler) }.perform();
		};

		return asio::async_initiate<
			CompletionToken,
			void (error_code, epoints, authority_path)
		>(
			std::move(initiation), token
		);
	}

	void brokers(std::string hosts, uint16_t default_port) {
		namespace x3 = boost::spirit::x3;

		_servers.clear();

		std::string host, port, path;

		// loosely based on RFC 3986
		auto unreserved_ = x3::char_("-a-zA-Z_0-9._~");
		auto digit_ = x3::char_("0-9");
		auto separator_ = x3::char_(',');

		auto host_ = as_<std::string>(+unreserved_)[to_(host)];
		auto port_ = as_<std::string>(':' >> +digit_)[to_(port)];
		auto path_ = as_<std::string>(x3::char_('/') >> *unreserved_)[to_(path)];
		auto uri_ = *x3::omit[x3::space] >> (host_ >> *port_ >> *path_) >>
			(*x3::omit[x3::space] >> x3::omit[separator_ | x3::eoi]);

		for (auto b = hosts.begin(); b != hosts.end(); ) {
			host.clear(); port.clear(); path.clear();
			if (phrase_parse(b, hosts.end(), uri_, x3::eps(false))) {
				_servers.push_back({
					std::move(host),
					port.empty()
						? std::to_string(default_port)
						: std::move(port),
					std::move(path)
				});
			}
			else b = hosts.end();
		}
	}

};


} // end namespace async_mqtt5::detail

#endif // !ASYNC_MQTT5_ENDPOINTS_HPP
