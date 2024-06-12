/* © 2017 Silicon Laboratories Inc.
 */
/*
 * command_analyzer.c
 *
 *  Created on: Nov 24, 2016
 *      Author: aes
 */
#include "CommandAnalyzer.h"

#include <stdio.h>
extern const int get_list_len;
extern const int get_list[];
extern const int set_list_len;
extern const int set_list[];
extern const int rep_list_len;
extern const int rep_list[];
extern const int supported_list_len;
extern const int supported_list[];

/**
 * BSearch in a sorted list
 *
 * Complexity: O(log(N))
 *
 * \param key         What to find
 * \param sorted_list numerical sorted list
 * \param list_len    Length of the list
 * \return true if element is found
 */
static int bsearch(uint16_t key, const int sorted_list[], int list_len) {
  int first,last,middle;

  first= 0;
  last = list_len-1;
  middle = (first+last) / 2;

  while(first <= last) {
    if(sorted_list[middle] < key) {
      first = middle +1;
    } else if(sorted_list[middle]== key) {
      return 1;
    } else {
      last = middle  -1;
    }
    middle = (first+last) / 2;
  }

  return 0;
}

int CommandAnalyzerIsGet(uint8_t cls, uint8_t cmd) {
  uint16_t key =  cls << 8  | cmd;
  return bsearch(key,get_list,get_list_len);
}

int CommandAnalyzerIsSet(uint8_t cls, uint8_t cmd) {
  uint16_t key =  cls << 8  | cmd;
  return bsearch(key,set_list,set_list_len);
}


int CommandAnalyzerIsReport(uint8_t cls, uint8_t cmd) {
  uint16_t key =  cls << 8  | cmd;
  return bsearch(key,rep_list,rep_list_len);
}

int CommandAnalyzerIsSupporting(uint8_t cls, uint8_t cmd) {
  uint16_t key =  cls << 8  | cmd;
  return bsearch(key,supported_list,supported_list_len);
}
