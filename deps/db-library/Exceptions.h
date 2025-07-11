#pragma once

#include <stdexcept>
#include <string>

namespace db
{

	class DatasourceConfigNotFoundException : public std::runtime_error
	{
	public:
		explicit DatasourceConfigNotFoundException(const std::string& message)
			: std::runtime_error(message)
		{
		}
	};

}
