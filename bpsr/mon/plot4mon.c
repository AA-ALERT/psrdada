/***************************************************************************/
/*                                                                         */
/* main module for plot4mon                                                */
/*                                                                         */
/* Ver 1.0        AP 20 Sept 2008                                          */
/*                                                                         */
/* code for generating various  1-D or 2-D plots (standard device set by   */
/* the variable STD_DEVICE in plot4mon.h) for monitoring the BPSR data     */
/* taking. Usage is reported on-line with plot4mon -h                      */
/*                                                                         */
/* Ver 2.0 RB 04 Oct 2008
   modified for new file format and to overplot bandpass and time series for 
   both pol0 and pol1, labels and title based on file extension, default dev
   png                                                                     */ 		
/* 
   RB 05 Oct 2008: plots with x axes in proper units (e.g. MHz, secs,...)  */
/*                                                                         */
/* RB 06 Oct 2008: implemented the resolution (pixel dimension) mode       */
/*                                                                         */
/* DRAFT VERSION                                                           */
/* (REFINEMENTS IN GRAPHICS, INTERACTIVE SETTING OF PARAMETERS, ADDTIONAL  */
/*  PLOTS... ETC... ARE IN PROGRESS)                                       */ 
/*                                                                         */ 
/***************************************************************************/

#include "plot4mon.h"

int main (int argc, char *argv[])
{
  int  plotnum=0,dolog=0,dolabel=1,dobox=1,dommm=0,ndata=156250;
  char inpfile[80],inpdev[80],outputfile[80];
  char inpfile0[80],inpfile1[80];
  char xlabel[80],ylabel[80],plottitle[80];
  char add_work[8];
  long totvaluesread,totvalues4plot;
  int  nchan,ndim,firstdump_line,work_flag,nbin_x,nsub_y;
  float xscale=1.0,yscale, tsamp, fch1, chbw;
  float *x_read, *y_read, *y_read1, *y_new, *y_new1;

  // plot dimensions in pixels
  unsigned width_pixels = 0, height_pixels = 0;


  /* reading the command line   */
  //get_commandline(argc,argv,inpfile,inpdev,outputfile);
  get_commandline(argc,argv,inpfile0,inpfile1,inpdev,outputfile,&dolog,&dolabel,
		  &dobox, &dommm, &width_pixels, &height_pixels);

  fprintf (stderr, "Pixel dimensions: %d x %d \n",width_pixels,height_pixels);

  /* determining the relevant parameters of the data and plot */
  read_params(inpfile1,&nchan,&tsamp,&fch1,&chbw,
	      &ndim,&yscale,&firstdump_line,
	      &work_flag,add_work);

  // make these dynamic once ascii header is implemented
  x_read=(float *) malloc(ndata*sizeof(float));
  y_read=(float *) malloc(ndata*sizeof(float));
  y_read1=(float *) malloc(ndata*sizeof(float));
  y_new=(float *) malloc(ndata*sizeof(float));
  y_new1=(float *) malloc(ndata*sizeof(float));

  // if time series also fft and plot
  if (strstr(inpfile1,"ts") != NULL) {
    work_flag=1;
    strcpy(add_work,"fft"); } 
  else {
    work_flag=0;
    strcpy(add_work,"null"); }

  /* reading the data and filling the y array with them */
  read_stream(ndata,inpfile0,&y_read[0],&totvaluesread);
  read_stream(ndata,inpfile1,&y_read1[0],&totvaluesread);


  /* enter the loop on the plots to be produced with the data in inpfile1 */
  while (plotnum <= work_flag) { 
   

    // if time series scale x axis by tsamp

    if (strstr(inpfile1,"ts") && (plotnum==0)) xscale=tsamp;

    /* perform additional work (fft,averaging,etc...) on the data if required */ 
    work_on_data(inpfile0,&y_read[0],&y_new[0],totvaluesread,
		 &totvalues4plot,tsamp,yscale,plotnum,add_work,dolog,dommm);

    work_on_data(inpfile1,&y_read1[0],&y_new1[0],totvaluesread,
		 &totvalues4plot,tsamp,yscale,plotnum,add_work,dolog,dommm);

    /* creating the name for the output pgplot file */ 
    create_outputname(inpfile1,inpdev,outputfile,plotnum,add_work);

  /* assigning the labels of the plot */
  //create_labels(inpfile1,plotnum,xlabel,ylabel,plottitle);

  // labels and title - make this new create_labels (todo)
  if (strstr(inpfile1, "bps") != NULL) {
    strcpy(xlabel," Frequency (MHz) ");
    strcpy(ylabel," Power level ");
    strcpy(plottitle," RMS Bandpass ");
  } else if (strstr(inpfile1,"bp") != NULL) {
    strcpy(xlabel," Frequency (MHz) ");
    strcpy(ylabel," Power level ");
    strcpy(plottitle," Mean Bandpass ");
  } else if (strstr(inpfile1,"ts") != NULL) {
    if (plotnum == 1) {
      strcpy(xlabel," Frequency (Hz) ");
      if (dolog)
	strcpy(ylabel," Log(Power) ");
      else
	strcpy(ylabel," Power ");
      strcpy(plottitle," Zero DM FFT ");
    } else { 
      strcpy(xlabel," Time (seconds) ");
      strcpy(ylabel," Power level ");
      strcpy(plottitle," Zero DM Time Series");
    }
  }

  /* plotting the data */
  if (ndim==1) 
  {
    float mmm_scale = tsamp;
    if (dommm && plotnum == 0)
      mmm_scale *= MMM_REDUCTION;

    /* filling the x array with suitable indexes */
    create_xaxis(inpfile1,plotnum,totvaluesread,totvalues4plot,
                 fch1,chbw,xscale,nchan,mmm_scale,&x_read[0]);  

    /* creating a 1-D plot with pgplot */
    plot_stream_1D(&x_read[0], &y_new[0], &y_new1[0], totvalues4plot,
		   outputfile, xlabel, ylabel, plottitle, dolabel,
		   dobox, dommm && (plotnum==0), width_pixels, height_pixels);
  } 
  else if (ndim==2) 
  {
    set_array_dim(totvalues4plot,nchan,&nbin_x,&nsub_y);
    /* creating a 2-D plot with pgplot */
    plot_stream_2D(&y_new[0],nbin_x,nsub_y,yscale,firstdump_line,
		outputfile,xlabel,ylabel,plottitle);  
  }
  plotnum++; 
  } // closing the loop

  /* free up memory */
  free(x_read);  
  free(y_read);
  free(y_read1);
  free(y_new);
  free(y_new1);

  /* finishing the program */
  exit(0);
}
