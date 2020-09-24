// See the file  in the main distribution directory for copyright.


#include "plugin/Plugin.h"

#include "DHCP.h"

namespace plugin {
namespace Zeek_DHCP {

class Plugin : public plugin::Plugin {
public:
	plugin::Configuration Configure()
		{
		AddComponent(new ::analyzer::Component("DHCP", ::analyzer::dhcp::DHCP_Analyzer::Instantiate));

		plugin::Configuration config;
		config.name = "Zeek::DHCP";
		config.description = "DHCP analyzer";
		return config;
		}
} plugin;

}
}
