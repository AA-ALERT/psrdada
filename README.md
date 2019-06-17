# psrdada
Local changes for the AA-Alert project for the PRSDada libraray

This is a clone of the [PSRDada repository on SourceForge](http://psrdada.sourceforge.net/)

Summary of changes:
 * metadata written by dada\_dbevent to the ringbuffer header

# dadadb\_event trigger format
An event looks like this:
```
  N_EVENTS [n]
  [UTC_START]
  [START] [START_FRACTIONAL] [END] [END_FRACTIONAL] [ARRIVAL] [ARRIVAL_FRACTIONAL] [DM] [SNR] [WIDTH] [BEAM]
```
Where N\_EVENTS is a literal string, and [NAME] are variables in the following format:
 * n: uint64
 * UTC\_START, START, END, ARRIVAL: strptime format: "%Y-%m-%d-%H:%M:%S", with UTC appended (no conversions)
 * START\_FRACTIONAL, END\_FRACTIONAL, ARRIVAL\_FRACTIONAL: uint64
 * DM: sscanf format %f
 * SNR: sscanf format %f
 * WIDTH: sscanf format %f
 * BEAM: sscanf format %u

# Contact
Jisk Attema j.attema@esciencecenter.nl
