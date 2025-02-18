#ifndef ASYNC_MQTT5_ERROR_HPP
#define ASYNC_MQTT5_ERROR_HPP

#include <algorithm>
#include <optional>

#include <boost/asio/error.hpp>

namespace async_mqtt5 {

/**
 * \brief A representation of Disconnect Reason Code.
 *
 * \details Represents all Reason Codes that the Client can send to the Server
 * in the \__DISCONNECT\__ packet as the reason for the disconnection.
 */
enum class disconnect_rc_e : std::uint8_t {
	/** Close the connection normally. Do not send the Will Message. */
	normal_disconnection = 0x00,

	/** The Client wishes to disconnect but requires that
	 the Server also publishes its Will Message. */
	disconnect_with_will_message = 0x04
};

namespace detail {

enum class disconnect_rc_e : std::uint8_t {
	normal_disconnection = 0x00,
	disconnect_with_will_message = 0x04,

	unspecified_error = 0x80,
	malformed_packet = 0x81,
	protocol_error = 0x82,
	implementation_specific_error = 0x83,
	topic_name_invalid = 0x90,
	receive_maximum_exceeded = 0x93,
	topic_alias_invalid = 0x94,
	packet_too_large = 0x95,
	message_rate_too_high = 0x96,
	quota_exceeded = 0x97,
	administrative_action = 0x98,
	payload_format_invalid = 0x99
};

}


namespace client {
/**
 * \brief MQTT client error codes.
 *
 * \details Represents error that occur on the client side. 
 */
enum class error : int {
	/// \cond INTERNAL
	/** Malformed packet has been detected. */
	malformed_packet = 100,
	/// \endcond

	/** There are no more available Packet Identifiers to use. */
	pid_overrun,

	/** The Client's session does not exist or it has expired. */
	session_expired,

	// publish
	/** The Server does not support the specified \ref qos_e. */
	qos_not_supported,

	/** The Server dos not support retained messages. */
	retain_not_available,

	/** The Client attempted to send a Topic Alias that is greater than Topic Alias Maximum. */
	topic_alias_maximum_reached
};


inline std::string client_error_to_string(error err) {
	using enum error;

	switch (err) {
		case malformed_packet:
			return "Malformed packet has been detected";
		case session_expired:
			return "The Client's session does not exist or it has expired. ";
		case pid_overrun:
			return "There are no more available Packet Identifiers to use.";
		case qos_not_supported:
			return "The Server does not support the specified QoS";
		case retain_not_available:
			return "The Server does not support retained messages.";
		case topic_alias_maximum_reached:
			return "The Client attempted to send a Topic Alias "
				"that is greater than Topic Alias Maximum.";
		default:
			return "Unknown client error";
	}
}

struct client_ec_category : public boost::system::error_category {
	const char* name() const noexcept override { return "mqtt_client_error"; }
	std::string message(int ev) const noexcept override {
		return client_error_to_string(static_cast<error>(ev));
	}
};

/// Returns the error category associated with \ref client::error.
inline const client_ec_category& get_error_code_category() {
	static client_ec_category cat;
	return cat;
}

/// Creates an \ref error_code from a \ref client::error.
inline boost::system::error_code make_error_code(error r) {
	return { static_cast<int>(r), get_error_code_category() };
}


} // end namespace client

/// \cond internal
namespace reason_codes {

enum class category : uint8_t {
	none,
	connack, puback, pubrec,
	pubrel, pubcomp, suback,
	unsuback, auth, disconnect
};

} // end namespace reason_codes

/// \endcond

/**
 * \brief A class holding Reason Code values originating from Control Packets.
 *
 * \details A Reason Code is a one byte unsigned value that indicates the result of an operation.
 *	Reason Codes less than 0x80 indicate successful completion of an operation.
 *	The normal Reason Code for success is 0.
 *	Reason Code values of 0x80 or greater indicate failure.
 *	The \__CONNACK\__,  \__PUBACK\__,  \__PUBREC\__,  \__PUBREL\__,  \__PUBCOMP\__,  \__DISCONNECT\__
 *	and  \__AUTH\__ Control Packets have a single Reason Code as part of the Variable Header.
 *	The \__SUBACK\__ and \__UNSUBACK\__ packets contain a list of one or more Reason Codes in the Payload.
 *
 *	\see See \__REASON_CODES\__ for a complete list of all possible instances of this class.
 */
class reason_code {
	uint8_t _code;
	reason_codes::category _category { reason_codes::category::none };
public:
/// \cond INTERNAL
	constexpr reason_code() : _code(0xff) {}

