// See the file  in the main distribution directory for copyright.


#include "plugin/Plugin.h"

#include "IRC.h"

namespace plugin {
namespace Zeek_IRC {

class Plugin : public plugin::Plugin {
public:
	plugin::Configuration Configure()
		{
		AddComponent(new ::analyzer::Component("IRC", ::analyzer::irc::IRC_Analyzer::Instantiate));

		plugin::Configuration config;
		config.name = "Zeek::IRC";
		config.description = "IRC analyzer";
		return config;
		}
} plugin;

}
}
