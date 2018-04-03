// adpcm.c - Korg ADPCM implementation used in the ES-1
// compression not yet working
// RW 040314

#include <stdio.h>
#include "adpcm.h"

#define DEBUG (0)

#define DELTA_SIGNBIT  (64)
#define DELTA_MAX      (63)

// ADPCM table sizes
#define TABLES     (4)
#define TABLESIZE  (64)

// State for the ADPCM algorithm
struct adpcmstate
{
  short framestartval;               // -32768..32767 (sample)
  unsigned char stepsize_index;      // 0..63
  unsigned char maxdiff_index;       // 0..63
  unsigned char bitdynamics;         // 0..15
  unsigned char tableno;             // 0..3
  long diff_average;                 // average of all (abs) diffs in frame
  long diff_max;                     // max diff (between samples) within frame
  long startdiff_max;                // max diff from first sample in frame
  long startdiff;                    // diff between first and second sample
  long highestval;
  long lowestval;
  long *stepsizeptr;                 // pointer to current step size in table
  long *tablestart;                  // start of current stepsize table
  long *tableend;                    // end of current stepsize table
  long *maxdiffptr;                  // maxdiff_tablepos as pointer
};


// ADPCM tables

long indextable[TABLESIZE] =
{
  -1, -1, -1, -1, -1, -1, -1, -1, 
  -1, -1, -1, -1, -1, -1, -1, -1, 
  -1, -1, -1, -1, -1, -1, -1, -1, 
  -1, -1, -1, -1, -1, -1, -1, -1, 
  1, 1, 1, 1, 2, 2, 3, 3,
  4, 4, 5, 5, 6, 6, 7, 7,
  8, 8, 9, 9, 10, 10, 11, 12,
  13, 13, 14, 15, 16, 17, 18, 19
};


long stepsizetable[TABLES][TABLESIZE] =
{  
  {
    2, 3, 3, 3, 3, 4, 4, 4,
    5, 5, 6, 6, 7, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 17, 18,
    20, 22, 24, 27, 29, 32, 35, 39,
    43, 47, 52, 57, 62, 69, 75, 83,
    91, 100, 110, 121, 133, 146, 161, 177,
    195, 214, 235, 259, 285, 313, 344, 379,
    417, 458, 504, 554, 610, 671, 738, 811
  },
  {  
    29, 32, 34, 37, 39, 42, 45, 49,
    52, 56, 60, 65, 70, 75, 80, 86,
    93, 100, 107, 115, 124, 133, 143, 154,
    166, 178, 191, 206, 221, 238, 255, 274,
    295, 317, 341, 367, 394, 424, 455, 490,
    526, 566, 608, 654, 703, 756, 813, 874,
    939, 1010, 1086, 1167, 1255, 1349, 1450, 1559,
    1677, 1802, 1938, 2083, 2240, 2408, 2589, 2783
  },
  {
    442, 465, 488, 512, 538, 565, 593, 622,
    653, 686, 720, 756, 794, 834, 875, 919,
    965, 1013, 1064, 1117, 1173, 1232, 1293, 1358,
    1426, 1497, 1572, 1650, 1733, 1819, 1910, 2005,
    2106, 2211, 2321, 2437, 2559, 2687, 2821, 2962,
    3110, 3266, 3429, 3600, 3780, 3969, 4168, 4376,
    4595, 4824, 5065, 5319, 5584, 5864, 6157, 6464,
    6787, 7127, 7483, 7857, 8250, 8662, 9095, 9549
  },
  {
    6916, 7089, 7267, 7448, 7634, 7825, 8021, 8221,
    8427, 8638, 8853, 9075, 9302, 9534, 9773, 10017,
    10267, 10524, 10787, 11057, 11333, 11616, 11907, 12204,
    12509, 12822, 13143, 13471, 13808, 14153, 14507, 14870,
    15241, 15622, 16013, 16413, 16823, 17244, 17675, 18117,
    18570, 19034, 19510, 19998, 20497, 21010, 21535, 22073,
    22625, 23191, 23771, 24365, 24974, 25598, 26238, 26894,
    27566, 28256, 28962, 29686, 30428, 31189, 31968, 32767
  }
};

// prototypes

