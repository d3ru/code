@REM make sure everything is exported
call sbs --export-only

@REM the ~dp below turns %0 (the path to this script) into the path for this directory - means you don't have to be cd'd into this directory to run this script correctly

@REM Do the SMP ones first because the armv5_urel one will try to build a SIS, which needs the SMP binaries to be ready.
@REM and skip if there doesn't seem to be any SMP support
@if not exist %EPOCROOT%epoc32\release\armv5smp goto nextstep
call sbs -c armv5_urel.smp -c armv5_urel.fshellminigui.smp -c armv5_urel.fshelltshell.smp -b %~dp0..\..\common\drivers\bld.inf
@if %ERRORLEVEL% NEQ 0 goto bail

:nextstep
call sbs -c armv5_urel -b %~dp0bld.inf
@if %ERRORLEVEL% NEQ 0 goto bail

call sbs -c armv5_urel.fshellminigui -b %~dp0bld.inf
@if %ERRORLEVEL% NEQ 0 goto bail

call sbs -c armv5_urel.fshelltshell -b %~dp0bld.inf
@if %ERRORLEVEL% NEQ 0 goto bail

@goto end

:bail

@echo fshell build failed with %ERRORLEVEL%.

:end