	constexpr reason_code(uint8_t code, reason_codes::category cat)
		: _code(code), _category(cat)
	{}

	constexpr reason_code(uint8_t code) : _code(code) {}
/// \endcond 

	/// Copy constructor.
	reason_code(const reason_code&) = default;

	/// Move constructor.
	reason_code(reason_code&&) = default;

	/// Copy assignment operator.
	reason_code& operator=(const reason_code&) = default;

	/// Move assignment operator.
	reason_code& operator=(reason_code&&) = default;

	/**
	 * \brief Indication if the object holds a Reason Code indicating an error.
	 *
	 * \details Any Reason Code holding a value equal or greater than 0x80.
	 */
	explicit operator bool() const noexcept {
		return _code >= 0x80;
	}

	/**
	 * \brief Returns the byte value of the Reason Code.
	 */
	constexpr uint8_t value() const noexcept {
		return _code;
	}

	/// Insertion operator.
	friend std::ostream& operator<<(std::ostream& os, const reason_code& rc) {
		os << rc.message();
		return os;
	}

	/// Operator less than.
	friend bool operator<(const reason_code& lhs, const reason_code& rhs) {
		return lhs._code < rhs._code;
	}

	/// Equality operator.
	friend bool operator==(const reason_code& lhs, const reason_code& rhs) {
		return lhs._code == rhs._code && lhs._category == rhs._category;
	}

