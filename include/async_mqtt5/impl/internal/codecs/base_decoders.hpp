#ifndef ASYNC_MQTT5_BASE_DECODERS_HPP
#define ASYNC_MQTT5_BASE_DECODERS_HPP

#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/binary/binary.hpp>

#include <boost/fusion/adapted/std_tuple.hpp>

#include <async_mqtt5/property_types.hpp>

#include <async_mqtt5/impl/internal/codecs/traits.hpp>

namespace async_mqtt5::decoders {

namespace x3 = boost::spirit::x3;

template <typename T>
struct convert { using type = T; };

template <typename ...Args>
struct convert<boost::fusion::deque<Args...>> {
	using type = std::tuple<typename convert<Args>::type...>;
};

template <typename T>
struct convert<boost::optional<T>> {
	using type = std::optional<T>;
};

template <typename T>
struct convert<std::vector<T>> {
	using type = std::vector<typename convert<T>::type>;
};

template <typename T, typename Parser>
constexpr auto as(Parser&& p) {
	return x3::rule<struct _, T>{} = std::forward<Parser>(p);
}


template <typename It, typename Parser>
auto type_parse(It& first, const It last, const Parser& p) {
	using ctx_type = decltype(x3::make_context<struct _>(std::declval<Parser&>()));
	using attr_type = typename x3::traits::attribute_of<Parser, ctx_type>::type;

	using rv_type = typename convert<attr_type>::type;

	std::optional<rv_type> rv;
	rv_type value {};
	if (x3::phrase_parse(first, last, as<rv_type>(p), x3::eps(false), value))
		rv = std::move(value);
	return rv;
}
	

template <typename AttributeType, typename It, typename Parser>
auto type_parse(It& first, const It last, const Parser& p) {
	std::optional<AttributeType> rv;
	AttributeType value {};
	if (x3::phrase_parse(first, last, as<AttributeType>(p), x3::eps(false), value))
		rv = std::move(value);
	return rv;
}

namespace basic {

template <typename T>
constexpr auto to(T& arg) {
	return [&](auto& ctx) {
		using ctx_type = decltype(ctx);
		using attr_type = decltype(x3::_attr(std::declval<const ctx_type&>()));
		if constexpr (is_boost_iterator<attr_type>)
			arg = T { x3::_attr(ctx).begin(), x3::_attr(ctx).end() };
		else
			arg = x3::_attr(ctx); 
	};
}

template <typename LenParser, typename Subject>
class scope_limit {};

template <typename LenParser, typename Subject>
requires (x3::traits::is_parser<LenParser>::value)
class scope_limit<LenParser, Subject> : 
	public x3::unary_parser<Subject, scope_limit<LenParser, Subject>> {

	using base_type = x3::unary_parser<Subject, scope_limit<LenParser, Subject>>;
	LenParser _lp;
public:
	using ctx_type = decltype(x3::make_context<struct _>(std::declval<Subject&>()));
	using attribute_type = typename x3::traits::attribute_of<Subject, ctx_type>::type;
	static bool const has_attribute = true;

	scope_limit(const LenParser& lp, const Subject& subject) : base_type(subject), _lp(lp) {}

	template <typename It, typename Ctx, typename RCtx, typename Attr>
	bool parse(It& first, const It last, const Ctx& ctx, RCtx& rctx, Attr& attr) const {

		It iter = first;
		typename x3::traits::attribute_of<LenParser, Ctx>::type len;
		if (!_lp.parse(iter, last, ctx, rctx, len))
			return false;
		if (iter + len > last)
			return false;

		if (!base_type::subject.parse(iter, iter + len, ctx, rctx, attr))
			return false;

		first = iter;
		return true;
	}
};

template <typename Size, typename Subject>
requires (std::is_arithmetic_v<Size>)
class scope_limit<Size, Subject> : 
	public x3::unary_parser<Subject, scope_limit<Size, Subject>> {
	
	using base_type = x3::unary_parser<Subject, scope_limit<Size, Subject>>;
	size_t _limit;
public:
	using ctx_type = decltype(x3::make_context<struct _>(std::declval<Subject&>()));
	using attribute_type = typename x3::traits::attribute_of<Subject, ctx_type>::type;
	static bool const has_attribute = true;

	scope_limit(Size limit, const Subject& subject) : base_type(subject), _limit(limit) {}

	template <typename It, typename Ctx, typename RCtx, typename Attr>
	bool parse(It& first, const It last, const Ctx& ctx, RCtx& rctx, Attr& attr) const {

		It iter = first;
		if (iter + _limit > last)
			return false;

		if (!base_type::subject.parse(iter, iter + _limit, ctx, rctx, attr))
			return false;

		first = iter;
		return true;
	}
};

template <typename LenParser>
struct scope_limit_gen {
	template <typename Subject>
	auto operator[](const Subject& p) const {
		return scope_limit<LenParser, Subject> { _lp, x3::as_parser(p) };
	}
	LenParser _lp;
};

template <typename Size>
requires (std::is_arithmetic_v<Size>)
struct scope_limit_gen<Size> {
	template <typename Subject>
	auto operator[](const Subject& p) const {
		return scope_limit<Size, Subject> { limit, x3::as_parser(p) };
	}
	Size limit;
};

template <typename Parser>
requires (x3::traits::is_parser<Parser>::value)
scope_limit_gen<Parser> scope_limit_(const Parser& p) {
	return { p };
}

template <typename Size>
requires (std::is_arithmetic_v<Size>)
scope_limit_gen<Size> scope_limit_(Size limit) {
	return { limit };
}


struct verbatim_parser : x3::parser<verbatim_parser> {
	using attribute_type = std::string;
	static bool const has_attribute = true;

