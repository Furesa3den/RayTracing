#pragma once
#include "Objects/Model.h"

namespace Objects::Models
{
	class Alicia : public Model
	{
	public:
		Alicia() : Model(L"assets/models/Alicia/Alicia_solid_Unity.FBX")
		{
		}

	};
}
