#pragma once

#include <memory>
#include <string>

#include "DatabaseConnManager.h"
#include "Model.h"
#include "SqlBuilder.h"

#include <nanodbc/nanodbc.h>

namespace db
{

	/// \brief queries the templated model table using an iterator to the result set
	template <typename T>
	class ModelRecordSet
	{
	public:
		/// \brief opens a connection to the model's database and queries its table using 
		/// a SqlBuilder.
		/// Results are accessed by iterating with next() and requesting a bound model object
		/// with get()
		///
		/// \see next(), get()
		/// \throws nanodbc::database_error
		ModelRecordSet(DatabaseConnManager& mgr, SqlBuilder<T>& filterObj) noexcept(false)
			: _mgr(mgr)
		{
			open(filterObj);
		}

		/// \brief opens a connection to the model's database and queries its table using 
		/// a SqlBuilder.
		/// Results are accessed by iterating with next() and requesting a bound model object
		/// with get()
		///
		/// \see next(), get()
		/// \throws nanodbc::database_error
		ModelRecordSet(DatabaseConnManager& mgr) noexcept(false)
			: _mgr(mgr)
		{
			SqlBuilder<T> filterObj {};
			open(filterObj);
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

		/// \brief binds the current result record to an associated model object and returns it
		T get()
		{
			T model {};
			Model::BindResult(_result, model, _bindingIndex);
			return std::move(model);
		}

		/// \brief binds the current result record to an associated model object and returns it
		void get_ref(T& model)
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

	protected:
		/// \brief opens a connection to the model's database and queries its table using 
		/// a SqlBuilder.
		/// Results are accessed by iterating with next() and requesting a bound model object
		/// with get()
		///
		/// \see next(), get()
		/// \throws nanodbc::database_error
		void open(SqlBuilder<T>& filterObj) noexcept(false)
		{
			_conn = _mgr.GetConnectionTo(T::DbType());

			std::string query = filterObj.SelectString();
			_stmt = std::make_unique<nanodbc::statement>(*_conn, query);
			_result = nanodbc::execute(*_stmt);

			Model::IndexColumnNameBindings<T>(
				_result,
				_bindingIndex);
		}

	private:
		DatabaseConnManager& _mgr;
		std::shared_ptr<nanodbc::connection> _conn;
		std::unique_ptr<nanodbc::statement> _stmt;
		nanodbc::result _result;
		Model::BindingIndex<T> _bindingIndex;
	};

}
