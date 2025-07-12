#pragma once

#include <string>

#include "Model.h"
#include "ModelRecordSet.h"
#include "Exceptions.h" // db::DatasourceConfigNotFoundException

#include <nanodbc/nanodbc.h> // nanodbc::database_error

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

	template <typename ModelType>
	class Base
	{
	public:
		Base() = default;

		inline const Error& GetError() const
		{
			return _error;
		}

		inline bool Load_AllowEmpty()
		{
			return Load<true>();
		}

		inline bool Load_ForbidEmpty()
		{
			return Load<false>();
		}

	protected:
		virtual void ProcessRow(db::ModelRecordSet<ModelType>& recordset) = 0;
		virtual void Finalize() = 0;

		template <bool AllowEmptyTable>
		bool Load()
		{
			using EnumErrorType = Error::EnumErrorType;

			try
			{
				db::ModelRecordSet<ModelType> recordset;

				if constexpr (!AllowEmptyTable)
				{
					if (recordset.at_end())
					{
						_error.Type = EnumErrorType::EmptyTable;
						_error.Message = "Table empty!";
						return false;
					}
				}

				while (recordset.next())
					ProcessRow(recordset);

				Finalize();

				_error.Type = EnumErrorType::None;
				_error.Message.clear();

				return true;
			}
			catch (const db::DatasourceConfigNotFoundException& ex)
			{
				_error.Type = EnumErrorType::DatasourceConfig;
				_error.Message = ex.what();
			}
			catch (const nanodbc::database_error& ex)
			{
				_error.Type = EnumErrorType::Database;
				_error.Message = ex.what();
			}

			return false;
		}

	protected:
		Error _error = {};
	};

}
