#include "Data.h"
#include "File.h"
#include "broker/data.bif.h"

#include <broker/error.hh>

#include <caf/stream_serializer.hpp>
#include <caf/stream_deserializer.hpp>
#include <caf/streambuf.hpp>

using namespace std;

OpaqueType* bro_broker::opaque_of_data_type;
OpaqueType* bro_broker::opaque_of_set_iterator;
OpaqueType* bro_broker::opaque_of_table_iterator;
OpaqueType* bro_broker::opaque_of_vector_iterator;
OpaqueType* bro_broker::opaque_of_record_iterator;

BroType* bro_broker::DataVal::script_data_type = nullptr;

static bool data_type_check(const broker::data& d, BroType* t);

static broker::port::protocol to_broker_port_proto(TransportProto tp)
	{
	switch ( tp ) {
	case TRANSPORT_TCP:
		return broker::port::protocol::tcp;
	case TRANSPORT_UDP:
		return broker::port::protocol::udp;
	case TRANSPORT_ICMP:
		return broker::port::protocol::icmp;
	case TRANSPORT_UNKNOWN:
	default:
		return broker::port::protocol::unknown;
	}
	}

TransportProto bro_broker::to_bro_port_proto(broker::port::protocol tp)
	{
	switch ( tp ) {
	case broker::port::protocol::tcp:
		return TRANSPORT_TCP;
	case broker::port::protocol::udp:
		return TRANSPORT_UDP;
	case broker::port::protocol::icmp:
		return TRANSPORT_ICMP;
	case broker::port::protocol::unknown:
	default:
		return TRANSPORT_UNKNOWN;
	}
	}

struct val_converter {
	using result_type = Val*;

	BroType* type;

	result_type operator()(broker::none)
		{
		return nullptr;
		}

	result_type operator()(bool a)
		{
		if ( type->Tag() == TYPE_BOOL )
			return val_mgr->GetBool(a);
		return nullptr;
		}

	result_type operator()(uint64_t a)
		{
		if ( type->Tag() == TYPE_COUNT )
			return val_mgr->GetCount(a);
		if ( type->Tag() == TYPE_COUNTER )
			return val_mgr->GetCount(a);
		return nullptr;
		}

	result_type operator()(int64_t a)
		{
		if ( type->Tag() == TYPE_INT )
			return val_mgr->GetInt(a);
		return nullptr;
		}

	result_type operator()(double a)
		{
		if ( type->Tag() == TYPE_DOUBLE )
			return new Val(a, TYPE_DOUBLE);
		return nullptr;
		}

	result_type operator()(std::string& a)
		{
		switch ( type->Tag() ) {
		case TYPE_STRING:
			return new StringVal(a.size(), a.data());
		case TYPE_FILE:
			{
			auto file = BroFile::GetFile(a.data());

			if ( file )
				return new Val(file);

			return nullptr;
			}
		default:
			return nullptr;
		}
		}

	result_type operator()(broker::address& a)
		{
		if ( type->Tag() == TYPE_ADDR )
			{
			auto bits = reinterpret_cast<const in6_addr*>(&a.bytes());
			return new AddrVal(IPAddr(*bits));
			}

		return nullptr;
		}

	result_type operator()(broker::subnet& a)
		{
		if ( type->Tag() == TYPE_SUBNET )
			{
			auto bits = reinterpret_cast<const in6_addr*>(&a.network().bytes());
			return new SubNetVal(IPPrefix(IPAddr(*bits), a.length()));
			}

		return nullptr;
		}

	result_type operator()(broker::port& a)
		{
		if ( type->Tag() == TYPE_PORT )
			return val_mgr->GetPort(a.number(), bro_broker::to_bro_port_proto(a.type()));

		return nullptr;
		}

	result_type operator()(broker::timestamp& a)
		{
		if ( type->Tag() != TYPE_TIME )
			return nullptr;

		using namespace std::chrono;
		auto s = duration_cast<broker::fractional_seconds>(a.time_since_epoch());
		return new Val(s.count(), TYPE_TIME);
		}

