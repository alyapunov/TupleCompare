#pragma once

#include <cassert>
#include <cstring>

#include <ByteSwap.h>

// Encode helper. Copy byte-swapped value to buffer. Advance buffer pointer to the end of value.
template <class T>
void
mp_write(char *&data, T t)
{
	t = bswap(t);
	memcpy(data, &t, sizeof(t));
	data += sizeof(t);
}

// Decode helper. Copy, byte-swap and return value from buffer. Advance buffer pointer to the end of value.
template <class T>
T
mp_read(const char *&data)
{
	T t;
	memcpy(&t, data, sizeof(t));
	data += sizeof(t);
	return bswap(t);
}

// Encode uint into given data buffer. Move data pointer to the end of encoded data.
inline void
mp_encode_uint(char *&data, uint64_t num)
{
	if (num <= 0x7f) {
		mp_write<uint8_t>(data, num);
	} else if (num <= UINT8_MAX) {
		mp_write<uint8_t>(data, 0xcc);
		mp_write<uint8_t>(data, num);
	} else if (num <= UINT16_MAX) {
		mp_write<uint8_t>(data, 0xcd);
		mp_write<uint16_t>(data, num);
	} else if (num <= UINT32_MAX) {
		mp_write<uint8_t>(data, 0xce);
		mp_write<uint32_t>(data, num);
	} else {
		mp_write<uint8_t>(data, 0xcf);
		mp_write<uint64_t>(data, num);
	}
}

// Decode uint from given data buffer. Move data pointer to the end of decoded data.
inline uint64_t
mp_decode_uint(const char *&data)
{
	uint8_t c = mp_read<uint8_t>(data);
	switch (c) {
		case 0xcc:
			return mp_read<uint8_t>(data);
		case 0xcd:
			return mp_read<uint16_t>(data);
		case 0xce:
			return mp_read<uint32_t>(data);
		case 0xcf:
			return mp_read<uint64_t>(data);
		default:
			assert(c <= 0x7f);
			return c;
	}
}

// Encode string into given data buffer. Move data pointer to the end of encoded data.
inline void
mp_encode_string(char *&data, const char *string, uint32_t len)
{
	if (len <= 31) {
		mp_write<uint8_t>(data, 0xa0 | len);
	} else if (len <= UINT8_MAX) {
		mp_write<uint8_t>(data, 0xd9);
		mp_write<uint8_t>(data, len);
	} else if (len <= UINT16_MAX) {
		mp_write<uint8_t>(data, 0xda);
		mp_write<uint16_t>(data, len);
	} else {
		mp_write<uint8_t>(data, 0xdb);
		mp_write<uint32_t>(data, len);
	}
	memcpy(data, string, len);
	data += len;
}

// Decode string from given data buffer. Move data pointer to the end of decoded data.
inline const char *
mp_decode_string(const char *&data, uint32_t& len)
{
	uint8_t c = mp_read<uint8_t>(data);
	switch (c) {
		case 0xd9:
			len = mp_read<uint8_t>(data);
			break;
		case 0xda:
			len = mp_read<uint16_t>(data);
			break;
		case 0xdb:
			len = mp_read<uint32_t>(data);
			break;
		default:
			assert(c >= 0xa0 && c <= 0xbf);
			len = c & 0x1f;
	}
	const char *string = data;
	data += len;
	return string;
}
