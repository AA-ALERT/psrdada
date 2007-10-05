#ifndef __DADA_UDP_H
#define __DADA_UDP_H

/* Maximum size of a UDP packet */
#define UDPBUFFSIZE 16384

/* Size of header component of the data packet */
#define UDPHEADERSIZE 14

/* Size of data component of the data packet */
#define UDPDATASIZE 1458

/* header struct for UDP packet from board */ 
typedef struct {
  unsigned char length;
  unsigned char source;
  unsigned int sequence;
  unsigned char bits;
  unsigned char channels;
  unsigned char bands;
  unsigned char bandID[4];
  unsigned int pollength;
} header_struct;

void decode_header(char *buffer, header_struct *header);
void encode_header(char *buffer, header_struct *header);

void encode_header(char *buffer, header_struct *header) {

  if (header->bits == 0) header->bits = 8;
        
  buffer[0] = header->length;
  buffer[1] = header->source;
  int temp = header->sequence;
  buffer[5] = temp & 0xff;
  buffer[4] = (temp >> 8) & 0xff;
  buffer[3] = (temp >> 16) & 0xff;
  buffer[2] = (temp >> 24) & 0xff;
  buffer[6] = header->bits;
  buffer[8] = header->channels;
  buffer[9] = header->bands;
  buffer[10] = header->bandID[0];
  buffer[11] = header->bandID[1];
  buffer[12] = header->bandID[2];
  buffer[13] = header->bandID[3];

}

void decode_header(char *buffer, header_struct *header) {
    
  int temp;

  /* header decode */
  header->length    = buffer[0];
  header->source    = buffer[1];
  header->sequence  = buffer[2]; 
  header->sequence  <<= 24;
  temp              = buffer[3];
  header->sequence  |= ((temp << 16) & 0xff0000);
  temp              = buffer[4];
  header->sequence  |= (temp << 8) & 0xff00;
  header->sequence  |=  (buffer[5] & 0xff);
                                                                
  header->bits      = buffer[6];    
  header->channels  = buffer[8];
  header->bands     = buffer[9];
                                                 
  header->bandID[0] = buffer[10];
  header->bandID[1] = buffer[11];
  header->bandID[2] = buffer[12];
  header->bandID[3] = buffer[13];
                      
}

#endif /* UDP_H */