	result_type operator()(broker::timespan& a)
		{
		if ( type->Tag() != TYPE_INTERVAL )
			return nullptr;

		using namespace std::chrono;
		auto s = duration_cast<broker::fractional_seconds>(a);
		return new Val(s.count(), TYPE_INTERVAL);
		}

	result_type operator()(broker::enum_value& a)
		{
		if ( type->Tag() == TYPE_ENUM )
			{
			auto etype = type->AsEnumType();
			auto i = etype->Lookup(GLOBAL_MODULE_NAME, a.name.data());

			if ( i == -1 )
				return nullptr;

			return etype->GetVal(i);
			}

		return nullptr;
		}

	result_type operator()(broker::set& a)
		{
		if ( ! type->IsSet() )
			return nullptr;

		auto tt = type->AsTableType();
		auto rval = new TableVal(tt);

		for ( auto& item : a )
			{
			auto expected_index_types = tt->Indices()->Types();
			broker::vector composite_key;
			auto indices = caf::get_if<broker::vector>(&item);

			if ( indices )
				{
				if ( expected_index_types->length() == 1 )
					{
					auto index_is_vector_or_record =
					     (*expected_index_types)[0]->Tag() == TYPE_RECORD ||
					     (*expected_index_types)[0]->Tag() == TYPE_VECTOR;

					if ( index_is_vector_or_record )
						{
						// Disambiguate from composite key w/ multiple vals.
						composite_key.emplace_back(move(item));
						indices = &composite_key;
						}
					}
				}
			else
				{
				composite_key.emplace_back(move(item));
				indices = &composite_key;
				}

			if ( static_cast<size_t>(expected_index_types->length()) !=
			     indices->size() )
				{
				Unref(rval);
				return nullptr;
				}

			auto list_val = new ListVal(TYPE_ANY);

			for ( auto i = 0u; i < indices->size(); ++i )
				{
				auto index_val = bro_broker::data_to_val(move((*indices)[i]),
				                                         (*expected_index_types)[i]);

				if ( ! index_val )
					{
					Unref(rval);
					Unref(list_val);
					return nullptr;
					}

				list_val->Append(index_val);
				}


			rval->Assign(list_val, nullptr);
			Unref(list_val);
			}

		return rval;
		}

	result_type operator()(broker::table& a)
		{
		if ( ! type->IsTable() )
			return nullptr;

		auto tt = type->AsTableType();
		auto rval = new TableVal(tt);

		for ( auto& item : a )
			{
			auto expected_index_types = tt->Indices()->Types();
			broker::vector composite_key;
			auto indices = caf::get_if<broker::vector>(&item.first);

			if ( indices )
				{
				if ( expected_index_types->length() == 1 )
					{
					auto index_is_vector_or_record =
					     (*expected_index_types)[0]->Tag() == TYPE_RECORD ||
					     (*expected_index_types)[0]->Tag() == TYPE_VECTOR;

					if ( index_is_vector_or_record )
						{
						// Disambiguate from composite key w/ multiple vals.
						composite_key.emplace_back(move(item.first));
						indices = &composite_key;
						}
					}
				}
			else
				{
				composite_key.emplace_back(move(item.first));
				indices = &composite_key;
				}

			if ( static_cast<size_t>(expected_index_types->length()) !=
			     indices->size() )
				{
				Unref(rval);
				return nullptr;
				}

			auto list_val = new ListVal(TYPE_ANY);

			for ( auto i = 0u; i < indices->size(); ++i )
				{
				auto index_val = bro_broker::data_to_val(move((*indices)[i]),
				                                         (*expected_index_types)[i]);

				if ( ! index_val )
					{
					Unref(rval);
					Unref(list_val);
					return nullptr;
					}

				list_val->Append(index_val);
				}

			auto value_val = bro_broker::data_to_val(move(item.second),
			                                         tt->YieldType());

			if ( ! value_val )
				{
				Unref(rval);
				Unref(list_val);
				return nullptr;
				}

			rval->Assign(list_val, value_val);
			Unref(list_val);
			}

		return rval;
		}

