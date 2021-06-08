#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <setjmp.h>

#include <MsgPack.h>
#include <Timer.h>

const size_t MAX_NUM_FIELDS_IN_KEY = 16;
const size_t TEST_FIELD_COUNT_IN_TUPLE = 16;
const size_t MAX_TEST_TUPLE_DATA_SIZE = 16 * TEST_FIELD_COUNT_IN_TUPLE;

/**
 * A tuple is a data structure that consist of variable number of values
 * with variable type. Those values are usually called 'fields'.
 * In order to provide type variability all the values are serialized
 * into char buffer one value after other. For serialization the msgpack
 * format is chosen (https://msgpack.org/).
 * This causes some read performance problems: since every field has a variable
 * size it is not possible to jump directly to nth field. If it is critical to
 * has a high performance access to some field a tuple stores additional
 * offsets for it - position in buffer where the serialized value is located.
 * Generally this requires common (for many tuples) tuple format that declares
 * which field has offset and where in tuple it is store. Since those offsets
 * can be of variable size they are also stored in the same buffer as fields.

 * Here is schematic layout of tuple in memory:
 * (fldX - serialized field X; offX - offset of field X.)

 * [ static part - struct members ][     dynamic part - char buffer (data)     ]
 * [...........][off0][...........][off1][off2]..[fld0][ fld1 ][fld3]...       ]
 *                                 <----off0---->
 *                                 <-------off1------->
 *                                 <-----------off2----------->
 *
 * For test purposes the tuple below stores num_offsets offsets for the first
 * num_offsets fields. That make tuples not to need tuple format.
 * Also for the same reason it has limited size and can store only
 * unsigned integers and strings.
 */
struct Tuple {
	/*
	 * The three members below a made specially for this test, in order to
	 * make dynamic modification of tuple.
	 * Actual tuple is built once and immutable later.
	 */
	// Current number of fields.
	uint32_t field_count;
	// Current used number of bytes in data.
	uint32_t data_used;
	// Maximal number of offsets in this tuple.
	uint32_t num_offsets;

	// Offset of the first field,
	// i.e. the first field starts in data[first_field_offset].
	// Note that we use shorter type for this offset to safe some space.
	uint16_t first_field_offset;
	// Type of field offset starting from field two.
	typedef uint32_t offset_t;
	// In real life there are several useful fields.
	uint16_t some_useful_data;
	// Data buffer for both field offsets and msgpack data.
	char data[MAX_TEST_TUPLE_DATA_SIZE];

	// Get dynamically allocated offset.
	offset_t& get_offset(size_t i)
	{
		assert(i > 0);
		return ((offset_t *)data)[i - 1];
	}

	// Get field with offset.
	const char *get_field(size_t i)
	{
		if (i == 0)
			return data + first_field_offset;
		else
			return data + get_offset(i);
	}

	/**
	 * Several methods for tuple modification. Are not need in real life.
	 */
	// Clean up the tuple.
	void reset(uint32_t a_num_offsets)
	{
		field_count = 0;
		num_offsets = a_num_offsets;
		assert(a_num_offsets > 0);
		// We have to store a_num_offsets offsets.
		// But the first offset is stored in first_field_offset member.
		// We have to store one less in data buffer.
		data_used = (a_num_offsets - 1) * sizeof(offset_t);
	}

	// Add integer value to the end of tuple, save offset if necessary.
	void add(uint64_t value)
	{
		char *p = data + data_used;
		mp_encode_uint(p, value);

		if (field_count == 0)
			first_field_offset = data_used;
		else if (field_count < num_offsets)
			get_offset(field_count) = data_used;
		data_used = p - data;
		field_count++;
	}

	// Add string value to the end of tuple, save offset if necessary.
	void add(const char *string, uint32_t len)
	{
		char *p = data + data_used;
		mp_encode_string(p, string, len);

		if (field_count == 0)
			first_field_offset = data_used;
		else if (field_count < num_offsets)
			get_offset(field_count) = data_used;
		data_used = p - data;
		field_count++;
	}
};

/**
 * KeyDef is a definition of how tuples are compared. Generally any order
 * of tuple fields may be chosen and then tuples are compared lexicographically
 * field by field, with the chosen field order. Of course any subset of tuple
 * fields may be chosen either.
 * For example we may create a key def: compare fields number 3; than (if 3rds
 * are equal) compare fields number 0; that's all.
 * Different key defs thus must define different order for the same group of
 * tuples. That allows, for example, to construct several indexes for faster
 * tuple search by different tuple fields.
 */
struct KeyDef {
	enum field_type_t {
		UINT,
		STRING,
		UNDEFINED,
	};
	struct KeyPart {
		field_type_t field_type;
		size_t field_no;
	};

	/**
	 * Parts describe how tuples are compared.
	 * Each part stores field_no and that field type.
	 */
	size_t part_count;
	KeyPart parts[MAX_NUM_FIELDS_IN_KEY];

	int (*tuple_compare_f)(KeyDef *def, Tuple *tuple1, Tuple *tuple2);
};