void unpackbuf(unsigned char inbuf[FRAMESIZE], 
               unsigned char deltas[FRAMESIZE],
               struct adpcmstate *state);
long scale(long delta, long stepsize);
void set_initialstate(struct adpcmstate *state);
int direction(long *curval, struct adpcmstate *state);
void newstep(int valcase, int sign, int delta, struct adpcmstate *state);
long update(long curval, long diff, int sign);
void uncompressbuf(unsigned char deltas[FRAMESIZE],
                   short outbuf[FRAMESIZE],
                   struct adpcmstate *state);

void packbuf(unsigned char outbuf[FRAMESIZE], 
             unsigned char deltas[FRAMESIZE],
             struct adpcmstate *state);
long calcdelta(long *vpdiff, int *sign, long diff, struct adpcmstate *state);
void set_bitdynamics(struct adpcmstate *state);
void calcdiffs(short inbuf[FRAMESIZE], struct adpcmstate *state);
void set_tableno(struct adpcmstate *state);
void set_maxdiff_index(struct adpcmstate *state);
long stepsizetoindex(long stepsize, struct adpcmstate *state);
void compressbuf(unsigned char deltas[FRAMESIZE],
                 short inbuf[FRAMESIZE],
                 struct adpcmstate *state);


// code

// routines used by uncompression routines; some are also used when
// compressing

// unpack input frame to delta buffer and state
void unpackbuf(unsigned char inbuf[FRAMESIZE], 
               unsigned char deltas[FRAMESIZE],
               struct adpcmstate *state)
{
  // load initial state into adpcm state
  state->framestartval = (inbuf[0] << 8) | inbuf[1];
  state->stepsize_index = inbuf[2] >> 2;
  state->maxdiff_index = ((inbuf[2] << 4) & 48) | (inbuf[3] >> 4);
  state->bitdynamics = inbuf[3] & 15;
  state->tableno = inbuf[4] >> 6;

  // repack packed input deltas from 27x 8-bit bytes to 31x 7-bit bytes
  // inbuf[4..31] -> deltas[0..30]
  deltas[0] = ((inbuf[4] & 1) << 6) | (inbuf[5] >> 2);
  deltas[1] = ((inbuf[5] & 3) << 5) | (inbuf[6] >> 3);
  deltas[2] = ((inbuf[6] & 7) << 4) | (inbuf[7] >> 4);
  deltas[3] = ((inbuf[7] & 15) << 3) | (inbuf[8] >> 5);
  deltas[4] = ((inbuf[8] & 31) << 2) | (inbuf[9] >> 6);
  deltas[5] = ((inbuf[9] & 63) << 1) | (inbuf[10] >> 7);
  deltas[6] = inbuf[10] & 127;
  deltas[7] = inbuf[11] >> 1;
  deltas[8] = ((inbuf[11] & 1) << 6) | (inbuf[12] >> 2);
  deltas[9] = ((inbuf[12] & 3) << 5) | (inbuf[13] >> 3);
  deltas[10] = ((inbuf[13] & 7) << 4) | (inbuf[14] >> 4);
  deltas[11] = ((inbuf[14] & 15) << 3) | (inbuf[15] >> 5);
  deltas[12] = ((inbuf[15] & 31) << 2) | (inbuf[16] >> 6);
  deltas[13] = ((inbuf[16] & 63) << 1) | (inbuf[17] >> 7);
  deltas[14] = inbuf[17] & 127;
  deltas[15] = inbuf[18] >> 1;
  deltas[16] = ((inbuf[18] & 1) << 6) | (inbuf[19] >> 2);
  deltas[17] = ((inbuf[19] & 3) << 5) | (inbuf[20] >> 3);
  deltas[18] = ((inbuf[20] & 7) << 4) | (inbuf[21] >> 4);
  deltas[19] = ((inbuf[21] & 15) << 3) | (inbuf[22] >> 5);
  deltas[20] = ((inbuf[22] & 31) << 2) | (inbuf[23] >> 6);
  deltas[21] = ((inbuf[23] & 63) << 1) | (inbuf[24] >> 7);
  deltas[22] = inbuf[24] & 127;
  deltas[23] = inbuf[25] >> 1;
  deltas[24] = ((inbuf[25] & 1) << 6) | (inbuf[26] >> 2);
  deltas[25] = ((inbuf[26] & 3) << 5) | (inbuf[27] >> 3);
  deltas[26] = ((inbuf[27] & 7) << 4) | (inbuf[28] >> 4);
  deltas[27] = ((inbuf[28] & 15) << 3) | (inbuf[29] >> 5);
  deltas[28] = ((inbuf[29] & 31) << 2) | (inbuf[30] >> 6);
  deltas[29] = ((inbuf[30] & 63) << 1) | (inbuf[31] >> 7);
  deltas[30] = inbuf[31] & 127;

#if DEBUG
  printf("unpackbuf: startval %d, stepsize_idx %d, maxdiff_idx %d, bitdyn %d, tableno %d\n",
         state->framestartval, state->stepsize_index, state->maxdiff_index,
         state->bitdynamics, state->tableno);
  {
    int i; 
    printf("deltas: ");
    for (i = 0; i <= 27; i++)
    {
      printf("%d ", deltas[i]);
    }
    printf("\n");
  }
#endif
}


