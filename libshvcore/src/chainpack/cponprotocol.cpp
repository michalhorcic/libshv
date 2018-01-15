#include "cponprotocol.h"

#include "../core/utils.h"

#include <limits>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace shv {
namespace core {
namespace chainpack {

//==============================================================
// Parsing
//==============================================================
enum {exception_aborts = 0};
//enum {exception_aborts = 1};

#define PARSE_EXCEPTION(msg) {\
	std::clog << __FILE__ << ':' << __LINE__; \
	if(exception_aborts) { \
		std::clog << ' ' << (msg) << std::endl; \
		abort(); \
	} \
	else { \
		std::clog << ' ' << (msg) << std::endl; \
		throw CponProtocol::ParseException(msg, m_pos); \
	} \
}

static const int MAX_RECURSION_DEPTH = 1000;

static inline std::string dump_char(char c)
{
	char buf[12];
	if (static_cast<uint8_t>(c) >= 0x20 && static_cast<uint8_t>(c) <= 0x7f) {
		snprintf(buf, sizeof buf, "'%c' (%d)", c, c);
	}
	else {
		snprintf(buf, sizeof buf, "(%d)", c);
	}
	return std::string(buf);
}

static inline bool in_range(long x, long lower, long upper)
{
	return (x >= lower && x <= upper);
}

static inline bool starts_with(const std::string &str, size_t pos, const std::string &prefix)
{
	return std::equal(prefix.begin(), prefix.end(), str.begin() + pos);
}

static inline bool starts_with(const std::string &str, size_t pos, const char prefix)
{
	return (str.begin() + pos) != str.end() && *(str.begin() + pos) == prefix;
}

CponProtocol::CponProtocol(const std::string &str)
	: m_str(str)
{
}

RpcValue CponProtocol::parse(const std::string &in)
{
	CponProtocol parser{in};
	RpcValue result = parser.parseAtPos();
	return result;
}

namespace {
class DepthScope
{
public:
	DepthScope(int &depth) : m_depth(depth) {m_depth++;}
	~DepthScope() {m_depth--;}
private:
	int &m_depth;
};
}
RpcValue CponProtocol::parseAtPos()
{
	if (m_depth > MAX_RECURSION_DEPTH)
		PARSE_EXCEPTION("maximum nesting depth exceeded");
	DepthScope{m_depth};

	RpcValue ret_val;
	RpcValue::MetaData meta_data;

	skipGarbage();
	if(parseMetaData(meta_data))
		skipGarbage();
	do {
		if(parseString(ret_val)) { break; }
		else if(parseNumber(ret_val)) { break; }
		else if(parseNull(ret_val)) { break; }
		else if(parseBool(ret_val)) { break; }
		else if(parseList(ret_val)) { break; }
		else if(parseMap(ret_val)) { break; }
		else if(parseIMap(ret_val)) { break; }
		else if(parseArray(ret_val)) { break; }
		else if(parseBlob(ret_val)) { break; }
		else if(parseDateTime(ret_val)) { break; }
		else
			PARSE_EXCEPTION("unknown type: " + m_str.substr(m_pos, 40));
	} while(false);
	if(!meta_data.isEmpty())
		ret_val.setMetaData(std::move(meta_data));
	return ret_val;
}

void CponProtocol::skipWhiteSpace()
{
	while (m_str[m_pos] == ' ' || m_str[m_pos] == '\r' || m_str[m_pos] == '\n' || m_str[m_pos] == '\t')
		m_pos++;
}

bool CponProtocol::skipComment()
{
	bool comment_found = false;
	if (m_str[m_pos] == '/') {
		m_pos++;
		if (m_pos == m_str.size())
			PARSE_EXCEPTION("unexpected end of input after start of comment");
		if (m_str[m_pos] == '/') { // inline comment
			m_pos++;
			// advance until next line, or end of input
			while (m_pos < m_str.size() && m_str[m_pos] != '\n') {
				m_pos++;
			}
			comment_found = true;
		}
		else if (m_str[m_pos] == '*') { // multiline comment
			m_pos++;
			if (m_pos > m_str.size()-2)
				PARSE_EXCEPTION("unexpected end of input inside multi-line comment");
			// advance until closing tokens
			while (!(m_str[m_pos] == '*' && m_str[m_pos+1] == '/')) {
				m_pos++;
				if (m_pos > m_str.size()-2)
					PARSE_EXCEPTION( "unexpected end of input inside multi-line comment");
			}
			m_pos += 2;
			comment_found = true;
		}
		else {
			PARSE_EXCEPTION("malformed comment");
		}
	}
	return comment_found;
}

char CponProtocol::skipGarbage()
{
	if (m_pos >= m_str.size())
		PARSE_EXCEPTION("unexpected end of input");
	skipWhiteSpace();
	{
		bool comment_found = false;
		do {
			comment_found = skipComment();
			skipWhiteSpace();
		}
		while(comment_found);
	}
	return m_str[m_pos];
}

char CponProtocol::nextValidChar()
{
	m_pos++;
	skipGarbage();
	return m_str[m_pos];
}

char CponProtocol::currentChar()
{
	if (m_pos >= m_str.size())
		PARSE_EXCEPTION("unexpected end of input");
	return m_str[m_pos];
}

bool CponProtocol::parseMetaData(RpcValue::MetaData &meta_data)
{
	char ch = m_str[m_pos];
	if(ch != '<')
		return false;
	ch = nextValidChar();
	RpcValue::IMap im = parseIMapContent();
	ch = skipGarbage();
	if(ch != '>')
		PARSE_EXCEPTION("closing MetaData '>' missing");
	meta_data = RpcValue::MetaData(std::move(im));
	m_pos++;
	return true;
}

bool CponProtocol::parseNull(RpcValue &val)
{
	static const std::string S_NULL("null");
	if(starts_with(m_str, m_pos, S_NULL)) {
		m_pos += S_NULL.size();
		val = RpcValue(nullptr);
		return true;
	}
	return false;
}

bool CponProtocol::parseBool(RpcValue &val)
{
	static const std::string S_TRUE("true");
	static const std::string S_FALSE("false");
	if(starts_with(m_str, m_pos, S_TRUE)) {
		m_pos += S_TRUE.size();
		val = RpcValue(true);
		return true;
	}
	if(starts_with(m_str, m_pos, S_FALSE)) {
		m_pos += S_FALSE.size();
		val = RpcValue(false);
		return true;
	}
	return false;
}

bool CponProtocol::parseString(RpcValue &val)
{
	std::string s;
	if(parseStringHelper(s)) {
		val = s;
		return true;
	}
	return false;
}

bool CponProtocol::parseBlob(RpcValue &val)
{
	char ch = currentChar();
	if (ch != 'x')
		return false;
	m_pos++;
	std::string s;
	if(parseStringHelper(s)) {
		s = shv::core::Utils::fromHex(s);
		val = RpcValue::Blob(s);
		return true;
	}
	return false;
}

bool CponProtocol::parseNumber(RpcValue &val)
{
	int sign = 1;
	uint64_t int_part = 0;
	uint64_t dec_part = 0;
	int dec_cnt = 0;
	int radix = 10;
	int exponent = std::numeric_limits<int>::max();
	RpcValue::Type type = RpcValue::Type::Invalid;

	bool is_number = false;
	char ch = currentChar();
	if (ch == '-') {
		sign = -1;
		ch = nextValidChar();
	}
	if (starts_with(m_str, m_pos, "0x")) {
		radix = 16;
		nextValidChar();
		ch = nextValidChar();
		is_number = true;
	}
	else if (in_range(ch, '0', '9')) {
		is_number = true;
	}
	if(!is_number)
		return false;
	if (in_range(ch, '0', '9')) {
		type = RpcValue::Type::Int;
		size_t start_pos = m_pos;
		int_part = parseDecimalUnsigned(radix);
		if (m_pos == start_pos)
			PARSE_EXCEPTION("number integer part missing");
		if (starts_with(m_str, m_pos, 'u')) {
			type = RpcValue::Type::UInt;
			m_pos++;
		}
	}
	if (starts_with(m_str, m_pos, '.')) {
		m_pos++;
		size_t start_pos = m_pos;
		dec_part = parseDecimalUnsigned(radix);
		type = RpcValue::Type::Double;
		dec_cnt = m_pos - start_pos;
		if (starts_with(m_str, m_pos, 'n')) {
			type = RpcValue::Type::Decimal;
			m_pos++;
		}
	}
	else if(starts_with(m_str, m_pos, 'e') || starts_with(m_str, m_pos, 'E')) {
		bool neg = false;
		if (starts_with(m_str, m_pos, '-')) {
			neg = true;
			m_pos++;
		}
		size_t start_pos = m_pos;
		exponent = parseDecimalUnsigned(radix);
		if (m_pos == start_pos)
			PARSE_EXCEPTION("double exponent part missing");
		exponent *= neg;
		type = RpcValue::Type::Double;
	}

	switch (type) {
	case RpcValue::Type::Int:
		val = RpcValue((RpcValue::Int)(int_part * sign));
		break;
	case RpcValue::Type::UInt:
		val = RpcValue((RpcValue::UInt)int_part);
		break;
	case RpcValue::Type::Decimal: {
		int64_t n = int_part * sign;
		for (int i = 0; i < dec_cnt; ++i)
			n *= 10;
		n += dec_part;
		val = RpcValue(RpcValue::Decimal(n, dec_cnt));
		break;
	}
	case RpcValue::Type::Double: {
		double d = 0;
		if(exponent == std::numeric_limits<int>::max()) {
			d = dec_part;
			for (int i = 0; i < dec_cnt; ++i)
				d /= 10;
			d += int_part;
		}
		else {
			d = int_part;
			d *= std::pow(10, exponent);
		}
		val = RpcValue(d * sign);
		break;
	}
	default:
		PARSE_EXCEPTION("number parse error");
	}
	return true;
}

bool CponProtocol::parseList(RpcValue &val)
{
	char ch = currentChar();
	if (ch != '[')
		return false;
	nextValidChar();
	RpcValue::List lst;
	while (true) {
		lst.push_back(parseAtPos());
		ch = skipGarbage();
		if (ch == ',') {
			nextValidChar();
			continue;
		}
		if (ch == ']') {
			m_pos++;
			val = lst;
			return true;
		}
		PARSE_EXCEPTION("expected ',' in list, got " + dump_char(ch));
	}
	return false;
}

bool CponProtocol::parseMap(RpcValue &val)
{
	char ch = currentChar();
	if (ch != '{')
		return false;
	nextValidChar();
	RpcValue::Map map;
	while (true) {
		std::string key;
		if(!parseStringHelper(key))
			PARSE_EXCEPTION("expected string key, got " + dump_char(ch));
		ch = skipGarbage();
		if (ch != ':')
			PARSE_EXCEPTION("expected ':' in Map, got " + dump_char(ch));
		m_pos++;
		RpcValue key_val = parseAtPos();
		map[key] = key_val;
		ch = skipGarbage();
		if (ch == ',') {
			nextValidChar();
			continue;
		}
		if (ch == '}') {
			m_pos++;
			val = map;
			return true;
		}
		PARSE_EXCEPTION("unexpected delimiter in IMap, got " + dump_char(ch));
	}
	return false;
}

bool CponProtocol::parseIMap(RpcValue &val)
{
	static const std::string S_IMAP("i{");
	if (!starts_with(m_str, m_pos, S_IMAP))
		return false;
	m_pos += S_IMAP.size();
	skipGarbage();
	RpcValue::IMap imap = parseIMapContent();
	char ch = skipGarbage();
	if (ch != '}')
		PARSE_EXCEPTION("unexpected closing bracket in IMap, got " + dump_char(ch));
	m_pos++;
	val = imap;
	return true;
}

bool CponProtocol::parseArray(RpcValue &ret_val)
{
	static const std::string S_ARRAY("a[");
	if(!starts_with(m_str, m_pos, S_ARRAY))
		return false;
	m_pos += S_ARRAY.size();

	RpcValue::Array arr;
	while (true) {
		RpcValue val = parseAtPos();
		if(arr.empty()) {
			arr = RpcValue::Array(val.type());
		}
		else {
			if(val.type() != arr.type())
				PARSE_EXCEPTION("Mixed types in Array");
		}
		arr.push_back(RpcValue::Array::makeElement(val));
		char ch = skipGarbage();
		if (ch == ',') {
			nextValidChar();
			continue;
		}
		if (ch == ']') {
			m_pos++;
			ret_val = arr;
			return true;
		}
		PARSE_EXCEPTION("unexpected closing bracket in Array, got " + dump_char(ch));
	}
	return false;
}

bool CponProtocol::parseDateTime(RpcValue &val)
{
	static const std::string S_DATETIME("d\"");
	if(!starts_with(m_str, m_pos, S_DATETIME))
		return false;
	m_pos += 1;
	std::string s;
	if(parseStringHelper(s)) {
		val = RpcValue::DateTime::fromUtcString(s);
		return true;
	}
	PARSE_EXCEPTION("error parsing DateTime");
}

uint64_t CponProtocol::parseDecimalUnsigned(int radix)
{
	uint64_t ret = 0;
	while (m_pos < m_str.size()) {
		char ch = m_str[m_pos];
		if(in_range(ch, '0', '9')) {
			ret *= radix;
			ret += (ch - '0');
			m_pos++;
		}
		else {
			break;
		}
	}
	return ret;
}

RpcValue::IMap CponProtocol::parseIMapContent()
{
	RpcValue::IMap map;
	while (true) {
		RpcValue v;
		if(!parseNumber(v))
			PARSE_EXCEPTION("number key expected");
		if(!(v.type() == RpcValue::Type::Int || v.type() == RpcValue::Type::UInt))
			PARSE_EXCEPTION("int key expected");
		RpcValue::UInt key = v.toUInt();
		char ch = skipGarbage();
		if (ch != ':')
			PARSE_EXCEPTION("expected ':' in IMap, got " + dump_char(ch));
		m_pos++;
		RpcValue val = parseAtPos();
		map[key] = val;
		ch = skipGarbage();
		if (ch == ',') {
			nextValidChar();
		}
		else {
			break;
		}
	}
	return map;
}

bool CponProtocol::parseStringHelper(std::string &val)
{
	char ch = currentChar();
	if (ch != '"')
		return false;

	m_pos++;
	std::string str_val;
	long last_escaped_codepoint = -1;
	while (true) {
		if (m_pos >= m_str.size())
			PARSE_EXCEPTION("unexpected end of input in string");

		ch = m_str[m_pos++];

		if (ch == '"') {
			encodeUtf8(last_escaped_codepoint, str_val);
			val = str_val;
			return true;
		}

		if (in_range(ch, 0, 0x1f))
			PARSE_EXCEPTION("unescaped " + dump_char(ch) + " in string");

		// The usual case: non-escaped characters
		if (ch != '\\') {
			encodeUtf8(last_escaped_codepoint, str_val);
			last_escaped_codepoint = -1;
			str_val += ch;
			continue;
		}

		// Handle escapes
		if (m_pos == m_str.size())
			PARSE_EXCEPTION("unexpected end of input in string");

		ch = m_str[m_pos++];

		if (ch == 'u') {
			// Extract 4-byte escape sequence
			std::string esc = m_str.substr(m_pos, 4);
			// Explicitly check length of the substring. The following loop
			// relies on std::string returning the terminating NUL when
			// accessing str[length]. Checking here reduces brittleness.
			if (esc.length() < 4) {
				PARSE_EXCEPTION("bad \\u escape: " + esc);
			}
			for (size_t j = 0; j < 4; j++) {
				if (!in_range(esc[j], 'a', 'f') && !in_range(esc[j], 'A', 'F')
					&& !in_range(esc[j], '0', '9'))
					PARSE_EXCEPTION("bad \\u escape: " + esc);
			}

			long codepoint = strtol(esc.data(), nullptr, 16);

			// JSON specifies that characters outside the BMP shall be encoded as a pair
			// of 4-hex-digit \u escapes encoding their surrogate pair components. Check
			// whether we're in the middle of such a beast: the previous codepoint was an
			// escaped lead (high) surrogate, and this is a trail (low) surrogate.
			if (in_range(last_escaped_codepoint, 0xD800, 0xDBFF)
				&& in_range(codepoint, 0xDC00, 0xDFFF)) {
				// Reassemble the two surrogate pairs into one astral-plane character, per
				// the UTF-16 algorithm.
				encodeUtf8((((last_escaped_codepoint - 0xD800) << 10)
							 | (codepoint - 0xDC00)) + 0x10000, str_val);
				last_escaped_codepoint = -1;
			} else {
				encodeUtf8(last_escaped_codepoint, str_val);
				last_escaped_codepoint = codepoint;
			}

			m_pos += 4;
			continue;
		}

		encodeUtf8(last_escaped_codepoint, str_val);
		last_escaped_codepoint = -1;

		if (ch == 'b') {
			str_val += '\b';
		} else if (ch == 'f') {
			str_val += '\f';
		} else if (ch == 'n') {
			str_val += '\n';
		} else if (ch == 'r') {
			str_val += '\r';
		} else if (ch == 't') {
			str_val += '\t';
		} else if (ch == '"' || ch == '\\' || ch == '/') {
			str_val += ch;
		} else {
			PARSE_EXCEPTION("invalid escape character " + dump_char(ch));
		}
	}
}

//==============================================================
// Serialization
//==============================================================

void CponProtocol::encodeUtf8(long pt, std::string & out)
{
	if (pt < 0)
		return;

	if (pt < 0x80) {
		out += static_cast<char>(pt);
	} else if (pt < 0x800) {
		out += static_cast<char>((pt >> 6) | 0xC0);
		out += static_cast<char>((pt & 0x3F) | 0x80);
	} else if (pt < 0x10000) {
		out += static_cast<char>((pt >> 12) | 0xE0);
		out += static_cast<char>(((pt >> 6) & 0x3F) | 0x80);
		out += static_cast<char>((pt & 0x3F) | 0x80);
	} else {
		out += static_cast<char>((pt >> 18) | 0xF0);
		out += static_cast<char>(((pt >> 12) & 0x3F) | 0x80);
		out += static_cast<char>(((pt >> 6) & 0x3F) | 0x80);
		out += static_cast<char>((pt & 0x3F) | 0x80);
	}
}

void CponProtocol::serialize(std::nullptr_t, std::string &out)
{
	out += "null";
}

void CponProtocol::serialize(double value, std::string &out)
{
	if (std::isfinite(value)) {
		char buf[32];
		snprintf(buf, sizeof buf, "%.17g", value);
		out += buf;
	}
	else {
		out += "null";
	}
}

void CponProtocol::serialize(RpcValue::Int value, std::string &out)
{
	out += shv::core::Utils::toString(value);
}

void CponProtocol::serialize(RpcValue::UInt value, std::string &out)
{
	out += shv::core::Utils::toString(value);
}

void CponProtocol::serialize(bool value, std::string &out)
{
	out += value ? "true" : "false";
}

void CponProtocol::serialize(RpcValue::DateTime value, std::string &out)
{
	out += value.toString();
}

void CponProtocol::serialize(RpcValue::Decimal value, std::string &out)
{
	out += value.toString();
}

void CponProtocol::serialize(const std::string &value, std::string &out)
{
	out += '"';
	for (size_t i = 0; i < value.length(); i++) {
		const char ch = value[i];
		if (ch == '\\') {
			out += "\\\\";
		} else if (ch == '"') {
			out += "\\\"";
		} else if (ch == '\b') {
			out += "\\b";
		} else if (ch == '\f') {
			out += "\\f";
		} else if (ch == '\n') {
			out += "\\n";
		} else if (ch == '\r') {
			out += "\\r";
		} else if (ch == '\t') {
			out += "\\t";
		} else if (static_cast<uint8_t>(ch) <= 0x1f) {
			char buf[8];
			snprintf(buf, sizeof buf, "\\u%04x", ch);
			out += buf;
		} else if (static_cast<uint8_t>(ch) == 0xe2 && static_cast<uint8_t>(value[i+1]) == 0x80
				 && static_cast<uint8_t>(value[i+2]) == 0xa8) {
			out += "\\u2028";
			i += 2;
		} else if (static_cast<uint8_t>(ch) == 0xe2 && static_cast<uint8_t>(value[i+1]) == 0x80
				 && static_cast<uint8_t>(value[i+2]) == 0xa9) {
			out += "\\u2029";
			i += 2;
		} else {
			out += ch;
		}
	}
	out += '"';
}

void CponProtocol::serialize(const RpcValue::Blob &value, std::string &out)
{
	std::string s = value.toString();
	serialize(s, out);
}

void CponProtocol::serialize(const RpcValue::List &values, std::string &out)
{
	bool first = true;
	out += "[";
	for (const auto &value : values) {
		if (!first)
			out += ", ";
		value.dumpJson(out);
		first = false;
	}
	out += "]";
}

void CponProtocol::serialize(const RpcValue::Array &values, std::string &out)
{
	out += "[";
	for (size_t i = 0; i < values.size(); ++i) {
		if (i > 0)
			out += ", ";
		values.valueAt(i).dumpJson(out);
	}
	out += "]";
}

void CponProtocol::serialize(const RpcValue::Map &values, std::string &out)
{
	bool first = true;
	out += "{";
	for (const auto &kv : values) {
		if (!first)
			out += ", ";
		serialize(kv.first, out);
		out += ": ";
		kv.second.dumpJson(out);
		first = false;
	}
	out += "}";
}

void CponProtocol::serialize(const RpcValue::IMap &values, std::string &out)
{
	bool first = true;
	out += "{";
	for (const auto &kv : values) {
		if (!first)
			out += ", ";
		serialize(kv.first, out);
		out += ": ";
		kv.second.dumpJson(out);
		first = false;
	}
	out += "}";
}

}}}