#include "pch.h"
#include "utils.h"
#include "hooks.h"

void db::utils::Log(const std::string& message)
{
	if (hooks::Log == nullptr)
		return;

	auto now = std::chrono::system_clock::now();
	std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
	std::tm localTm {};
	localtime_s(&localTm, &nowTime);

	std::string logLine = std::format("{:04}/{:02}/{:02} {:02}:{:02}:{:02}: {}\r\n",
		localTm.tm_year + 1900,
		localTm.tm_mon + 1,
		localTm.tm_mday,
		localTm.tm_hour,
		localTm.tm_min,
		localTm.tm_sec,
		message);
	hooks::Log(logLine);
}

void db::utils::LogDatabaseError(const nanodbc::database_error& dbErr, const char* source)
{
	std::string logLine = std::format("{}: {}", source, dbErr.what());
	Log(logLine);
}