	result_type operator()(broker::vector& a)
		{
		if ( type->Tag() == TYPE_VECTOR )
			{
			auto vt = type->AsVectorType();
			auto rval = new VectorVal(vt);

			for ( auto& item : a )
				{
				auto item_val = bro_broker::data_to_val(move(item), vt->YieldType());

				if ( ! item_val )
					{
					Unref(rval);
					return nullptr;
					}

				rval->Assign(rval->Size(), item_val);
				}

			return rval;
			}
		else if ( type->Tag() == TYPE_FUNC )
			{
			if ( a.size() < 1 || a.size() > 2 )
				return nullptr;

			auto name = broker::get_if<std::string>(a[0]);
			if ( ! name )
				return nullptr;

			auto id = global_scope()->Lookup(name->c_str());
			if ( ! id )
				return nullptr;

			auto rval = id->ID_Val();
			if ( ! rval )
				return nullptr;

			auto t = rval->Type();
			if ( ! t )
				return nullptr;

			if ( t->Tag() != TYPE_FUNC )
				return nullptr;

			if ( a.size() == 2 ) // We have a closure.
				{
				auto frame = broker::get_if<broker::vector>(a[1]);
				if ( ! frame )
					return nullptr;

				BroFunc* b = dynamic_cast<BroFunc*>(rval->AsFunc());
				if ( ! b )
					return nullptr;

				if ( ! b->UpdateClosure(*frame) )
					return nullptr;
				}

			return rval->Ref();
			}
		else if ( type->Tag() == TYPE_RECORD )
			{
			auto rt = type->AsRecordType();
			auto rval = new RecordVal(rt);
			auto idx = 0u;

			for ( auto i = 0u; i < static_cast<size_t>(rt->NumFields()); ++i )
				{
				if ( idx >= a.size() )
					{
					Unref(rval);
					return nullptr;
					}

				if ( caf::get_if<broker::none>(&a[idx]) != nullptr )
					{
					rval->Assign(i, nullptr);
					++idx;
					continue;
					}

				auto item_val = bro_broker::data_to_val(move(a[idx]),
																								rt->FieldType(i));

				if ( ! item_val )
					{
					Unref(rval);
					return nullptr;
					}

				rval->Assign(i, item_val);
				++idx;
				}

			return rval;
			}
		else if ( type->Tag() == TYPE_PATTERN )
			{
			if ( a.size() != 2 )
				return nullptr;

			auto exact_text = caf::get_if<std::string>(&a[0]);
			auto anywhere_text = caf::get_if<std::string>(&a[1]);

			if ( ! exact_text || ! anywhere_text )
				return nullptr;

			RE_Matcher* re = new RE_Matcher(exact_text->c_str(),
			                                anywhere_text->c_str());

			if ( ! re->Compile() )
				{
				reporter->Error("failed compiling unserialized pattern: %s, %s",
				                exact_text->c_str(), anywhere_text->c_str());
				delete re;
				return nullptr;
				}

			auto rval = new PatternVal(re);
			return rval;
			}
		else if ( type->Tag() == TYPE_OPAQUE )
			return OpaqueVal::Unserialize(a);

		return nullptr;
		}
};

struct type_checker {
	using result_type = bool;

	BroType* type;

	result_type operator()(broker::none)
		{
		return false;
		}

	result_type operator()(bool a)
		{
		if ( type->Tag() == TYPE_BOOL )
			return true;
		return false;
		}

	result_type operator()(uint64_t a)
		{
		if ( type->Tag() == TYPE_COUNT )
			return true;
		if ( type->Tag() == TYPE_COUNTER )
			return true;
		return false;
		}

	result_type operator()(int64_t a)
		{
		if ( type->Tag() == TYPE_INT )
			return true;
		return false;
		}

	result_type operator()(double a)
		{
		if ( type->Tag() == TYPE_DOUBLE )
			return true;
		return false;
		}

	result_type operator()(const std::string& a)
		{
		switch ( type->Tag() ) {
		case TYPE_STRING:
			return true;
		case TYPE_FILE:
			return true;
		default:
			return false;
		}
		}

	result_type operator()(const broker::address& a)
		{
		if ( type->Tag() == TYPE_ADDR )
			return true;

		return false;
		}

	result_type operator()(const broker::subnet& a)
		{
		if ( type->Tag() == TYPE_SUBNET )
			return true;

		return false;
		}

