/* © 2018 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "test_helpers.h"
#include "zgw_nodemask.h"

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/

#define XSTR(s) #s
#define STR(t) XSTR(t)
#define CT(x) (check_true((x), "Line " STR(__LINE__)))

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/

/**
 * Test the version one set/get commands
 */
void test_nodelist_manipulation(void)
{
   char *tc_name = "nodemask_manipulation";
  start_case(tc_name, 0);

  nodemask_t nodelist1={0};

  CT(0 == nodemask_test_node(8, nodelist1));
  CT(1 == nodemask_add_node(8, nodelist1));
  CT(1 == nodemask_test_node(8, nodelist1));

  CT(0 == nodemask_test_node(1, nodelist1));
  CT(1 == nodemask_add_node(1, nodelist1));
  CT(1 == nodemask_test_node(8, nodelist1));
  CT(1 == nodemask_test_node(1, nodelist1));

  CT(0 == nodemask_test_node(207, nodelist1));
  CT(1 == nodemask_add_node(207, nodelist1));
  CT(1 == nodemask_test_node(207, nodelist1));
  CT(0 == nodemask_test_node(208, nodelist1));

  CT(1 == nodemask_remove_node(8, nodelist1));
  CT(0 == nodemask_test_node(8, nodelist1));
  CT(1 == nodemask_test_node(1, nodelist1));

  close_case(tc_name);


  tc_name = "nodemask error/edge cases";
  start_case(tc_name, 0);
  /* Try invalid node id */
  check_true(0 == nodemask_add_node(0, nodelist1),
             "0 is not a valid node to add");
  check_true(0 == nodemask_add_node(233, nodelist1),
             "233 is not a valid node to add");

  check_true(nodemask_remove_node(0, nodelist1) == 0,
             "0 is not a valid node to remove");
  check_true(nodemask_remove_node(233, nodelist1) == 0,
             "233 is not a valid node to remove");
  check_true(nodemask_remove_node(255, nodelist1) == 0,
             "255 is not a valid node to remove");

  check_true(0 == nodemask_test_node(0, nodelist1),
             "0 is not a valid node to test");
  check_true(0 == nodemask_test_node(255, nodelist1),
             "255 is not a valid node to test");
  check_true(nodemask_test_node(233, nodelist1) == 0, "233 is not a valid node");
  check_true(nodemask_test_node(254, nodelist1) == 0, "254 is not a valid node");
  check_true(nodemask_add_node(256, nodelist1) == 1, "256 can be added to network");
  check_true(nodemask_add_node(4000, nodelist1) == 1, "4000 can be added to network");
  check_true(nodemask_add_node(2000, nodelist1) == 1, "2000 can be added to network");
  check_true(nodemask_test_node(256, nodelist1) == 1, "256 is in mask");
  check_true(nodemask_test_node(4000, nodelist1) == 1, "4000 is in mask");
  check_true(nodemask_test_node(2000, nodelist1) == 1, "2000 is in mask");
  check_true(nodemask_nodeid_is_valid(234) == 0, "234 is not valid");
  check_true(nodemask_nodeid_is_valid(256) == 1, "256 is valid");

  check_true(nodemask_test_node(232, nodelist1) == 0, "232 is not in mask");
  check_true(nodemask_add_node(232, nodelist1) == 1, "232 can be added to mask");
  check_true(nodelist1[28] == 0x80,  "232 is in nodelist1");
  check_true(nodemask_test_node(232, nodelist1) == 1, "232 is in mask");
  close_case(tc_name);
}

 /**
  * Main function
  */
int main()
{
   test_nodelist_manipulation();

   close_run();
   return numErrs;
}
