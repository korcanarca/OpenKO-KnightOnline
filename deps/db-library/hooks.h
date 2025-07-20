#pragma once

#include <string>

namespace db
{
	class Connection;

	namespace hooks
	{
		extern void (*Log)(const std::string& logLine);
	}
}
