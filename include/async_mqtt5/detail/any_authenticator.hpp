#ifndef ASYNC_MQTT5_ANY_AUTHENTICATOR
#define ASYNC_MQTT5_ANY_AUTHENTICATOR

#include <boost/asio/any_completion_handler.hpp>
#include <boost/asio/async_result.hpp>

#include <async_mqtt5/types.hpp>

namespace async_mqtt5 {

namespace asio = boost::asio;
using error_code = boost::system::error_code;

namespace detail {

using auth_handler_type = asio::any_completion_handler<
	void(error_code ec, std::string auth_data)
>;

template <typename T>
concept is_authenticator = requires (T a) {
	{
		a.async_auth(auth_step_e {}, std::string {}, auth_handler_type {})
	} -> std::same_as<void>;
	{ a.method() } -> std::same_as<std::string_view>;
};

class auth_fun_base {
	using auth_func = void(*)(
		auth_step_e, std::string, auth_handler_type, auth_fun_base*
	);
	auth_func _auth_func;

public:
	auth_fun_base(auth_func f) : _auth_func(f) {}
	~auth_fun_base() = default;

	void async_auth(
		auth_step_e step, std::string data, auth_handler_type auth_handler
	) {
		_auth_func(step, std::move(data), std::move(auth_handler), this);
	}
};

template <is_authenticator Authenticator>
class auth_fun : public auth_fun_base {
	Authenticator _authenticator;

public:
	auth_fun(Authenticator authenticator) :
		auth_fun_base(&async_auth),
		_authenticator(std::forward<Authenticator>(authenticator))
	{}

	static void async_auth(
		auth_step_e step, std::string data, auth_handler_type auth_handler,
		auth_fun_base* base_ptr
	) {
		auto auth_fun_ptr = static_cast<auth_fun*>(base_ptr);
		auth_fun_ptr->_authenticator.async_auth(
			step, std::move(data), std::move(auth_handler)
		);
	}
};

} // end namespace detail

class any_authenticator {
	std::string _method;
	std::unique_ptr<detail::auth_fun_base> _auth_fun;

public:
	any_authenticator() = default;

	template <detail::is_authenticator Authenticator>
	any_authenticator(Authenticator&& a) :
		_method(a.method()),
		_auth_fun(
			new detail::auth_fun<Authenticator>(
				std::forward<Authenticator>(a)
			)
		)
	{}

	std::string_view method() const {
		return _method;
	}

	template <typename CompletionToken>
	decltype(auto) async_auth(
		auth_step_e step, std::string data,
		CompletionToken&& token
	) {
		using Signature = void(error_code, std::string);

		auto initiate = [this](auto handler, auth_step_e step, std::string data) {
			_auth_fun->async_auth(step, std::move(data), std::move(handler));
		};

		return asio::async_initiate<CompletionToken, Signature>(
			initiate, token, step, std::move(data)
		);
	}
};

} // end namespace async_mqtt5

#endif // !ASYNC_MQTT5_ANY_AUTHENTICATOR
