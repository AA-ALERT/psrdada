#!/usr/bin/env perl

###############################################################################
#
# Handles observations that have been archived onto parkes tapes. Deletes P630
# survey pointings [source= ^G*]  and archives all other pointings
#

use lib $ENV{"DADA_ROOT"}."/bin";

use strict;        
use warnings;
use File::Basename;
use threads;
use threads::shared;
use Dada;

#
# Constants
#
use constant DATA_DIR       => "/lfs/raid0/bpsr";
use constant META_DIR       => "/lfs/data0/bpsr";
use constant REQUIRED_HOST  => "raid0";
use constant REQUIRED_USER  => "bpsr";


#
# Function prototypes
#
sub controlThread($$);
sub good($);

#
# Global variable declarations
#
our $dl : shared;
our $daemon_name : shared;
our $quit_daemon : shared;
our $warn : shared;
our $error : shared;

#
# Global initialization
#
$dl = 1;
$daemon_name = Dada::daemonBaseName(basename($0));
$quit_daemon = 0;

# Autoflush STDOUT
$| = 1;

# Main
{
  my $log_file   = META_DIR."/logs/".$daemon_name.".log";
  my $pid_file   = META_DIR."/control/".$daemon_name.".pid";
  my $quit_file  = META_DIR."/control/".$daemon_name.".quit";

  my $src_path   = DATA_DIR."/parkes/on_tape";
  my $dst_path   = DATA_DIR."/archived";

  $warn          = META_DIR."/logs/".$daemon_name.".warn";
  $error         = META_DIR."/logs/".$daemon_name.".error";

  my $control_thread = 0;

  my $line = "";
  my $obs = "";
  my $pid = "";
  my $source = "";
  my $beam = "";

  my $cmd = "";
  my $result = "";
  my $response = "";
  my @finished = ();
  my @bits = ();
  my %sources = ();

  my $i = 0;
  my $obs_start_file = "";
  my $n_beam = 0;
  my $n_transferred = 0;

  my $curr_time = 0;
  my $path = "";
  my $mtime = 0;

  # quick sanity check
  ($result, $response) = good($quit_file);
  if ($result ne "ok") {
    print STDERR $response."\n";
    exit 1;
  }

  # install signal handles
  $SIG{INT}  = \&sigHandle;
  $SIG{TERM} = \&sigHandle;
  $SIG{PIPE} = \&sigPipeHandle;

  # become a daemon
  Dada::daemonize($log_file, $pid_file);

  # Auto flush output
  $| = 1;

  Dada::logMsg(0, $dl, "STARTING SCRIPT");

  # start the daemon control thread
  $control_thread = threads->new(\&controlThread, $quit_file, $pid_file);

  # main Loop
  while ( !$quit_daemon ) 
  {
    @finished = ();

    # look for all obs/beams in the src_path
    $cmd = "find ".$src_path." -mindepth 3 -maxdepth 3 -type d -printf '\%h/\%f\n' | sort";
    Dada::logMsg(2, $dl, "main: ".$cmd);
    ($result, $response) = Dada::mySystem($cmd);
    Dada::logMsg(3, $dl, "main: ".$result." ".$response);
    if ($result ne "ok")
    {
      Dada::logMsgWarn($warn, "main: ".$cmd." failed: ".$response);
    }
    else
    {
      @finished = split(/\n/, $response);
      Dada::logMsg(2, $dl, "main: found ".($#finished+1)." on_tape observations");

      for ($i=0; (!$quit_daemon && $i<=$#finished); $i++)
      {  
        $line = $finished[$i]; 
        @bits = split(/\//, $line);
        if ($#bits < 3)
        {
          Dada::logMsgWarn($warn, "main: not enough components in path");
          next;
        }

        $pid  = $bits[$#bits-2];
        $obs  = $bits[$#bits-1];
        $beam = $bits[$#bits];

        Dada::logMsg(2, $dl, "main: processing ".$pid."/".$obs."/".$beam);

        if (exists($sources{$obs})) 
        {
          $source = $sources{$obs};
        }
        else
        {

          # try to find and obs.start file
          $cmd = "find ".$src_path."/".$pid."/".$obs." -mindepth 2 -maxdepth 2 -type f -name 'obs.start' | head -n 1";
          Dada::logMsg(3, $dl, "main: ".$cmd);
          ($result, $response) = Dada::mySystem($cmd);
          Dada::logMsg(3, $dl, "main: ".$result." ".$response);
          if (($result ne "ok") || ($response eq ""))
          {
            Dada::logMsg(2, $dl, "main: could not find and obs.start file");
            next;
          }
          $obs_start_file = $response;

          # determine the source
          $cmd = "grep SOURCE ".$obs_start_file." | awk '{print \$2}'";
          Dada::logMsg(3, $dl, "main: ".$cmd);
          ($result, $response) = Dada::mySystem($cmd);
          Dada::logMsg(3, $dl, "main: ".$result." ".$response);
          if (($result ne "ok") || ($response eq ""))
          {
            Dada::logMsgWarn($warn, "could not extact SOURCE from ".$obs_start_file);
            next;
          }
          $source = $response;
          $sources{$obs} = $source;
        }
  
        if (! -d  $dst_path."/".$pid )
        {
          $cmd = "mkdir -m 0755 ".$dst_path."/".$pid;
          Dada::logMsg(3, $dl, "main: ".$cmd);
          ($result, $response) = Dada::mySystem($cmd);
          Dada::logMsg(3, $dl, "main: ".$result." ".$response);
          if ($result ne "ok")
          {
            Dada::logMsgWarn($warn, "could not create dst/pid dir [".$dst_path."/".$pid."]");
            next;
          }
        }

        if (! -d  $dst_path."/".$pid."/".$obs ) 
        {
          $cmd = "mkdir -m 0755 ".$dst_path."/".$pid."/".$obs;
          Dada::logMsg(3, $dl, "main: ".$cmd);
          ($result, $response) = Dada::mySystem($cmd);
          Dada::logMsg(3, $dl, "main: ".$result." ".$response);
          if ($result ne "ok")
          {
            Dada::logMsgWarn($warn, "could not create dst/pid/obs dir [".$dst_path."/".$pid."/".$obs."]");
            next;
          }
        }

        # remove any existing flags 
        $cmd = "rm -f ".$src_path."/".$pid."/".$obs."/".$beam."/xfer.complete ".
                        $src_path."/".$pid."/".$obs."/".$beam."/on.tape.parkes";
        Dada::logMsg(2, $dl, "main: ".$cmd);
        ($result, $response) = Dada::mySystem($cmd);
        Dada::logMsg(3, $dl, "main: ".$result." ".$response);

        $cmd = "mv ".$src_path."/".$pid."/".$obs."/".$beam." ".$dst_path."/".$pid."/".$obs."/";
        Dada::logMsg(2, $dl, "main: ".$cmd);
        ($result, $response) = Dada::mySystem($cmd);
        Dada::logMsg(3, $dl, "main: ".$result." ".$response);
        if ($result ne "ok")
        {
          Dada::logMsgWarn($warn, "failed to move beam ".$beam." to ".$dst_path."/".$pid."/".$obs."/");
        }
        else
        {
          Dada::logMsg(1, $dl, $pid."/".$obs."/".$beam." parkes/on_tape -> archived");
        }

        sleep(1); 
      }
    }

    my $counter = 60;
    Dada::logMsg(2, $dl, "main: sleeping ".($counter)." seconds");
    while ((!$quit_daemon) && ($counter > 0)) 
    {
      sleep(1);
      $counter--;
    }

    # get a list of all directories in src_path
    $cmd = "find ".$src_path." -mindepth 2 -maxdepth 2 -type d -printf '\%h/\%f \%T@\n'";
    Dada::logMsg(2, $dl, "main: ".$cmd);
    ($result, $response) = Dada::mySystem($cmd);
    Dada::logMsg(3, $dl, "main: ".$result." ".$response);
    if ($result eq "ok")
    {
      @finished = split(/\n/, $response);
      $curr_time = time;
      for ($i=0; (!$quit_daemon && $i<=$#finished); $i++)
      {
        $line = $finished[$i];

        # extract path and time
        @bits = split(/ /, $line, 2);
        if ($#bits != 1) 
        {
          Dada::logMsgWarn($warn, "main: not enough components in path");
          next;
        }
        $path  = $bits[0];
        $mtime = int($bits[1]);

        # if the last modification time for this directory > 1 hr
        if ($curr_time > ($mtime + (60)))
        {
          @bits = split(/\//, $path);
          if ($#bits < 3)
          {
            Dada::logMsgWarn($warn, "main: not enough components in path");
            next;
          }

          $pid  = $bits[$#bits-1];
          $obs  = $bits[$#bits];

          # check if the src_path/pid/obs directory is empty
          $cmd = "find ".$src_path."/".$pid."/".$obs." -mindepth 1 -maxdepth 1 -type d -name '??' | wc -l";
          Dada::logMsg(2, $dl, "main: ".$cmd);
          ($result, $response) = Dada::mySystem($cmd);
          Dada::logMsg(3, $dl, "main: ".$result." ".$response);
          if (($result eq "ok") && ($response eq "0"))
          {
            $cmd = "rmdir ".$src_path."/".$pid."/".$obs;
            Dada::logMsg(2, $dl, "main: ".$cmd);
            ($result, $response) = Dada::mySystem($cmd);
            Dada::logMsg(3, $dl, "main: ".$result." ".$response);
          }
        }
      }
    }
  }

  Dada::logMsg(2, $dl, "main: joining threads");
  $control_thread->join();
  Dada::logMsg(2, $dl, "main: control_thread joined");

  Dada::logMsg(0, $dl, "STOPPING SCRIPT");
}

exit 0;

###############################################################################
#
# Functions
#

#
# control thread to ask daemon to quit
#
sub controlThread($$) 
{
  my ($quit_file, $pid_file) = @_;
  Dada::logMsg(2, $dl, "controlThread: starting");

  my $cmd = "";
  my $regex = "";
  my $result = "";
  my $response = "";

  while ((!(-f $quit_file)) && (!$quit_daemon)) {
    sleep(1);
  }

  $quit_daemon = 1;

  if ( -f $pid_file) {
    Dada::logMsg(2, $dl, "controlThread: unlinking PID file");
    unlink($pid_file);
  } else {
    Dada::logMsgWarn($warn, "controlThread: PID file did not exist on script exit");
  }

  Dada::logMsg(2, $dl, "controlThread: exiting");

  return 0;
}

#
# Handle a SIGINT or SIGTERM
#
sub sigHandle($) {

  my $sigName = shift;
  print STDERR $daemon_name." : Received SIG".$sigName."\n";
  
  # tell threads to try and quit
  if (($sigName ne "INT") || ($quit_daemon))
  {
    $quit_daemon = 1;
    sleep(3);
  
    print STDERR $daemon_name." : Exiting\n";
    exit 1;
  }
  $quit_daemon = 1;
}

#
# Handle a SIGPIPE
#
sub sigPipeHandle($) {

  my $sigName = shift;
  print STDERR $daemon_name." : Received SIG".$sigName."\n";
}

#
# Test to ensure all module variables are set before main
#
sub good($) {

  my ($quit_file) = @_;

  # check the quit file does not exist on startup
  if (-f $quit_file) {
    return ("fail", "Error: quit file ".$quit_file." existed at startup");
  }

  my $host = Dada::getHostMachineName();
  if ($host ne REQUIRED_HOST) {
    return ("fail", "Error: this script can only be run on ".REQUIRED_HOST);
  }

  my $curr_user = `whoami`;
  chomp $curr_user;
  if ($curr_user ne REQUIRED_USER) {
    return ("fail", "Error: this script can only be run as user ".REQUIRED_USER);
  }
  
  # Ensure more than one copy of this daemon is not running
  my ($result, $response) = Dada::checkScriptIsUnique(basename($0));
  if ($result ne "ok") {
    return ($result, $response);
  }

  return ("ok", "");

}