	template <typename It, typename Ctx, typename RCtx, typename Attr>
	bool parse(It& first, const It last, const Ctx&, RCtx&, Attr& attr) const {
		attr = std::string { first, last };
		first = last;
		return true;
	}
};

constexpr auto verbatim_ = verbatim_parser{};

struct varint_parser : x3::parser<varint_parser> {
	using attribute_type = uint32_t;
	static bool const has_attribute = true;

	template <typename It, typename Ctx, typename RCtx, typename Attr>
	bool parse(It& first, const It last, const Ctx& ctx, RCtx& rctx, Attr& attr) const {

		It iter = first;
		x3::skip_over(iter, last, ctx);

		if (iter == last)
			return false;

		uint32_t result = 0; unsigned bit_shift = 0;

		for (; iter != last && bit_shift < sizeof(int32_t) * 7; ++iter) {
			auto val = *iter;
			if (val & 0b1000'0000u) {
				result |= (val & 0b0111'1111u) << bit_shift;
				bit_shift += 7;
			}
			else {
				result |= (static_cast<int32_t>(val) << bit_shift);
				bit_shift = 0;
				break;
			}
		}
		if (bit_shift)
			return false;

		attr = result;
		first = ++iter;
		return true;
	}
};

constexpr varint_parser varint_{};

struct len_prefix_parser : x3::parser<len_prefix_parser> {
	using attribute_type = std::string;
	static bool const has_attribute = true;

	template <typename It, typename Ctx, typename RCtx, typename Attr>
	bool parse(It& first, const It last, const Ctx& ctx, RCtx& rctx, Attr& attr) const {
		It iter = first;
		x3::skip_over(iter, last, ctx);

		typename x3::traits::attribute_of<decltype(x3::big_word), Ctx>::type len;
		if (x3::big_word.parse(iter, last, ctx, rctx, len)) {
			if (std::distance(iter, last) < len)
				return false;
		}
		else 
			return false;

		attr = std::string(iter, iter + len);
		first = iter + len;
		return true;
	}
};

constexpr len_prefix_parser utf8_{};
constexpr len_prefix_parser binary_{};

/*
	 Boost Spirit incorrectly deduces atribute type for a parser of the form 
		 (eps(a) | parser1) >> (eps(b) | parser)
	and we had to create if_ parser to remedy the issue 
*/

template <typename Subject>
class conditional_parser : public x3::unary_parser<Subject, conditional_parser<Subject>> {
	using base_type = x3::unary_parser<Subject, conditional_parser<Subject>>;
	bool _condition;
public:
	using ctx_type = decltype(x3::make_context<struct _>(std::declval<Subject&>()));
	using subject_attr_type = typename x3::traits::attribute_of<Subject, ctx_type>::type;