// calculate scaled diff from delta
long scale(long delta, long stepsize)
{
  // ((delta+0.5) * stepsize) / 32
  // i.e. ((2*delta + 1) * stepsize) / 64
  return ((delta*2 + 1) * stepsize) >> 6;
}


// set_state_minmax_addrs 
// Set up state from start values in state inherited from inbuf
void set_initialstate(struct adpcmstate *state)
{
  long dynamics;

  // dynamics = ones (# bits in bitdynamics + 1)
  // e.g. bitdynamics = 2 => dynamics = 111b 
  dynamics = (1 << (state->bitdynamics + 1)) - 1;

  // set highestval, lowestval, limit at +/- 65535
  state->highestval = state->framestartval + dynamics;
  if (state->highestval > 65535) 
    state->highestval = 65535;
  state->lowestval = state->framestartval - dynamics;
  if (state->lowestval < -65535) 
    state->lowestval = -65535;

  // set limits for table to use (tableno)
  // also set up pointer corresponding to maxdiff_index  
  state->tablestart = &stepsizetable[state->tableno][0];
  state->tableend = &stepsizetable[state->tableno][DELTA_MAX];
  state->maxdiffptr = &stepsizetable[state->tableno][state->maxdiff_index];
}


// calculate "direction" value from curval, clamp curval if necessary
int direction(long *curval, struct adpcmstate *state)
{
  int valcase;
  long newval;
  long maxval;
  long minval;
  long temp;

  // newval = framestartval + *curval, +1 if < 0, / 2
  newval = state->framestartval + *curval;
  newval += (newval < 0);
  newval >>= 1;

  temp = *state->stepsizeptr << 1;
  minval = state->highestval - temp;
  maxval = state->lowestval + temp;
  if (*curval >= minval)
  {
    if (*curval <= maxval)
    {
      *curval = newval;
      valcase = 3;
      if (state->stepsizeptr != state->tablestart)
        state->stepsizeptr--;
    }
    else
    {
      *curval = minval;
      valcase = 2;
    }
  }
  else
  {
    if (*curval <= maxval)
    {
      *curval = maxval;
      valcase = 1;
    }
    else
      valcase = 0;
  }
  return valcase;
}


// update pointer in step size table, depending on valcase
void newstep(int valcase, int sign, int delta, struct adpcmstate *state)
{
  if (valcase > 1)
  { 
    if (valcase == 2 && sign)  // valcase == 2 && sign
      state->stepsizeptr += indextable[delta];
    else                       // valcase == 3, or valcase == 2 && !sign
      state->stepsizeptr--; 
  }
  else 
  {
    if (valcase == 1 && sign)  // valcase == 1 && sign
      state->stepsizeptr--;
    else                       // valcase == 0, or valcase == 1 && !sign
      state->stepsizeptr += indextable[delta];
  }

#if 0 // more like the original code
  {
    if (valcase == 1)
    {
      if (!sign)               // valcase == 1 && !sign
        state->stepsizeptr += *indextableptr;
      else                     // valcase == 1 && sign
        state->stepsizeptr--;
    }
    else
      state->stepsizeptr += *indextableptr; // valcase == 0
  }
#endif

  if (state->stepsizeptr < state->maxdiffptr)
    state->stepsizeptr = state->maxdiffptr;
  if (state->stepsizeptr > state->tableend)
    state->stepsizeptr = state->tableend;
}


