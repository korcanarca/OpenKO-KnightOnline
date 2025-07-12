#pragma once

#include "RecordSetLoader.h"

#include <type_traits>

namespace recordset_loader
{

	template <typename ModelType>
	class Vector : public Base<ModelType>
	{
	public:
		using ContainerType = std::vector<ModelType*>;
		using RecordSetType = Base<ModelType>::RecordSetType;

		Vector(ContainerType& targetContainer, size_t capacity = 0)
			: _targetContainer(targetContainer), _capacity(capacity)
		{
			this->SetProcessFetchCallback(
				std::bind(&Vector::ProcessFetch, this, std::placeholders::_1));
		}

	protected:
		void ProcessFetch(RecordSetType& recordset)
		{
			ContainerType localContainer;
			if (_capacity != 0)
				localContainer.reserve(_capacity);

			do
			{
				ModelType* model = new ModelType();
				recordset.get_ref(*model);
				localContainer.push_back(model);
			}
			while (recordset.next());

			_targetContainer.swap(localContainer);
		}

	protected:
		ContainerType& _targetContainer;
		size_t _capacity;
	};

}
