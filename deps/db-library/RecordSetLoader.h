#pragma once

#include <string>

namespace recordset_loader
{
	struct Error
	{
		enum class EnumErrorType
		{
			None,
			DatasourceConfig,
			Database,
			EmptyTable
		};

		EnumErrorType	Type = EnumErrorType::None;
		std::string		Message;
	};

}
