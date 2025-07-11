@ECHO OFF
SETLOCAL EnableDelayedExpansion

:: Config
SET "SOURCE_DIR=..\db-modules"
SET "OUTPUT_FILE=%~dp0GeneratedFileList.props"

:: Get absolute path of source dir so that we can replace it in the output with relative paths
PUSHD "%SOURCE_DIR%"
SET "ABS_SOURCE_DIR=%CD%"
POPD

:: Write XML header and opening tags
(
	ECHO ^<?xml version="1.0" encoding="utf-8"?^>
	ECHO ^<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"^>
	ECHO   ^<ItemGroup^>
 
	:: Scan for all .ixx (module) files recursively
	FOR /R "%~dp0%SOURCE_DIR%" %%F IN (*.ixx) DO (
		SET "FILE=%%F"
		SET "FILE=!FILE:%ABS_SOURCE_DIR%=%SOURCE_DIR%!"

		:: Add each file.
		ECHO     ^<ClCompile Include="!FILE!"^>
		ECHO         ^<CompileAsCppModule^>true^</CompileAsCppModule^>
		ECHO     ^</ClCompile^>
	)

	:: Write closing tags
	ECHO   ^</ItemGroup^>
	ECHO ^</Project^>
) > "%OUTPUT_FILE%"

ECHO Generated %OUTPUT_FILE%
EXIT /B 0