	/**
	 * \brief Returns a message describing the meaning behind the Reason Code.
	 */
	std::string message() const {
		switch (_code) {
			case 0x00:
				using enum reason_codes::category;
				if (_category == suback)
					return "The subscription is accepted with maximum QoS sent at 0";
				if (_category == disconnect)
					return "Close the connection normally. Do not send the Will Message";
				return "The operation completed successfully";
			case 0x01:
				return "The subscription is accepted with maximum QoS sent at 1";
			case 0x02:
				return "The subscription is accepted with maximum QoS sent at 2";
			case 0x04:
				return "The Client wishes to disconnect but requires"
						"that the Server also publishes its Will Message";
			case 0x10:
				return "The message is accepted but there are no subscribers";
			case 0x11:
				return "No matching Topic Filter is being used by the Client.";
			case 0x18:
				return "Continue the authentication with another step";
			case 0x19:
				return "Initiate a re-authentication";
			case 0x80:
				return "The Server does not wish to reveal the reason for the"
						"failure, or none of the other Reason Codes apply";
			case 0x81:
				return "Data within the packet could not be correctly parsed";
			case 0x82:
				return "Data in the packet does not conform to this specification";
			case 0x83:
				return "The packet is valid but not accepted by this Server";
			case 0x84:
				return "The Server does not support the requested "
						"version of the MQTT protocol";
			case 0x85:
				return "The Client ID is valid but not allowed by this Server";
			case 0x86:
				return "The Server does not accept the User Name or Password provided";
			case 0x87:
				return "The request is not authorized";
			case 0x88:
				return "The MQTT Server is not available";
			case 0x89:
				return "The MQTT Server is busy, try again later";
			case 0x8a:
				return "The Client has been banned by administrative action";
			case 0x8b:
				return "The Server is shutting down";
			case 0x8c:
				return "The authentication method is not supported or "
						"does not match the method currently in use";
			case 0x8d:
				return "No packet has been received for 1.5 times the Keepalive time";
			case 0x8e:
				return "Another Connection using the same ClientID has connected "
						"causing this Connection to be closed";
			case 0x8f:
				return "The Topic Filer is not malformed, but it is not accepted";
			case 0x90:
				return "The Topic Name is not malformed, but it is not accepted";
			case 0x91:
				return "The Packet Identifier is already in use";
			case 0x92:
				return "The Packet Identifier is not known";
			case 0x93:
				return "The Client or Server has received more than Receive "
						"Maximum publication for which it has not sent PUBACK or PUBCOMP";
			case 0x94:
				return "The Client or Server received a PUBLISH packet containing "
						"a Topic Alias greater than the Maximum Topic Alias";
			case 0x95:
				return "The packet exceeded the maximum permissible size";
			case 0x96:
				return "The received data rate is too high";
			case 0x97:
				return "An implementation or administrative imposed limit has been exceeded";
			case 0x98:
				return "The Connection is closed due to an administrative action";
			case 0x99:
				return "The Payload does not match the specified Payload Format Indicator";
			case 0x9a:
				return "The Server does not support retained messages";
			case 0x9b:
				return "The Server does not support the QoS the Client specified or "
						"it is greater than the Maximum QoS specified";
			case 0x9c:
				return "The Client should temporarily use another server";
			case 0x9d:
				return "The Client should permanently use another server";
			case 0x9e:
				return "The Server does not support Shared Subscriptions for this Client";
			case 0x9f:
				return "The connection rate limit has been exceeded";
			case 0xa0:
				return "The maximum connection time authorized for this "
						"connection has been exceeded";
			case 0xa1:
				return "The Server does not support Subscription Identifiers";
			case 0xa2:
				return "The Server does not support Wildcard Subscriptions";
			case 0xff:
				return "No reason code";
			default:
				return "Invalid reason code";
		}
	}
};

namespace reason_codes {

using enum category;

/** No Reason Code. A \ref client::error occurred.*/
constexpr reason_code empty {};

/** The operation completed successfully. */
constexpr reason_code success { 0x00 };

/** Close the connection normally. Do not send the Will Message. */
constexpr reason_code normal_disconnection { 0x00, disconnect };

/** The subscription is accepted with maximum QoS sent at 0. */
constexpr reason_code granted_qos_0 { 0x00, suback };

/** The subscription is accepted with maximum QoS sent at 1. */
constexpr reason_code granted_qos_1 { 0x01 };

/** The subscription is accepted with maximum QoS sent at 2 */
constexpr reason_code granted_qos_2 { 0x02 };

/** The Client wishes to disconnect but requires that
 the Server also publishes its Will Message. */
constexpr reason_code disconnect_with_will_message { 0x04 };

/** The message is accepted but there are no subscribers. */
constexpr reason_code no_matching_subscribers { 0x10 };

/** No matching Topic Filter is being used by the Client. */
constexpr reason_code no_subscription_existed { 0x11 };

/** Continue the authentication with another step. */
constexpr reason_code continue_authentication { 0x18 };

/** Initiate a re-authentication. */
constexpr reason_code reauthenticate { 0x19 };

/** The Server does not wish to reveal the reason for the
 failure, or none of the other Reason Codes apply. */
constexpr reason_code unspecified_error { 0x80 };

/** Data within the packet could not be correctly parsed. */
constexpr reason_code malformed_packet { 0x81 };

/** Data in the packet does not conform to this specification. */
constexpr reason_code protocol_error { 0x82 };

/** The packet is valid but not accepted by this Server. */
constexpr reason_code implementation_specific_error { 0x83 };

/** The Server does not support the requested version of the MQTT protocol. */
constexpr reason_code unsupported_protocol_version { 0x84 };

/** The Client ID is valid but not allowed by this Server. */
constexpr reason_code client_id_not_valid { 0x85 };

/** The Server does not accept the User Name or Password provided. */
constexpr reason_code bad_username_or_password { 0x86 };

/** The request is not authorized. */
constexpr reason_code not_authorized { 0x87 };

/** The MQTT Server is not available. */
constexpr reason_code server_unavailable { 0x88 };

/** The MQTT Server is busy, try again later. */
constexpr reason_code server_busy { 0x89 };

/** The Client has been banned by administrative action. */
constexpr reason_code banned { 0x8a };

/** The Server is shutting down. */
constexpr reason_code server_shutting_down { 0x8b };

/** The authentication method is not supported or 
 does not match the method currently in use. */
constexpr reason_code bad_authentication_method { 0x8c };

/** No packet has been received for 1.5 times the Keepalive time. */
constexpr reason_code keep_alive_timeout { 0x8d };

/** Another Connection using the same ClientID has connected
 causing this Connection to be closed. */
constexpr reason_code session_taken_over { 0x8e };

/** The Topic Filter is not malformed, but it is not accepted. */
constexpr reason_code topic_filter_invalid { 0x8f };

/** The Topic Name is not malformed, but it is not accepted. */
constexpr reason_code topic_name_invalid { 0x90 };

/** The Packet Identifier is already in use. */
constexpr reason_code packet_id_in_use { 0x91 };

/** The Packet Identifier is not known. */
constexpr reason_code packet_id_not_found { 0x92 };

/** The Client or Server has received more than Receive
 Maximum publication for which it has not sent PUBACK or PUBCOMP. */
constexpr reason_code receive_maximum_exceeded { 0x93 };

/** The Client or Server received a PUBLISH packet containing
 a Topic Alias greater than the Maximum Topic Alias. */
constexpr reason_code topic_alias_invalid { 0x94 };

/** The packet exceeded the maximum permissible size. */
constexpr reason_code packet_too_large { 0x95 };

/** The received data rate is too high. */
constexpr reason_code message_rate_too_high { 0x96 };

/** An implementation or administrative imposed limit has been exceeded. */
constexpr reason_code quota_exceeded { 0x97 };

/** The Connection is closed due to an administrative action. */
constexpr reason_code administrative_action { 0x98 };

/** The Payload does not match the specified Payload Format Indicator. */
constexpr reason_code payload_format_invalid { 0x99 };

/** The Server does not support retained messages. */
constexpr reason_code retain_not_supported { 0x9a };

/** The Server does not support the QoS the Client specified or
 it is greater than the Maximum QoS specified. */
constexpr reason_code qos_not_supported { 0x9b };

/** The Client should temporarily use another server. */
constexpr reason_code use_another_server { 0x9c };

/** The Client should permanently use another server. */
constexpr reason_code server_moved { 0x9d };

/** The Server does not support Shared Subscriptions for this Client. */
constexpr reason_code shared_subscriptions_not_supported { 0x9e };

/** The connection rate limit has been exceeded. */
constexpr reason_code connection_rate_exceeded { 0x9f };

/** The maximum connection time authorized for this
 connection has been exceeded. */
constexpr reason_code maximum_connect_time { 0xa0 };

/** The Server does not support Subscription Identifiers. */
constexpr reason_code subscription_ids_not_supported { 0xa1 };

/** The Server does not support Wildcard Subscriptions. */
constexpr reason_code wildcard_subscriptions_not_supported { 0xa2 };

namespace detail {

using enum category;

template <category cat>
std::pair<reason_code*, size_t> valid_codes()
requires (cat == connack) {
	static reason_code valid_codes[] = {
		success, unspecified_error, malformed_packet,
		protocol_error, implementation_specific_error,
		unsupported_protocol_version, client_id_not_valid,
		bad_username_or_password, not_authorized,
		server_unavailable, server_busy, banned,
		bad_authentication_method, topic_name_invalid,
		packet_too_large, quota_exceeded,
		payload_format_invalid, retain_not_supported,
		qos_not_supported, use_another_server,
		server_moved, connection_rate_exceeded
	};
	static size_t len = sizeof(valid_codes) / sizeof(reason_code);
	return std::make_pair(valid_codes, len);
}

template <category cat>
std::pair<reason_code*, size_t> valid_codes()
requires (cat == auth) {
	static reason_code valid_codes[] = {
		success, continue_authentication
	};
	static size_t len = sizeof(valid_codes) / sizeof(reason_code);
	return std::make_pair(valid_codes, len);
}

template <category cat>
std::pair<reason_code*, size_t> valid_codes()
requires (cat == puback || cat == pubrec) {
	static reason_code valid_codes[] = {
		success, no_matching_subscribers, unspecified_error,
		implementation_specific_error, not_authorized,
		topic_name_invalid, packet_id_in_use,
		quota_exceeded, payload_format_invalid
	};
	static size_t len = sizeof(valid_codes) / sizeof(reason_code);
	return std::make_pair(valid_codes, len);
}

template <category cat>
std::pair<reason_code*, size_t> valid_codes()
requires (cat == pubrel || cat == pubcomp) {
	static reason_code valid_codes[] = {
		success, packet_id_not_found
	};
	static size_t len = sizeof(valid_codes) / sizeof(reason_code);
	return std::make_pair(valid_codes, len);
}

template <category cat>
std::pair<reason_code*, size_t> valid_codes()
requires (cat == suback) {
	static reason_code valid_codes[] = {
		granted_qos_0, granted_qos_1, granted_qos_2,
		unspecified_error, implementation_specific_error,
		not_authorized, topic_filter_invalid,
		packet_id_in_use, quota_exceeded,
		shared_subscriptions_not_supported,
		subscription_ids_not_supported,
		wildcard_subscriptions_not_supported
	};
	static size_t len = sizeof(valid_codes) / sizeof(reason_code);
	return std::make_pair(valid_codes, len);
}

template <category cat>
std::pair<reason_code*, size_t> valid_codes()
requires (cat == unsuback) {
	static reason_code valid_codes[] = {
		success, no_subscription_existed,
		unspecified_error, implementation_specific_error,
		not_authorized, topic_filter_invalid,
		packet_id_in_use
	};
	static size_t len = sizeof(valid_codes) / sizeof(reason_code);
	return std::make_pair(valid_codes, len);
}

template <category cat>
std::pair<reason_code*, size_t> valid_codes()
requires (cat == disconnect) {
	static reason_code valid_codes[] = {
		normal_disconnection, unspecified_error,
		malformed_packet,protocol_error,
		implementation_specific_error, not_authorized,
		server_busy, server_shutting_down,
		keep_alive_timeout, session_taken_over,
		topic_filter_invalid, topic_name_invalid,
		receive_maximum_exceeded, topic_alias_invalid,
		packet_too_large,message_rate_too_high,
		quota_exceeded, administrative_action,
		payload_format_invalid, retain_not_supported,
		qos_not_supported, use_another_server,
		server_moved, shared_subscriptions_not_supported,
		connection_rate_exceeded, maximum_connect_time,
		subscription_ids_not_supported,
		wildcard_subscriptions_not_supported
	};
	static size_t len = sizeof(valid_codes) / sizeof(reason_code);
	return std::make_pair(valid_codes, len);
}


} // end namespace detail
} // end namespace reason_codes


template <reason_codes::category cat>
std::optional<reason_code> to_reason_code(uint8_t code) {
	auto [ptr, len] = reason_codes::detail::valid_codes<cat>();
	auto it = std::lower_bound(ptr, ptr + len, reason_code(code));

	if (it->value() == code)
		return *it;
	return std::nullopt;
}

} // end namespace async_mqtt5

namespace boost::system {

template <>
struct is_error_code_enum <async_mqtt5::client::error> : std::true_type {};

} // end namespace boost::system

#endif // !ASYNC_MQTT5_ERROR_HPP
