#!/usr/bin/env perl

#
# Starts/Stops/Restarts the CASPSR Instrument
#

use lib $ENV{"DADA_ROOT"}."/bin";

use Caspsr;
use threads;
use threads::shared;
use strict;         # strict mode (like -Wall)
# use warnings;
use Getopt::Std;

#
# Function Prototypes
#
sub usage();
sub debugMessage($$);
sub issueTelnetCommand($$);
sub stopDaemons();
sub startDaemons();

#
# Constants
#
use constant  DEBUG_LEVEL         => 1;

#
# Global Variables
#
our %cfg : shared = Caspsr::getConfig();
our @server_daemons = split(/ /,$cfg{"SERVER_DAEMONS"});
our @client_daemons = split(/ /,$cfg{"CLIENT_DAEMONS"});
our @demux_daemons = split(/ /,$cfg{"DEMUX_DAEMONS"});

#
# Local Variables
#

my $cmd = "";
my $result = "";
my $response = "";
my @clients = ();
my @demuxers = ();
my @all_clients = ();
my @servers = ();
my @all = ();
my $i=0;
my $j=0;
my $start=1;
my $stop=1;

my %opts;
getopts('his', \%opts);

if ($opts{h}) {
  usage();
  exit(0);
}

if ($opts{i}) {
  $start = 1;
  $stop = 0;
}
if ($opts{s}) {
  $stop = 1;
  $start = 0;
}

if (($start == 1) && ($stop == 1)) {
  debugMessage(0, "Restarting CASPSR");
} elsif ($start == 0) {
  debugMessage(0, "Stopping CASPSR");
} elsif ($stop == 0) {
  debugMessage(0, "Starting CASPSR");
} else {
  debugMessage(0, "Unsure of command");
  usage();
  exit(1);
}

# Setup directories should they not exist
my $control_dir = $cfg{"SERVER_CONTROL_DIR"};

if (! -d $control_dir) {
  system("mkdir -p ".$control_dir);
}

# Generate hosts lists
for ($i=0; $i < $cfg{"NUM_PWC"}; $i++) {
  push(@clients, $cfg{"PWC_".$i});
  push(@all_clients, $cfg{"PWC_".$i});
  push(@all, $cfg{"PWC_".$i});
}
for ($i=0; $i < $cfg{"NUM_DEMUX"}; $i++) {
  for ($j=0; $j<=$#demuxers; $j++) {
  }
  push(@demuxers, $cfg{"DEMUX_".$i});
  push(@all_clients, $cfg{"DEMUX_".$i});
  push(@all, $cfg{"DEMUX_".$i});
}
for ($i=0; $i < $cfg{"NUM_SRV"}; $i++) {
  push(@servers, $cfg{"SRV_".$i});
  push(@all, $cfg{"SRV_".$i});
}

# ensure we dont have duplicates [from DEMUXS especially]
@clients = Dada::array_unique(@clients);
@all_clients = Dada::array_unique(@all_clients);
@all = Dada::array_unique(@all);
@demuxers = Dada::array_unique(@demuxers);

if ($stop == 1) {

  # Stop PWC's
  debugMessage(0, "Stopping PWCs");
  if (!(issueTelnetCommand("stop_pwcs",\@clients))) {
    debugMessage(0, "stop_pwcs failed");
  }

  # Stop client scripts
  debugMessage(0, "Stopping client scripts");
  if (!(issueTelnetCommand("stop_daemons",\@all_clients))) {
    debugMessage(0,"stop_daemons failed");
  }

  # Destroy DB's
  debugMessage(0, "Destroying Data blocks");
  if (!(issueTelnetCommand("destroy_db",\@clients))) {
    debugMessage(0,"destroy_db failed");
  }

  # Stop server scripts
  debugMessage(0, "Stopping server scripts");
  ($result, $response) = stopDaemons();
  if ($result ne "ok") {
    debugMessage(0, "Could not stop server daemons: ".$response);
  }

  # Stop client master script
  debugMessage(0, "Stopping client master script");
  if (!(issueTelnetCommand("stop_master_script",\@all))) {
    debugMessage(0,"stop_master_script failed");
  }

}

if (($stop == 1) && ($start == 1)) {
  debugMessage(0,"snoozin");
  sleep(3);
}

if ($start == 1) {

  # Start client master script
  debugMessage(0, "Starting client master script");
  if (!(issueTelnetCommand("start_master_script",\@all))) {
    debugMessage(0,"start_master_script failed");
  }

  sleep(1); 

  # initalize DB's
  debugMessage(1, "Initializing Data blocks");
  if (!(issueTelnetCommand("init_db",\@clients))) {
    debugMessage(0, "init_db failed");
  }

  sleep(1);

  # Start PWC's
  debugMessage(1, "Starting PWCs");
  if (!(issueTelnetCommand("start_pwcs",\@clients))) {
    debugMessage(0,"start_pwcs failed");
  }

  # Start server scripts
  debugMessage(0, "Starting server scripts");
  ($result, $response) = startDaemons();
  if ($result ne "ok") {
    debugMessage(0, "Could not start server daemons: ".$response);
  }

  # Start client scripts
  debugMessage(0, "Starting client scripts");
  if (!(issueTelnetCommand("start_daemons",\@all_clients))) {
    debugMessage(0,"start_daemons failed");
  }

}

