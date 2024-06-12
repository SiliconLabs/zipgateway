/* © 2018 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "test_helpers.h"
#include "multicast_group_manager.h"
#include<string.h>
extern uint_least16_t mcast_byte_distance(uint8_t old, uint8_t new);
extern uint_least16_t mcast_nodemask_distance(const nodemask_t old_group, const nodemask_t new_group);

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
 * \defgroup mcast_group_test Multicast group manager unit test.

Test Plan

Cases are loosely organized by sub-feature.

\note Implemented cases have a '-' in front.

Create/access test cases
------------------------

- Create empty group - should fail

- Create the first group
Create the second group when the first has never been accessed
- Create the second group when the first has been modified or accessed
Create the third group when the previous have never been looked up
Create the third group when the first, then second have been looked up

Basic access test cases
- Access an existing group when there is only one
- Access an existing group when there are more than one
- Access an existing group that partially overlaps another existing group
- Access a previous group that has been expanded

Expansion test cases
--------------------

- Access a non-existing group when an existing group can be expanded
- Access a non-existing group when an existing group cannot be expanded
Access a non-existing group when an existing group cannot be expanded because of the last byte


Replacement test cases
--------------------

- Create a group when there are no unused groups (aka, replace stg)
- Access a group that has been replaced (trigger a second replacement)

Use_age list management
------------------------

while filling up list:
- create first element
- access single element (ie, move to top)
- create second element
access second element (ie, move to top, which should be a NOP)
access first element (ie, move to top, which should be a swap)
access second element (ie, move to top, which should be another swap)
create third element
access third element (should be a NOP)
access first element (should be a rotation 3,2,1 -> 1,3,2)
access third element (should be a swap of the first two elements 3,1,2)
create fourth element
access fourth element (should be a NOP)
access second element (should be a rotation 4,3,1,2 -> 2,4,3,1)
access fourth element (should be a swap of the first two elements 2,4,3,1 -> 4,2,3,1)
access third element (should be a rotation on the first three elements 2,4,3,1 -> 3,4,2,1)

when list is full:
- access first element
- access last element
- access middle element
- access the same element repeatedly (should be NOP after first go)


Lookup by id cases
--------------------

- lookup by id and check that the right mask is returned
- lookup by unused id 
- lookup by invalid id 
*/


/* libs2 mock */
static uint8_t S2_deleted_group_id = 0;

/**
 * Forget about the MPAN group with id group_id.
 *
 * @param Multicast group id
 * @return 1 if group was found, 0 otherwise.
 */
int S2_mpan_set_unused(uint8_t group_id);

int S2_mpan_set_unused(uint8_t group_id) {
   S2_deleted_group_id = group_id;
   return 1;
}

extern int mcast_group_use_age_check(uint8_t expc, uint8_t *expv);


void test_gm_errors(void) {
  mcast_groupid_t grpid;
  int res;
  nodemask_t nodemask1 = {0};
  nodemask_t nodemask2 = {0};

  start_case("Handle errors in group id", 0);
  res = mcast_group_get_nodemask_by_id(0, nodemask1);
  check_true(res == 0,
             "mcast_group_manager_get_nodelist_by_id should return 0 on id 0");
  res = mcast_group_get_nodemask_by_id(233, nodemask2);
  check_true(res == 0,
             "mcast_group_manager_get_nodelist_by_id should return 0 on id 233");
  close_case("Handle errors in group id");
}

nodemask_t nodelist2 = {0};
nodemask_t nodelist3 = {0}; // node 8

/**
 * Test the version one set/get commands
 */