int default_tuple_compare(KeyDef *def, Tuple *tuple1, Tuple *tuple2)
{
	assert(def->part_count > 0);

	const char *part1;
	const char *part2;

	for (size_t i = 0; i < def->part_count; i++) {
		KeyDef::KeyPart *part = &def->parts[i];

		if (i == 0 ||
		    def->parts[i].field_no != def->parts[i - 1].field_no + 1) {
			/*
			 * That's an important part. In real life the field
			 * access by index is a bit more complicated and should
			 * be avoided.
			 * One the other hand decoding of a field puts the
			 * pointer exactly to the next field. So if the fields
			 * are sequential there's no need to reposition the
			 * pointer.
			 * The if condition above makes this optimization.
			 */
			part1 = tuple1->get_field(part->field_no);
			part2 = tuple2->get_field(part->field_no);
		}

		if (def->parts[0].field_type == KeyDef::UINT) {
			uint64_t value1 = mp_decode_uint(part1);
			uint64_t value2 = mp_decode_uint(part2);
			if (value1 < value2)
				return -1;
			else if (value1 > value2)
				return 1;
		} else {
			uint32_t len1,len2;
			const char *string1 = mp_decode_string(part1, len1);
			const char *string2 = mp_decode_string(part2, len2);
			uint32_t min_len = len1 < len2 ? len1 : len2;
			int r = memcmp(string1, string2, min_len);
			if (r != 0)
				return r;
			if (len1 < len2)
				return -1;
			else if (len1 > len2)
				return 1;
		}
		// If parts are equal - go to the next part.
	}

	// All parts are equal.
	return 0;
}

int tuple_compare_by_first_uint(KeyDef *, Tuple *tuple1, Tuple *tuple2)
{
	const char *part1 = tuple1->data + tuple1->first_field_offset;
	const char *part2 = tuple2->data + tuple2->first_field_offset;
	uint64_t value1 = mp_decode_uint(part1);
	uint64_t value2 = mp_decode_uint(part2);
	return value1 < value2 ? -1 : value1 > value2;
}

#ifdef _WIN32
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

const size_t N = 5000;
Tuple tuples[N];

// Benchmark for particular key def.
// Generates N tuples and compares them measuring cosumed time.
NOINLINE void bench_key_def(KeyDef *def, const char *test_name)
{
	// Generate tuples compatible with key def.
	KeyDef::field_type_t field_type[TEST_FIELD_COUNT_IN_TUPLE];
	for (size_t i = 0; i < TEST_FIELD_COUNT_IN_TUPLE; i++)
		field_type[i] = KeyDef::UNDEFINED;
	size_t max_field_no = 0;
	for (size_t i = 0; i < def->part_count; i++) {
		size_t field_no = def->parts[i].field_no;
		assert(field_no < TEST_FIELD_COUNT_IN_TUPLE);
		assert(field_type[field_no] == KeyDef::UNDEFINED);
		field_type[field_no] = def->parts[i].field_type;
		assert(field_type[field_no] != KeyDef::UNDEFINED);
		if (i == 0 || field_no > max_field_no)
			max_field_no = field_no;
	}
	for (size_t i = 0; i < N; i++) {
		tuples[i].reset(max_field_no + 1);
		for (size_t j = 0; j < TEST_FIELD_COUNT_IN_TUPLE; j++) {
			KeyDef::field_type_t generate_type = field_type[j];
			if (generate_type == KeyDef::UNDEFINED) {
				generate_type = rand() % 2 ? KeyDef::UINT
							   : KeyDef::STRING;
			}
			if (generate_type == KeyDef::UINT) {
				uint64_t value = rand();
				tuples[i].add(value);
			} else {
				uint32_t len = 3 + rand() % 6;
				char string[16];
				for (uint32_t k = 0; k < len; k++)
					string[k] = 'a' + rand() % 20;
				tuples[i].add(string, len);
			}
		}
	}

	// The test itself
	CTimer t;
	t.Start();
	int r = 0;
	for (size_t i = 0; i < N; i++)
		for (size_t j = 0; j < N; j++)
			r += def->tuple_compare_f(def, &tuples[i], &tuples[j]);
	t.Stop();
	std::cout << test_name << " Mrps: " << t.Mrps(N * N) << std::endl;
}

NOINLINE void bench_setjump()
{
	jmp_buf env;
	const size_t M = 1000000;
	CTimer t;
	t.Start();
	for (size_t i = 0; i < M; i++)
		if (setjmp(env) != 0)
			abort();
	t.Stop();
	std::cout << "setjmp Mrps: " << t.Mrps(M) << std::endl;
}

int main(int, const char**)
{
	KeyDef def;
	def.tuple_compare_f = default_tuple_compare;
	// Uncomment the line below to improve benchmark result.
	//def.tuple_compare_f = tuple_compare_by_first_uint;

	def.part_count = 1;
	def.parts[0].field_no = 0;
	def.parts[0].field_type = KeyDef::UINT;
	bench_key_def(&def, "uint first field");

	def.tuple_compare_f = default_tuple_compare;
	def.part_count = 2;
	def.parts[0].field_no = 1;
	def.parts[0].field_type = KeyDef::UINT;
	def.parts[1].field_no = 2;
	def.parts[1].field_type = KeyDef::UINT;
	bench_key_def(&def, "uint sequential fields");

	def.tuple_compare_f = default_tuple_compare;
	def.part_count = 2;
	def.parts[0].field_no = 2;
	def.parts[0].field_type = KeyDef::STRING;
	def.parts[1].field_no = 1;
	def.parts[1].field_type = KeyDef::STRING;
	bench_key_def(&def, "string non-sequential fields");

	bench_setjump();
}