// ** es12wav.c 
// ** Reverse engineered version of Korg's ES2WAV.EXE program 
// ** Take an ES-1 input file and generate .wav files.
// ** RW 040314
// ** 1.0  initial test
// ** 1.2  create output directory
// ** 1.3  stereo samples fixed
// ** 1.4  wav output not cpu-endian-dependent

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "adpcm.h"


#define DEBUG (0)

// # samples in the ES-1
#define MONO_SAMPLES  (100)
#define STEREO_SAMPLES  (50)
#define TOTAL_SAMPLES (MONO_SAMPLES+STEREO_SAMPLES)
#define ES1_SAMPLERATE (32000)
#define ES1_SAMPLEBITS (16)

// Location of stuff in input file (.es1)
#define HEADERPOS (524288L)
#define SAMPLESPOS (655360L) // not used ?

// Size of sample headers in .es1 file
#define MONO_SAMPLEHEAD_SIZE (26)
#define STEREO_SAMPLEHEAD_SIZE (28)

// Offset for sample addresses in the sample headers
#define ADDR_OFFSET (393216L)

// Mono sample header offsets
enum msamplehead
{
  MSMPLHEAD_ST_H = 0, MSMPLHEAD_ST_M, MSMPLHEAD_ST_L,
  MSMPLHEAD_END_H, MSMPLHEAD_END_M, MSMPLHEAD_END_L,
  MSMPLHEAD_STADDR_H, MSMPLHEAD_STADDR_M, MSMPLHEAD_STADDR_L,
  MSMPLHEAD_ENDADDR_H, MSMPLHEAD_ENDADDR_M, MSMPLHEAD_ENDADDR_L,
  MSMPLHEAD_STATUS = 21
};

enum ssamplehead
{
  SSMPLHEAD_END_H = 3, SSMPLHEAD_END_M, SSMPLHEAD_END_L,
  SSMPLHEAD_STADDR_H, SSMPLHEAD_STADDR_M, SSMPLHEAD_STADDR_L,
  SSMPLHEAD_STATUS = 21,
  SSMPLHEAD_ST_H = 22, SSMPLHEAD_ST_M, SSMPLHEAD_ST_L,
  SSMPLHEAD_ENDADDR_H, SSMPLHEAD_ENDADDR_M, SSMPLHEAD_ENDADDR_L
};


// Sample info structure.
// We create this ourselves after reading the .es1 file sample headers
// One for each sample
struct sampleinf
{
  int  sampleno;
  int  status;
  long startaddr;
  long lenbytes;
  long lensamples;
};


// Info for all samples
struct sampleinf sampleinfo[TOTAL_SAMPLES];


// Prototypes
int process_file(FILE *infile);
int read_sampleheaders(FILE *infile);
int write_wavfile(FILE *infile, char *filename, struct sampleinf *info);
int write_samples(FILE *infile, FILE *outfile, struct sampleinf *info);
int write_32bit_le(long value, FILE *file);
int write_16bit_le(short value, FILE *file);

// Code

int main(int argc, char **argv)
{
  char *infilename, *dirname;
  FILE *infile;
  int status;

  if (argc < 3)
  { 
    fprintf(stderr, "es12wav  v1.4\n");
    fprintf(stderr, "Usage: es12wav <es1file> <new-directory>\n");
    exit(1);
  }

  assert(sizeof(short) == 2);
  assert(sizeof(long) >= 4);

  dirname = argv[2];
  if (mkdir(dirname, 0777) < 0)
  {
    perror("Error creating directory");
    exit(1);
  }

  infilename = argv[1];
  infile = fopen(infilename, "rb");
  if (infile == NULL)
  {
    fprintf(stderr, "Can't open %s!\n", infilename);
    exit(1);
  }

  if (chdir(dirname) < 0)
  {
    perror("Error changing directory");
    fclose(infile);
    exit(1);
  }

  status = process_file(infile);

  fclose(infile);

  switch (status)
  {
    case 0: printf("Done.\n"); break;
    case 1: fprintf(stderr, "Error reading or bad format in infile\n"); break;
    case 2: fprintf(stderr, "Error writing outfile\n"); break;
    default: fprintf(stderr, "Undefined error occurred\n"); break;
  }

  return status;
}


