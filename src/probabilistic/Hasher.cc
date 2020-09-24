// See the file "COPYING" in the main distribution directory for copyright.

#include <typeinfo>
#include <openssl/evp.h>

#include "Hasher.h"
#include "NetVar.h"
#include "digest.h"
#include "siphash24.h"

using namespace probabilistic;

Hasher::seed_t Hasher::MakeSeed(const void* data, size_t size)
	{
	u_char buf[SHA256_DIGEST_LENGTH];
	seed_t tmpseed;
	EVP_MD_CTX* ctx = hash_init(Hash_SHA256);

	assert(sizeof(tmpseed) == 16);

	if ( data )
		hash_update(ctx, data, size);

	else if ( global_hash_seed && global_hash_seed->Len() > 0 )
		hash_update(ctx, global_hash_seed->Bytes(), global_hash_seed->Len());

	else
		{
		unsigned int first_seed = initial_seed();
		hash_update(ctx, &first_seed, sizeof(first_seed));
		}

	hash_final(ctx, buf);
	memcpy(&tmpseed, buf, sizeof(tmpseed)); // Use the first bytes as seed.
	return tmpseed;
	}

Hasher::digest_vector Hasher::Hash(const HashKey* key) const
	{
	return Hash(key->Key(), key->Size());
	}

Hasher::Hasher(size_t arg_k, seed_t arg_seed)
	{
	k = arg_k;
	seed = arg_seed;
	}

broker::expected<broker::data> Hasher::Serialize() const
	{
	return {broker::vector{
		static_cast<uint64>(Type()), static_cast<uint64>(k),
		seed.h1, seed.h2 }};
	}

std::unique_ptr<Hasher> Hasher::Unserialize(const broker::data& data)
	{
	auto v = caf::get_if<broker::vector>(&data);

	if ( ! (v && v->size() == 4) )
		return nullptr;

	auto type = caf::get_if<uint64>(&(*v)[0]);
	auto k = caf::get_if<uint64>(&(*v)[1]);
	auto h1 = caf::get_if<uint64>(&(*v)[2]);
	auto h2 = caf::get_if<uint64>(&(*v)[3]);

	if ( ! (type && k && h1 && h2) )
		return nullptr;

	std::unique_ptr<Hasher> hasher;

	switch ( *type ) {
	case Default:
		hasher = std::unique_ptr<Hasher>(new DefaultHasher(*k, {*h1, *h2}));
		break;

	case Double:
		hasher = std::unique_ptr<Hasher>(new DoubleHasher(*k, {*h1, *h2}));
		break;
	}

	// Note that the derived classed don't hold any further state of
	// their own. They reconstruct all their information from their
	// constructors' arguments.

	return hasher;
	}

UHF::UHF()
	{
	memset(&seed, 0, sizeof(seed));
	}

UHF::UHF(Hasher::seed_t arg_seed)
	{
	seed = arg_seed;
	}

// This function is almost equivalent to HashKey::HashBytes except that it
// does not depend on global state and that we mix in the seed multiple
// times.
Hasher::digest UHF::hash(const void* x, size_t n) const
	{
	assert(sizeof(Hasher::seed_t) == SIPHASH_KEYLEN);

	if ( n <= UHASH_KEY_SIZE )
		{
		hash_t outdigest;
		siphash(&outdigest, reinterpret_cast<const uint8_t*>(x), n, reinterpret_cast<const uint8_t*>(&seed));
		return outdigest;
		}

	union {
		unsigned char d[16];
		Hasher::digest rval;
	} u;

	internal_md5(reinterpret_cast<const unsigned char*>(x), n, u.d);

	const unsigned char* s = reinterpret_cast<const unsigned char*>(&seed);
	for ( size_t i = 0; i < 16; ++i )
		u.d[i] ^= s[i % sizeof(seed)];

	internal_md5(u.d, 16, u.d);
	return u.rval;
	}

DefaultHasher::DefaultHasher(size_t k, Hasher::seed_t seed)
	: Hasher(k, seed)
	{
	for ( size_t i = 1; i <= k; ++i )
		{
		seed_t s = Seed();
		s.h1 += bro_prng(i);
		hash_functions.push_back(UHF(s));
		}
	}

Hasher::digest_vector DefaultHasher::Hash(const void* x, size_t n) const
	{
	digest_vector h(K(), 0);

	for ( size_t i = 0; i < h.size(); ++i )
		h[i] = hash_functions[i](x, n);

	return h;
	}

DefaultHasher* DefaultHasher::Clone() const
	{
	return new DefaultHasher(*this);
	}

bool DefaultHasher::Equals(const Hasher* other) const
	{
	if ( typeid(*this) != typeid(*other) )
		return false;

	const DefaultHasher* o = static_cast<const DefaultHasher*>(other);
	return hash_functions == o->hash_functions;
	}

DoubleHasher::DoubleHasher(size_t k, seed_t seed)
	: Hasher(k, seed), h1(seed + bro_prng(1)), h2(seed + bro_prng(2))
	{
	}

Hasher::digest_vector DoubleHasher::Hash(const void* x, size_t n) const
	{
	digest d1 = h1(x, n);
	digest d2 = h2(x, n);
	digest_vector h(K(), 0);

	for ( size_t i = 0; i < h.size(); ++i )
		h[i] = d1 + i * d2;

	return h;
	}

DoubleHasher* DoubleHasher::Clone() const
	{
	return new DoubleHasher(*this);
	}

bool DoubleHasher::Equals(const Hasher* other) const
	{
	if ( typeid(*this) != typeid(*other) )
		return false;

	const DoubleHasher* o = static_cast<const DoubleHasher*>(other);
	return h1 == o->h1 && h2 == o->h2;
	}

