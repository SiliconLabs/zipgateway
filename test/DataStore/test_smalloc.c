/* © 2014 Silicon Laboratories Inc.
 */
#include <unity.h>
#include <stdio.h>
#include "smalloc.h"
#include <stdlib.h>
#include <string.h>

/**
 * This tests two things:

1. That we can do 10.000 random  operations (alloc+write data or free) without
   - getting an invalid result from alloc (ie, ptr must be in valid range or NULL),
   - without corrupting the written data,
   - without leaking all the memory.

Note: this is an old test, it may fail by looping, which we should fix.

2. That freeing an out of bounds pointer does not break anything.

We also test that freeing invalid in-bounds pointers does not write out of bounds.

 *  \note Note that this is a grey-box test, not a proper unit test.
 *  Several validations depend a little too much on internal parts of
 *  smalloc.
 */

#define TEST_SMALLOC_SIZE 0x1000

struct dummy_file_layout {
   uint32_t magic;
   /** Home ID of the stored gateway. */
   uint32_t homeID;
   /** Node ID of the stored gateway. */
   uint8_t  nodeID;
   uint8_t  smalloc_space[TEST_SMALLOC_SIZE];
   uint8_t padding[0xEFFF];
};

static struct dummy_file_layout dummy_file;
static uint8_t *rd_membuf = dummy_file.smalloc_space;

/** Mock xassert */
int asserts_so_far = 0;
int assert_latest_line = 0;

void _xassert(const char *file, int line) {
   printf("Assertion failed: file %s, line %d.\n", file, line);
   asserts_so_far++;
   assert_latest_line = line;
}

/** Like in RD_DataStore, we put some space in front of the available
 * malloc block.  This way, we can check that freeing a below-bounds
 * pointer does not corrupt the out-of-bounds space without crashing
 * the test case.
 */
static uint16_t rd_mem_read(uint16_t offset, uint8_t len, void *data)
{
  memcpy(data, rd_membuf + offset, len);
  return len;
}

static uint16_t rd_mem_write(uint16_t offset, uint8_t len, void *data)
{
  memcpy(rd_membuf + offset, data, len);
  return len;
}

const small_memory_device_t m = {
    .offset = 1,
    .size = 0xFF,
    .psize = 8,
    .read = rd_mem_read,
    .write = rd_mem_write,
};

struct elem {
   uint16_t ptr;
   uint8_t len;
};

void test_filling_mem() {
   int num_elems = m.size/2;
   struct elem e[m.size/2];
   uint8_t dummy_val = 0xF8;

   memset(rd_membuf, 0, TEST_SMALLOC_SIZE);
   memset(e, 0, sizeof(e));

   for (int ii = 0; ii < num_elems; ii++) {
      e[ii].ptr = smalloc_write(&m, 1, &dummy_val);
      printf("Alloc ptr=%04x \n", e[ii].ptr);
      TEST_ASSERT_EQUAL_MESSAGE(e[ii].ptr > 0, 1, "Memory too small");
   }
   uint16_t out_of_space = smalloc(&m, 1);
   TEST_ASSERT_EQUAL_MESSAGE(out_of_space, 0, "Memory too large");
}

#define NUM_ELEM 16

void test_random_allocation_free() {
  int j, i;
  int k = 0;
  struct elem e[NUM_ELEM];

  memset(rd_membuf, 0, TEST_SMALLOC_SIZE);
  memset(e, 0, sizeof(e));

  while (1) {
    //pick a random element
    j = rand() % NUM_ELEM;
    if (e[j].ptr == 0)
    {
      //if not allocated allocate an element of random length > 0
      e[j].len = rand() % 0x7c + 1;
      e[j].ptr = smalloc(&m, e[j].len);
      k++;

      if (e[j].ptr == 0)
      {
         //printf("Out of mem\n");
         ;
      } else {
         printf("Alloc %02x ptr=%04x \n",e[j].len,e[j].ptr);
        memset(&(rd_membuf[e[j].ptr]), e[j].len, e[j].len);
        /* Test with boolean, since I could not find LESS */
        TEST_ASSERT_EQUAL_MESSAGE(e[j].ptr < m.offset+m.size, 
                                  1, "Strange (too large) pointer received");
        TEST_ASSERT_EQUAL_MESSAGE(e[j].ptr > m.offset, 
                                  1, "Strange (too small) pointer received");
      }
    } else {
       printf("Free ptr=ptr=%04x %02x\n", e[j].ptr, e[j].len);
       for (i = 0; i < e[j].len; i++) {
          TEST_ASSERT_EQUAL_MESSAGE(e[j].len, rd_membuf[e[j].ptr + i], "Memory corruption");
       }
       smfree(&m, e[j].ptr);
       e[j].ptr = 0;
    }

    if (k > 10000) {
      break;
    }
  }
  TEST_ASSERT_EQUAL_MESSAGE(0, asserts_so_far,
                            "Triggered assert while allocating and freeing legally.");
}

