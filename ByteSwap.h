#pragma once

// Simple byte swap
uint8_t
inline bswap(uint8_t t)
{
	return t;
}

uint16_t
inline bswap(uint16_t t)
{
#ifdef _WIN32
	return _byteswap_ushort(t);
#else
	return __builtin_bswap16(t);
#endif
}

uint32_t
inline bswap(uint32_t t)
{
#ifdef _WIN32
	return _byteswap_ulong(t);
#else
	return __builtin_bswap32(t);
#endif
}

uint64_t
inline bswap(uint64_t t)
{
#ifdef _WIN32
	return _byteswap_uint64(t);
#else
	return __builtin_bswap64(t);
#endif
}