int process_file(FILE *infile)
{
  char buf[20];
  char namebuf[10];
  int no_of_samples;
  int waveno;
  int sampleno;
  int status;

  // Sanity check on input file

  fread(buf, 1, 20, infile);
  if (strncmp(buf, "KORG", 4) != 0 || buf[6] != 87)
  {
    fprintf(stderr, "Not an ES1 file! (1)\n");
    return 1;
  }

  fseek(infile, HEADERPOS, SEEK_SET);
  fread(buf, 1, 20, infile);
  if (strncmp(buf, "KORG", 4) != 0 || buf[6] != 87)
  {
    fprintf(stderr, "Not an ES1 file! (2)\n");
    return 1;
  }

  // Read sample headers
  no_of_samples = read_sampleheaders(infile);
  if (no_of_samples == 0)
  {
    printf("No data in input file.\n");
    return 0;
  }

  // Process each sample
  for (waveno = 0; waveno < no_of_samples; waveno++)
  {
    sampleno = sampleinfo[waveno].sampleno;
    if (sampleno < MONO_SAMPLES)
      sprintf(namebuf, "%02d.wav", sampleno);
    else
      sprintf(namebuf, "%02ds.wav", sampleno - MONO_SAMPLES);

    status = write_wavfile(infile, namebuf, &sampleinfo[waveno]);

    if (status != 0)
      return status;

#if 0
    if (file_exists(namebuf))
    {
      fprintf(stderr, "%s exists!\n");
      exit(2);
    }
#endif
  }

  return 0;
}



int read_sampleheaders(FILE *infile)
{
  unsigned char monobuf[MONO_SAMPLEHEAD_SIZE];
  unsigned char stereobuf[STEREO_SAMPLEHEAD_SIZE];
  int waveno = 0;
  int sampleno;
  struct sampleinf *info;

  // Mono samples 0..99
  for (sampleno = 0; sampleno < MONO_SAMPLES; sampleno++)
  {
    fread(monobuf, sizeof monobuf, 1, infile);
    if (monobuf[MSMPLHEAD_STATUS] != 255)
    {
      info = &sampleinfo[waveno];
      info->sampleno = sampleno;
      info->status = monobuf[MSMPLHEAD_STATUS];
      info->lensamples = (monobuf[MSMPLHEAD_END_H] << 16) + 
                         (monobuf[MSMPLHEAD_END_M] << 8) +
                          monobuf[MSMPLHEAD_END_L] -
                         (monobuf[MSMPLHEAD_ST_H] << 16) -
                         (monobuf[MSMPLHEAD_ST_M] << 8) -
                          monobuf[MSMPLHEAD_ST_L] + 1;
      info->lenbytes = (monobuf[MSMPLHEAD_ENDADDR_H] << 16) + 
                       (monobuf[MSMPLHEAD_ENDADDR_M] << 8) +
                        monobuf[MSMPLHEAD_ENDADDR_L] -
                       (monobuf[MSMPLHEAD_STADDR_H] << 16) -
                       (monobuf[MSMPLHEAD_STADDR_M] << 8) -
                        monobuf[MSMPLHEAD_STADDR_L] + 1;
      info->startaddr = (monobuf[MSMPLHEAD_STADDR_H] << 16) +
                        (monobuf[MSMPLHEAD_STADDR_M] << 8) +
                         monobuf[MSMPLHEAD_STADDR_L] - ADDR_OFFSET;
#if DEBUG
      printf("MSample %d, status %d, lensamples %ld, lenbytes %ld, addr %ld\n", 
             info->sampleno, info->status, 
             info->lensamples, info->lenbytes, info->startaddr);
#endif
      waveno++;
    }
  }

  // stereo samples 100..149
  for (sampleno = 0; sampleno < STEREO_SAMPLES; sampleno++)
  {
    fread(stereobuf, sizeof stereobuf, 1, infile);
    if (stereobuf[SSMPLHEAD_STATUS] != 255)
    {
      info = &sampleinfo[waveno];
      info->sampleno = sampleno + MONO_SAMPLES;
      info->status = stereobuf[SSMPLHEAD_STATUS];
      info->lensamples = ((stereobuf[SSMPLHEAD_END_H] << 16) + 
                          (stereobuf[SSMPLHEAD_END_M] << 8) +
                           stereobuf[SSMPLHEAD_END_L] -
                          (stereobuf[SSMPLHEAD_ST_H] << 16) -
                          (stereobuf[SSMPLHEAD_ST_M] << 8) -
                           stereobuf[SSMPLHEAD_ST_L] + 1) * 2;
      info->lenbytes = (stereobuf[SSMPLHEAD_ENDADDR_H] << 16) + 
                       (stereobuf[SSMPLHEAD_ENDADDR_M] << 8) +
                        stereobuf[SSMPLHEAD_ENDADDR_L] -
                       (stereobuf[SSMPLHEAD_STADDR_H] << 16) -
                       (stereobuf[SSMPLHEAD_STADDR_M] << 8) -
                        stereobuf[SSMPLHEAD_STADDR_L];     // no + 1 !
      info->startaddr = (stereobuf[SSMPLHEAD_STADDR_H] << 16) +
                        (stereobuf[SSMPLHEAD_STADDR_M] << 8) +
                         stereobuf[SSMPLHEAD_STADDR_L] - ADDR_OFFSET;
#if DEBUG
      printf("SSample %d, status %d, lensamples %ld, lenbytes %ld, addr %ld\n", 
             info->sampleno, info->status, 
             info->lensamples, info->lenbytes, info->startaddr);
#endif
      waveno++;
    }
  }

  return waveno;
}


