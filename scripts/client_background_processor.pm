package Dada::client_background_processor;

use lib $ENV{"DADA_ROOT"}."/bin";

use strict;
use warnings;
use threads;
use threads::shared;
use Net::hostent;
use Dada;

BEGIN {

  require Exporter;
  our ($VERSION, @ISA, @EXPORT, @EXPORT_OK, %EXPORT_TAGS);

  require AutoLoader;

  $VERSION = '1.00';

  @ISA         = qw(Exporter AutoLoader);
  @EXPORT      = qw(&main);
  %EXPORT_TAGS = ( );
  @EXPORT_OK   = qw($dl $daemon_name %cfg);

}

our @EXPORT_OK;

#
# exported package globals
#
our $dl : shared;
our $daemon_name : shared;
our %cfg : shared;

#
# non-exported package globals go here
#
our $quit_daemon : shared;
our $log_host;
our $log_port;
our $log_sock;
our $proc_pid : shared;

#
# initialize package globals
#
$dl = 1; 
$daemon_name = 0;
%cfg = ();

#
# initialize other variables
#
$quit_daemon = 0;
$log_host = 0;
$log_port = 0;
$log_sock = 0;
$proc_pid = 0;


###############################################################################
#
# package functions
# 

sub main() {

  my $log_file      = $cfg{"CLIENT_LOG_DIR"}."/".$daemon_name.".log";;
  my $pid_file      = $cfg{"CLIENT_CONTROL_DIR"}."/".$daemon_name.".pid";
  my $quit_file     = $cfg{"CLIENT_CONTROL_DIR"}."/".$daemon_name.".quit";
  my $raw_data_dir  = $cfg{"CLIENT_RECORDING_DIR"};
  my $proc_mng_host = Dada->getHostMachineName();
  my $proc_mng_port = $cfg{"CLIENT_PROC_LOAD_PORT"};

  $log_host         = $cfg{"SERVER_HOST"};
  $log_port         = $cfg{"SERVER_SYS_LOG_PORT"};

  my $control_thread = 0;
  my $result = "";
  my $response = "";
  my $file = "";
  my $raw_header = "";
  my $ncores = 0;
  my $cmd = "";

  # sanity check on whether the module is good to go
  ($result, $response) = good($quit_file);
  if ($result ne "ok") {
    print STDERR $response."\n";
    return 1;
  }

  # install signal handles
  $SIG{INT}  = \&sigHandle;
  $SIG{TERM} = \&sigHandle;
  $SIG{PIPE} = \&sigPipeHandle;

  # become a daemon
  Dada->daemonize($log_file, $pid_file);

  # Open a connection to the server_sys_monitor.pl script
  $log_sock = Dada->nexusLogOpen($log_host, $log_port);
  if (!$log_sock) {
    print STDERR "Could not open log port: ".$log_host.":".$log_port."\n";
  }

  logMsg(0, "INFO", "STARTING SCRIPT");

  # Start the daemon control thread
  $control_thread = threads->new(\&controlThread, $quit_file, $pid_file);

  # Main Loop
  while (!($quit_daemon)) {

    $ncores = 0;
    chdir $cfg{"CLIENT_RESULTS_DIR"};

    ($file, $raw_header) = getUnprocessedFile($raw_data_dir);

    # If we have file(s) to process
    if ($file ne "none") {

      logMsg(2, "INFO", "file to process ".$file);

      # Find out how many cores are available to background process on
      my $handle = Dada->connectToMachine($proc_mng_host, $proc_mng_port);
      if ($handle) {

        logMsg(2, "INFO", "connection to processing manager established");
        print $handle "how many cores are available?\r\n";
        $response = Dada->getLine($handle);

        $ncores = $response;
        $handle->close();

      } else {
        logMsg(2, "WARN", "Could not connect to processing manager ".
                      $proc_mng_host.":".$proc_mng_port);
      } 

      logMsg(2, "INFO", "cores available: ".$ncores);

      # If we have cores available to background process the files
      if ($ncores > 1) {

        if ($ncores > 3) {
          logMsg(1, "INFO", "clamping number of cores from ".$ncores." to 3");
          $ncores = 3;
        }

        ($result, $response) = processingThread($file, $raw_header, $ncores);

      } else {
        logMsg(2, "INFO", "File awaiting processing: ".$file);
      }
    }
  
    logMsg(2, "INFO", "Sleeping for 10 seconds");
    my $counter = 10;
    while ((!$quit_daemon) && ($counter > 0)) {
      sleep(1);
      $counter--;
    }
  }

  $control_thread->join();

  logMsg(0, "INFO", "STOPPING SCRIPT");
  Dada->nexusLogClose($log_sock);

  return 0;
}


