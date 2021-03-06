#include "bits.h"
#include <assert.h>

#define BIT_NUM_8	8
//#define BIT_NUM (sizeof(char)*8)

void bits_init(struct bits_t* bits, const void* data, size_t bytes)
{
	bits->data = (const uint8_t*)data;
	bits->bytes = bytes;
	bits->offsetBits = 0;
	bits->offsetBytes = 0;
}

static inline void bitstream_move(struct bits_t* bits, int n)
{
	bits->offsetBytes += (bits->offsetBits + n) / BIT_NUM_8;
	bits->offsetBits = (bits->offsetBits + n) % BIT_NUM_8;
}

int bits_next(struct bits_t* bits)
{
	uint8_t bit;
	assert(bits && bits->data && bits->bytes > 0);
	if (bits->offsetBytes >= bits->bytes)
		return -1; // throw exception

	bit = bits->data[bits->offsetBytes] & (0x80U >> bits->offsetBits);
	return bit ? 1 : 0;
}

int bits_next2(struct bits_t* bits, int n)
{
	int i, v;

	assert(n > 0 && n <= 32);
	assert(bits && bits->data && bits->bytes > 0);
	if (bits->offsetBytes >= bits->bytes)
		return -1; // throw exception

	v = bits->data[bits->offsetBytes] & (0xFFU >> bits->offsetBits); // remain valid value
	if (n <= BIT_NUM_8 - bits->offsetBits)
		return v >> (BIT_NUM_8 - bits->offsetBits - n); // shift right value

	n -= BIT_NUM_8 - bits->offsetBits;
	for (i = 1; n >= BIT_NUM_8 && bits->offsetBytes + i < bits->bytes; i++)
	{
		v <<= 8;
		v += bits->data[bits->offsetBytes + i];
		n -= 8;
	}

	if (n > 0 && bits->offsetBytes + i >= bits->bytes)
		return -1;

	if (n > 0)
	{
		v <<= n;
		v += bits->data[bits->offsetBytes + i] >> (BIT_NUM_8 - n);
	}
	return v;
}

int bits_read(struct bits_t* bits)
{
	int bit;
	bit = bits_next(bits);
	bitstream_move(bits, 1); // update offset
	return bit;
}

int bits_read2(struct bits_t* bits, int n)
{
	int bit;
	bit = bits_next2(bits, n);
	bitstream_move(bits, n); // update offset
	return bit;
}

#if defined(_DEBUG) || defined(DEBUG)
void bits_test(void)
{
	struct bits_t bits;
	const uint8_t data[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xCD, 0xAB };
	bits_init(&bits, data, sizeof(data));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(1 == bits_read(&bits));

	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(1 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(0 == bits_read(&bits));
	assert(1 == bits_read(&bits));
//	assert(1 == bits_read(&bits));

	assert(0x05 == bits_read2(&bits, 3));
	assert(0x15 == bits_read2(&bits, 8));
	assert(0x27 == bits_read2(&bits, 6));
	assert(0x08 == bits_read2(&bits, 4));
	assert(0x09ABCDEF == bits_read2(&bits, 28));

	assert(-1 == bits_read2(&bits, 17));
}
#endif