	result_type operator()(const broker::port& a)
		{
		if ( type->Tag() == TYPE_PORT )
			return true;

		return false;
		}

	result_type operator()(const broker::timestamp& a)
		{
		if ( type->Tag() == TYPE_TIME )
			return true;

		return false;
		}

	result_type operator()(const broker::timespan& a)
		{
		if ( type->Tag() == TYPE_INTERVAL )
			return true;

		return false;
		}

	result_type operator()(const broker::enum_value& a)
		{
		if ( type->Tag() == TYPE_ENUM )
			{
			auto etype = type->AsEnumType();
			auto i = etype->Lookup(GLOBAL_MODULE_NAME, a.name.data());
			return i != -1;
			}

		return false;
		}

	result_type operator()(const broker::set& a)
		{
		if ( ! type->IsSet() )
			return false;

		auto tt = type->AsTableType();

		for ( const auto& item : a )
			{
			auto expected_index_types = tt->Indices()->Types();
			auto indices = caf::get_if<broker::vector>(&item);
			vector<const broker::data*> indices_to_check;

			if ( indices )
				{
				if ( expected_index_types->length() == 1 )
					{
					auto index_is_vector_or_record =
					     (*expected_index_types)[0]->Tag() == TYPE_RECORD ||
					     (*expected_index_types)[0]->Tag() == TYPE_VECTOR;

					if ( index_is_vector_or_record )
						// Disambiguate from composite key w/ multiple vals.
						indices_to_check.emplace_back(&item);
					else
						{
						indices_to_check.reserve(indices->size());

						for ( auto i = 0u; i < indices->size(); ++i )
							indices_to_check.emplace_back(&(*indices)[i]);
						}
					}
				else
					{
					indices_to_check.reserve(indices->size());

					for ( auto i = 0u; i < indices->size(); ++i )
						indices_to_check.emplace_back(&(*indices)[i]);
					}
				}
			else
				indices_to_check.emplace_back(&item);

			if ( static_cast<size_t>(expected_index_types->length()) !=
			     indices_to_check.size() )
				return false;

			for ( auto i = 0u; i < indices_to_check.size(); ++i )
				{
				auto expect = (*expected_index_types)[i];
				auto& index_to_check = *(indices_to_check)[i];

				if ( ! data_type_check(index_to_check, expect) )
					return false;
				}
			}

		return true;
		}

	result_type operator()(const broker::table& a)
		{
		if ( ! type->IsTable() )
			return false;

		auto tt = type->AsTableType();

		for ( auto& item : a )
			{
			auto expected_index_types = tt->Indices()->Types();
			auto indices = caf::get_if<broker::vector>(&item.first);
			vector<const broker::data*> indices_to_check;

			if ( indices )
				{
				if ( expected_index_types->length() == 1 )
					{
					auto index_is_vector_or_record =
					     (*expected_index_types)[0]->Tag() == TYPE_RECORD ||
					     (*expected_index_types)[0]->Tag() == TYPE_VECTOR;

					if ( index_is_vector_or_record )
						// Disambiguate from composite key w/ multiple vals.
						indices_to_check.emplace_back(&item.first);
					else
						{
						indices_to_check.reserve(indices->size());

						for ( auto i = 0u; i < indices->size(); ++i )
							indices_to_check.emplace_back(&(*indices)[i]);
						}
					}
				else
					{
					indices_to_check.reserve(indices->size());

					for ( auto i = 0u; i < indices->size(); ++i )
						indices_to_check.emplace_back(&(*indices)[i]);
					}
				}
			else
				indices_to_check.emplace_back(&item.first);


			if ( static_cast<size_t>(expected_index_types->length()) !=
			     indices_to_check.size() )
				{
				return false;
				}

			for ( auto i = 0u; i < indices_to_check.size(); ++i )
				{
				auto expect = (*expected_index_types)[i];
				auto& index_to_check = *(indices_to_check)[i];

				if ( ! data_type_check(index_to_check, expect) )
					return false;
				}

			if ( ! data_type_check(item.second, tt->YieldType()) )
				return false;
			}

		return true;
		}

