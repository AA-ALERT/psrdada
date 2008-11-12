#!/usr/bin/env perl

###############################################################################
#
# server_bpsr_tape_archiver.pl
#

use lib $ENV{"DADA_ROOT"}."/bin";

use IO::Socket;     # Standard perl socket library
use IO::Select;     # Allows select polling on a socket
use Net::hostent;
use File::Basename;
use Bpsr;           # BPSR Module 
use strict;         # strict mode (like -Wall)
use threads;
use threads::shared;


#
# Constants
#
use constant DEBUG_LEVEL  => 2;
use constant PIDFILE      => "bpsr_transfer_manager.pid";
use constant LOGFILE      => "bpsr_transfer_manager.log";
use constant TAPES_DB     => "tapes.db";
use constant FILES_DB     => "files.db";
use constant TAPE_SIZE    => "750.00";

#
# Global Variables
#
our %cfg = Bpsr->getBpsrConfig();      # Bpsr.cfg
our $quit_daemon : shared  = 0;
our $dev = "";
our $robot = 0;                        # Flag for a tape robot
our $current_tape = "";                # Whatever tape is currently in the drive
our $type = "";
our $tape_id_pattern = "";
our $indent = "";                      # log message indenting
our $db_dir = "";


# Autoflush output
$| = 1;

# Signal Handler
$SIG{INT} = \&sigHandle;
$SIG{TERM} = \&sigHandle;

if ($#ARGV != 0) {
  usage();
  exit(1);
} else {

  $type = @ARGV[0];

  if (($type eq "swin") || ($type eq "parkes")) {
    logMessage(1, "Running ".$type." Tape archiver");
  } else {

    if ($type eq "robot_init") {
      logMessage(1, "WARNING: Initializing ALL tapes in robot in 15 seconds. Press CTRL+C to abort this.");
      sleep(15);
    } else {
      usage();
      exit(1);
    }
  }

}

#
# Local Varaibles
#
my $logfile = $cfg{"SERVER_LOG_DIR"}."/".LOGFILE;
my $pidfile = $cfg{"SERVER_CONTROL_DIR"}."/".PIDFILE;

my $daemon_control_thread = 0;

my $i=0;
my @dirs  = ();
my $uc_type = "";

my $user;
my $host;
my $path;

# setup the disks
for ($i=0; $i<$cfg{"NUM_".$uc_type."_DIRS"}; $i++) {
  ($user, $host, $path) = split(/:/,$cfg{$uc_type."_DIR_".$i},3);
  push (@dirs, $path);
}

# location of DB files
$db_dir = $cfg{$uc_type."_DB_DIR"};

# set the pattern tape id pattern for each location
if (($type eq "swin") || ($type eq "robot_init")) {
  $tape_id_pattern = "HRA[0-9][0-9][0-9]S4";
  $robot = 1;
  $uc_type = "SWIN";
} 
if ($type eq "parkes") {
  $tape_id_pattern = "HRE[0-9][0-9][0-9]S4";
  $uc_type = "PARKES";
}

# set global variable for the S4 device name
$dev = $cfg{$uc_type."_S4_DEVICE"};

my $result;
my $response;

#
# Main
#

if ($type eq "robot_init") {

  if (!$quit_daemon) {

    logMessage(0, "STARTING ROBOT INITIALISATION");
    robotInitializeAllTapes();
    logMessage(0, "FINISHED ROBOT INITIALISATION");

  } else {

    logMessage(0, "ABORTED ROBOT INITIALISATION");

  }

  exit(0);

}


# Dada->daemonize($logfile, $pidfile);

logMessage(0, "STARTING SCRIPT");

# Start the daemon control thread
$daemon_control_thread = threads->new(\&daemonControlThread);


# Force a re-read of the current tape. This rewinds the tape
logMessage(1, "main: checking current tape");
($result, $response) = getCurrentTape();

if ($result ne "ok") {
  logMessage(0, "main: getCurrentTape() failed: ".$response);
  exit_script(1);
}

$current_tape = $response;
logMessage(1, "main: current tape = ".$current_tape);

# Get the tape information from the tape database
($result, $response) = getTapeInfo($current_tape);

if ($result ne "ok") {
  logMessage(0, "main : getTapeInfo() failed: ".$response);
  exit_script(1);
}

my ($id, $size, $used, $free, $nfiles, $full) = split(/:/,$response);

# If the current tape is full, we need to switch to the next empty one
if (int($full) == 1) {

  logMessage(1, "tape ".$id." marked full, selecting new tape");

  ($result, $response) = newTape();

  if ($result ne "ok") {
    logMessage(0, "Could not load a new tape: ".$response);
    exit_script(1);
  }

  $current_tape = $response;
  logMessage(1, "New tape selected: ".$current_tape);

  
} else {

  # Since getCurrentTape will have read the first "file"
  logMessage(2, "main: tapeFSF(".($nfiles-1).")");
  ($result, $response) = tapeFSF(($nfiles-1));

  if ($result ne "ok") {
    logMessage(0, "main : ".$response);
    exit_script(1);
  }
}


my $obs = "";
my $dir = "";