void test_gm_lookup(void)
{
  char *tc_name;
  mcast_groupid_t grpid;
  nodemask_t nodelist1 = {0}; // empty, then node 8, then also 10

  tc_name = "GroupManager lookup of empty nodemask";
  start_case(tc_name, 0);
  test_print(3, "sizeof nodelist %u\n", sizeof(nodelist1));
  grpid = mcast_group_get_id_by_nodemask(nodelist1);
  check_true(grpid == 0,
             "mcast_group_manager_get_id_by_nodelist should return id 0 on empty list");
  close_case(tc_name);


  // create first element,
  // access single element (ie, move to top)
  tc_name = "GroupManager lookup in empty group list";
  start_case(tc_name, 0);
  check_true(1 == nodemask_add_node(8, nodelist1),
             "Node 8 added to mask 1");
  test_print(3, "Lookup nodemask 1 in empty group db\n");
  grpid = mcast_group_get_id_by_nodemask(nodelist1);
  check_true(grpid == 1, "id of first group added is 1");

  test_print(2, "Lookup the same nodemask in the group db should give the same group\n");
  grpid = mcast_group_get_id_by_nodemask(nodelist1);
  check_true(grpid == 1, "id of first group added is still 1");
  close_case(tc_name);
  // 1

  tc_name = "GroupManager lookup in group list with expandable group";
  start_case(tc_name, 0);
  test_print(2, "Looking up the nodemask with one node added should give the same group\n");
  test_print(2, "Add node 10 to mask 1\n");
  nodemask_add_node(10, nodelist1);
  grpid = mcast_group_get_id_by_nodemask(nodelist1);
  test_print(2, "Got group id %u\n", grpid);
  check_true(grpid == 1, "id of first group added is still 1");
  close_case(tc_name);
  // 1

  tc_name = "Lookup in group list with non-expandable group";
  start_case(tc_name, 0);
  nodemask_add_node(0x16, nodelist2);
  grpid = mcast_group_get_id_by_nodemask(nodelist2);
  check_true(grpid == 2,
             "Looking up non-expandable mask should give new id (2)");
  close_case(tc_name);
  // 2, 1

  tc_name = "Lookup in group list with previously used mask that has been expanded.";
  start_case(tc_name, 0);
  nodemask_add_node(8, nodelist3);
  grpid = mcast_group_get_id_by_nodemask(nodelist3);
  check_true(grpid == 3,
             "A new group is created when the previous instance has been expanded (3).");
  close_case(tc_name);
  // 3, 2, 1

  start_case("Access an existing group when there are more than one", NULL);
  grpid = mcast_group_get_id_by_nodemask(nodelist2);
  check_true(grpid == 2,
             "Found back group 2");
  close_case("Access an existing group when there are more than one");
  // 2,3,1

  start_case("Access an existing group that partially overlaps another existing group",
             NULL);
  grpid = mcast_group_get_id_by_nodemask(nodelist3);
  check_true(grpid == 3,
             "Found back group 3");
  close_case("Access an existing group that partially overlaps another existing group");
  // 3,2,1

  // The lookup cases should be run before filling up the group list.
  tc_name = "Get mask from group id";
  start_case(tc_name, NULL);
  nodemask_t mask2;
  int res = mcast_group_get_nodemask_by_id(2, mask2);
  check_true(res != 0, "Group 2 lookup succeeded");
  check_true(memcmp(mask2, nodelist2, sizeof(nodemask_t)) == 0,
             "Found back nodemask2");

  res = mcast_group_get_nodemask_by_id(7, mask2);
  check_true(res == 0, "Group 7 lookup failed");

  res = mcast_group_get_nodemask_by_id(5, mask2);
  check_true(res == 0, "Group 5 lookup failed");
  check_true(memcmp(mask2, nodelist2, sizeof(nodemask_t)) == 0,
             "nodemask2 not modified");
  close_case(tc_name);
  // lookup by id does not change the order
  // 3, 2, 1
}