	result_type operator()(const broker::vector& a)
		{
		if ( type->Tag() == TYPE_VECTOR )
			{
			auto vt = type->AsVectorType();

			for ( auto& item : a )
				{
				if ( ! data_type_check(item, vt->YieldType()) )
					return false;
				}

			return true;
			}
		else if ( type->Tag() == TYPE_FUNC )
			{
			if ( a.size() < 1 || a.size() > 2 )
				return false;

			auto name = broker::get_if<std::string>(a[0]);
			if ( ! name )
				return false;

			auto id = global_scope()->Lookup(name->c_str());
			if ( ! id )
				return false;

			auto rval = id->ID_Val();
			if ( ! rval )
				return false;

			auto t = rval->Type();
			if ( ! t )
				return false;

			if ( t->Tag() != TYPE_FUNC )
				return false;

			return true;
			}
		else if ( type->Tag() == TYPE_RECORD )
			{
			auto rt = type->AsRecordType();
			auto idx = 0u;

			for ( auto i = 0u; i < static_cast<size_t>(rt->NumFields()); ++i )
				{
				if ( idx >= a.size() )
					return false;

				if ( caf::get_if<broker::none>(&a[idx]) != nullptr )
					{
					++idx;
					continue;
					}

				if ( ! data_type_check(a[idx], rt->FieldType(i)) )
					return false;

				++idx;
				}

			return true;
			}
		else if ( type->Tag() == TYPE_PATTERN )
			{
			if ( a.size() != 2 )
				return false;

			auto exact_text = caf::get_if<std::string>(&a[0]);
			auto anywhere_text = caf::get_if<std::string>(&a[1]);

			if ( ! exact_text || ! anywhere_text )
				return false;

			RE_Matcher* re = new RE_Matcher(exact_text->c_str(),
			                                anywhere_text->c_str());
			auto compiled = re->Compile();
			delete re;

			if ( ! compiled )
				{
				reporter->Error("failed compiling pattern: %s, %s",
				                exact_text->c_str(), anywhere_text->c_str());
				return false;
				}

			return true;
			}
		else if ( type->Tag() == TYPE_OPAQUE )
			{
			// TODO: Could avoid doing the full unserialization here
			// and just check if the type is a correct match.
			auto ov = OpaqueVal::Unserialize(a);
			auto rval = ov != nullptr;
			Unref(ov);
			return rval;
			}

		return false;
		}
};

static bool data_type_check(const broker::data& d, BroType* t)
	{
	if ( t->Tag() == TYPE_ANY )
		return true;

	return caf::visit(type_checker{t}, d);
	}

Val* bro_broker::data_to_val(broker::data d, BroType* type)
	{
	if ( type->Tag() == TYPE_ANY )
		return bro_broker::make_data_val(move(d));

	return caf::visit(val_converter{type}, std::move(d));
	}

