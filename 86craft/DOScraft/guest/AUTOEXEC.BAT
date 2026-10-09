@ECHO OFF
SET PATH=A:\
ECHO DOScraft isolated platform test. No paging.
CWSDPMI -p -s-
PLATFORM.EXE /AUTO
ECHO Platform test finished. See C:\PLATFORM.TXT.