void test_gm_replace(void) {
  char *tc_name;
  mcast_groupid_t grpid;
  nodemask_t nodelist4 = {0};
  nodemask_t nodelist5 = {0};
  nodemask_t nodelist6 = {0};
  nodemask_t nodelist7 = {0};
  uint8_t expEmpty[] = {0};
  uint8_t exp321[] = {3,2,1};
  uint8_t exp4321[] = {4,3,2,1};
  uint8_t exp13254[] = {1,3,2,5,4};
  uint8_t exp15432[] = {1,5,4,3,2};
  uint8_t exp12354[] = {1,2,3,5,4};
  uint8_t exp21543[] = {2,1,5,4,3};
  uint8_t exp32541[] = {3,2,5,4,1};
  uint8_t exp32154[] = {3,2,1,5,4};
  uint8_t exp23154[] = {2,3,1,5,4};
  uint8_t exp25413[] = {2,5,4,1,3};
  uint8_t exp54132[] = {5,4,1,3,2};
  uint8_t exp54321[] = {5,4,3,2,1};

  tc_name = "Use up all groups and trigger group replacement";
  start_case(tc_name, 0);
  check_true(mcast_group_use_age_check(3, exp321), "list is 321");

  nodemask_add_node(17, nodelist4);
  nodemask_add_node(18, nodelist5);
  nodemask_add_node(19, nodelist6);
  nodemask_add_node(30, nodelist7);

  grpid = mcast_group_get_id_by_nodemask(nodelist4);
  check_true(grpid == 4,
             "A new group (4) is created.");
  check_true(mcast_group_use_age_check(4, exp4321), "list is 4321");
  // 4, 3, 2, 1

  grpid = mcast_group_get_id_by_nodemask(nodelist5);
  check_true(grpid == 5,
             "A new group (5) is created.");
  check_true(mcast_group_use_age_check(5, exp54321), "list is 54321");
  // 5, 4, 3, 2, 1

  grpid = mcast_group_get_id_by_nodemask(nodelist6);
  check_true(grpid == 1,
             "Group 1 is replaced.");
  check_true(S2_deleted_group_id == 1, "Deleted group 1 in libs2");
  check_true(mcast_group_use_age_check(5,exp15432), "list is 15432");
  // 1, 5, 4, 3, 2

  grpid = mcast_group_get_id_by_nodemask(nodelist2);
  // Use the last group
  check_true(grpid == 2,
             "Group 2 is used.");
  check_true(mcast_group_use_age_check(5,exp21543), "list is 21543");
  // 2, 1, 5, 4, 3

  grpid = mcast_group_get_id_by_nodemask(nodelist7);
  check_true(grpid == 3,
             "Group 3, not group 2, is replaced (group .");
  check_true(S2_deleted_group_id == 3, "Deleted group 3 in libs2");
  check_true(mcast_group_use_age_check(5, exp32154), "list is 32154");
  close_case(tc_name);
  // 3, 2, 1, 5, 4 (n7, n2, n6, n5, n4)

  tc_name = "Test use_age list consistency when accessing the same element repeatedly";
  start_case(tc_name, 0);

  for (int ii = 0; ii < 10; ii++) {
     // Access the middle element, access the first element
     grpid = mcast_group_get_id_by_nodemask(nodelist6);
     check_true(grpid == 1,
                "Group 1 is used.");
     check_true(mcast_group_use_age_check(5, exp13254), "list is 13254");
  }
  // 1, 2, 3, 5, 4

  close_case(tc_name);

  tc_name = "Test use_age list consistency when accessing the elements in reverse order";
  start_case(tc_name, 0);
  check_true(mcast_group_use_age_check(5, exp13254), "list should be 13254");

  close_case(tc_name);

  tc_name = "Test use_age list consistency when accessing the last element repeatedly";
  start_case(tc_name, 0);
  uint8_t exp[] = {4,1,3,2,5};
  check_true(mcast_group_use_age_check(5, exp13254), "list should still be 13254");

  grpid = mcast_group_get_id_by_nodemask(nodelist4);
  check_true(grpid == 4,
             "Group 4 is found.");
  check_true(mcast_group_use_age_check(5, exp),
             "list should be 4,1,3,2,5");

  grpid = mcast_group_get_id_by_nodemask(nodelist5);
  check_true(grpid == 5,
             "Group 5 is found.");
  check_true(mcast_group_use_age_check(5, exp54132),
             "list should be 5,4,1,3,2");

  grpid = mcast_group_get_id_by_nodemask(nodelist2);
  check_true(grpid == 2,
             "Group 2 is found.");
  check_true(mcast_group_use_age_check(5, exp25413),
             "list should be 2,5,4,1,3");
  close_case(tc_name);


  tc_name = "Test use_age list consistency when replacing elements";
  start_case(tc_name, 0);
  check_true(mcast_group_use_age_check(5,exp25413), "list is 25413");
  grpid = mcast_group_get_id_by_nodemask(nodelist3);
  check_true(grpid == 3,
             "Group 3 replaced.");
  check_true(mcast_group_use_age_check(5,exp32541), "list is 32541");
  close_case(tc_name);

  {
     mcast_groupid_t grpidA1;
     nodemask_t nodelistA1 = {0};
     nodemask_t nodelistA2 = {0};
     uint8_t expA[] = {1};

     start_case("Accessing one-element use age list", 0);
     test_print(3, "Flush the list\n");
     mcast_group_init();
     check_true(mcast_group_use_age_check(0, nodelistA1),
                "The initial use age list is sound");

     nodemask_add_node(37, nodelistA1);
     grpid = mcast_group_get_id_by_nodemask(nodelistA1);
     check_true(grpid == 1,
                "A new group (1) is created.");
     check_true(mcast_group_use_age_check(1, expA),
             "list chould be 1");

     close_case("Accessing one-element use age list");

     // TODO: the rest of the use_age list construction cases
  }
}

/**
Test the distance functions.
*/
void test_distance() {
   start_case("Test mcast_byte_distance", NULL);

   check_true(mcast_byte_distance(0x00, 0x00) == 0, "0 - 0 is 0");
   check_true(mcast_byte_distance(0x10, 0x10) == 0, "16 - 16 is 0");
   check_true(mcast_byte_distance(0x20, 0x10) == 0xFFFF, "32 - 16 is FFFF");
   check_true(mcast_byte_distance(0x10, 0x20) == 0xFFFF, "16 - 32 is FFFF");
   check_true(mcast_byte_distance(0x10, 0x30) == 1, "16 - 48 is 1");
   check_true(mcast_byte_distance(0x10, 0x18) == 1, "16 - 24 is 1");
   check_true(mcast_byte_distance(0xF0, 0xFF) == 4, "0xF0 - 0xFF is 4");
   close_case("Test mcast_byte_distance");

   start_case("Test mcast_nodemask_distance", NULL);

   nodemask_t old_m;
   nodemask_t new_m;

   test_print(3, "nodemask is %u\n", sizeof(nodemask_t));
   memset(old_m, 0, sizeof(nodemask_t));
   memset(new_m, 0, sizeof(nodemask_t));

   new_m[1] = 0xff;
   new_m[2] = 0x55;
   test_print(3, "Dist is %u\n", mcast_nodemask_distance(old_m, new_m));
   check_true(mcast_nodemask_distance(old_m, new_m) == 12,
              "Diff is 12");

   old_m[1] = 0x55;
   test_print(3, "Dist is %u\n", mcast_nodemask_distance(old_m, new_m));
   check_true(mcast_nodemask_distance(old_m, new_m) == 8,
              "Diff is 8");
   close_case("Test mcast_nodemask_distance");
}

 /**
  * Group Manager test begins here
  */
int main()
{
   test_distance();

   mcast_group_init();

   test_gm_errors();
   test_gm_lookup();
   test_gm_replace();
   close_run();
   return numErrs;
}