broker::expected<broker::data> bro_broker::val_to_data(Val* v)
	{
	switch ( v->Type()->Tag() ) {
	case TYPE_BOOL:
		return {v->AsBool()};
	case TYPE_INT:
		return {v->AsInt()};
	case TYPE_COUNT:
		return {v->AsCount()};
	case TYPE_COUNTER:
		return {v->AsCounter()};
	case TYPE_PORT:
		{
		auto p = v->AsPortVal();
		return {broker::port(p->Port(), to_broker_port_proto(p->PortType()))};
		}
	case TYPE_ADDR:
		{
		auto a = v->AsAddr();
		in6_addr tmp;
		a.CopyIPv6(&tmp);
		return {broker::address(reinterpret_cast<const uint32_t*>(&tmp),
			                    broker::address::family::ipv6,
			                    broker::address::byte_order::network)};
		}
		break;
	case TYPE_SUBNET:
		{
		auto s = v->AsSubNet();
		in6_addr tmp;
		s.Prefix().CopyIPv6(&tmp);
		auto a = broker::address(reinterpret_cast<const uint32_t*>(&tmp),
		                         broker::address::family::ipv6,
		                         broker::address::byte_order::network);
		return {broker::subnet(std::move(a), s.Length())};
		}
		break;
	case TYPE_DOUBLE:
		return {v->AsDouble()};
	case TYPE_TIME:
		{
		auto secs = broker::fractional_seconds{v->AsTime()};
		auto since_epoch = std::chrono::duration_cast<broker::timespan>(secs);
		return {broker::timestamp{since_epoch}};
		}
	case TYPE_INTERVAL:
		{
		auto secs = broker::fractional_seconds{v->AsInterval()};
		return {std::chrono::duration_cast<broker::timespan>(secs)};
		}
	case TYPE_ENUM:
		{
		auto enum_type = v->Type()->AsEnumType();
		auto enum_name = enum_type->Lookup(v->AsEnum());
		return {broker::enum_value(enum_name ? enum_name : "<unknown enum>")};
		}
	case TYPE_STRING:
		{
		auto s = v->AsString();
		return {string(reinterpret_cast<const char*>(s->Bytes()), s->Len())};
		}
	case TYPE_FILE:
		return {string(v->AsFile()->Name())};
	case TYPE_FUNC:
		{
		Func* f = v->AsFunc();
		std::string name(f->Name());

		broker::vector rval;
		rval.push_back(name);

		if ( name.find("lambda_<") == 0 )
			{
			// Only BroFuncs have closures.
			if ( auto b = dynamic_cast<BroFunc*>(f) )
				{
				auto bc = b->SerializeClosure();
				if ( ! bc )
					return broker::ec::invalid_data;

				rval.emplace_back(std::move(*bc));
				}
			else
				{
				reporter->InternalWarning("Closure with non-BroFunc");
				return broker::ec::invalid_data;
				}
			}

		return {std::move(rval)};
		}
	case TYPE_TABLE:
		{
		auto is_set = v->Type()->IsSet();
		auto table = v->AsTable();
		auto table_val = v->AsTableVal();
		broker::data rval;

		if ( is_set )
			rval = broker::set();
		else
			rval = broker::table();

		struct iter_guard {
			iter_guard(HashKey* arg_k, ListVal* arg_lv)
			    : k(arg_k), lv(arg_lv)
				{}

			~iter_guard()
				{
				delete k;
				Unref(lv);
				}

			HashKey* k;
			ListVal* lv;
		};

		HashKey* k;
		TableEntryVal* entry;
		auto c = table->InitForIteration();

		while ( (entry = table->NextEntry(k, c)) )
			{
			auto vl = table_val->RecoverIndex(k);
			iter_guard ig(k, vl);

			broker::vector composite_key;
			composite_key.reserve(vl->Length());

			for ( auto k = 0; k < vl->Length(); ++k )
				{
				auto key_part = val_to_data((*vl->Vals())[k]);

				if ( ! key_part )
					return broker::ec::invalid_data;

				composite_key.emplace_back(move(*key_part));
				}

			broker::data key;

			if ( composite_key.size() == 1 )
				key = move(composite_key[0]);
			else
				key = move(composite_key);

			if ( is_set )
				caf::get<broker::set>(rval).emplace(move(key));
			else
				{
				auto val = val_to_data(entry->Value());

				if ( ! val )
					return broker::ec::invalid_data;

				caf::get<broker::table>(rval).emplace(move(key), move(*val));
				}
			}

		return {std::move(rval)};
		}
	case TYPE_VECTOR:
		{
		auto vec = v->AsVectorVal();
		broker::vector rval;
		rval.reserve(vec->Size());

		for ( auto i = 0u; i < vec->Size(); ++i )
			{
			auto item_val = vec->Lookup(i);

			if ( ! item_val )
				continue;

			auto item = val_to_data(item_val);

			if ( ! item )
				return broker::ec::invalid_data;

			rval.emplace_back(move(*item));
			}

		return {std::move(rval)};
		}
	case TYPE_RECORD:
		{
		auto rec = v->AsRecordVal();
		broker::vector rval;
		size_t num_fields = v->Type()->AsRecordType()->NumFields();
		rval.reserve(num_fields);

		for ( auto i = 0u; i < num_fields; ++i )
			{
			auto item_val = rec->LookupWithDefault(i);

			if ( ! item_val )
				{
				rval.emplace_back(broker::nil);
				continue;
				}

			auto item = val_to_data(item_val);
			Unref(item_val);

			if ( ! item )
				return broker::ec::invalid_data;

			rval.emplace_back(move(*item));
			}

		return {std::move(rval)};
		}
	case TYPE_PATTERN:
		{
		RE_Matcher* p = v->AsPattern();
		broker::vector rval = {p->PatternText(), p->AnywherePatternText()};
		return {std::move(rval)};
		}
	case TYPE_OPAQUE:
		{
		auto c = v->AsOpaqueVal()->Serialize();
		if ( ! c )
			{
			reporter->Error("unsupported opaque type for serialization");
			break;
			}

		return {c};
		}
	default:
		reporter->Error("unsupported Broker::Data type: %s",
		                type_name(v->Type()->Tag()));
		break;
	}

	return broker::ec::invalid_data;
	}

