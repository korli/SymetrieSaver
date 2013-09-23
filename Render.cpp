#include <stdio.h>
#include <math.h>
#include <string.h>
#include <String.h>
#include <Window.h>

#include "fbview.h"
#include "Symetrie.h"

/* Integer types with exact size.
 */
typedef signed char        S8;
typedef signed short       S16;
typedef signed int         S32;
typedef signed long long   S64;
typedef unsigned char      U8;
typedef unsigned short     U16;
typedef unsigned int       U32;
typedef unsigned long long U64;

/* Integer type that can hold a pointer.
 */
typedef unsigned int       PTR;

unsigned *_;

#define BLEND  6
#define SCALE (WIDTH < HEIGHT*4/3 ? WIDTH/5 : HEIGHT*4/3/5)

float 	SPEED = 0.20;
bool 	BIG_POINTS = false;
bool	CORNERS = true;

#define FLIP(x) (WIDTH*HEIGHT-1-(x))
#define buf    ((U16*)_)

// Macros for loop unrolling
#define MACRO(m)           do{ m }while(0)
#define PASTE(a,b)         a##b
#define UNROLL(N,f)        PASTE(UNROLL,N)(f)
#define UNROLL2(f)         MACRO( f((2*i)); f((2*i+1)); )
#define UNROLL4(f)         MACRO( f((4*i)); f((4*i+1)); f((4*i+2)); f((4*i+3)); )
#define UNROLL8(f)         MACRO( f((8*i)); f((8*i+1)); f((8*i+2)); f((8*i+3)); f((8*i+4)); f((8*i+5)); f((8*i+6)); f((8*i+7)); )

#define MASK_RED_BLUE(i)   MACRO( _[i] &= 0xFF00FF; )
#define FLIP_COPY(i)       MACRO( _[i] = _[FLIP(i)]; )
#define ADD_PLUS_WIDTH(i)  MACRO( _[i] += _[i+WIDTH]; )
#define ADD_MINUS_WIDTH(i) MACRO( _[i] += _[i-WIDTH]; )
#define HORIZONTAL_BLUR(i) MACRO( actual = _[i] + _[i+1]; _[i] = 15 * (last + actual); last = actual; )
#define PAL_CONVERT(i)     MACRO( _[FLIP(i)] = _[i] = (_[i]>>8 & 0xFF00FF) + green_tab[((U8*)_)[4*i+3]]; )

// FPU constants.
const double
  du = 1.4426950408889634,
  dw = 3.1415926535897932,
  sin240 = -0.86602540378443865,
  cos240 = -0.5,
  radiusS = 0.3010299956639812*SCALE,
  radiusSS = 0.3010299956639812*SCALE*SCALE;  // saves time in int conversion

//#define sin240 sin((123*M_PI)/180.0)
//#define cos240 cos((17*M_PI)/180.0)

double _starttime, _lasttime;

// displacements for big points
const int adr_tab[] = {0, 1*2, WIDTH*2, (WIDTH+1)*2};
int green_tab[256];

double getTime(void)
{ 
	return system_time()/1000000.0;
}

int init(FBView *view)
{ 
  int i;
  for (i=0; i<256; i++) green_tab[i] = i*i & 0xFF00;
  
  _ = (unsigned int*)view->GetBuffer();
  _starttime = _lasttime = getTime();  
  return 0;
}

void cleanup(void) {}

// Blur top half of screen with 16 bit precision.
#define FACTOR 8
void blur(void)
{ int i;
 // mask out green and mirror one row for correct blur
  for (i=0; i<WIDTH*HEIGHT/2/FACTOR; i++)        UNROLL(FACTOR,MASK_RED_BLUE);
  for (; i<(WIDTH*HEIGHT/2 + WIDTH)/FACTOR; i++) UNROLL(FACTOR,FLIP_COPY);

 // convolute with
 // 15 30 15
 // 30 60 30
 // 15 30 15
  for (i=0; i<WIDTH*HEIGHT/2/FACTOR; i++)        UNROLL(FACTOR,ADD_PLUS_WIDTH);
  for (; i>=WIDTH/FACTOR; i--)                   UNROLL(FACTOR,ADD_MINUS_WIDTH);
  unsigned last = _[0], actual;
  for (i=0; i<WIDTH*HEIGHT/2/FACTOR; i++)        UNROLL(FACTOR,HORIZONTAL_BLUR);
}
#undef FACTOR

// Convert 16bit colors to 8bit and compute green channel.
#define FACTOR 4
void convert(void)
{
  int i;
  for (i=0; i<WIDTH*HEIGHT/2/FACTOR; i++)        UNROLL(FACTOR,PAL_CONVERT);
}
#undef FACTOR

void frame(double t)
{
  t = t*SPEED + _starttime;
  double u=sin(t*du)*SCALE, v=2*cos(t)*SCALE, w=sin(t*dw)*SCALE, x=w, y=v, z;
  unsigned r = t;
 
  int i = BIG_POINTS?(-2*SCALE*SCALE):(-16*SCALE*SCALE);

  blur();

 // rotation (66%), fixed inversion (16%) and variable inversion (33%)
  do
  { if (r >= 0x55555555)
    { x = cos240*x + sin240*y, y += y, y = 2*sin240*x - y;
      if (r < 0x80000000) { z = radiusSS / (x*x + y*y); x *= z; y *= z; }
    }
    else { y += u; z = radiusS * v / (x*x + y*y); x *= z; y = y*z + w; }

   // need to round down: translate before converting
    unsigned adr = (int)(y + HEIGHT/2)*WIDTH + (int)(x + WIDTH/2);
    if (adr >= WIDTH*HEIGHT/2) adr = FLIP(adr);
    if (adr < WIDTH*HEIGHT/2)
    { adr = adr<<1 | ~r>>31;
     if(BIG_POINTS) {  // big points are made of two pixels
      buf[adr + adr_tab[i   &3]] += (U16)~buf[adr + adr_tab[i   &3]] >> BLEND;
      buf[adr + adr_tab[i>>2&3]] += (U16)~buf[adr + adr_tab[i>>2&3]] >> BLEND;
      if(CORNERS) {  // corners are mirrored when drawing
      	adr = WIDTH*HEIGHT-1+WIDTH-adr;
      	buf[adr + adr_tab[i   &3]] += (U16)~buf[adr + adr_tab[i   &3]] >> BLEND;
      	buf[adr + adr_tab[i>>2&3]] += (U16)~buf[adr + adr_tab[i>>2&3]] >> BLEND;
      }
     } else {
      buf[adr] += (U16)~buf[adr] >> BLEND;  // Brighten pixel, simulate gamma
      if(CORNERS) {
      	adr = WIDTH*HEIGHT-1+WIDTH-adr;
      	buf[adr] += (U16)~buf[adr] >> BLEND;
      }
     }
    }
  } while (r = r*3+1, ++i);

  convert();
}

int32 renderer(FBView *view)
{
  	double time = getTime();    	
   	frame(time - _starttime);
    _lasttime = time;
    view->Paint();
}
