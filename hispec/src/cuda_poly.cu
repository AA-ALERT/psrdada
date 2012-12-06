#if USE_DADA
#include <ipcio.h>
#include <dada_hdu.h>
#endif
#include <sys/time.h>
#include <cufft.h>
#include <complex.h>

#include "cuda_poly.h"

/********************************************************************
 *                     Various Kernel Functions                     *
 * *****************************************************************/

/* Kernel function for polyphase filter, using the overlap-add method */
__global__ void overlap_add_kernel(float *out, float *in, float *window, int windowBlocks)
{
  int i;

  int size = blockDim.x * gridDim.x;

  int index = blockIdx.x * blockDim.x + threadIdx.x;

  int batch = blockIdx.y;
  int nbatch = gridDim.y;

  int inp = blockIdx.z;


  for( i = 0; i < windowBlocks; i++ )
  {
    out[inp*nbatch*size + batch*size + index] = 
      out[inp*nbatch*size + batch*size + index] + window[i*size + index] * 
      in[inp*(nbatch+windowBlocks-1)*size + (batch+i)*size + index];
  }
}

#if 0 /* Newer but somehow slower method */
__global__ void overlap_add_kernel(float *out, float *in, float *window)
{
  int size = blockDim.x * gridDim.x;
  int index = blockIdx.x * blockDim.x + threadIdx.x;

  int batch = blockIdx.y;
  int nbatch = gridDim.y;

  int inp = blockIdx.z;
  int windowBlocks = blockDim.y;

  /* Use shared memory to store the intermiediate results */
  /* FIXME: Currently for simplicity, hard code the share memory size.
   * It should be the same as numThreads * windowBlocks.
   */
  int tempIdx = threadIdx.x*blockDim.y + threadIdx.y;
  __shared__ float temp[1024];

  temp[tempIdx] = 
    window[threadIdx.y*size + index] * 
    in[inp*(nbatch+windowBlocks-1)*size + (batch+threadIdx.y)*size + index];

  __syncthreads();
  /* FIXME: Currently hard coded for 8 windowBlocks. 
   * Should make it more rebust with a loop, or use another level 
   * of parallel sum reduction 
   */
  if( threadIdx.y % 2 == 0 )
    temp[tempIdx] += temp[tempIdx+1];

  __syncthreads();

  if( threadIdx.y % windowBlocks == 0 )
    out[inp*nbatch*size + batch*size + index] = 
      temp[tempIdx] + temp[tempIdx+2] + temp[tempIdx+4] + temp[tempIdx+6];
}
#endif
/* Kernel function for polyphase filter, using oversample-decimate method */
/* FIXME */
__global__ void oversample_decimate_kernel(float *out, float *in, float *window, int windowBlocks)
{

}


/* Kernel for reading unsigned data into GPU */
__global__ void unpackUnsignedData_kernel(unsigned char *buf, float *out)
{
  int npoints = blockDim.x * gridDim.x;
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  
  int nbatch = gridDim.y;
  int batch = blockIdx.y; 
  
  int ninp = gridDim.z;
  int inp = blockIdx.z; 

  out[inp*nbatch*npoints + batch*npoints + index] = 
    (float)( buf[batch*npoints*ninp + index*ninp + inp] - 128 );
}

/* Kernel for reading signed data into GPU */
/* FIXME */
__global__ void unpackSignedData_kernel(unsigned char *buf, float *out)
{
  int npoints = blockDim.x * gridDim.x;
  int index = blockIdx.x * blockDim.x + threadIdx.x;

  int nbatch = gridDim.y;
  int batch = blockIdx.y;

  int ninp = gridDim.z;
  int inp = blockIdx.z;

  out[inp*nbatch*npoints + batch*npoints + index] = 
    ( (char *)buf )[batch*npoints*ninp + index*ninp + inp];
}

/* Kernel for performing CMAC auto correlation */
__global__ void CMAC_auto_kernel(int nbatch, cufftComplex *ft, float *corr)
{
  int batch;
  int nchan = blockDim.x * gridDim.x;
  int inp = blockIdx.y;
  int chan = blockIdx.x * blockDim.x + threadIdx.x;
  int index;

  for( batch = 0; batch < nbatch; batch++ )
  {
    /* The fourier transform is 1 size larger */
    index = inp*nbatch*(nchan+1) + batch*(nchan+1) + chan;

    corr[inp*nchan + chan] +=
      ft[index].x * ft[index].x + ft[index].y * ft[index].y; 
  }
}