	using attribute_type = boost::optional<subject_attr_type>;
	static bool const has_attribute = true;

	conditional_parser(const Subject& s, bool condition) : base_type(s), _condition(condition) {}

	template <typename It, typename Ctx, typename RCtx, typename Attr>
	bool parse(It& first, const It last, const Ctx& ctx, RCtx& rctx, Attr& attr) const {
		if (!_condition)
			return true;

		It iter = first;
		subject_attr_type sattr {};
		if (!base_type::subject.parse(iter, last, ctx, rctx, sattr))
			return false;

		attr.emplace(std::move(sattr));
		first = iter;
		return true;
	}
};

struct conditional_gen {
	bool _condition;

	template <typename Subject>
	auto operator[](const Subject& p) const {
		return conditional_parser<Subject> { p, _condition };
	}
};

inline conditional_gen if_(bool condition) {
	return { condition };
}

} // end namespace basic


namespace prop {

namespace basic = async_mqtt5::decoders::basic;

namespace detail {

template <typename It, typename Ctx, typename RCtx, typename Prop>
bool parse_to_prop(It& iter, const It last, const Ctx& ctx, RCtx& rctx, Prop& prop) {
	using prop_type = decltype(prop);

	bool rv = false;
	if constexpr (is_optional<prop_type>) {
		using value_type = typename std::remove_reference_t<prop_type>::value_type;
		if constexpr (std::is_same_v<value_type, uint8_t>) {
			uint8_t attr;
			rv = x3::byte_.parse(iter, last, ctx, rctx, attr);
			prop = attr;
		}
		if constexpr (std::is_same_v<value_type, int16_t>) {
			int16_t attr;
			rv = x3::big_word.parse(iter, last, ctx, rctx, attr);
			prop = attr;
		}
		if constexpr (std::is_same_v<value_type, uint16_t>) {
			uint16_t attr;
			rv = x3::big_word.parse(iter, last, ctx, rctx, attr);
			prop = attr;
		}
		if constexpr (std::is_same_v<value_type, int32_t>) {
			int32_t attr;
			rv = x3::big_dword.parse(iter, last, ctx, rctx, attr);
			prop = attr;
		}
		if constexpr (std::is_same_v<value_type, uint32_t>) {
			uint32_t attr;
			rv = basic::varint_.parse(iter, last, ctx, rctx, attr);
			prop = attr;
		}
		if constexpr (std::is_same_v<value_type, std::string>) {
			std::string attr;
			rv = basic::utf8_.parse(iter, last, ctx, rctx, attr);
			prop.emplace(std::move(attr));
		}
	}

	if constexpr (async_mqtt5::is_vector<prop_type>) {
		std::string value;
		rv = basic::utf8_.parse(iter, last, ctx, rctx, value);
		if (rv) prop.push_back(std::move(value));
	}
	return rv;
}

} // end namespace detail

template <typename Props>
class prop_parser : public x3::parser<prop_parser<Props>> {
public:
	using attribute_type = Props;
	static bool const has_attribute = true;

	template <typename It, typename Ctx, typename RCtx, typename Attr>
	bool parse(It& first, const It last, const Ctx& ctx, RCtx& rctx, Attr& attr) const {

		It iter = first;
		x3::skip_over(iter, last, ctx);

		if (iter == last)
			return true;

		uint32_t props_length;
		if (!basic::varint_.parse(iter, last, ctx, rctx, props_length))
			return false;

		const It scoped_last = iter + props_length;
		// attr = Props{};

		while (iter < scoped_last) {
			uint8_t prop_id = *iter++; 
			bool rv = true;
			It saved = iter;

			attr.apply_on(prop_id, [&rv, &iter, scoped_last, &ctx, &rctx](auto& prop) {
				rv = detail::parse_to_prop(iter, scoped_last, ctx, rctx, prop);
			});

			// either rv = false or property with prop_id was not found
			if (!rv || iter == saved)
				break;
		}

		first = iter;
		return true;
	}
};


template <typename Props>
constexpr auto props_ = prop_parser<Props>{};

} // end namespace prop


} // end namespace async_mqtt5::decoders

#endif // !ASYNC_MQTT5_BASE_DECODERS_HPP