// calculate new value, clamp to +/- 32767
long update(long curval, long diff, int sign)
{
  if (sign)
    curval -= diff;
  else
    curval += diff;
  if (curval > 32767)
    curval = 32767;
  if (curval < -32767)
    curval = -32767;

  return curval;
}


// uncompress one frame
void uncompress(unsigned char inbuf[FRAMESIZE], short outbuf[FRAMESIZE])
{
  unsigned char deltas[FRAMESIZE]; // frame of (unpacked) deltas
  struct adpcmstate state;

  unpackbuf(inbuf, deltas, &state); // unpack frame to deltas and state
  set_initialstate(&state);
  uncompressbuf(deltas, outbuf, &state);
    

#if 0 // test, just generate full amplitude sawtooth at f_s/256 = 125
  static val = 0;
  int count = 0;

  while (count < FRAMESIZE)
  {
    outbuf[count++] = val += 256;
  }
#endif

}


// actually perform the decompression, given unpacked values and initial state
void uncompressbuf(unsigned char deltas[FRAMESIZE],
                   short outbuf[FRAMESIZE],
                   struct adpcmstate *state)
{
  long curval;   // current sample value
  long diff;     // scaled delta 
  long delta;    // 0..63, need long for easy multiplication to long
  int sign;      // 0 or 64
  int valcase;   // 0..3
  int sampleno;  // 1..32

  curval = outbuf[0] = state->framestartval;
  state->stepsizeptr = &state->tablestart[state->stepsize_index];
  
  for (sampleno = 1; sampleno < FRAMESIZE; sampleno++)
  {
    valcase = direction(&curval, state);
    sign = deltas[sampleno-1] & DELTA_SIGNBIT;
    delta = deltas[sampleno-1] & DELTA_MAX;
    diff = scale(delta, *state->stepsizeptr);
    curval = update(curval, diff, sign);
    newstep(valcase, sign, delta, state);
    outbuf[sampleno] = (short) curval;
  }
}
    

#if 0 // test to verify that the data is stored in the right order
int main()
{
  int *tblptr = &steptable[0][0];
  int i;
 
  for (i = 0; i < TABLES*TABLESIZE; i++)
  {
    printf(" %d", *tblptr++);
  }
}
#endif


// routines used solely when compressing data
# if 0 // not used, also, there seems to be a bug somewhere
       // (output not identical to ES-1 output)


// pack delta buffer and state to output frame
void packbuf(unsigned char outbuf[FRAMESIZE], 
             unsigned char deltas[FRAMESIZE],
             struct adpcmstate *state)
{
  // save adpcm state into beginning of output frame
  // (outbuf[4] also contains first bit from deltas[])
  outbuf[0] = state->framestartval >> 8;
  outbuf[1] = state->framestartval & 255;
  outbuf[2] = (state->stepsize_index << 2) | (state->maxdiff_index >> 4);
  outbuf[3] = (state->maxdiff_index << 4) | state->bitdynamics;
  outbuf[4] = (state->tableno << 6) | (deltas[0] >> 6);