/* Kernel for performing CMAC cross correlation */
__global__ void CMAC_cross_kernel(int ninp, int nbatch, cufftComplex *ft, cufftComplex *corr)
{
  int i, j;
  int batch;
  
  int nchan = blockDim.x * gridDim.x;
  int cross = blockIdx.y;
  
  int chan = blockIdx.x * blockDim.x + threadIdx.x;
  int inp1 = -1, inp2 = 1;

  int index1, index2;

  /* An algorithm to calculate inp1 and inp2 in this thread for cross correlation. */
  /* Need a bit more experiments to investigate the time performance of this method */  
  i = -1;
  j = -1;
  do
  {
    i++;
    inp1++;
    inp2 = inp1 + 1;
    j = j + ninp - inp1 - 1;
    while( i < cross && i < j )
    {
      inp2++;
      i++;
    }
  }
  while( i < cross );

  for( batch = 0; batch < nbatch; batch++ )
  {
    /* Calculate the indices from inp1 and inp2 to identify which input to be used */
    index1 = inp1*nbatch*nchan + batch*nchan + chan;
    index2 = inp2*nbatch*nchan + batch*nchan + chan;

    corr[cross * nchan + chan].x += 
      ft[index1].x * ft[index2].x + ft[index1].y * ft[index2].y;
    
    corr[cross * nchan + chan].y += 
      ft[index1].y * ft[index2].x - ft[index1].x * ft[index2].y;
  }
}

/* Kernel for performing the creal function for auto correlation 
 * (extracting real part of complex array from the needed stream) */
#if 0 /* Not needed anymore as the auto correlation now uses float * directly */
__global__ void creal_auto_kernel(float *out, cufftComplex *buf)
{
  int i;
  int tx = threadIdx.x;
  int bx = blockIdx.x;
  int by = blockIdx.y;
  int ntx = blockDim.x;
  int nchan = ntx * gridDim.x;
  int cprod = 0;

  int chan = bx * ntx + tx;

  int ninp = gridDim.y;

  int index = by * nchan + chan;

  /* An algorithm to calculate cprod for this thread, only for auto correlation */
  for( i = 0; i < by; i++ )
    cprod += ninp - i;

  out[index] = buf[cprod * nchan + chan].x;
}
#endif

/* Kernel for performing normalisation */
__global__ void normalise_complex_kernel(cufftComplex *buf, float normaliser)
{
  int nchan = blockDim.x * gridDim.x;
  int index = blockIdx.y * nchan + blockIdx.x * blockDim.x + threadIdx.x;
  
  buf[index].x *= normaliser;
  buf[index].y *= normaliser;
}

__global__ void normalise_float_kernel(float *buf, float normaliser)
{
  int nchan = blockDim.x * gridDim.x;
  int index = blockIdx.y * nchan + blockIdx.x * blockDim.x + threadIdx.x;

  buf[index] *= normaliser;
}


/********************************************************************
 * **********              End of GPU kernels               *********
 *******************************************************************/



/* GPU correlator, will replace the CPU correlator if GPU is enabled */
void gpu_corr( int nchan, int ninp, int ncross, int windowBlocks, int nbatch,
    int prod_type, char *polyMethod, float *cuda_inp_buf, float *cuda_window_buf, 
    float *cuda_poly_buf, cufftComplex *cuda_ft_buf, 
    cufftComplex *cuda_cross_corr, float *cuda_auto_corr,
    float *poly_time, float *fft_time, float *cmac_time )
{
  struct timeval thetime;

  /* Multiply the data with the window function */
  gettimeofday(&thetime, NULL);
  polyphase_gpu( ninp, windowBlocks, nchan * 2, nbatch, polyMethod, 
      cuda_poly_buf, cuda_inp_buf, cuda_window_buf);
  cudaThreadSynchronize();
  *poly_time += elapsed_time(&thetime);

  /* Perform CUDA FFT */
  gettimeofday(&thetime, NULL);
  do_CUFFT(nchan, ninp, nbatch, cuda_poly_buf, cuda_ft_buf);
  cudaThreadSynchronize();
  *fft_time += elapsed_time(&thetime);

  /* Perform CMAC */
  gettimeofday(&thetime, NULL);
  do_CUDA_CMAC(nchan, ninp, ncross, nbatch, prod_type, 
      cuda_ft_buf, cuda_cross_corr, cuda_auto_corr);
  cudaThreadSynchronize();
  *cmac_time += elapsed_time(&thetime);
}