$i = 0;
while (!$quit_daemon) {

  # look for a file sequentially in each of the @dirs
  $dir = $dirs[$i];
  logMessage(2, "main: getObsToTar(".$dir.")");

  # Look for files in the @dirs
  $obs = getObsToTar($dir);
  logMessage(2, "main: getObsToTar() ".$obs);

  # If we have one, write to tape
  if ($obs ne "none") {

    logMessage(2, "main: tarObs(".$dir.", ".$obs.")");
    ($result, $response) = tarObs($dir, $obs);
    logMessage(2, "main: tarObs() ".$result." ".$response);
 
  }

  # reset the dirs counter
  if ($i >= ($#dirs+1)) {
    $i = 0;
  }

  $quit_daemon = 1;
}

# rejoin threads
$daemon_control_thread->join();

logMessage(0, "STOPPING SCRIPT");

exit 0;



###############################################################################
#
# Functions
#


#
# try and find an observation to tar
#
sub getObsToTar($) {

  indent();

  (my $dir) = @_;
  logMessage(2, "getObsToTar: ".$dir);

  my $obs = "none";
  my $subdir = "";
  my @subdirs = ();

  # find all subdirectories with an xfer.complete file in them
  opendir(DIR,$dir);
  @subdirs = sort grep { !/^\./ && -f $dir."/".$_."/xfer.complete" } readdir(DIR);
  closedir DIR;

  # now select the best one to tar
  foreach $subdir (@subdirs) {

    if (-f $dir."/".$subdir."/sent.to.tape") {
      # ignore if this has already been sent to tape
      logMessage(2, "getObsToTar: ignoring obs ".$subdir);

    } else {

      logMessage(2, "getObsToTar: found obs ".$subdir);
      $obs = $subdir;
    }    
  }

  logMessage(2, "getObsToTar: returning ".$obs);

  unindent();
  return $obs;
}


#
# tars the observation to the tape drive
#
sub tarObs($$) {

  my ($dir, $obs) = @_;
  indent();

  logMessage(2, "tarObs: (".$dir.", ".$obs.")");
  logMessage(1, "tarObs: archiving observation: ".$obs);

  my $subdir = "";
  my @subdirs = ();
  my $beam_problem = 0;

  opendir(DIR,$dir."/".$obs);
  @subdirs = sort grep { !/^\./ && -d $dir."/".$obs."/".$_ } readdir(DIR);
  closedir DIR;

  my $result = "ok";
  my $response = "";

  foreach $subdir (@subdirs) {

    if (! $beam_problem) {

      logMessage(2, "tarObs: checkIfArchived(".$dir.", ".$obs.", ".$subdir.")");
      ($result, $response) = checkIfArchived($dir, $obs, $subdir);
      logMessage(2, "tarObs: checkIfArchived() ".$result." ".$response);

      if ($result ne "ok") {
        logMessage(0, "tarObs: checkIfArchived failed: ".$response);
        unindent();
        return ("fail", "checkIfArchived() failed: ".$response);
      } 
  
      # If this beam has been archived, skip it 
      if ($response eq "archived") {

        logMessage(1, "Skipping archival of ".$obs."/".$subdir.", already archived to tape");

      # Archive the beam 
      } else {
  
        logMessage(2, "tarObs: tarBeam(".$dir.", ".$obs.", ".$subdir.")");
        ($result, $response) = tarBeam($dir, $obs, $subdir);

        if ($result ne "ok") {
          logMessage(0, "tarObs: tarBeam(".$dir.", ".$obs.", ".$subdir.") failed: ".$response);
          $beam_problem = 1;
        }
      }
    }
  }
  
  if ($beam_problem) {
    logMessage(0, "tarObs: problem archiving a beam");
    unindent();
    return ("fail", "problem occurred during archival of a beam");
  }  

  logMessage(1, "tarObs: finished observation: ".$obs);

  unindent();
  return ("ok", "");
}

#
# tars the beam to the tape drive
#
sub tarBeam($$$) {

  my ($dir, $obs, $beam) = @_;

  indent();
  logMessage(2, "tarBeam: (".$dir.", ".$obs.", ".$beam.")");

  logMessage(1, "tarBeam: archiving ".$obs.", beam ".$beam);

  my $cmd = "";
  my $result = "";
  my $ressponse = "";

  # Check the tape is the expected one
  my $expected_tape = getExpectedTape(); 

  if ($expected_tape ne $current_tape) {
    
    # try to load it?
    logMessage(2, "tarBeam: expected tape mismatch \"".$expected_tape."\" != \"".$current_tape."\"");

    # try to get the tape loaded (robot or manual)
    ($result, $response) = loadTape($expected_tape);

    if ($result ne "ok") {
      unindent();
      return ("fail", "Could not load expected tape: ".$expected_tape);
    }

    $current_tape = $expected_tape;
  }

  my $tape = $expected_tape;

  # Find the combined file size in bytes
  $cmd  = "du -sLb ".$dir."/".$obs."/".$beam;
  logMessage(2, "tarBeam: ".$cmd);

  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {
    logMessage(0, "tarBeam: ".$cmd. "failed: ".$response);
    unindent();
    return ("fail", "Could not determine archive size");
  } 

  # get the upper limit on the archive size
  my $size_est_bytes = tarSizeEst(3, int($response));
  my $size_est_gbytes = $size_est_bytes / (1024*1024*1024);

  # check if this beam will fit on the tape
  #($result, $response) = getTapeSpace($tape);
  ($result, $response) = getTapeInfo($tape);

  if ($result ne "ok") {
    logMessage(0, "tarBeam: getTapeInfo() failed: ".$response);
    unindent();
    return ("fail", "could not determine space left on tape");
  } 

  my ($id, $size, $used, $free, $nfiles, $full) = split(/:/,$response);

  logMessage(2, "tarBeam: ".$free." GB left on tape");
  logMessage(2, "tarBeam: size of this beam is estimated at ".$size_est_gbytes." GB");


  if ($free < $size_est_gbytes) {

    logMessage(0, "tarBeam: tape ".$tape." full. (".$free." < ".$size_est_gbytes.")");

    logMessage(0, "tarBeam: marking tape full");
    # Mark the current tape as full and load a new tape;$a
    ($result, $response) = markTapeFull($tape);

    if ($result ne "ok") {
      logMessage(0, "tarBeam: markTapeFull() failed: ".$response);
      unindent();
      return ("fail", "could not mark tape full");
    }

    # Get the next expected tape
    my $new_tape = getExpectedTape();
    logMessage(0, "tarBeam: new tape is ".$new_tape);

    ($result, $response) = loadTape($new_tape);
    if ($result ne "ok") {
      logMessage(0, "tarBeam: newTape() failed: ".$response);
      unindent();
      return ("fail", "New tape was not loaded");
    }

    $tape = $response; 
    $current_tape = $tape;
  }


  # now we have a tape with enough space to fit this archive
  chdir $dir;
  $cmd = "tar -cf ".$dev." ".$obs."/".$beam;
  logMessage(0, "tarBeam: ".$cmd);

  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {
    logMessage(0, "tarBeam: failed to write archive to tape: ".$response);
    unindent();
    return ("fail", "Archiving failed");
  }

  # Else we wrote 3 files to the TAPE in 1 archive and need to update the database files
  $used += $size_est_gbytes;
  $free -= $size_est_gbytes;
  $nfiles += 1;

  # If less than 100 MB left, mark tape as full
  if ($free < 0.1) {
    $full = 1;
  }

  logMessage(2, "tarBeam: updatesTapesDB($id, $size, $used, $free, $nfiles, $full)");
  ($result, $response) = updateTapesDB($id, $size, $used, $free, $nfiles, $full);
  logMessage(2, "tarBeam: updatesTapesDB(): ".$result." ".$response);
  if ($result ne "ok") {
    logMessage(0, "tarBeam: updateTapesDB() failed: ".$response);
    unindent();
    return("fail", "error ocurred when updating tapes DB: ".$response);
  }

  logMessage(2, "tarBeam: updatesFilesDB(".$obs."/".$beam.", ".$id.", ".$size_est_gbytes.", ".($nfiles-1).")");
  ($result, $response) = updateFilesDB($obs."/".$beam, $id, $size_est_gbytes, ($nfiles-1));
  logMessage(2, "tarBeam: updatesFilesDB(): ".$result." ".$response);
  if ($result ne "ok") {
    logMessage(0, "tarBeam: updateFilesDB() failed: ".$response);
    unindent();
    return("fail", "error ocurred when updating filesDB: ".$response);
  }

  unindent();
  return ("ok",""); 

}


#
# Rewinds the current tape and reads the first "index" file
#
sub getCurrentTape() {
 
  indent(); 
  logMessage(2, "getCurrentTape()"); 

  logMessage(2, "getCurrentTape: tapeGetID()");
  ($result, $response) = tapeGetID();
  logMessage(2, "getCurrentTape: tapeGetID() ".$result." ".$response);

  if ($result ne "ok") {

    logMessage(0, "getCurrentTape: tapeGetID failed: ".$response);
    unindent();
    return ("fail", "bad binary label on current tape");

  }

  # we either have a good ID or no ID
  my $tape_id = $response;

  # Robot / Swinburne
  if ($robot) {

    my $robot_id = "";

    # also get the current tape in the robot
    logMessage(2, "getCurrentTape: getCurrentRobotTape()");
    ($result, $response) = getCurrentRobotTape();
    logMessage(2, "getCurrentTape: getCurrentRobotTape() ".$result." ".$response);

    if ($result ne "ok") {
      logMessage(2, "getCurrentTape: getCurrentRobotTape failed: ".$response);
      unindent();
      return ("fail", "robot could not determine tape label");

    } else {
      $robot_id = $response;
    }

    # check for binary label
    if ($tape_id eq "") {

      # if the robots id was sensible, use it
      if ($robot_id =~ m/^$tape_id_pattern$/) {

        logMessage(2, "getCurrentTape: tapeInit(".$robot_id.")");
        ($result, $response) = tapeInit($robot_id);
        logMessage(2, "getCurrentTape: tapeInit() ".$result." ".$response);

        if ($result ne "ok") {
          logMessage(0, "getCurrentTape: tapeInit() failed: ".$response);
          unindent();
          return ("fail", "could not initialise tape");

        # tape was successfully initialized
        } else {
          logMessage(1, "getCurrentTape: successfully intialized tape: ".$response);
          return ("ok", $response);
        }

      } else {

        logMessage(0, "getCurrentTape: no binary label and physical label was malformed");
        unindent();
        return ("fail", "no binary label and physical label was malformed");

      }
 
    # the binary label existed  
    } else {

      # check that the binary label matches the pattern
      if (!($tape_id =~ m/^$tape_id_pattern$/)) {
        logMessage(0, "getCurrentTape: binary label ".$tape_id." did not match pattern");
        unindent();
        return ("fail", "binary label on tape ".$tape_id." was malformed");
      }

      # if the robots id was sensible, use it
      if (!($robot_id =~ m/^$tape_id_pattern$/)) {
        logMessage(0, "getCurrentTape: physical label ".$robot_id." did not match pattern");
        unindent();
        return ("fail", "physical label on tape ".$robot_id." was malformed");
      }

      if ($robot_id ne $tape_id) {
        logMessage(0, "getCurrentTape: physical label ".$robot_id." did not match binary label".$tape_id);
        unindent();
        return ("fail", "physical and binary labels did not match (".$robot_id." != ".$tape_id.")");
      }

      # ID's matched and we of the right form
    }

  # if we have no robot
  } else {

    # check for empty tape with no binary label
    if ($tape_id eq "") {
      logMessage(0, "getCurrentTape: no binary label existed on tape");
      unindent();
      return ("fail", "no binary label existed on tape");
    }

    # check that the binary label matched the pattern
    if (!($tape_id =~ m/^$tape_id_pattern$/)) {
      logMessage(0, "getCurrentTape: binary label ".$tape_id." did not match pattern");
      unindent();
      return ("fail", "binary label on tape ".$tape_id." was malformed")
    } 

  }

  logMessage(2, "getCurrentTape: current tape = ".$tape_id);
  unindent();
  return ("ok", $tape_id);

}

#
# Get tape specified loaded into the drive. fail on timeout
#
sub loadTape($) {

  (my $tape) = @_;
  indent();

  logMessage(2, "loadTape (".$tape.")");
  my $cmd = "";

  if ($robot) {

    logMessage(2, "loadTape: robotGetStatus()");
  
    my %status = robotGetStatus();
    my @keys = keys (%status);

    my $slot = "none";

    # find the tape
    my $i=0;
    for ($i=0; $i<=$#keys; $i++) {
      if ($tape eq $status{$keys[$i]}) {
        $slot = $keys[$i];
      }
    }

    if ($slot eq "none") {

      logMessage(0, "loadTape: tape ".$tape." did not exist in robot");
      unindent();
      return ("fail", "tape not in robot") ;

    } elsif ($slot eq "transfer") {

      logMessage(2, "loadTape: tape ".$tape." was already in transfer slot");
      unindent();
      return ("ok","");

    } else {

      logMessage(2, "loadTape: tape ".$tape." in slot ".$slot);

      # unload the current tape
      logMessage(2, "loadTape: robotUnloadCurrentTape()");
      ($result, $response) = robotUnloadCurrentTape();
      if ($result ne "ok") {
        logMessage(0, "loadTape: robotUnloadCurrentTape failed: ".$response);
        unindent();
        return ("fail", "Could not unload current robot tape: ".$response);
      }

      # load the tape in the specified slot
      logMessage(2, "loadTape: robotLoadTapeFromSlot(".$slot.")");
      ($result, $response) = robotLoadTapeFromSlot($slot);
      logMessage(2, "loadTape: robotLoadTapeFromSlot: ".$result." ".$response);
      if ($result ne "ok") {
        logMessage(0, "loadTape: robotLoadTapeFromSlot failed: ".$response);
        unindent();
        return ("fail", "Could not load tape in robot slot ".$slot);
      }

      # Now that its loaded ok, check the ID matches the barcode
      logMessage(2, "loadTape: tapeGetID()");
      ($result, $response) = tapeGetID();
      logMessage(2, "loadTape: tapeGetID: ".$result." ".$response);

      if ($result ne "ok") {
        logMessage(2, "loadTape: tapeGetID() failed: ".$response);
        unindent();
        return ("fail", "could not get ID from newly loaded tape: ".$response);
      }

      my $id = $response;

      # Test that the new tapes ID matched
      if ($id ne $tape) {

        # Initialize the tape with the specified ID 
        logMessage(0, "ID on tape \"".$id."\" did not match tape label \"".$tape."\"");

        logMessage(2, "loadTape: tapeInit(".$tape.")");
        ($result, $response) = tapeInit($tape);
        logMessage(2, "loadTape: tapeInit: ".$result." ".$response);

        if ($result ne "ok") {
          logMessage(0, "loadTape: tapeInit failed: ".$response);
          unindent();
          return ("fail", "could not initialize tape");
        }
      }
      unindent();
      return ("ok", "");
    }

  } else {

    # No tape robot
    my $inserted_tape = "none";
    my $n_tries = 10;

    while (($inserted_tape ne $tape) && ($n_tries >= 0)) {

      # Ask the user to insert the tape
      logMessage(2, "loadTape: manualInsertTape()");
      ($result, $response) = manualInsertTape($tape);
      logMessage(2, "loadTape: manualInsertTape: ".$result." ".$response);
      if ($result ne "ok") {
        logMessage(0, "loadTape: manualInsertTape() failed: ".$response);
      }

      logMessage(2, "loadTape: tapeGetID()");
      ($result, $response) = tapeGetID();
      logMessage(2, "loadTape: tapeGetID() ".$result." ".$response);
      if ($result ne "ok") {
        logMessage(0, "loadTape: tapeGetID() failed: ".$response);
      } else {
        $inserted_tape = $response;
      }

      $n_tries--;

    }

    unindent();
    if ($inserted_tape eq $tape) {
      return ("ok", "");
    } else {
      return ("fail", $tape." was not inserted after 10 attempts");
    }
  }
}


#
# Read the local tape database and determine what the current
# tape should be
#
sub getExpectedTape() {
  
  indent();
  my $fname = $db_dir."/".TAPES_DB;
  my $expected_tape = "none";

  logMessage(2, "getExpectedTape: ()");

  open FH, "<".$fname or return ("fail", "Could not read tapes db ".$fname); 
  my @lines = <FH>;
  close FH;

  my $line = "";
  # parse the file
  foreach $line (@lines) {

    chomp $line;

    if ($line =~ /^#/) {
      # ignore comments
    } else {

      logMessage(3, "getExpectedTape: testing ".$line);

      if ($expected_tape eq "none") {
        my ($id, $size, $used, $free, $nfiles, $full) = split(/ +/,$line);
     
        if (int($full) == 1) {
          logMessage(2, "getExpectedTape: skipping tape ".$id.", marked full");
        } elsif ($free < 0.1) {
          logMessage(1, "getExpectedTape: skipping tape ".$id." only ".$free." MB left");
        } else {
          $expected_tape = $id;
        }
      }
    }
  } 
  logMessage(2, "getExpectedTape: returning ".$expected_tape);
  unindent();
  if ($expected_tape ne "none") {
    return ("ok", $expected_tape);
  } else {
    return ("fail", "could not find acceptable tape");
  }
}

#
# Determine what the next tape should be from tapes.db
# and try to get it loaded
#
sub newTape() {

  indent();
  logMessage(2, "newTape()");

  my $result = "";
  my $response = "";

  # Determine what the "next" tape should be
  logMessage(2, "newTape: getExpectedTape()");
  ($result, $response) = getExpectedTape();
  logMessage(2, "newTape: getExpectedTape() ".$result." ".$response);

  if ($result ne "ok") {
    unindent();
    return ("fail", "getExpectedTape failed: ".$response);
  }
  
  my $new_tape = $response;

  # Now get the tape loaded
  logMessage(2, "newTape: loadTape(".$new_tape.")");
  ($result, $response) = loadTape($new_tape);
  logMessage(2, "newTape: loadTape(): ".$result." ".$response);

  if ($result ne "ok") {
    unindent();
    return ("fail", "loadTape failed: ".$response);
  }

  unindent();
  return ("ok", $new_tape);

}


sub updateTapesDB($$$$$$) {

  my ($id, $size, $used, $free, $nfiles, $full) = @_;

  indent();

  logMessage(2, "updateTapesDB: ($id, $size, $used, $free, $nfiles, $full)");

  my $fname = $db_dir."/".TAPES_DB;
  my $expected_tape = "none";

  open FH, "<".$fname or return ("fail", "Could not read tapes db ".$fname);
  my @lines = <FH>;
  close FH;

  my $newline = $id."  ";

  $newline .= floatPad($size, 3, 2)."  ";
  $newline .= floatPad($used, 3, 2)."  ";
  $newline .= floatPad($free, 3, 2)."  ";
  $newline .= sprintf("%06d",$nfiles)."  ";
  $newline .= $full;


  #my $newline = $id."  ".sprintf("%05.2f",$size)."  ".sprintf("%05.2f",$used).
  #              "  ".sprintf("%05.2f",$free)."  ".$nfiles."       ".$full."\n";

  open FH, ">".$fname or return ("fail", "Could not write to tapes db ".$fname);

  # parse the file
  my $line = "";
  foreach $line (@lines) {

    if ($line =~ /^$id/) {
      logMessage(1, "updateTapesDB: ".$newline);
      print FH $newline."\n";
    } else {
      print FH $line;
    }
  
  }

  close FH;

  unindent();
  return ("ok", "");

}

#
# update the Files DB
#
sub updateFilesDB($$$$) {

  my ($archive, $tape, $fsf, $size) = @_;

  indent();
  logMessage(2, "updateFilesDB(".$archive.", ".$tape.", ".$fsf.", ".$size.")");

  my $fname = $db_dir."/".FILES_DB;

  my $date = Dada->getCurrentDadaTime();

  my $newline = $archive." ".$tape." ".$date." ".$fsf." ".$size;

  open FH, ">>".$fname or return ("fail", "Could not write to tapes db ".$fname);
  logMessage(1, "updateFilesDB: ".$newline);
  print FH $newline."\n";
  close FH;

  unindent();
  return ("ok", "");
}


sub getTapeInfo($) {

  my ($id) = @_;

  indent();
  logMessage(2, "getTapeInfo: (".$id.")");

  my $fname = $db_dir."/".TAPES_DB;

  open FH, "<".$fname or return ("fail", "Could not read tapes db ".$fname);
  my @lines = <FH>;
  close FH;

  my $size = -1;
  my $used = -1;
  my $free = -1;
  my $nfiles = 0;
  my $full = 0;

  # parse the file
  my $line = "";
  foreach $line (@lines) {
    
    chomp $line;

    if ($line =~ m/^$id/) {

      logMessage(3, "getTapeInfo: processing line: ".$line);
      ($id, $size, $used, $free, $nfiles, $full) = split(/ +/,$line);
      
    } else {

      logMessage(3, "getTapeInfo: ignoring line: ".$line);
      #ignore
    }
  }

  $nfiles = int($nfiles);

  if ($size eq -1) {
    unindent();
    return ("fail", "could not determine space from tapes.db");
  } else {

    logMessage(2, "getTapeInfo: id=".$id.", size=".$size.", used=".$used.", free=".$free.", nfiles=".$nfiles.", full=".$full);
    unindent();
    return ("ok", $id.":".$size.":".$used.":".$free.":".$nfiles.":".$full);
  }

}





#
# get the current tape in the robot
#
sub getCurrentRobotTape() {

  indent();
  logMessage(2, "getCurrentRobotTape()");

  my $cmd = "mtx status | grep 'Data Transfer Element' | awk '{print \$10}'";
  logMessage(2, "getCurrentRobotTape: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {

    logMessage(0, "getCurrentRobotTape: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "could not determine current tape in robot");

  }

  logMessage(2, "getCurrentRobotTape: ID = ".$response);
  unindent();
  return ("ok", $response);
   
}

#
# Checks the beam directory to see if it has been marked as archived
# and also checks the files.db to check if it has been recorded as
# archived. Returns an error on mismatch.
#
sub checkIfArchived($$$) {

  my ($dir, $obs, $beam) = @_;
  indent();
  logMessage(2, "checkIfArchived(".$dir.", ".$obs.", ".$beam.")");

  my $cmd = "";

  my $archived_db = 0;    # If the obs/beam is recorded in FILES_DB
  my $archived_disk = 0;  # If the obs/beam has been marked with sent.to.tape file 

  # Check the files.db to see if the beam is recorded there
  $cmd = "grep '".$obs."/".$beam."' ".$db_dir."/".FILES_DB;
  logMessage(2, "checkIfArchived: ".$cmd);
  my $grep_result = `$cmd`;
    
  # If the grep command failed, probably due to the beam not existing in the file
  if ($? != 0) {

    logMessage(2, "checkIfArchived: ".$obs."/".$beam." did not exist in ".$db_dir."/".FILES_DB);
    $archived_db = 0;

  } else {

    logMessage(2, "checkIfArchived: ".$obs."/".$beam." existed in ".$db_dir."/".FILES_DB);
    $archived_db = 1;

    # check there is only 1 entry in files.db
    my @lines = split(/\n/, $grep_result);
    if ($#lines != 0) {
      logMessage(0, "checkIfArchived: more than 1 entry for ".$obs."/".$beam." in ".$db_dir."/".FILES_DB);
      unindent();
      return("fail", $obs."/".$beam." had more than 1 entry in FILES database");
    } 

  }

  # Check the directory for a sent.to.tape file
  if (-f $dir."/".$obs."/".$beam."/sent.to.tape") {
    $archived_disk = 1;
    logMessage(2, "checkIfArchived: ".$dir."/".$obs."/".$beam."/sent.to.tape existed");
  } else {
    logMessage(2, "checkIfArchived: ".$dir."/".$obs."/".$beam."/sent.to.tape did not exist");
    $archived_disk = 0;
  }

  unindent();
  if (($archived_disk == 0) && ($archived_db == 0)) {
    return ("ok", "not archived");
  } elsif (($archived_disk == 1) && ($archived_db == 1)) {
    return ("ok", "archived");
  } else {
    return ("fail", "FILES database does not match flagged files on disk");
  }

}




###############################################################################
##
## ROBOT FUNCTIONS
##


#
# Return array of current robot status
#
sub robotGetStatus() {

  indent();
  logMessage(2, "robotGetStatus()");
  my $cmd = "";
  my $result = "";
  my $response = "";
  
  $cmd = "mtx status";
  logMessage(2, "robotGetStatus: ".$cmd);

  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {
    logMessage(2, "robotGetStatus: ".$cmd." failed: ".$response);
    unindent();
    return "fail";
  }

  # parse the response
  my $line = "";
  my @lines = split(/\n/,$response);

  my %results = ();

  foreach $line (@lines) {

    my @tokens = ();
    @tokens = split(/ +/,$line);

    if ($line =~ m/^Data Transfer Element/) {

      logMessage(3, "Transfer: $line");
      if ($tokens[3] eq "0:Full") {
        $results{"transfer"} = $tokens[9];
      } else {
        $results{"transfer"} = "Empty";
      }

    } elsif ($line =~ m/Storage Element/) {

      logMessage(3, "Storage: $line");
      my ($slotid, $state) = split(/:/,$tokens[3]);
      
      if ($state eq "Empty") {
        $results{$slotid} = "Empty";
      } else {
        my ($junk, $tapeid) = split(/=/,$tokens[4]);
        $results{$slotid} = $tapeid;
      } 
    } else {
      # ignore
    }
  } 

  unindent();
  return %results;
}



#
# Unloads the tape currently in the robot
#
sub robotUnloadCurrentTape() {

  indent();

  logMessage(2, "robotUnloadCurrentTape()");
  my $cmd = "";

  $cmd = "mt -f ".$dev." eject";
  logMessage(2, "robotUnloadCurrentTape: ".$cmd);

  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {
    logMessage(0, "robotUnloadCurrentTape: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "eject command failed");
  }

  $cmd = "mtx unload";
  logMessage(2, "robotUnloadCurrentTape: ".$cmd);
                                                                                                                             
  ($result, $response) = Dada->mySystem($cmd);
                                                                                                                             
  if ($result ne "ok") {
    logMessage(0, "robotUnloadCurrentTape: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "mtx unload command failed");;
  }

  unindent();
  return ("ok", "");

}

#
# load the tape in the specified slot
#
sub robotLoadTapeFromSlot($) {

  (my $slot) = @_;
  indent();
  
  logMessage(2, "robotLoadTapeFromSlot(".$slot.")");

  my $cmd = "";
  my $result = "";
  my $response = "";

  $cmd = "mtx load ".$slot." 0";
  logMessage(2, "robotLoadTapeFromSlot: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);
  logMessage(2, "robotLoadTapeFromSlot: ".$result." ".$response);

  if ($result ne "ok") {
    logMessage(0, "robotLoadTapeFromSlot: ".$cmd." failed: ".$response);
    unindent();
    return "fail";
  }

  unindent();
  return ("ok", "");

}

#
# For each tape in the robot, read its label via mtx and write the
# binary label to the first file on the tape. Update the tapes DB
#
sub robotInitializeAllTapes() {

  logMessage(1, "robotInitializeAllTapes()");
  my $result = "";
  my $response = "";
  my $init_error = 0;

  # Get a listing of all tapes
  logMessage(2, "robotInitializeAllTapes: robotGetStatus()");
  my %status = robotGetStatus();

  if ($status{"transfer"} ne "Empty") {

    # Unload whatever tape is in the drive
    logMessage(2, "robotInitializeAllTapes: robotUnloadCurrentTape()");
    ($result, $response) = robotUnloadCurrentTape();
    logMessage(2, "robotInitializeAllTapes: robotUnloadCurrentTape() ".$result." ".$response);
    
    if ($result ne "ok") {
      $init_error = 1;
      logMessage(0, "robotInitializeAllTapes: robotUnloadCurrentTape failed: ".$response);
      return ($result, $response);
    }

  }

  # Get a listing of all tapes
  logMessage(2, "robotInitializeAllTapes: robotGetStatus()");
  %status = robotGetStatus();
  my @keys = keys (%status);

  my $i=0;

  # Go through the sloats, and initialize each tape
  for ($i=0; (($i<=$#keys) && (!$init_error) && (!$quit_daemon)); $i++) {
    my $slot = $keys[$i];

    if ($slot eq "transfer") {
      # ignore the (now empty) transfer slot

    } elsif ($status{$slot} eq "Empty") {
      # ignore empty slots

    } else {

      my $tape = $status{$slot};
      logMessage(1, "robotInitializeAllTapes: initializing tape ".$tape);

      logMessage(2, "robotInitializeAllTapes: robotLoadTapeFromSlot(".$slot.")");
      ($result, $response) = robotLoadTapeFromSlot($slot);
      logMessage(2, "robotInitializeAllTapes: robotLoadTapeFromSlot() ".$result." ".$response);

      if ($result ne "ok") {
        logMessage(0, "robotInitializeAllTapes: robotLoadTapeFromSlot failed: ".$response);
        $init_error = 1;

      } else {
  
        logMessage(2, "robotInitializeAllTapes: tapeInit(".$tape.")");
        ($result, $response) = tapeInit($tape);
        logMessage(2, "robotInitializeAllTapes: tapeInit() ".$result." ".$response);

        if ($result ne "ok") {
          logMessage(0, "robotInitializeAllTapes: tapeInit failed: ".$response);
          $init_error = 1;

        } else {

          # Unload whatever tape is in the drive
          logMessage(2, "robotInitializeAllTapes: robotUnloadCurrentTape()");
          ($result, $response) = robotUnloadCurrentTape();
          logMessage(2, "robotInitializeAllTapes: robotUnloadCurrentTape() ".$result." ".$response);

          if ($result ne "ok") {
            logMessage(0, "robotInitializeAllTapes: robotUnloadCurrentTape() failed: ".$response);
            $init_error = 1;
          }

        }
      }
    }
  }

  if ($init_error) {
    return ("fail", $response);
  } else {
    return ("ok", "");
  }
  
}

##
## ROBOT FUNCTIONS
##
###############################################################################


###############################################################################
##
## MANUAL TAPE FUNCTIONS
##


#
# Contact the User interface and ask it to manually insert the requested
# tape. Requires a direct connection to the machine hosting the web interace
#
sub manualInsertTape($) {

  (my $tape) = @_;

  indent();
  logMessage(2, "manualInsertTape()");
  logMessage(1, "manualInsertTape: asking for tape ".$tape);

  my $cmd = "";

  my $user = "dada";
  my $host = "shrek211";
  my $port = 31001;
  my $dir = "/nfs/control/bpsr/";
  my $opts = "HostKeyAlias=srv0 -o StrictHostKeyChecking=no -o Loglevel=QUIET";

  my $to_file = $type.".state";
  my $from_file = $type.".response";

  # Delete the existing command and response files
  $cmd = "ssh -l ".$user." -p ".$port." -o ".$opts." ".$host." 'cd ".$dir."; rm -f ".$to_file." ".$from_file."'";
  logMessage(2, "manualInsertTape: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);
  logMessage(2, "manualInsertTape: ".$result." ".$response);

  if ($result ne "ok") {
    logMessage(0, "manualInsertTape: could not delete the existing command and response files");
    unindent();
    return ("fail", "could not remove command and response files");
  }

  # Send the "Insert Tape" command
  $cmd = "ssh -l ".$user." -p ".$port." -o ".$opts." ".$host." 'cd ".$dir."; echo \"Insert Tape:::".$tape."\" > ".$to_file."'";
  logMessage(2, "manualInsertTape: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);
  logMessage(2, "manualInsertTape: ".$result." ".$response);

  my $have_response = 0;
  my $n_tries = 10;
  while ((!$have_response) && (!$quit_daemon)) {

    # Wait for a response to appear from the user
    $cmd = "ssh -l ".$user." -p ".$port." -o ".$opts." ".$host." 'cd ".$dir."; cat ".$from_file."'";
    logMessage(2, "manualInsertTape: ".$cmd);
    ($result, $response) = Dada->mySystem($cmd);
    logMessage(2, "manualInsertTape: ".$result." ".$response);

    if ($result eq "ok") {
      $have_response = 1;
    } else {
      logMessage(2, "manualInsertTape: sleeping 10 seconds whilst waiting for reply");
      sleep(10);
      $n_tries--;
    }

  } 

  if ($have_response) {

    my $new_tape = $response;

    # Remove the insert tape command 
    $cmd = "ssh -l ".$user." -p ".$port." -o ".$opts." ".$host." 'cd ".$dir."; rm -f ".$to_file."; echo \"Writing to tape ".$tape."\" > ".$to_file."'";
    logMessage(2, "manualInsertTape: ".$cmd);
    ($result, $response) = Dada->mySystem($cmd);
    logMessage(2, "manualInsertTape: ".$result." ".$response);

    unindent();
    return ("ok", $new_tape);

  } else {
    unindent();
    return ("fail", "did not received a response in 100 seconds");
  }

}


##
## MANUAL TAPE FUNCTIONS
##
###############################################################################

################################################################################
##
## TAPE functions
##

#
# seek forward the specified number of files
#
sub tapeFSF($) {

  (my $nfiles) = @_;

  indent();
  logMessage(2, "tapeFSF: (".$nfiles.")");

  my $cmd = "";
  my $result = "";
  my $response = "";

  $cmd = "mt -f ".$dev." fsf ".$nfiles;
  logMessage(2, "tapeFSF: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {
    logMessage(0, "tapeFSF: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "FSF failed: ".$response);
  }

  unindent();
  return ("ok", "");

} 


#
# Initialise the tape, writing the ID to the first file on the
# tape
#

sub tapeInit($) {

  (my $id) = @_;

  indent();
  logMessage(2, "tapeInit(".$id.")");

  my $result = "";
  my $response = "";

  logMessage(2, "tapeInit: tapeWriteID(".$id.")");
  ($result, $response) = tapeWriteID($id);
  logMessage(2, "tapeInit: tapeWriteID: ".$result." ".$response);

  if ($result ne "ok") {
    logMessage(0, "tapeInit: tapeWriteID() failed: ".$response);
    unindent();
    return ("fail", "could not write tape ID: ". $response);
  }
                                              
  logMessage(2, "tapeInit: tapeGetID()");
  ($result, $response) = tapeGetID();
  logMessage(2, "tapeInit: tapeGetID: ".$result." ".$response);

  if ($result ne "ok") {
    logMessage(0, "tapeInit: tapeGetID() failed: ".$response);
    unindent();
    return ("fail", "could not get tape ID from tape");
  }

  if ($id ne $response) {
    logMessage(0, "tapeInit: newly written ID did not match specified");
    unindent();
    return ("fail", "could not write tape ID to tape");
  }

  unindent();
  return ("ok", $id);

}
  

 
#
# Rewind, and read the first file from the tape
#
sub tapeGetID() {

  indent();
  logMessage(2, "tapeGetID()");

  my $cmd = "";
  my $result = "";
  my $response = "";

  my $cmd = "mt -f ".$dev." rewind";
  logMessage(2, "tapeGetID: ".$cmd);

  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {
    logMessage(0, "tapeGetID: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "mt rewind command failed: ".$response);;
  }

  $cmd = "tar -tf ".$dev;
  logMessage(2, "tapeGetID: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {

    # if there is no ID on the tape this command will fail, 
    # but we can test the output message
    if ($response =~ m/tar: At beginning of tape, quitting now/) {

      logMessage(0, "tapeGetID: No ID on Tape");
      unindent();
      return ("ok", "");

    } else {

      logMessage(0, "tapeGetID: ".$cmd." failed: ".$response);
      unindent();
      return ("fail", "tar list command failed: ".$response);

    }
  }

  logMessage(2, "tapeGetID: ID = ".$response);
  unindent();

  return ("ok", $response);
}

#
# Rewind, and write the first file from the tape
#
sub tapeWriteID($) {

  (my $tape_id) = @_;

  indent();
  logMessage(2, "tapeWriteID()");

  my $cmd = "";
  my $result = "";
  my $response = "";

  my $cmd = "mt -f ".$dev." rewind";
  logMessage(2, "tapeWriteID: ".$cmd);

  ($result, $response) = Dada->mySystem($cmd);

  if ($result ne "ok") {
    logMessage(0, "tapeWriteID: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "mt rewind failed: ".$response);
  }

  # create an emprty file in the CWD to use
  $cmd = "touch ".$tape_id;
  logMessage(2, "tapeWriteID: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);
  if ($result ne "ok") {
    logMessage(0, "tapeWriteID: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "could not create tmp file in cwd: ".$response);
  }

  # write the empty file to tape
  $cmd = "tar -cf ".$dev." ".$tape_id;
  logMessage(2, "tapeWriteID: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);

  unlink($tape_id);

  if ($result ne "ok") {
    logMessage(0, "tapeWriteID: ".$cmd." failed: ".$response);
    unindent();
    return ("fail", "could not write ID to tape: ".$response);
  } 

  # Initialisze the tapes DB record also
  logMessage(2, "tapeWriteID: updatesTapesDB(".$tape_id.", ".TAPE_SIZE.", 0, ".TAPE_SIZE.", 1, 0)");
  ($result, $response) = updateTapesDB($tape_id, TAPE_SIZE, 0, TAPE_SIZE, 1, 0);
  logMessage(2, "tapeWriteID: updatesTapesDB(): ".$result.", ".$response);

  unindent();
  return ("ok", $response);

}

##
## TAPE functions
##
################################################################################


#
# Polls for the "quitdaemons" file in the control dir
#
sub daemonControlThread() {

  logMessage(2, "daemon_control: thread starting");

  my $pidfile = $cfg{"SERVER_CONTROL_DIR"}."/".PIDFILE;

  my $daemon_quit_file = Dada->getDaemonControlFile($cfg{"SERVER_CONTROL_DIR"});

  # poll for the existence of the control file
  while ((!-f $daemon_quit_file) && (!$quit_daemon)) {
    logMessage(3, "daemon_control: Polling for ".$daemon_quit_file);
    sleep(1);
  }

  # signal threads to exit
  $quit_daemon = 1;

  logMessage(2, "daemon_control: Unlinking PID file ".$pidfile);
  unlink($pidfile);

  logMessage(2, "daemon_control: exiting");

}

#
# Logs a message to the Nexus
#
sub logMessage($$) {
  (my $level, my $message) = @_;
  if ($level <= DEBUG_LEVEL) {
    my $time = Dada->getCurrentDadaTime();
    print "[".$time."] ".$indent.$message."\n";
  }
}
sub indent() {
  $indent .= "  ";
}

sub unindent() {
  $indent = substr($indent,0,-2);
}

#
# Handle INT AND TERM signals
#
sub sigHandle($) {

  my $sigName = shift;
  print STDERR basename($0)." : Received SIG".$sigName."\n";
  $quit_daemon = 1;
  sleep(3);
  print STDERR basename($0)." : Exiting: ".Dada->getCurrentDadaTime(0)."\n";

}

sub usage() {
  print STDERR "Usage:\n";
  print STDERR basename($0)." [swin|parkes]\n";
}

sub exit_script($) {

  my $val = shift;
  print STDERR "exit_script(".$val.")\n";
  $quit_daemon = 1;
  sleep(3);
  exit($val);

}


sub floatPad($$$) {

  my ($val, $n, $m) = @_;

  my $str = "";

  if (($val >= 10.00000) && ($val < 100.00000)) {
    $str = " ".sprintf("%".($n-1).".".$m."f", $val);
  } elsif ($val < 10.0000) {
    $str = "  ".sprintf("%".($n-2).".".$m."f", $val);
  } else {
    $str = sprintf("%".$n.".".$m."f", $val)
  }

  return $str;
}


#
# Estimate the archive size based on file size and number of files
#
sub tarSizeEst($$) {

  my ($nfiles, $files_size) = @_;

  # 512 bytes for header and up to 512 bytes padding for data
  my $tar_overhead_files = (1024 * $nfiles);

  # all archives are a multiple of 10240 bytes, add for max limit
  my $tar_overhead_archive = 10240;           

  # upper limit on archive size in bytes
  my $size_est = $files_size + $tar_overhead_files + $tar_overhead_archive;

  return $size_est;

}

