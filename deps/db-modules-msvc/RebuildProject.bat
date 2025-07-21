@ECHO OFF
SETLOCAL DisableDelayedExpansion

:: Config
SET "SOURCE_DIR=..\db-modules"
SET "OUTPUT_FILE=%~dp0db-modules.vcxproj"
SET "TEMPLATE_FILE=%OUTPUT_FILE%.template"
SET "OUTPUT_FILE_FILTERS=%OUTPUT_FILE%.filters"

:: Get absolute path of source dir so that we can replace it in the output with relative paths
PUSHD "%SOURCE_DIR%"
SET "ABS_SOURCE_DIR=%CD%"
POPD

ECHO Generating %OUTPUT_FILE% from %TEMPLATE_FILE%...

> "%OUTPUT_FILE%" (
	FOR /F "usebackq delims=" %%L IN ("%TEMPLATE_FILE%") DO (
		REM Found the placeholder. Find all our module files and include them here.
		IF "%%L"=="<!-- SOURCE FILES -->" (
			REM Scan for all .ixx (module) files recursively
			CALL "%~dp0GenerateIncludes.cmd" "%SOURCE_DIR%" "%ABS_SOURCE_DIR%"
		) ELSE IF "%%L"=="</Project>" (
			REM Preserve official project behaviour with no trailing newline (so VS shouldn't change it)
			<nul SET /P=^</Project^>
		) ELSE (
			REM Echo lines starting with special characters safely (it looks weird, but that's batch for you)
			ECHO(%%L
		)
	)
)

ECHO Done.

> "%OUTPUT_FILE_FILTERS%" (
	CALL "%~dp0GenerateFilters.cmd" "%SOURCE_DIR%" "%ABS_SOURCE_DIR%"
)

EXIT /B 0