RecordVal* bro_broker::make_data_val(Val* v)
	{
	auto rval = new RecordVal(BifType::Record::Broker::Data);
	auto data = val_to_data(v);

	if  ( data )
		rval->Assign(0, new DataVal(move(*data)));
	else
		reporter->Warning("did not get a value from val_to_data");

	return rval;
	}

RecordVal* bro_broker::make_data_val(broker::data d)
	{
	auto rval = new RecordVal(BifType::Record::Broker::Data);
	rval->Assign(0, new DataVal(move(d)));
	return rval;
	}

struct data_type_getter {
	using result_type = EnumVal*;

	result_type operator()(broker::none)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::NONE);
		}

	result_type operator()(bool)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::BOOL);
		}

	result_type operator()(uint64_t)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::COUNT);
		}

	result_type operator()(int64_t)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::INT);
		}

	result_type operator()(double)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::DOUBLE);
		}

	result_type operator()(const std::string&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::STRING);
		}

	result_type operator()(const broker::address&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::ADDR);
		}

	result_type operator()(const broker::subnet&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::SUBNET);
		}

	result_type operator()(const broker::port&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::PORT);
		}

	result_type operator()(const broker::timestamp&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::TIME);
		}

	result_type operator()(const broker::timespan&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::INTERVAL);
		}

	result_type operator()(const broker::enum_value&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::ENUM);
		}

	result_type operator()(const broker::set&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::SET);
		}

	result_type operator()(const broker::table&)
		{
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::TABLE);
		}

	result_type operator()(const broker::vector&)
		{
		// Note that Broker uses vectors to store record data, so there's
		// no actual way to tell if this data was originally associated
		// with a Bro record.
		return BifType::Enum::Broker::DataType->GetVal(BifEnum::Broker::VECTOR);
		}
};

EnumVal* bro_broker::get_data_type(RecordVal* v, Frame* frame)
	{
	return caf::visit(data_type_getter{}, opaque_field_to_data(v, frame));
	}

broker::data& bro_broker::opaque_field_to_data(RecordVal* v, Frame* f)
	{
	Val* d = v->Lookup(0);

	if ( ! d )
		reporter->RuntimeError(f->GetCall()->GetLocationInfo(),
		                       "Broker::Data's opaque field is not set");

	return static_cast<DataVal*>(d)->data;
	}

bool bro_broker::DataVal::canCastTo(BroType* t) const
	{
	return data_type_check(data, t);
	}

Val* bro_broker::DataVal::castTo(BroType* t)
	{
	return data_to_val(data, t);
	}

IMPLEMENT_OPAQUE_VALUE(bro_broker::DataVal)

broker::expected<broker::data> bro_broker::DataVal::DoSerialize() const
	{
	return data;
	}

bool bro_broker::DataVal::DoUnserialize(const broker::data& data_)
	{
	data = data_;
	return true;
	}

IMPLEMENT_OPAQUE_VALUE(bro_broker::SetIterator)

broker::expected<broker::data> bro_broker::SetIterator::DoSerialize() const
	{
	return broker::vector{dat, *it};
	}

