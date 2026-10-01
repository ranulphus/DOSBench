@ECHO OFF
REM DOSBench: the menu, then whatever it chose to run, until Quit.
REM Run from the DOSBench directory (BENCHG.EXE, BENCHGL.EXE, DBMENU.EXE,
REM GLIDE2X.OVL, DATA\, and CWSDPMI.EXE and DOS4GW.EXE here or on PATH).
IF NOT EXIST OUT\NUL MD OUT
:MENU
DBMENU.EXE
IF ERRORLEVEL 2 GOTO RUN
GOTO END
:RUN
REM RUNSEL.BAT ends with this run's results screen (DBMENU --results).
CALL RUNSEL.BAT
GOTO MENU
:END
