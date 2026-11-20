#include "Aegis/Core/Layer.h"

#include <utility>

namespace aegis
{
	Layer::Layer(std::string name)
		: m_DebugName(std::move(name))
	{
	}
}
