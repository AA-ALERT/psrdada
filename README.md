# psrdada
Local changes for the AA-Alert project for the PRSDada libraray

This is a clone of the [PSRDada repository on SourceForge](http://psrdada.sourceforge.net/)

Summary of changes:

 * metadata written by dada\_dbevent to the ringbuffer header
 * inclusion of arrival time in the triggering

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


The metadata that is added to the ringbuffer header is:

| Key                       | Type  | Documentation                             |
|---------------------------|-------|-------------------------------------------|
| OBS\_OFFSET               |uint64 |                                           |
| FILE\_SIZE                |ld     |                                           |
| N\_EVENTS                 |u      | Number of events in this slice of data    |
| | | |
| EVENTxxxx\_SNR            |f      | Copied from trigger                       |
| EVENTxxxx\_DM             |f      | Copied from trigger                       |
| EVENTxxxx\_WIDTH          |f      | Copied from trigger                       |
| EVENTxxxx\_BEAM           |u      | Copied from trigger                       |
| EVENTxxxx\_ARRIVAL        |uint64 | Copied from trigger                       |
| EVENTxxxx\_ARRIVAL\_NUMER |uint64 | Copied from trigger                       |
| EVENTxxxx\_ARRIVAL\_DENOM |uint64 | Copied from trigger                       |

Where ```xxxx``` is the event number (a zero padded integer starting at 0, reset for each new ringbuffer header).

# Contact
Jisk Attema j.attema@esciencecenter.nl
