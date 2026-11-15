#include <stdio.h>
#include "../src/rng.h"
int main(void){
 int seeds[]={1,12345,65535};
 for(int s=0;s<3;s++){rnga_seed(seeds[s]);printf("A%d",seeds[s]);for(int i=0;i<20;i++)printf(" %d",rnga_rand());puts("");}
 int ns[]={10,4,20,5,3,24,9,8,16,2};rnga_seed(777);printf("S");for(int i=0;i<10;i++)printf(" %d",rnga_scaled(ns[i]));puts("");
 int pr[][2]={{1,100},{1,1000},{0,100},{22,25},{0,3},{1,100}};rnga_seed(31337);printf("M");for(int i=0;i<6;i++)printf(" %d",rnga_range(pr[i][0],pr[i][1]));puts("");
 int ps[]={1,42,65535};for(int s=0;s<3;s++){rngb_seed(ps[s]);printf("B%d",ps[s]);for(int i=0;i<20;i++)printf(" %d",rngb_next());puts("");}
 int rr[][2]={{0,4},{0,3},{3,120},{15,80},{10,1},{0,0},{-5,5},{11,99}};rngb_seed(5);printf("R");for(int i=0;i<8;i++)printf(" %d",rngb_range(rr[i][0],rr[i][1]));puts("");
}
