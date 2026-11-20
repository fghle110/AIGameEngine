#pragma once

#include "Aegis/Core/Base.h"

namespace aegis
{
	// Frame time in seconds. Mirrors Hazel's Timestep so the naming feels familiar.
	class Timestep
	{
	public:
		Timestep() = default;
		explicit Timestep(f32 time)
			: m_Time(time)
		{
		}

		operator f32() const { return m_Time; }
		f32 GetSeconds() const { return m_Time; }
		f32 GetMilliseconds() const { return m_Time * 1000.0f; }

	private:
		f32 m_Time = 0.0f;
	};
}