bool bro_broker::SetIterator::DoUnserialize(const broker::data& data)
	{
	auto v = caf::get_if<broker::vector>(&data);
	if ( ! (v && v->size() == 2) )
		return false;

	auto x = caf::get_if<broker::set>(&(*v)[0]);

	// We set the iterator by finding the element it used to point to.
	// This is not perfect, as there's no guarantee that the restored
	// container will list the elements in the same order. But it's as
	// good as we can do, and it should generally work out.
	if( x->find((*v)[1]) == x->end() )
		return false;

	dat = *x;
	it = dat.find((*v)[1]);
	return true;
	}

IMPLEMENT_OPAQUE_VALUE(bro_broker::TableIterator)

broker::expected<broker::data> bro_broker::TableIterator::DoSerialize() const
	{
	return broker::vector{dat, it->first};
	}

bool bro_broker::TableIterator::DoUnserialize(const broker::data& data)
	{
	auto v = caf::get_if<broker::vector>(&data);
	if ( ! (v && v->size() == 2) )
		return false;

	auto x = caf::get_if<broker::table>(&(*v)[0]);

	// We set the iterator by finding the element it used to point to.
	// This is not perfect, as there's no guarantee that the restored
	// container will list the elements in the same order. But it's as
	// good as we can do, and it should generally work out.
	if( x->find((*v)[1]) == x->end() )
		return false;

	dat = *x;
	it = dat.find((*v)[1]);
	return true;
	}

IMPLEMENT_OPAQUE_VALUE(bro_broker::VectorIterator)

broker::expected<broker::data> bro_broker::VectorIterator::DoSerialize() const
	{
	broker::integer difference = it - dat.begin();
	return broker::vector{dat, difference};
	}

bool bro_broker::VectorIterator::DoUnserialize(const broker::data& data)
	{
	auto v = caf::get_if<broker::vector>(&data);
	if ( ! (v && v->size() == 2) )
		return false;

	auto x = caf::get_if<broker::vector>(&(*v)[0]);
	auto y = caf::get_if<broker::integer>(&(*v)[1]);

	if ( ! (x && y) )
		return false;

	dat = *x;
	it = dat.begin() + *y;
	return true;
	}

IMPLEMENT_OPAQUE_VALUE(bro_broker::RecordIterator)

broker::expected<broker::data> bro_broker::RecordIterator::DoSerialize() const
	{
	broker::integer difference = it - dat.begin();
	return broker::vector{dat, difference};
	}

bool bro_broker::RecordIterator::DoUnserialize(const broker::data& data)
	{
	auto v = caf::get_if<broker::vector>(&data);
	if ( ! (v && v->size() == 2) )
		return false;

	auto x = caf::get_if<broker::vector>(&(*v)[0]);
	auto y = caf::get_if<broker::integer>(&(*v)[1]);

	if ( ! (x && y) )
		return false;

	dat = *x;
	it = dat.begin() + *y;
	return true;
	}

broker::data bro_broker::threading_field_to_data(const threading::Field* f)
	{
	auto name = f->name;
	auto type = static_cast<uint64_t>(f->type);
	auto subtype = static_cast<uint64_t>(f->subtype);
	auto optional = f->optional;

	broker::data secondary = broker::nil;

	if ( f->secondary_name )
		secondary = {f->secondary_name};

	return broker::vector({name, secondary, type, subtype, optional});
	}

threading::Field* bro_broker::data_to_threading_field(broker::data d)
	{
	if ( ! caf::holds_alternative<broker::vector>(d) )
		return nullptr;

	auto& v = caf::get<broker::vector>(d);
	auto name = caf::get_if<std::string>(&v[0]);
	auto secondary = v[1];
	auto type = caf::get_if<broker::count>(&v[2]);
	auto subtype = caf::get_if<broker::count>(&v[3]);
	auto optional = caf::get_if<broker::boolean>(&v[4]);

	if ( ! (name && type && subtype && optional) )
		return nullptr;

	if ( secondary != broker::nil && ! caf::holds_alternative<std::string>(secondary) )
		return nullptr;

	return new threading::Field(name->c_str(),
				    secondary != broker::nil ? caf::get<std::string>(secondary).c_str() : nullptr,
				    static_cast<TypeTag>(*type),
				    static_cast<TypeTag>(*subtype),
				    *optional);
	}
