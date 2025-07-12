#pragma once

#include "RecordSetLoader.h"

#include "Model.h"
#include "ModelRecordSet.h"
#include "Exceptions.h" // db::DatasourceConfigNotFoundException

#include <nanodbc/nanodbc.h> // nanodbc::database_error

namespace recordset_loader
{
	namespace
	{
		template <typename ContainerType, bool AllowEmptyTable>
		static bool STLMap(ContainerType& container, Error& err)
		{
			using ModelType = ContainerType::ValueType;
			using EnumErrorType = Error::EnumErrorType;

			using ContainerKeyType = ContainerType::KeyType;
			using ModelKeyType = decltype(std::declval<ModelType>().MapKey());

			// Ensure the model's primary key matches our container's primary key.
			static_assert(
				std::is_same_v<
					std::remove_const_t<std::remove_reference_t<ContainerKeyType>>,
					std::remove_const_t<std::remove_reference_t<ModelKeyType>>>);

			try
			{
				db::ModelRecordSet<ModelType> recordset;
				ContainerType localMap;

				while (recordset.next())
				{
					ModelType* model = new ModelType();
					recordset.get_ref(*model);

					ModelKeyType id = model->MapKey();
					if (!localMap.PutData(id, model))
						delete model;
				}

				if constexpr (!AllowEmptyTable)
				{
					if (localMap.IsEmpty())
					{
						err.Type = EnumErrorType::EmptyTable;
						err.Message = "Table empty!";
						return false;
					}
				}

				container.Swap(localMap);
				return true;
			}
			catch (const db::DatasourceConfigNotFoundException& ex)
			{
				err.Type = EnumErrorType::DatasourceConfig;
				err.Message = ex.what();
			}
			catch (const nanodbc::database_error& ex)
			{
				err.Type = EnumErrorType::Database;
				err.Message = ex.what();
			}

			return false;
		}
	}

	template <typename ContainerType>
	static inline bool STLMap_AllowEmpty(ContainerType& container, Error& err)
	{
		return STLMap<ContainerType, true>(container, err);
	}

	template <typename ContainerType>
	static inline bool STLMap_ForbidEmpty(ContainerType& container, Error& err)
	{
		return STLMap<ContainerType, false>(container, err);
	}

}