  // pack deltas from 31x 7-bit bytes into 27x 8-bit bytes
  // (1. bit already packed into outbuf[4] above)
  outbuf[5] = (deltas[0] << 2) | (deltas[1] >> 5);
  outbuf[6] = (deltas[1] << 3) | (deltas[2] >> 4);
  outbuf[7] = (deltas[2] << 4) | (deltas[3] >> 3);
  outbuf[8] = (deltas[3] << 5) | (deltas[4] >> 2);
  outbuf[9] = (deltas[4] << 6) | (deltas[5] >> 1);
  outbuf[10] = (deltas[5] << 7) | deltas[6];
  outbuf[11] = (deltas[7] << 1) | (deltas[8] >> 6);
  outbuf[12] = (deltas[8] << 2) | (deltas[9] >> 5);
  outbuf[13] = (deltas[9] << 3) | (deltas[10] >> 4);
  outbuf[14] = (deltas[10] << 4) | (deltas[11] >> 3);
  outbuf[15] = (deltas[11] << 5) | (deltas[12] >> 2);
  outbuf[16] = (deltas[12] << 6) | (deltas[13] >> 1);
  outbuf[17] = (deltas[13] << 7) | deltas[14];
  outbuf[18] = (deltas[15] << 1) | (deltas[16] >> 6);
  outbuf[19] = (deltas[16] << 2) | (deltas[17] >> 5);
  outbuf[20] = (deltas[17] << 3) | (deltas[18] >> 4);
  outbuf[21] = (deltas[18] << 4) | (deltas[19] >> 3);
  outbuf[22] = (deltas[19] << 5) | (deltas[20] >> 2);
  outbuf[23] = (deltas[20] << 6) | (deltas[21] >> 1);
  outbuf[24] = (deltas[21] << 7) | deltas[22];
  outbuf[25] = (deltas[23] << 1) | (deltas[24] >> 6);
  outbuf[26] = (deltas[24] << 2) | (deltas[25] >> 5);
  outbuf[27] = (deltas[25] << 3) | (deltas[26] >> 4);
  outbuf[28] = (deltas[26] << 4) | (deltas[27] >> 3);
  outbuf[29] = (deltas[27] << 5) | (deltas[28] >> 2);
  outbuf[30] = (deltas[28] << 6) | (deltas[29] >> 1);
  outbuf[31] = (deltas[29] << 7) | deltas[30];

#if DEBUG
  printf("packbuf: startval %d, stepsize_idx %d, maxdiff_idx %d, bitdyn %d, tableno %d\n",
         state->framestartval, state->stepsize_index, state->maxdiff_index,
         state->bitdynamics, state->tableno);
  {
    int i; 
    printf("deltas: ");
    for (i = 0; i <= 27; i++)
    {
      printf("%d ", deltas[i]);
    }
    printf("\n");
  }
#endif
}

// calculate delta, given diff between this and previous sample (diff)
// also sets new prediceted diff (vpdiff) and sign.
long calcdelta(long *vpdiff, int *sign, long diff, struct adpcmstate *state)
{
  long delta;

  *sign = (diff < 0) ? 64 : 0;
  if (*sign)
    diff = -diff;

  delta = (diff*32) / *state->stepsizeptr;
  if (delta > 63)
    delta = 63;

  // *vpdiff = ((delta+0.5) * stepsize) / 32
  *vpdiff = ((2*delta + 1) * *state->stepsizeptr) >> 6;

  return delta;
}


// set bitdynamics (0..15) as # bits needed to represent max
// diff from first sample = dynamics in frame
void set_bitdynamics(struct adpcmstate *state)
{
  state->bitdynamics = 0;
  if (state->startdiff_max > 255)
  {
    state->bitdynamics += 8;
    state->startdiff_max >>= 8;
  }
  if (state->startdiff_max > 15)
  {
    state->bitdynamics += 4;
    state->startdiff_max >>= 4;
  }
  if (state->startdiff_max > 3)
  {
    state->bitdynamics += 2;
    state->startdiff_max >>= 2;
  }
  if (state->startdiff_max > 1)
    state->bitdynamics += 1;
}


// Calculate max and sum diff values
void calcdiffs(short inbuf[FRAMESIZE], struct adpcmstate *state)
{
  int sampleno;
  long diff;
  long startdiff;
  
  state->framestartval = inbuf[0];
  state->diff_average = 0;
  state->diff_max = 0;
  state->startdiff_max = 0;
  state->startdiff = abs(inbuf[1] - inbuf[0]);
  for (sampleno = 1; sampleno < 32; sampleno++)
  {
    diff = abs(inbuf[sampleno] - inbuf[sampleno-1]);
    startdiff = abs(inbuf[sampleno] - inbuf[0]);
    if (diff > state->diff_max)
      state->diff_max = diff;
    if (startdiff > state->startdiff_max)
      state->startdiff_max = startdiff;
    state->diff_average += diff;
  }
  state->diff_average >>= 5;  // /32 to get average
}

