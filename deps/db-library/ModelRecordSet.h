#pragma once

#include <memory>
#include <optional>
#include <string>

#include "DatabaseConnManager.h"
#include "Model.h"
#include "SqlBuilder.h"

#include <nanodbc/nanodbc.h>

namespace db
{

	/// \brief queries the templated model table using an iterator to the result set
	template <
		typename ModelType,
		typename BoundModelType = ModelType>
	class ModelRecordSet
	{
	public:
		ModelRecordSet(bool fetchRowCount = false)
			: _fetchRowCount(fetchRowCount)
		{
		}

		~ModelRecordSet()
		{
			close();
		}

		/// \brief closes and releases all associated database resources
		void close()
		{
			_stmt.reset();
			_conn.reset();
		}

		/// \brief fetches the current result set's row count, if requested and available
		const std::optional<int64_t>& rowCount() const
		{
			return _rowCount;
		}

		/// \brief fetches the current result set's column count
		const short columnCount() const
		{
			return _columnCount;
		}

		/// \brief binds the current result record to an associated model object and returns it
		BoundModelType get()
		{
			BoundModelType model {};
			Model::BindResult(_result, model, _bindingIndex);
			return std::move(model);
		}

		/// \brief binds the current result record to an associated model object and returns it
		void get_ref(BoundModelType& model)
		{
			Model::BindResult(_result, model, _bindingIndex);
		}

		/// \brief attempts to move the result iterator one forward
		///
		/// \return Boolean: true if there is a next record, false otherwise
		bool next()
		{
			return _result.next();
		}

		/// \brief opens a connection to the model's database and queries its table using 
		/// a SqlBuilder.
		/// Results are accessed by iterating with next() and requesting a bound model object
		/// with get()
		///
		/// \see next(), get()
		/// \throws db::DatasourceConfigNotFoundException
		/// \throws nanodbc::database_error
		void open() noexcept(false)
		{
			SqlBuilder<ModelType> filterObj {};
			open(filterObj);
		}

		/// \brief opens a connection to the model's database and queries its table using 
		/// a SqlBuilder.
		/// Results are accessed by iterating with next() and requesting a bound model object
		/// with get()
		///
		/// \see next(), get()
		/// \throws db::DatasourceConfigNotFoundException
		/// \throws nanodbc::database_error
		void open(SqlBuilder<ModelType>& filterObj) noexcept(false)
		{
			std::string query;

			_columnCount = 0;
			_rowCount.reset();

			_conn = DatabaseConnManager::GetConnectionTo(ModelType::DbType());

			if (_fetchRowCount)
			{
				std::string query = filterObj.SelectCountString();

				nanodbc::statement stmt(*_conn, query);
				nanodbc::result result = nanodbc::execute(stmt);

				int64_t rowCount = 0;
				if (result.next())
				{
					rowCount = result.get<int64_t>(0);
					if (filterObj.Limit > 0)
						rowCount = (std::min)(rowCount, filterObj.Limit); // NOTE: allow for Windows.h defining min

					_rowCount = rowCount;
				}
			}

			query = filterObj.SelectString();
			_stmt = std::make_unique<nanodbc::statement>(*_conn, query);
			_result = nanodbc::execute(*_stmt);

			_columnCount = _result.columns();

			Model::IndexColumnNameBindings<BoundModelType>(
				_result,
				_columnCount,
				_bindingIndex);
		}

	private:
		std::shared_ptr<nanodbc::connection> _conn {};
		std::unique_ptr<nanodbc::statement> _stmt {};
		nanodbc::result _result {};
		Model::BindingIndex<BoundModelType> _bindingIndex {};
		bool _fetchRowCount {};
		std::optional<int64_t> _rowCount {};
		short _columnCount {};
	};

}
