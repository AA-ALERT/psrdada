#!/usr/bin/env perl 

#
# Author:   Andrew Jameson
# Created:  6 Dec, 2007
# Modified: 9 Jan, 2008
#
# This daemons runs continuously produces feedback plots of the
# current observation


require "Dada.pm";        # DADA Module for configuration options
use strict;               # strict mode (like -Wall)
use File::Basename;
use threads;
use threads::shared;



#
# Constants
#
use constant DEBUG_LEVEL         => 1;
use constant PROCESSED_FILE_NAME => "processed.txt";
use constant IMAGE_TYPE          => ".png";
use constant TOTAL_F_RES         => "total_f_res.ar";
use constant TOTAL_T_RES         => "total_t_res.ar";
use constant PIDFILE             => "results_manager.pid";
use constant LOGFILE             => "results_manager.log";


#
# Global Variable Declarations
#
our %cfg = Dada->getDadaConfig();
our $quit_daemon : shared = 0;


#
# Signal Handlers
#
$SIG{INT} = \&sigHandle;
$SIG{TERM} = \&sigHandle;


#
# Local Variable Declarations
#

my $logfile = $cfg{"SERVER_LOG_DIR"}."/".LOGFILE;
my $pidfile = $cfg{"SERVER_CONTROL_DIR"}."/".PIDFILE;

my $bindir              = Dada->getCurrentBinaryVersion();
my $obs_results_dir     = $cfg{"SERVER_RESULTS_DIR"};
my $daemon_control_thread = 0;

my $cmd;
my $timestamp = "";
my $fname = "";

#$cmd = "rm -f *.gif";
#system($cmd);

# This will have to be determined */
my $have_new_archive = 1;
my $node;
my $nodedir;

my %unprocessed = ();
my $key;
my $value;
my $num_results = 0;
my $current_key = 0;

my $fres = "";
my $tres = "";
my $current_archive = "";
my $last_archive = "";
my $obs_dir = "";

my $dir;
my @subdirs;
my @keys;
my @processed;
my $i;

# Autoflush output
$| = 1;

# Sanity check for this script
if (index($cfg{"SERVER_ALIASES"}, $ENV{'HOSTNAME'}) < 0 ) {
  print STDERR "ERROR: Cannot run this script on ".$ENV{'HOSTNAME'}."\n";
  print STDERR "       Must be run on the configured server: ".$cfg{"SERVER_HOST"}."\n";
  exit(1);
}


# Redirect standard output and error
Dada->daemonize($logfile, $pidfile);

debugMessage(0, "STARTING SCRIPT: ".Dada->getCurrentDadaTime(0));

# Start the daemon control thread
$daemon_control_thread = threads->new(\&daemonControlThread);

chdir $obs_results_dir;


#
# Main Loop
#
@processed = ();

