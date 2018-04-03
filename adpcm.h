// ** adpcm.h - Korg ADPCM implementation used in the ES-1
// ** Reverse engineered version of Korg's ES2WAV.EXE program 
// ** RW 040310

// Korg ADPCM frame size (bytes)
#define FRAMESIZE (32) 

void uncompress(unsigned char inbuf[FRAMESIZE], short outbuf[FRAMESIZE]);
void compress(unsigned char outbuf[FRAMESIZE], short inbuf[FRAMESIZE]);