/* Reads memory into GPU in batch */
#if USE_DADA
int readDataToGPU(int nchan, int ninp, int windowBlocks, int nbatch, int bits_per_samp, dada_hdu_t *hdu, float *cuda_inp_buf, int debug, int wordtype)
#else
int readDataToGPU(int nchan, int ninp, int windowBlocks, int nbatch, int bits_per_samp, FILE *fpin, float *cuda_inp_buf, int debug, int wordtype)
#endif
{
  int i;
  static int init = 0, ntoread = 0;
  static unsigned char *buffer = NULL;
  static unsigned char *cudaBuffer;

  int nread;
  struct timeval starttime;

  gettimeofday( &starttime, NULL );

  if( init == 0 )
  {
    ntoread = ninp * nchan * 2 * nbatch * bits_per_samp / 8;
    init = 1;
    buffer = (unsigned char *)malloc(ntoread);
    cudaMalloc( (void **)&cudaBuffer, ntoread );
    if( debug )
      fprintf( stderr, "size of read buffer: %d bytes\n", ntoread );
  }

  struct timeval thetime;
  float fileReadTime=0, cudaCopyTime=0, totalTime=0, unpackTime=0;

  gettimeofday( &thetime, NULL );
#if USE_DADA
  nread = ipcio_read( hdu->data_block, (char *)buffer, ntoread );
#else
  nread = fread( buffer, 1, ntoread, fpin );
#endif
  fileReadTime += elapsed_time(&thetime);
  

  if( nread < ntoread ) 
  {
    free( buffer );
    cudaFree( cudaBuffer );
    return 1; 
  }

  /* Call the appropriate function base of wordtype */
  /* Experiments show that CUDA kernel is slower than CPU with ninp == 1.
   * Still need to test with ninp > 1 */
  
  /*if( ninp == 1 && nbatch == 1 )
  {
    for( chan = 0; chan < nchan * 2; chan++ )
    {
      temp[chan] = (float)(buffer[chan] - 128);
    }
    cudaMemcpy(&cuda_inp_buf[nchan*2*tail], temp, nchan * 2 * sizeof(float), 
	cudaMemcpyHostToDevice);
  }

  else*/

  gettimeofday( &thetime, NULL );
  cudaMemcpy( cudaBuffer, buffer, ntoread, cudaMemcpyHostToDevice );
  /* Copy the last (windowBlocks-1) chunks to the beginning for each stream.
   * Do it after the first reading.
   */
  if( init == 1 )
  {
    for( i = 0; i < ninp; i++ )
      cudaMemcpy( &cuda_inp_buf[i * (nbatch+windowBlocks-1) * nchan * 2], 
	  &cuda_inp_buf[i*(nbatch+windowBlocks-1)*nchan*2 + (nbatch)*nchan*2], 
	  (windowBlocks-1) * nchan * 2 * sizeof(float), cudaMemcpyDeviceToDevice );
  }
  cudaCopyTime += elapsed_time(&thetime);
  
  /* Thread number should be multiple of 32 for best efficiency */
  /* Assume nchan to be power of 2 */
  dim3 threads( 128, 1, 1 );
  dim3 blocks( nchan * 2 / 128, nbatch, ninp );
  
  /* cuda_inp_buf needs to be offset by (windowBlocks-1) chunks due to the circular queue design */
  gettimeofday( &thetime, NULL );
  if( wordtype == 0 )
    unpackUnsignedData_kernel<<< blocks, threads >>>(cudaBuffer, &cuda_inp_buf[(windowBlocks-1) * nchan * 2]);
  /* FIXME: Not sure about the correctness of signed data unpacking */
  else if( wordtype == 1 )
    unpackSignedData_kernel<<< blocks, threads >>>(cudaBuffer, &cuda_inp_buf[(windowBlocks-1) * nchan * 2]);

  cudaThreadSynchronize();
  unpackTime += elapsed_time(&thetime);
  totalTime += elapsed_time(&starttime);

  //fprintf( stderr, "File read: %f, cudaMemcpy: %f, data unpack: %f, total: %f\n", 
    //  fileReadTime, cudaCopyTime, unpackTime, totalTime );

  return 0;
}