while (!$quit_daemon) {

  $dir = "";
  @subdirs = ();

  # TODO check that directories are correctly sorted by UTC_START time
  debugMessage(2,"Main While Loop, looking for data in ".$obs_results_dir);

  opendir(DIR,$obs_results_dir);
  @subdirs = sort grep { !/^\./ && -d $obs_results_dir."/".$_ } readdir(DIR);
  closedir DIR;

  my $h=0;
  for ($h=0; (($h<=$#subdirs) && (!$quit_daemon)); $h++) {

    $dir = $subdirs[$h];

    # The number of results expected should be the number of obs.start files
    $num_results = countObsStart($obs_results_dir."/".$dir);

    if ($num_results > 0) {

      # Check how long ago the last result for the observation was received.
      # 60 minutes, then reprocess all the .lowres archives and finalise
      # this archive
      my $most_recent_result = getMostRecentResult($dir);

      # If more than 24 hours, then reprocess all the .lowres archives and finalise
      # this archive

      if ($most_recent_result > 24*60*60) { 

        debugMessage(0,"Finalising observation: ".$dir);
        ($fres, $tres) = processAllArchives($dir);

        my $cmd = "rm -f ".$dir."/*/*.lowres";
        system($cmd);

      } else {

        # Look for the oldest archives that have not been processed
        %unprocessed = countArchives($dir,1);

        # Sort the files into time order.
        @keys = sort (keys %unprocessed);
        $current_key = 0;

        # If we have at least 1 result 
        if ($#keys > -1)  {

          debugMessage(2, "Found ".($#keys+1)." results in ".$dir);
  
          for ($i=0;$i<=$#keys;$i++) {
            debugMessage(2, "file = ".$keys[$i].": ".$unprocessed{$keys[$i]}." / ".$num_results);
          }

          # If we have received at least 10 results, process the n-10 of them
          for ($i=0; ($i<=($#keys-3) || (($h < $#subdirs) && ($i <=$#keys)) ); $i++) {

            debugMessage(1, "Processing late archives ".$dir."/*/".$keys[$i]);

            # process the archive and summ it into the results archive for observation
            ($current_archive, $fres, $tres) = processArchive($dir, $keys[$i]);
            $obs_dir = $dir;

            $current_key += 1;

          }

          # If we have the full number of archives - then process this file
          if (($#keys >= 0) && ($unprocessed{$keys[$current_key]} >= $num_results)) {

            debugMessage(1, "Processing archives ".$dir."/*/".$keys[$current_key]);

            # process the archive and summ it into the results archive for observation
            ($current_archive, $fres, $tres) = processArchive($dir, $keys[$current_key]);
            $obs_dir = $dir;

          } 

          # If a fres or tres has been processed, check if its new...
          if ($current_archive ne $last_archive) {

            debugMessage(2, "Making plots from archive ".$current_archive);
            Dada->makePlotsFromArchives("", $obs_dir, $fres, $tres, "240x180");
            $last_archive = $current_archive;
          }
        }
      }
    }
    
  }

  # If we have been asked to exit, dont sleep
  if (!$quit_daemon) {
    sleep(2);
  }

}

# Rejoin our daemon control thread
$daemon_control_thread->join();
                                                                                
debugMessage(0, "STOPPING SCRIPT: ".Dada->getCurrentDadaTime(0));
                                                                                


exit(0);

###############################################################################
#
# Functions
#


sub archiveResults($) {
  (my $dir) = @_;

  my @files_to_archive = qw(PHASE_VS_TIME_FILE PHASE_VS_FREQ_FILE PHASE_VS_FLUX_FILE);

  return 0;
}

sub deleteObservation($) {
  (my $dir) = @_;

  debugMessage(1,"Completed Observation ".$dir);

  $cmd = "rm -rf $dir";
  `$cmd`;
  return $?

}


#
# For the given utc_start ($dir), and archive (file) add the archive to the 
# summed archive for the observation
#
sub processArchive($$) {

  (my $dir, my $file) = @_;

  debugMessage(2, "processArchive(".$dir.", ".$file.")");

  my $bindir =      Dada->getCurrentBinaryVersion();
  my $results_dir = $cfg{"SERVER_RESULTS_DIR"};

  # The combined results for this observation (dir == utc_start) 
  my $total_f_res = $dir."/".TOTAL_F_RES;
  my $total_t_res = $dir."/".TOTAL_T_RES;

  # If not all the archives are present, then we must create empty
  # archives in place of the missing ones
  $cmd = "find ".$dir."/* -type d";
  debugMessage(2, $cmd);
  my $find_result = `$cmd`;
  #print $find_result;
  my @sub_dirs = split(/\n/, $find_result);

  # Find out how many archives we actaully hae
  $cmd = "find ".$dir."/*/ -type f -name ".$file;
  debugMessage(2, $cmd);
  $find_result = `$cmd`;
  my @archives = split(/\n/, $find_result);

  debugMessage(2, "Found ".($#archives+1)." / ".($#sub_dirs+1)." archives");

  my $output = "";
  my $real_archives = "";
  my $subdir = "";

  if ($#archives < $#sub_dirs) {

    foreach $subdir (@sub_dirs) {

     debugMessage(2, "subdir = ".$subdir.", file = ".$file); 
     # If the archive does not exist in the frequency dir
      if (!(-f ($subdir."/".$file))) { 

        debugMessage(1, "archive ".$subdir."/".$file." was not present");

        my ($filename, $directories, $suffix) = fileparse($subdir);
        my $band_frequency = $filename;
        my $input_file = $archives[0];
        my $output_file = $subdir."/temp_".$file;
        my $tmp_file = $input_file;
        $tmp_file =~ s/\.lowres$/\.tmp/;

        $cmd = $bindir."/pam -o ".$band_frequency." -e tmp ".$input_file;
        debugMessage(2, $cmd);
        $output = `$cmd`;
        debugMessage(2, $output);

        $cmd = "mv -f ".$tmp_file." ".$output_file;
        $output = `$cmd`;

        $cmd = $bindir."/paz -w 0 -m ".$output_file;
        debugMessage(2, $cmd);
        $output = `$cmd`;
        debugMessage(2, $output);

        debugMessage(2, "Deleting tmp file ".$tmp_file);
        unlink($tmp_file);

      } else {
        my ($filename, $directories, $suffix) = fileparse($subdir);
        $real_archives .= " ".$filename;
      }
    }

  } else {
    foreach $subdir (@sub_dirs) {
      my ($filename, $directories, $suffix) = fileparse($subdir);
      $real_archives .= " ".$filename;
    } 
  }

  # The frequency summed archive for this time period
  my $current_archive = $dir."/".$file;

  # combine all thr frequency channels
  $cmd = $bindir."/psradd -R -f ".$current_archive." ".$dir."/*/*".$file;
  debugMessage(2, $cmd);
  $output = `$cmd`;
  debugMessage(2, $output);

  # Delete any "temp" files that we needed to use to produce the result
  $cmd = "rm -f ".$dir."/*/temp_".$file;
  $output = `$cmd`;
  debugMessage(2, $output);

  # If this is the first result for this observation
  if (!(-f $total_f_res)) {

    $cmd = "cp ".$current_archive." ".$total_f_res;
    debugMessage(2, $cmd);
    $output = `$cmd`;
    debugMessage(2, $output);
                                                                                                                 
    # Fscrunc the archive
    $cmd = $bindir."/pam -F -m ".$current_archive;
    debugMessage(2, $cmd);
    $output = `$cmd`;
    debugMessage(2, $output);
                                                                                                                 
    # Tres operations
    $cmd = "cp ".$current_archive." ".$total_t_res;
    debugMessage(2, $cmd);
    $output = `$cmd`;
    debugMessage(2, $output);

  } else {

    my $temp_ar = $dir."/temp.ar";
                                                                                                                 
    # Fres Operations
    $cmd = $bindir."/psradd -s -f ".$temp_ar." ".$total_f_res." ".$current_archive;
    debugMessage(2, $cmd);
    $output = `$cmd`;
    debugMessage(2, $output);
    unlink($total_f_res);
    rename($temp_ar,$total_f_res);
                                                                                                                 
    # Fscrunc the archive
    $cmd = $bindir."/pam -F -m ".$current_archive;
    debugMessage(2, $cmd);
    $output = `$cmd`;
    debugMessage(2, $output);
                                                                                                                 
    # Tres Operations
    $cmd = $bindir."/psradd -f ".$temp_ar." ".$total_t_res." ".$current_archive;
    debugMessage(2, $cmd);
    $output = `$cmd`;
    debugMessage(2, $output);
    unlink($total_t_res);
    rename($temp_ar,$total_t_res);

  }

  # clean up the current archive
  unlink($current_archive);
  debugMessage(2, "unlinking $current_archive");

  # Record this archive as processed and what sub bands were legit
  recordProcessed($dir, $file, $real_archives);

  return ($current_archive, $total_f_res, $total_t_res);

}

#
# Counts the numbers of *.lowres archives in total received
#
sub countArchives($$) {

  my ($dir, $skip_existing_archives) = @_;

  my @processed = getProcessedFiles($dir);

  my $cmd = "find ".$dir."/*/ -name \"*.lowres\" -printf \"%P\n\"";
  debugMessage(3, $cmd);
  my $find_result = `$cmd`;

  my %archives = ();

  my @files = split(/\n/,$find_result);
  my $file = "";
  foreach $file (@files) {
  
    my $has_been_processed = 0;
    # check that this file has not already been "processed";
    for ($i=0;$i<=$#processed;$i++) {
      if ($file eq $processed[$i]) {
        $has_been_processed = 1;
      }
    }


    # If we haven't processed this OR we want to get all archives
    if (($has_been_processed == 0) || ($skip_existing_archives == 0)) {
 
      if (!(exists $archives{$file})) {
        $archives{$file} = 1;
      } else {
        $archives{$file} += 1;
      }

    }
  }
  return %archives;

}


sub getProcessedFiles($) {

  (my $dir) = @_;
  my $i = 0;
  my @lines = ();
  my @arr = ();
  my @archives = ();
  my $fname = $dir."/".PROCESSED_FILE_NAME;

  open FH, "<".$fname or return @lines;
  @lines = <FH>;
  close FH;

  for ($i=0;$i<=$#lines;$i++) {
    @arr = split(/ /,$lines[$i]); 
    chomp($arr[0]);
    @archives[$i] = $arr[0];
    # print "Processed file $i = ".$archives[$i]."\n";
  }

  return @archives;

}


sub countObsStart($) {

  my ($dir) = @_;

  my $cmd = "find ".$dir." -name \"obs.start\" | wc -l";
  my $find_result = `$cmd`;
  chomp($find_result);
  return $find_result;

}

sub deleteArchives($$) {

  (my $dir, my $archive) = @_;

  my $cmd = "rm -f ".$dir."/*/".$archive;
  debugMessage(2, "Deleting processed archives ".$cmd);
  my $response = `$cmd`;
  if ($? != 0) {
    debugMessage(0, "rm failed: \"".$response."\"");
  }

  return 0;

}

sub recordProcessed($$$) {

  my ($dir, $archive, $subbands) = @_;

  my $FH = "";
  my $record = $dir."/".PROCESSED_FILE_NAME;
 
  debugMessage(2, "Recording ".$archive." as processed");
  open FH, ">>$record" or return ("fail","Could not append record file: $record");
  print FH $archive.$subbands."\n";
  close FH;
 
  return ("ok","");
}


sub registerAsProcessed($$) {

  (my $dir, my $archive) = @_;

  open FH, "<$dir/obs.last";
  print FH $archive."\n";
  close FH;

  return 0;

}

sub debugMessage($$) {
  (my $level, my $message) = @_;
  if ($level <= DEBUG_LEVEL) {
    my $time = Dada->getCurrentDadaTime();
    print "[".$time."] ".$message."\n";
  }
}

sub getMostRecentResult($) {

  (my $dir) = @_;

  my $cmd = "find ".$dir."/*/ -name \"*.lowres\" -printf \"%T@\\n\" | sort | tail -n 1";
  my $age = 0;

  my $unix_time_of_most_recent_result = `$cmd`;
  my $current_unix_time = time;
  if ($unix_time_of_most_recent_result) {
    $age = $current_unix_time - $unix_time_of_most_recent_result;
  }

  debugMessage(2, "Observation: ".$dir.", age: ".$age." seconds");

  return $age;
 
} 

sub processAllArchives($) {

  (my $dir) = @_;

  debugMessage(1, "processAllArchives(".$dir.")");

   # Delete the existing fres and tres files
   unlink($dir."/".TOTAL_T_RES);
   unlink($dir."/".TOTAL_F_RES);
  
   # Get ALL archives in the observation dir
   my %unprocessed = countArchives($dir,0);

   # Sort the files into time order.
   my @keys = sort (keys %unprocessed);
 
   my $i=0;
    
   for ($i=0; $i<=$#keys; $i++) {

     debugMessage(1, "Finalising archive ".$dir."/*/".$keys[$i]);
     # process the archive and summ it into the results archive for observation
     ($current_archive, $fres, $tres) = processArchive($dir, $keys[$i]);

   }

   return ($fres, $tres);

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
  exit(1);

}
                                                                                
sub daemonControlThread() {

  debugMessage(2, "Daemon control thread starting");

  my $pidfile = $cfg{"SERVER_CONTROL_DIR"}."/".PIDFILE;

  my $daemon_quit_file = Dada->getDaemonControlFile($cfg{"SERVER_CONTROL_DIR"});

  # Poll for the existence of the control file
  while ((!-f $daemon_quit_file) && (!$quit_daemon)) {
    sleep(1);
  }

  # set the global variable to quit the daemon
  $quit_daemon = 1;

  debugMessage(2, "Unlinking PID file: ".$pidfile);
  unlink($pidfile);

  debugMessage(2, "Daemon control thread ending");

}

