#!/usr/bin/env perl

###############################################################################
#
# This script transfers data from a directory on the pwc, to a directory on the
# nexus machine

use lib $ENV{"DADA_ROOT"}."/bin";

use strict;        
use warnings;        
use Caspsr;
use Dada::client_auxiliary_manager qw(%cfg);


#
# Global Variables
# 
%cfg = Caspsr->getConfig();

#
# Initialize module variables
#
$Dada::client_auxiliary_manager::dl = 2;
$Dada::client_auxiliary_manager::log_host = $cfg{"SERVER_HOST"};
$Dada::client_auxiliary_manager::log_port = $cfg{"SERVER_SYS_LOG_PORT"};
$Dada::client_auxiliary_manager::log_sock = 0;
$Dada::client_auxiliary_manager::daemon_name = Dada->daemonBaseName($0);
$Dada::client_auxiliary_manager::dada_header_cmd = "dada_header -k fada";
$Dada::client_auxiliary_manager::aux_db = "fada";


# Autoflush STDOUT
$| = 1;

my $result = 0;
$result = Dada::client_auxiliary_manager->main();

exit($result);