/* Assume that the digitised data coming in 8 bits array */
/* The data size is assumed to be nchan*2*ninp*nbatch */
int unpackDigitisedDataToGPU(int nchan, int ninp, int windowBlocks, int nbatch, int bits_per_samp, unsigned char *digitised_data, float *cuda_inp_buf, int debug, int wordtype)
{
  int i;
  static int init = 0, ntoread = 0;
  static unsigned char *cudaBuffer;
  //static unsigned char *buffer;
  static int numThreads = 64;

  struct timeval starttime;

  gettimeofday( &starttime, NULL );

  if( init == 0 )
  {
    ntoread = ninp * nchan * 2 * nbatch * bits_per_samp / 8;
    init = 1;
    cudaMalloc( (void **)&cudaBuffer, ntoread );
    //buffer = (unsigned char *)malloc(ntoread);
    if( debug )
      fprintf( stderr, "size of read buffer: %d bytes\n", ntoread );
  }

  struct timeval thetime;
  float cudaCopyTime=0, totalTime=0, unpackTime=0;

  //memcpy( buffer, digitised_data, ntoread );

  gettimeofday( &thetime, NULL );
  cudaMemcpy( cudaBuffer, digitised_data, ntoread, cudaMemcpyHostToDevice );
  cudaCopyTime += elapsed_time(&thetime);
  /* Copy the last (windowBlocks-1) chunks to the beginning for each stream.
   * 
   */
  for( i = 0; i < ninp; i++ )
    cudaMemcpy( &cuda_inp_buf[i * (nbatch+windowBlocks-1) * nchan * 2], 
	&cuda_inp_buf[i*(nbatch+windowBlocks-1)*nchan*2 + (nbatch)*nchan*2], 
	(windowBlocks-1) * nchan * 2 * sizeof(float), cudaMemcpyDeviceToDevice );
  
  /* Thread number should be multiple of 32 for best efficiency */
  /* Assume nchan to be power of 2 and larger than numThreads */
  dim3 threads( numThreads, 1, 1 );
  dim3 blocks( nchan * 2 / numThreads, nbatch, ninp );
  
  /* cuda_inp_buf needs to be offset by (windowBlocks-1) chunks due to the algorithm design */
  gettimeofday( &thetime, NULL );
  if( wordtype == 0 )
    unpackUnsignedData_kernel<<< blocks, threads >>>(cudaBuffer, &cuda_inp_buf[(windowBlocks-1) * nchan * 2]);
  /* FIXME: Not sure about the correctness of signed data unpacking */
  else if( wordtype == 1 )
    unpackSignedData_kernel<<< blocks, threads >>>(cudaBuffer, &cuda_inp_buf[(windowBlocks-1) * nchan * 2]);

  cudaThreadSynchronize();
  unpackTime += elapsed_time(&thetime);
  totalTime += elapsed_time(&starttime);

  //fprintf( stderr, "cudaMemcpy time: %g, size: %d MB\n", cudaCopyTime, ntoread / 1024 / 1024 );

  return 0;
}


