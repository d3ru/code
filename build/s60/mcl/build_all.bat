call sbs -c armv5_urel
call sbs -c armv5_urel.fshellminigui
call sbs -c armv5_urel.fshelltshell

@REM the ~dp below turns %0 (the path to this script) into the path for this directory - means you don't have to be cd'd into this directory to run this script correctly
call sbs -c armv5_urel.smp -c armv5_urel.fshellminigui.smp -c armv5_urel.fshelltshell.smp -b %~dp0..\..\common\drivers\bld.inf
