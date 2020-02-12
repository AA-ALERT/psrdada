
SUBDIRS = scripts config

# source code for mopsr heavily dependent on PGPLOT, FFTW3 and CUDA
if HAVE_CUDA
if HAVE_PGPLOT
if HAVE_FFTW3
  SUBDIRS += src
endif
endif
endif