# Clear the web interface status directory
my $dir = $cfg{"STATUS_DIR"};
if (-d $dir) {

  $cmd = "rm -f ".$dir."/*.error";
  debugMessage(0, "clearing .error from status_dir");
  ($result, $response) = Dada::mySystem($cmd);

  $cmd = "rm -f ".$dir."/*.warn";
  debugMessage(0, "clearing .warn from status_dir");
  ($result, $response) = Dada::mySystem($cmd);
}


exit 0;


sub sshCmdThread($) {

  (my $command) = @_;

  my $result = "fail";
  my $response = "Failure Message";

  $response = `$command`;
  if ($? == 0) {
    $result = "ok";
  }
  return ($result, $response);
  
}

sub commThread($$) {

  (my $command, my $machine) = @_;

  my $result = "fail";
  my $response = "Failure Message";
 
  my $handle = Dada::connectToMachine($machine, $cfg{"CLIENT_MASTER_PORT"}, 2);
  # ensure our file handle is valid
  if (!$handle) { 
    debugMessage(0, "Could not connect to machine ".$machine.":".$cfg{"CLIENT_MASTER_PORT"});
    return ("fail","Could not connect to machine ".$machine.":".$cfg{"CLIENT_MASTER_PORT"}); 
  }

  ($result, $response) = Dada::sendTelnetCommand($handle,$command);

  $handle->close();

  return ($result, $response);

}

sub usage() {
  print "Usage: ".$0." [options]\n";
  print "   -h     print help text\n";
  print "   -i     start the instrument\n";
  print "   -s     stop the instrument\n";
}

sub debugMessage($$) {
  my ($level, $message) = @_;

  if (DEBUG_LEVEL >= $level) {

    # print this message to the console
    print "[".Dada::getCurrentDadaTime(0)."] ".$message."\n";
  }
}

sub issueTelnetCommand($$){

  my ($command, $nodesRef) = @_;

  my @nodes = @$nodesRef;
  my @threads = ();
  my @results = ();
  my @responses = ();
  my $failure = "false";
  my $i=0;

  if ($command eq "start_master_script") {
    for ($i=0; $i<=$#nodes; $i++) {
      my $string = "ssh -x caspsr@".$nodes[$i]." \"client_caspsr_master_control.pl\"";
      @threads[$i] = threads->new(\&sshCmdThread, $string);
    } 
  } else {
    for ($i=0; $i<=$#nodes; $i++) {
      @threads[$i] = threads->new(\&commThread, $command, $nodes[$i]);
    } 
  }  
  for($i=0;$i<=$#nodes;$i++) {
    debugMessage(2, "Waiting for ".$nodes[$i]);
    (@results[$i],@responses[$i]) = $threads[$i]->join;
  }

  for($i=0;$i<=$#nodes;$i++) {
    if (($results[$i] eq "fail") || ($results[$i] eq "dnf")) {
      debugMessage(2, $nodes[$i]." failed \"".$result.":".$responses[$i]."\"");
      $failure = "true";
    }
  }

  if ($failure eq "true") {
    return 0;
  } else {
    return 1;
  }
}


sub stopDaemons() {

  my $allStopped = "false";
  my $control_dir = $cfg{"SERVER_CONTROL_DIR"};
  my $threshold = 20; # seconds
  my $daemon = "";
  my $result = "";
  my $response = "";

  my $i=0;
  my $cmd = "";

  # Ask all server daemons to quit
  for ($i=0; $i<=$#server_daemons; $i++) {
    $cmd = "touch ".$control_dir."/".$server_daemons[$i].".quit";
    system($cmd);
  }

  while (($allStopped eq "false") && ($threshold > 0)) {

    $allStopped = "true";
    foreach $daemon (@server_daemons) {

      $cmd = "ps auxwww | grep perl | grep \"".$daemon."\" | grep -v grep";
      `$cmd`;

      if ($? == 0) {
        debugMessage(1, "daemon ".$daemon." is still running");
        $allStopped = "false";
        if ($threshold < 10) {
          ($result, $response) = Dada::killProcess("server_".$daemon.".pl");
        }
      } else {
        debugMessage(2, "daemon ".$daemon." has been stopped");
      }
    }

    $threshold--;
    sleep(1);
  }

  # Clean up all the server daemons
  for ($i=0; $i<=$#server_daemons; $i++) {
    unlink($control_dir."/".$server_daemons[$i].".quit");
  }
  my $message = "";

  # If we had to resort to a "kill", send an warning message back
  if (($threshold > 0) && ($threshold < 10)) {
    $message = "Daemons did not exit cleanly within ".$threshold." seconds, a KILL signal was used and they exited";
    debugMessage(0, "Error: ".$message);
    return ("fail", $message);
  }
  if ($threshold <= 0) {
    $message = "Daemons did not exit cleanly after ".$threshold." seconds, a KILL signal was used and they exited";
    debugMessage(0, "Error: ".$message);
    return ("fail", $message);
  }

  return ("ok", "all stopped");

}

sub startDaemons() {

  my $daemon;
  my $response = "";
  my $result = "ok";
  my $cmd;
  my $string;

  chdir $cfg{"SCRIPTS_DIR"};

  foreach $daemon (@server_daemons) {
    $cmd = "server_".$daemon.".pl 2>&1";

    $string = `$cmd`;

    if ($? != 0) {
      $result = "fail";
      $response .= $daemon." failed to start: ".$string.". ";
      debugMessage(0, "Failed to start daemon ".$daemon.": ".$string);
    }
  }
  return ($result, $response);
}