int write_wavfile(FILE *infile, char *filename, struct sampleinf *info)
{
  FILE *outfile;
  long samplebytes;
  long channels;
  long fmt_headerlen;
  long totallength;
  int status;
  // fmtheader:
  short tag_sh;
  short channels_sh;
  long sample_rate;
  long data_rate;
  short blk_algn_sh;
  short bits_per_sample_sh;
 

#if 1 // always do this
  printf("Creating %s from sample# %d\n", filename, info->sampleno);
#endif

  channels = (info->sampleno >= MONO_SAMPLES) ? 2 : 1;

  outfile = fopen(filename, "wb");
  if (outfile == NULL)
    return 2;

  samplebytes = info->lensamples * 2;
  totallength = samplebytes + 36; // sizeof fmtheader

  tag_sh = 1;
  channels_sh = (short) channels;
  
  sample_rate = ES1_SAMPLERATE;
  data_rate = sample_rate * channels * 2;
  blk_algn_sh = channels_sh * 2;
  bits_per_sample_sh = ES1_SAMPLEBITS;

  // Write file

  // .WAV header
  fwrite("RIFF", sizeof (char), 4, outfile);
  write_32bit_le(totallength, outfile);
  fwrite("WAVE", sizeof (char), 4, outfile);

  // fmt chunk
  fmt_headerlen = 16;
  fwrite("fmt ", sizeof (char), 4, outfile);  
  write_32bit_le(fmt_headerlen, outfile);

  write_16bit_le(tag_sh, outfile);
  write_16bit_le(channels_sh, outfile);
  write_32bit_le(sample_rate, outfile);
  write_32bit_le(data_rate, outfile);
  write_16bit_le(blk_algn_sh, outfile);
  write_16bit_le(bits_per_sample_sh, outfile);

  // data chunk
  fwrite("data", sizeof (char), 4, outfile); // data chunk
  status = write_32bit_le(samplebytes, outfile);
  if (status != 0)
  {
    fclose(outfile);
    return 2;
  }

  // Go to start address of sample in infile
  status = fseek(infile, info->startaddr, SEEK_SET);
  if (status != 0)
    return 1;

  // Uncompress and write samples to outfile
  status = write_samples(infile, outfile, info);
  if (status != 0)
  {
    fclose(outfile);
    return status;
  }

  fclose(outfile);
  return 0;
}


int write_samples(FILE *infile, FILE *outfile, struct sampleinf *info)
{
  long sampleunits_left;
  int sampleunits;
  int sampleno;
  int bytes_read;
  int status;
  long where;
  int stereo;
  unsigned char inbuf[FRAMESIZE];  // input (compressed) frame mono/left
  unsigned char inbufa[FRAMESIZE]; // input (compressed) frame right
  short outbuf[FRAMESIZE];         // output samples mono/left
  short outbufa[FRAMESIZE];        // output samples right


  stereo = (info->sampleno >= MONO_SAMPLES);
  sampleunits_left = info->lensamples / (stereo ? 2 : 1);

  // Do this one frame at a time. Last frame will be complete,
  // but may have fewer than FRAMESIZE samples
  do
  {
    // ** Read data and uncompress **
    sampleunits = (sampleunits_left > FRAMESIZE) ? FRAMESIZE : sampleunits_left;
    where = ftell(infile);
    bytes_read = fread(inbuf, 1, sizeof inbuf, infile);
    if (bytes_read != FRAMESIZE)
      return 1;
    uncompress(inbuf, outbuf);
    if (stereo)
    {
      fseek(infile, where + info->lenbytes, SEEK_SET);
      fread(inbufa, 1, sizeof inbufa, infile);
      uncompress(inbufa, outbufa);
      status = fseek(infile, where+FRAMESIZE, SEEK_SET);
      if (status != 0)
        return 1;
    }
    // ** Write to file **
    for (sampleno = 0; sampleno < sampleunits; sampleno++)
    {
      status = write_16bit_le(outbuf[sampleno], outfile);
      if (status != 0)
        return 2;
      if (stereo) 
        write_16bit_le(outbufa[sampleno], outfile);
    }
    sampleunits_left -= sampleunits;
  } while (sampleunits_left > 0);

  return 0;
}


// Write 32-bit little endian to file. Return 1 if failure, else 0.
int write_32bit_le(long value, FILE *file)
{
  return fputc(value & 255, file) == EOF ||
         fputc((value >> 8) & 255, file) == EOF ||
         fputc((value >> 16) & 255, file) == EOF ||
         fputc((value >> 24) & 255, file) == EOF;
}

// Write 16-bit little endian to file. Return 1 if failure, else 0.
int write_16bit_le(short value, FILE *file)
{
  return fputc(value & 255, file) == EOF || 
         fputc((value >> 8) & 255, file) == EOF;
}
      