#
# find a raw data file in the specified dir
#
sub getUnprocessedFile($) {

  (my $dir) = @_;

  my $cmd = "";
  my $raw_header = "";
  my $result = "";
  my $response = "";
  my @files = ();
  
  $cmd = "find ".$dir." -maxdepth 1 -type f -name '*.dada' | sort";
  logMsg(2, "INFO", "getUnprocessedFile: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);
  logMsg(3, "INFO", "getUnprocessedFile: ".$result." ".$response);

  if ($result ne "ok") {
    logMsg(0, "WARN", "find command ".$cmd." failed: ".$response);
    return ("none", "find cmd failed");

  } else {
  
    @files = split(/\n/, $response);
  
    if ($#files >= 0) {
  
      $cmd = "head -c 4096 ".$files[0];
      $raw_header = `$cmd`;
      return ($files[0], $raw_header);

    } else {
      return ("none", "");

    }
  }
}


#
# process the raw file on ncores
#
sub processingThread($$$) {

  my ($file, $raw_header, $ncores) = @_;

  my $fail_dir = $cfg{"CLIENT_RECORDING_DIR"}."/unprocessable";
  my $result = "";
  my $response = "";
  my $proc_cmd = "";
  my $proc_dir = "";
  my %h = ();
  my $proj_id = "";
  my $apsr_groups = "";

  ($result, $response) = Apsr->processHeader($raw_header, \%cfg);

  if  ($result ne "ok") {
    logMsg(0, "ERROR", $response);
    logMsg(0, "ERROR", "DADA header malformed, ignoring file");
    ($result, $response) = moveToUnprocessed($file, $fail_dir);

  } else {

    %h = Dada->headerToHash($raw_header);

    $proc_cmd = $response;
    $proc_cmd .= " ".$file;

    # Adjust the proc_cmd to the right number of cores
    if ($proc_cmd =~ m/ -t \d/) {
      $proc_cmd =~ s/ -t \d/ -t $ncores/;

    } else {
      if ($proc_cmd =~ m/dspsr/) {
        $proc_cmd =~ s/dspsr /dspsr -t $ncores /;
      } else {
        logMsg(0, "WARN", "PROC_CMD did not contain dspsr");
      }
    }
    # Remove any -L 10 commands due to current dspsr bug 
    # TODO REMOVE THIS TODO
    if ($proc_cmd =~ m/ -L 10/) {
      # Adjust the number of threads to be used by trhe proc cmd
      $proc_cmd =~ s/ -L 10//;
    }

    logMsg(1, "INFO", "proc_cmd : ".$proc_cmd);

    $proc_dir = $cfg{"CLIENT_RESULTS_DIR"}."/".$h{"UTC_START"}."/".$h{"FREQ"};

    # This should not be requried as the observation manager should be creating
    # this directory for us
    if (! -d ($proc_dir)) {
      logMsg(0, "WARN", "The archive directory was not created by the ".
               "observation manager: \"".$proc_dir."\"");
      system("mkdir -p ".$proc_dir);
      system("chmod g+s ".$proc_dir);

      # Check whether the apsr user is a member of the specified group
      $proj_id = $h{"PID"};
      $apsr_groups = `groups`;
      chomp $apsr_groups;

     if ($apsr_groups =~ m/$proj_id/) {
       logMsg(2, "INFO", "chgrp to ".$proj_id);
     } else {
       logMsg(0, "WARN", "Project ".$proj_id." was not an apsr group. Using apsr instead'");
       $proj_id = "apsr";
     }
     system("chgrp -R ".$proj_id." ".$proc_dir);
    }

    logMsg(1, "INFO", "Processing ".basename($file)." on ".$ncores." cores");

    $result = processOneFile($proc_dir, $proc_cmd);

    logMsg(2, "INFO", "Result of processing: ".$result);

    # reset the client archive dir
    chdir $cfg{"CLIENT_RESULTS_DIR"};

    if ($result eq "ok") {
      logMsg(1, "INFO", "Processed file: ".$file);
      logMsg(1, "INFO", "Deleting processed file");
      unlink($file);
    } else {
      logMsg(0, "WARN", "Processing failed: ".$file);
      ($result, $response) = moveToUnprocessed($file, $fail_dir);
    }
  }
}


sub processOneFile($$) {

  (my $dir, my $cmd) = @_;

  my $result = "fail";
  my $pid = fork;

  # If for some reason the directory does not exist, create it
  if (!( -d $dir)) {
    logMsg(0, "WARN", "Output dir ".$dir." did not exist, creating it");
    `mkdir -p $dir`;
  }

  logMsg(2, "INFO", "processing in dir ".$dir);

  if ($pid) {

    $proc_pid = $pid;

    # Wait for the child to finish
    waitpid($pid,0);

    if ($? == 0) {
      $result = "ok";
    } else {
      $result = "fail";
    }

    # Reset the processing pid to 0
    $proc_pid = 0;

  } else {

    # child process. The exec command will never return 
    chdir $dir;
    logMsg(1, "INFO", $cmd);
    exec "$cmd";
  }

  logMsg(2, "INFO", "Result of processing was \"".$result."\"");
  return $result;

}

#
# Move an unprocessible file to the unprocesable_dir
#
sub moveUnprocessableFile($$) {

  my ($file, $unprocessable_dir) = @_;

  logMsg(2, "INFO", "moveUnprocessableFile(".$file.", ".$unprocessable_dir.")");

  logMsg(1, "INFO", "moving ".$file." ".$unprocessable_dir);
  
  my $result = "";
  my $response = "";
  my $cmd = "";
  
  $cmd = "mv ".$file." ".$unprocessable_dir."/";
  logMsg(2, "INFO", "moveUnprocessableFile: ".$cmd);
  ($result, $response) = Dada->mySystem($cmd);
  logMsg(2, "INFO", "moveUnprocessableFile: ".$result." ".$response);
  
  if ($result ne "ok") {
    logMsg(0, "WARN", "Could not move unprocessable file: ".$response);
    return ("fail", "could not move file");
  
  } else { 
    return ("ok", "");
  }
}


sub controlThread($$) {
  
  my ($quit_file, $pid_file) = @_;
  
  logMsg(2, "INFO", "controlThread : starting");

  my $cmd = "";
  my $result = "";
  my $response = "";
  
  while ((!$quit_daemon) && (!(-f $quit_file))) {
    sleep(1);
  }
  
  $quit_daemon = 1;
  
  if ($proc_pid) {
    $cmd = "kill -KILL ".$proc_pid;
    logMsg(1, "INFO", "controlThread: ".$cmd);
    ($result, $response) = Dada->mySystem($cmd);
    logMsg(2, "INFO", "controlThread: ".$result." ".$response);
  }
  
  if ( -f $pid_file) {
    logMsg(2, "INFO", "controlThread: unlinking PID file");
    unlink($pid_file);
  } else {
    logMsg(1, "WARN", "controlThread: PID file did not exist on script exit");
  }
 
  logMsg(2, "INFO", "controlThread: exiting");

}



#
# logs a message to the nexus logger and prints to stdout
#
sub logMsg($$$) {

  my ($level, $type, $msg) = @_;
  if ($level <= $dl) {
    my $time = Dada->getCurrentDadaTime();
    if (! $log_sock ) {
      $log_sock = Dada->nexusLogOpen($log_host, $log_port);
    }
    if ($log_sock) {
      Dada->nexusLogMessage($log_sock, $time, "sys", $type, "bg mngr", $msg);
    }
    print "[".$time."] ".$msg."\n";
  }
}


#
# Handle a SIGINT or SIGTERM
#
sub sigHandle($) {

  my $sigName = shift;
  print STDERR $daemon_name." : Received SIG".$sigName."\n";
  
  # Tell threads to try and quit
  $quit_daemon = 1;
  sleep(3);
  
  if ($log_sock) {
    close($log_sock);
  } 
  
  print STDERR $daemon_name." : Exiting\n";
  exit 1;
  
}

#
# Handle a SIGPIPE
#
sub sigPipeHandle($) {

  my $sigName = shift;
  print STDERR $daemon_name." : Received SIG".$sigName."\n";
  $log_sock = 0;
  if ($log_host && $log_port) {
    $log_sock = Dada->nexusLogOpen($log_host, $log_port);
  }

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

  # the calling script must have set this
  if (! defined($cfg{"INSTRUMENT"})) {
    return ("fail", "Error: package global hash cfg was uninitialized");
  }

  if ( $daemon_name eq "") {
    return ("fail", "Error: a package variable missing [daemon_name]");
  }

  return ("ok", "");
    
} 


END { }

1;  # return value from file
