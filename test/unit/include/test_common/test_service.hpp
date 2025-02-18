#ifndef ASYNC_MQTT5_TEST_TEST_SERVICE_HPP
#define ASYNC_MQTT5_TEST_TEST_SERVICE_HPP

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/prepend.hpp>

#include <async_mqtt5/impl/client_service.hpp>

namespace async_mqtt5::test {

namespace asio = boost::asio;

template <
	typename StreamType,
	typename TlsContext = std::monostate
>
class test_service : public detail::client_service<StreamType, TlsContext> {
	using error_code = boost::system::error_code;
	using base = detail::client_service<StreamType, TlsContext>;

	asio::any_io_executor _ex;
public:
	test_service(const asio::any_io_executor ex)
		: base(ex, {}), _ex(ex)
	{}

	template <typename BufferType, typename CompletionToken>
	decltype(auto) async_send(
		const BufferType&, uint32_t, unsigned,
		CompletionToken&& token
	) {
		auto initiation = [this](auto handler) {
			auto ex = asio::get_associated_executor(handler, _ex);
			asio::post(ex,
				asio::prepend(std::move(handler), error_code {})
			);
		};

		return asio::async_initiate<
			CompletionToken, void (error_code)
		> (std::move(initiation), token);
	}
};


} // end namespace async_mqtt5::test

#endif // ASYNC_MQTT5_TEST_TEST_SERVICE_HPP
