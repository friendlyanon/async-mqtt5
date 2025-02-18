#ifndef ASYNC_MQTT5_CONTROL_PACKET_HPP
#define ASYNC_MQTT5_CONTROL_PACKET_HPP

#include <mutex>
#include <vector>

#include <boost/smart_ptr/allocate_unique.hpp>

#include <async_mqtt5/types.hpp>

namespace async_mqtt5 {


enum class control_code_e : std::uint8_t {
	no_packet = 0b00000000, // 0

	connect = 0b00010000, // 1
	connack = 0b00100000, // 2
	publish = 0b00110000, // 3
	puback = 0b01000000, // 4
	pubrec = 0b01010000, // 5
	pubrel = 0b01100000, // 6
	pubcomp = 0b01110000, // 7
	subscribe = 0b10000000, // 8
	suback = 0b10010000, // 9
	unsubscribe = 0b10100000, // 10
	unsuback = 0b10110000, // 11
	pingreq = 0b11000000, // 12
	pingresp = 0b11010000, // 13
	disconnect = 0b11100000, // 14
	auth = 0b11110000, // 15
};

constexpr struct with_pid_ {} with_pid {};
constexpr struct no_pid_ {} no_pid {};

template <typename Allocator>
class control_packet {
	uint16_t _packet_id;

	using alloc_type = Allocator;
	using deleter = boost::alloc_deleter<std::string, alloc_type>;
	std::unique_ptr<std::string, deleter> _packet;

	control_packet(
		const Allocator& a,
		uint16_t packet_id, std::string packet
	) noexcept :
		_packet_id(packet_id),
		_packet(boost::allocate_unique<std::string>(a, std::move(packet)))
	{}

public:
	control_packet(control_packet&&) noexcept = default;
	control_packet(const control_packet&) noexcept = delete;

	template <
		typename EncodeFun,
		typename ...Args
	>
	static control_packet of(
		with_pid_, const Allocator& alloc,
		EncodeFun&& encode, uint16_t packet_id, Args&&... args
	) {
		return control_packet {
			alloc, packet_id, encode(packet_id, std::forward<Args>(args)...)
		};
	}

	template <
		typename EncodeFun,
		typename ...Args
	>
	static control_packet of(
		no_pid_, const Allocator& alloc,
		EncodeFun&& encode, Args&&... args
	) {
		return control_packet {
			alloc, 0, encode(std::forward<Args>(args)...)
		};
	}

	control_code_e control_code() const {
		return control_code_e(uint8_t(*(_packet->data())) & 0b11110000);
	}

	uint16_t packet_id() const {
		return _packet_id;
	}

	qos_e qos() const {
		assert(control_code() == control_code_e::publish);
		auto byte = (uint8_t(*(_packet->data())) & 0b00000110) >> 1;
		return qos_e(byte);
	}

	control_packet& set_dup() {
		assert(control_code() == control_code_e::publish);
		auto& byte = *(_packet->data());
		byte |= 0b00001000;
		return *this;
	}

	const std::string& wire_data() const {
		return *_packet;
	}
};

class packet_id_allocator {
	struct interval {
		uint16_t start, end;
		interval(uint16_t start, uint16_t end) : start(start), end(end) {}
	};

	std::mutex _mtx;
	std::vector<interval> _free_ids;
	static constexpr uint16_t MAX_PACKET_ID = 65535;

public:
	packet_id_allocator() {
		_free_ids.emplace_back(MAX_PACKET_ID, 0);
	}

	uint16_t allocate() {
		std::lock_guard _(_mtx);
		if (_free_ids.empty()) return 0;
		auto& last = _free_ids.back();
		if (last.start == ++last.end) {
			auto ret = last.end;
			_free_ids.pop_back();
			return ret;
		}
		return last.end;
	}

	void free(uint16_t pid) {
		std::lock_guard _(_mtx);
		auto it = std::upper_bound(
			_free_ids.begin(), _free_ids.end(), pid,
			[](const uint16_t x, const interval& i) { return x > i.start; }
		);
		uint16_t* end_p = nullptr;
		if (it != _free_ids.begin()) {
			auto pit = std::prev(it);
			if (pit->end == pid)
				end_p = &pit->end;
		}
		if (it != _free_ids.end() && pid - 1 == it->start) {
			if (!end_p)
				it->start = pid;
			else {
				*end_p = it->end;
				_free_ids.erase(it);
			}
		} else {
			if (!end_p)
				_free_ids.insert(it, interval(pid, pid - 1));
			else
				*end_p = pid - 1;
		}
	}
};

} // end namespace async_mqtt5

#endif // !ASYNC_MQTT5_CONTROL_PACKET_HPP