// set tableno depending on diff_max
void set_tableno(struct adpcmstate *state)
{
  long temp;

  // temp = (diff_max * 73 + diff_max * 9632 / 65536) / 128 
  // (i.e. temp = diff_max * 0.571428537)
  temp = (state->diff_max * 73 + ((state->diff_max * 9362L) >> 16)) >> 7;
  if (temp <= stepsizetable[1][DELTA_MAX])
  {
    if (temp <= stepsizetable[0][DELTA_MAX])
      state->tableno = 0;
    else
      state->tableno = 1;
  }
  else
  {
    if (temp <= stepsizetable[2][DELTA_MAX])
      state->tableno = 2;
    else
      state->tableno = 3;
  }
}
  
// set maxdiff_index depending on diff_max
void set_maxdiff_index(struct adpcmstate *state)
{
  int temp;

  // temp = (diff_max * 63 + diff_max * 32509 / 65536) / 128
  // (i.e. temp = diff_max * 0.495052874)
  temp = (state->diff_max * 63 + ((state->diff_max * 32509L) >> 16)) >> 7;
  state->maxdiff_index = stepsizetoindex(temp, state);
}

// convert step value to table index
long stepsizetoindex(long stepsize, struct adpcmstate *state)
{
  long index;
  long *tableptr;

  // set tableptr to 75% of table size
  tableptr = &stepsizetable[state->tableno][47];
  index = 0;
  // first set to one of four places in table (7,23,39 or 55)
  //                                          (7, +16, +16, +16)
  tableptr -= 16;
  if (stepsize >= tableptr[16])
  {
    index += 48;
    tableptr += 24;
  }
  else
  {
    tableptr -= 16;
    if (stepsize >= tableptr[16])
    {
      index += 32;
      tableptr += 24;
    }
    else
    {
      tableptr -= 8;
      if (stepsize >= tableptr[8])
      {
        index += 16;
        tableptr += 16;
      }
    }
  }
  // halve interval to 3,11 , 19,27 etc
  tableptr -= 4;
  if (stepsize >= tableptr[4])
  {
    index += 8;
    tableptr += 8;
  }
  // halve interval to 1,5, 9,13, 17,21, 25,29 etc
  tableptr -= 2;
  if (stepsize >= tableptr[2])
  {
    index += 4;
    tableptr += 4;
  }
  // halve interval to 0,2,4,6, 8,10,12,14 etc
  tableptr -= 1;
  if (stepsize >= tableptr[1])
  {
    index += 2;
    tableptr += 2;
  }
  // finally set to 0,1,2,3,4 etc
  if (stepsize >= tableptr[0])
    index += 1;
  return index;
}


// set stepsize_index from startdiff, affected by diff_average and 
// maxdiff_index
void set_stepsize_index(struct adpcmstate *state)
{
  int index;
  long *tableptr;

  tableptr = &stepsizetable[state->tableno][0];
  if (state->startdiff <= state->diff_average << 1)
    state->startdiff = state->diff_average;
  index = stepsizetoindex(state->startdiff, state);
  if (index < state->maxdiff_index)
    index = state->maxdiff_index;
  state->stepsize_index = index;
}


// compress complete frame
void compress(unsigned char outbuf[FRAMESIZE], short inbuf[FRAMESIZE])
{
  char deltas[FRAMESIZE];
  struct adpcmstate state;

  calcdiffs(inbuf, &state);
  set_tableno(&state);
  set_bitdynamics(&state);
  set_maxdiff_index(&state);
  set_stepsize_index(&state);
  set_initialstate(&state);
  compressbuf(deltas, inbuf, &state);
  packbuf(outbuf, deltas, &state);
}


// actually perform the compression, given input samples and initial state
void compressbuf(unsigned char deltas[FRAMESIZE],
                 short inbuf[FRAMESIZE],
                 struct adpcmstate *state)
{
  int valcase;
  long delta;
  int sign;
  long curval;
  int sampleno;
  long diff;

  curval = state->framestartval;
  state->stepsizeptr = &state->tablestart[state->stepsize_index];

  for (sampleno = 1; sampleno < 32; sampleno++)
  {
    valcase = direction(&curval, state);
    delta = calcdelta(&diff, &sign, inbuf[sampleno] - curval, state);
    curval = update(curval, diff, sign);
    newstep(valcase, sign, delta, state);
    deltas[sampleno-1] = delta | sign;
  }
}
#endif