int low_bounds_assert_line = 98;
int high_bounds_assert_line = 98;
int chunk_used_assert_line = 94;

void test_free_out_of_boundsNULL()
{
   int so_far = asserts_so_far;
   printf("*** Free NULL ptr *** \n");
   smfree(&m, 0);
   TEST_ASSERT_EQUAL_MESSAGE(++so_far, asserts_so_far, "Missing assert when freeing ptr 0");
   TEST_ASSERT_EQUAL_MESSAGE(low_bounds_assert_line, assert_latest_line,
                            "Wrong assert, expected different line");
}

void test_free_out_of_bounds1()
{
   int so_far = asserts_so_far;
   printf("*** Free ptr 1 *** \n");
   dummy_file.magic = 0xFFFFFFFF;
   dummy_file.homeID = 0xFFFFFFFF;
   dummy_file.nodeID = 0xFF;
   memset(rd_membuf, 0xFF, TEST_SMALLOC_SIZE);

   printf("rd_membuf[0]: %x\n", rd_membuf[0]);
   smfree(&m, 1);
   printf("rd_membuf[0]: %x\n", rd_membuf[0]);
   printf("rd_membuf[1]: %x\n", rd_membuf[1]);
   TEST_ASSERT_EQUAL_MESSAGE(++so_far, asserts_so_far, "Missing assert when freeing ptr 1");
}

void test_free_out_of_boundsfd()
{
   int so_far = asserts_so_far;
   uint16_t the_ptr = 0xfd;

   printf("*** Free ptr 0x%04x - this will corrupt memory! *** \n", the_ptr);
   printf("alloc space size: 0x%x.\n", m.size);
   rd_membuf[the_ptr-1] = 0xF2;
   smfree(&m, the_ptr);
   TEST_ASSERT_EQUAL_MESSAGE(rd_membuf[the_ptr-1], 0x72, 
                             "Freeing illegal pointer within bounds has been fixed??");
   TEST_ASSERT_EQUAL_MESSAGE(so_far, asserts_so_far, "Unexpected assert when freeing the_ptr");

   printf("*** Free ptr 0x%04x and test chunk_used assert - this will corrupt memory! *** \n", the_ptr);
   smfree(&m, the_ptr);
   TEST_ASSERT_EQUAL_MESSAGE(rd_membuf[the_ptr-1], 0x72,
                             "Freeing illegal pointer within bounds has been fixed??");
   TEST_ASSERT_EQUAL_MESSAGE(++so_far, asserts_so_far, "Unexpected assert when freeing the_ptr");
   TEST_ASSERT_EQUAL_MESSAGE(chunk_used_assert_line, assert_latest_line,
                             "Wrong assert, expected different line");
}

void test_free_out_of_boundsff()
{
   int so_far = asserts_so_far;
   printf("*** Free ptr 0xff *** \n");
   smformat(&m);
   smfree(&m, 0xff);
   TEST_ASSERT_EQUAL_MESSAGE(so_far, asserts_so_far, "Got assert when freeing ptr ff");
   TEST_ASSERT_EQUAL_MESSAGE(chunk_used_assert_line, assert_latest_line,
                             "Wrong assert, expected different line");
}

void test_free_out_of_bounds100()
{
   int so_far = asserts_so_far;
   printf("*** Free ptr 0x100 *** \n");
   smfree(&m, 0x100);
   TEST_ASSERT_EQUAL_MESSAGE(++so_far, asserts_so_far, "Missing assert when freeing ptr 100");
   TEST_ASSERT_EQUAL_MESSAGE(high_bounds_assert_line, assert_latest_line,
                             "Wrong assert, expected different line");
}

void test_free_out_of_boundsfff()
{
   int so_far = asserts_so_far;
   printf("*** Free ptr fff *** \n");
   smfree(&m, 0xfff);
   TEST_ASSERT_EQUAL_MESSAGE(++so_far, asserts_so_far, "Missing assert when freeing ptr fff");
   TEST_ASSERT_EQUAL_MESSAGE(high_bounds_assert_line, assert_latest_line,
                             "Wrong assert, expected different line");
}

void test_free_out_of_boundsffff()
{
   int so_far = asserts_so_far;
   printf("*** Free ptr fff *** \n");
   smfree(&m, 0xffff);
   TEST_ASSERT_EQUAL_MESSAGE(++so_far, asserts_so_far, "Missing assert when freeing ptr fff");
   TEST_ASSERT_EQUAL_MESSAGE(high_bounds_assert_line, assert_latest_line,
                             "Wrong assert, expected different line");
}
