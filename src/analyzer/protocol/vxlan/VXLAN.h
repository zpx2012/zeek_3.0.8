// See the file  in the main distribution directory for copyright.

#ifndef ANALYZER_PROTOCOL_VXLAN_VXLAN_H
#define ANALYZER_PROTOCOL_VXLAN_VXLAN_H

#include "analyzer/Analyzer.h"
#include "NetVar.h"
#include "Reporter.h"

namespace analyzer { namespace vxlan {

class VXLAN_Analyzer : public analyzer::Analyzer {
public:
	explicit VXLAN_Analyzer(Connection* conn)
	    : Analyzer("VXLAN", conn)
		{}

	void Done() override;

	void DeliverPacket(int len, const u_char* data, bool orig,
	                   uint64 seq, const IP_Hdr* ip, int caplen) override;

	static analyzer::Analyzer* Instantiate(Connection* conn)
		{ return new VXLAN_Analyzer(conn); }
};

} } // namespace analyzer::*

#endif