/* Calculate the polyphase output using the method of choice */
void polyphase_gpu(int ninp, int windowBlocks, int size, int nbatch, 
    char *polyMethod, float *cuda_poly_buf, float *cuda_inp_buf, float *cuda_window_buf)
{
  int numThreads = 128;
  cudaMemset( cuda_poly_buf, 0, ninp * size * nbatch * sizeof(float) );

  /* Polyphase calculation by adding up the weighted time segments */
  if( strcmp(polyMethod, "weighted-overlap-add") == 0 )
  {
    /* Thread numbers should be multiple of 32 for best efficiency */
    /* Assume also windowBlocks is a power of 2 */
    dim3 threads( numThreads, 1, 1 );
    dim3 blocks( size / numThreads, nbatch, ninp );
    overlap_add_kernel<<< blocks, threads >>>(cuda_poly_buf, cuda_inp_buf, 
	cuda_window_buf, windowBlocks);
  }

  /* Polyphase calculation by performing FFT at higher sample rate and decimate */
  /* FIXME: Not yet implemented */
  else if( strcmp(polyMethod, "oversample-decimate") == 0 )
  {
    dim3 threads( numThreads, 1, 1 );
    dim3 blocks( size / numThreads, nbatch, ninp );
    oversample_decimate_kernel<<< blocks, threads >>>(cuda_poly_buf, cuda_inp_buf, 
	cuda_window_buf, windowBlocks);
  }
  else
  {
    fprintf( stderr, "Invalid polyphase method: %s\n", polyMethod );
    exit(1);
  }
}

/* CUDA FFT, will perform parallel execution if ninp > 1 */
void do_CUFFT(int nchan, int ninp, int nbatch, float *cuda_poly_buf, cufftComplex *cuda_ft_buf)
{
  static cufftHandle plan;
  static int doneplan = 0;

  if( !doneplan ) 
  {
    /* Setup the FFT plan for CUDA, it will do parallel FFT if ninp > 1 */
    cufftPlan1d( &plan, nchan * 2, CUFFT_R2C, ninp * nbatch );
    doneplan = 1;
  }

  cufftExecR2C( plan, cuda_poly_buf, cuda_ft_buf );
}

/* Perform CMAC in GPU. */
void do_CUDA_CMAC(int nchan, int ninp, int ncross, int nbatch, int prod_type, cufftComplex *cuda_ft_buf, cufftComplex *cuda_cross_corr, float *cuda_auto_corr)
{
  int numThreads = 64;

  if( prod_type == 'A' || prod_type == 'B' )
  {
    /* Uses ninp instead of ncorr for auto correlator */
    dim3 threads( numThreads, 1, 1 );
    dim3 blocks( nchan / numThreads, ninp, 1 );
    CMAC_auto_kernel<<< blocks, threads >>>(nbatch, cuda_ft_buf, cuda_auto_corr);
  }
  else if( prod_type == 'C' || prod_type == 'B' )
  {
    /* Cannot do cross correlation with only 1 input stream */
    if( ninp == 1 || ncross == 0 ) 
    {
      fprintf( stderr, "Warning: attempt to perform cross correlation with only 1 input stream.\n" );
    }
    else
    {
      /* Cross correlator using ncorr */
      dim3 threads( numThreads, 1, 1);
      dim3 blocks( nchan / numThreads, ncross, 1 );

      CMAC_cross_kernel<<< blocks, threads >>>(ninp, nbatch, cuda_ft_buf, cuda_cross_corr);
    }
  }
  else
  {
    fprintf( stderr, "Invalid prod_type\n" );
    exit(1);
  }
}

/* Write the results into a buffer of output, size set to
 yaxis_size, the whole output will be written to a file everytime 
 it obtains nrows_per_refresh output. yaxis_size must be divisible
 by rows_per_refresh */
