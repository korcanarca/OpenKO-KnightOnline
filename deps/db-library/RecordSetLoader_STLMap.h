#pragma once

#include "RecordSetLoader.h"

#include <type_traits>

namespace recordset_loader
{
	template <
		typename ContainerType,
		typename ModelType = ContainerType::ValueType>
	class STLMap : public Base<ModelType>
	{
	public:
		STLMap(ContainerType& targetContainer)
			: _targetContainer(targetContainer)
		{
		}

	protected:
		void ProcessRow(db::ModelRecordSet<ModelType>& recordset) override
		{
			using ContainerKeyType = std::remove_const_t<
				std::remove_reference_t<typename ContainerType::KeyType>>;

			using ModelKeyType = std::remove_const_t<
				std::remove_reference_t<decltype(std::declval<ModelType>().MapKey())>>;

			// For integral key types (realistically should be all cases),
			// ensure the container's key type covers it the model's key type.
			// i.e. If the model's primary key is int16_t, and our container's type
			// is int32_t (which is the default), we should allow it.
			if constexpr (std::is_integral_v<ModelKeyType>)
			{
				static_assert(sizeof(ContainerKeyType) >= sizeof(ModelKeyType));
			}
			// For any other key types, ensure the model's primary key matches our container's primary key.
			else
			{
				static_assert(std::is_same_v<ContainerKeyType, ModelKeyType>);
			}

			ModelType* model = new ModelType();
			recordset.get_ref(*model);

			ModelKeyType id = model->MapKey();
			if (!_localContainer.PutData(id, model))
				delete model;
		}

		void Finalize() override
		{
			_targetContainer.Swap(_localContainer);
		}

	protected:
		ContainerType& _targetContainer;
		ContainerType _localContainer;
	};

}
