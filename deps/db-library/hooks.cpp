#include "pch.h"
#include "hooks.h"

namespace db
{
	namespace hooks
	{
		void (*Log)(const std::string& logLine);
	}
}