void writeGPUOutput(FILE *fout_ac, FILE *fout_cc, int ninp, int nchan, 
    int ncross, int naver, int prod_type, int nbatch, int isLast, 
    float normaliser, int yaxis_size, int rows_per_refresh,
    cufftComplex *cuda_cross_corr, float *cuda_auto_corr)
{
  int i;
  static int init = 1;
  static complex float *ctemp_buf;
  static float *temp_buf = NULL;

  static int row = 0;
  static int rowBlock= 0;
  static int nrowBlocks = yaxis_size / rows_per_refresh;
  static FILE *ftemp_ac;
  static FILE *ftemp_cc;

  char filename[BUFSIZ];

  //static FILE *fp;

  int numThreads = 32;

  if( init )
  {
    //fp = fopen( "temp.csv", "w" );

    init = 0;
    ctemp_buf = (complex float *)malloc( yaxis_size * nchan * ncross * sizeof(float) );
    temp_buf = (float *)malloc( yaxis_size * nchan * ninp * sizeof(float) );
  }

  dim3 threads(numThreads, 1, 1);
  dim3 blocks(nchan / numThreads, ninp, 1);

  /* Keep track of which row is it now */
  /* rowBlock is used to keep track of which block of data is getting printed out, when row = 10-19, rowBlock = 1 (if rows_per_refresh = 10) */
  rowBlock = row / rows_per_refresh; 
  
  if( prod_type == 'A' || prod_type == 'B' )
  {
    /* Normalisation, number of points are obtained from the threads and blocks number */
    normalise_float_kernel<<< blocks, threads >>>(cuda_auto_corr, normaliser);
    
    /* Extract the real numbers, for auto correlation only */
    cudaMemcpy( &temp_buf[row*nchan*ninp], cuda_auto_corr, nchan * ninp * sizeof(float), cudaMemcpyDeviceToHost );
    
    /*for( i = 0; i < nchan * ninp; i++ )
    {
      fprintf( fp, "%e ", 10 * log(temp_buf[i]) );
    }
    fprintf( fp, "\n" );*/
    fwrite( &temp_buf[row*nchan*ninp], sizeof(float), nchan * ninp, fout_ac );
    cudaMemset( cuda_auto_corr, 0, (nchan) * ninp * sizeof(float) );
  
    /* output auto correlation results to a file with time stamp */
    if( row % rows_per_refresh == 0 )
    {
      time_stamp(filename);
      /* ac extension to mean auto correlation */
      sprintf( filename, "%s.ac", filename );

      ftemp_ac = fopen( filename, "w" );

      for( i = (rowBlock+1)%nrowBlocks; i != rowBlock; i = (i+1)%nrowBlocks )
      {
	fwrite( &temp_buf[i*rows_per_refresh*nchan*ninp], sizeof(float), 
	    rows_per_refresh*nchan*ninp, ftemp_ac );
      }

      fclose(ftemp_ac);
    }

  }

  else if( prod_type == 'C' || prod_type == 'B' )
  {
    /* Just change the y-dimension of the block number */
    blocks.y = ncross;
    normalise_complex_kernel<<< blocks, threads >>>(cuda_cross_corr, normaliser);
    
    cudaMemcpy( &ctemp_buf[row*nchan*ncross], cuda_cross_corr, (nchan) * ncross * sizeof(complex float), cudaMemcpyDeviceToHost );

    fwrite( &ctemp_buf[row*nchan*ncross], sizeof(complex float), nchan * ncross, fout_cc );
    cudaMemset( cuda_cross_corr, 0, (nchan) * ncross * sizeof(cufftComplex) );

    /* output cross correlation results to a file with time stamp */
    if( row % rows_per_refresh == 0 )
    {
      time_stamp(filename);
      /* ac extension to mean auto correlation */
      sprintf( filename, "%s.cc", filename );

      ftemp_cc = fopen( filename, "w" );

      for( i = (rowBlock+1)%nrowBlocks; i != rowBlock; i = (i+1)%nrowBlocks )
      {
	fwrite( &ctemp_buf[i*rows_per_refresh*nchan*ninp], sizeof(cufftComplex), 
	    rows_per_refresh*nchan*ninp, ftemp_cc );
      }


      fclose(ftemp_cc);
    }
  }

  else
  {
    fprintf( stderr, "Invalid prod type: %c\n", (char) prod_type );
    exit(1);
  }

  row = (row+1) % yaxis_size;

  /* When this is the last output, free the memory */
  if( isLast )
  {
    free(temp_buf);
    free(ctemp_buf);
  }
}

/* returns the elapsed wall-clock time, in ms, since start (without resetting start) */
float elapsed_time(struct timeval *start){
    struct timeval now;
    gettimeofday(&now,NULL);
    return 1.e3f*(float)(now.tv_sec-start->tv_sec) +
        1.e-3f*(float)(now.tv_usec-start->tv_usec);
}

/* return a string representation of local time stamp, in the format of
 yyyy-mm-dd-hh:mm:ss */
void time_stamp(char *str)
{
  struct tm *tm;
  time_t current;

  time(&current);

  tm = localtime(&current);

  /* Assume that the char array is large enough */
  sprintf( str, "%d-%.2d-%.2d-%.2d:%.2d:%.2d", 
      1900 + tm->tm_year, tm->tm_mon, tm->tm_mday, 
      tm->tm_hour, tm->tm_min, tm->tm_sec, str );
}
