#pragma once

#include <string>
#include <functional>

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

	template <typename ModelType_>
	class Base
	{
	public:
		using ModelType = ModelType_;
		using RecordSetType = db::ModelRecordSet<ModelType>;

		using ProcessFetchCallbackType = std::function<void(RecordSetType&)>;

		Base() = default;

		inline const Error& GetError() const
		{
			return _error;
		}

		inline void SetProcessFetchCallback(
			ProcessFetchCallbackType processFetchCallback)
		{
			_processFetchCallback = std::move(processFetchCallback);
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
		template <bool AllowEmptyTable>
		bool Load()
		{
			using EnumErrorType = Error::EnumErrorType;

			try
			{
				RecordSetType recordset;

				if constexpr (!AllowEmptyTable)
				{
					if (!recordset.next())
					{
						_error.Type = EnumErrorType::EmptyTable;
						_error.Message = "Table empty!";
						return false;
					}

					if (_processFetchCallback != nullptr)
						_processFetchCallback(recordset);
				}
				else /* constexpr branch */
				{
					// We have no reason to trigger the callback if it was empty.
					if (recordset.next())
					{
						if (_processFetchCallback != nullptr)
							_processFetchCallback(recordset);
					}
				}

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
		std::function<void(RecordSetType&)> _processFetchCallback = {};
	};

}
