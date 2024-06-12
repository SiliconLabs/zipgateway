/* © 2018 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include <multicast_group_manager.h>
#include <ZW_transport_api.h>
#include <ZIP_Router_logging.h>
#include "zgw_nodemask.h"
/* TODO: use ASSERT without dragging in all of ZIP_Router. */
#include "assert.h"
#include<string.h>
#ifdef TEST_MCAST
extern int S2_mpan_set_unused(uint8_t group_id);
#else
/* TODO: remove this dummy */
#define S2_mpan_set_unused(first_unused)
#include "S2.h"
#endif
/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/

/** \defgroup mcast_gm_impl Multicast Group Manager Implementation
 * \ingroup mcast_gm
 * @{
 */

#ifndef MCAST_MAX_GROUPS
/** \def MCAST_MAX_GROUPS
 * Maximum number of simultanously supported multicast groups.
 *
 * If more groups are added, the group that has been unused the
 * longest will be removed and the new group added instead.
 */
#define MCAST_MAX_GROUPS MPAN_TABLE_SIZE
#endif

/**
 * Representation of infinite distance between m1 and m2.
 *
 * It is only legal to change a multicast group by adding nodes.  If
 * it is not possible to change m1 into m2 with only legal operations,
 * dist(m1, m2) is infinite.
 */
#define MCAST_DIST_INF 0XFFFF

/**
 * Multicast group structure.
 *
 * The mcast group structure contains the mask of nodes in the group.
 * The group id is implicit from the array index.
 *
 * The struct also contains the references in the linked list
 * of usage age.
 */
struct mcast_group {
   unsigned int in_use;
   mcast_groupid_t prev;  /**< Previous element in linked list of use age. */
   mcast_groupid_t next;  /**< Next element in linked list of use age. */
  //mcast_groupid_t gid;  // GroupID is implicit as (1 + index in mcast_groups_list)
   nodemask_t nlist; /**< Actual node mask of the mcast group. */
};

/***************************************************************************/
/*                             EXPORTED DATA                               */
/***************************************************************************/

/***************************************************************************/
/*                             PRIVATE DATA                                */
/***************************************************************************/

/** The collection of multicast groups.
 *
 * This array contains both the node masks and the use age list.
 * The group id is implicit from the array index (id = index+1).
 */
static struct mcast_group mcast_groups_list[MCAST_MAX_GROUPS];

/** Most recently used group.  Head element of the use_age list. */
static mcast_groupid_t mcast_group_use_age_head = 0;

/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/

/**
 * Get an mcast group entry from an mcast id.
 *
 * Internal use only, ie, the id is assumed to be valid.
 *
 * \param id A valid id.
 * \return A group entry.
 */
#define mcast_get_entry(id) mcast_groups_list[(id) - 1]

/**
 * This function updates the tracking of least recently used elements.
 *
 * It must be called every time a group_id is used, ie, the tracking
 * algorithm should be optimized for this case.
 *
 * @param group_id The group that has just been used and should be marked as most recently used.
 */
static void mcast_group_use_age_update(uint8_t group_id);

/**
 * Return the least recently used group id.
 */
static mcast_groupid_t mcast_group_use_age_get_oldest(void);

#ifdef TEST_MCAST
// For debugging
int mcast_group_use_age_check(uint8_t expc, uint8_t *expv) {
   int jj = 0;

   if (mcast_group_use_age_head == 0) {
      if (expc == 0) {
         return 1;
      } else {
         return 0;
      }
   }
   if (mcast_group_use_age_head != expv[0]) {
      ERR_PRINTF("Head: %u, expected: %u\n",
                 mcast_group_use_age_head, expv[0]);
      return 0;
   }
   ERR_PRINTF("Head: %u, expected: %u\n",
              mcast_group_use_age_head, expv[0]);
   int ii = mcast_get_entry(mcast_group_use_age_head).next;
   for (jj = 1; jj < expc; jj++) {
      printf("Next: %u, expected: %u.\n", ii, expv[jj]);
      if (ii != expv[jj]) {
         return 0;
      }
      ii = mcast_get_entry(ii).next;
   }
   if (ii != mcast_group_use_age_head) {
      ERR_PRINTF("Next list is not circular: Head: %u, last next: %u\n",
                 mcast_group_use_age_head, ii);
      return 0;
   }
   ii = mcast_get_entry(mcast_group_use_age_head).prev;
   for (jj = expc-1; jj != 0; jj--) {
      printf("Previous: %u, expected: %u.\n", ii, expv[jj]);
      if (ii != expv[jj]) {
         return 0;
      }
      ii = mcast_get_entry(ii).prev;
   }
   if (ii != mcast_group_use_age_head) {
      ERR_PRINTF("Prev list is not circular: Head: %u, last prev: %u\n",
                 mcast_group_use_age_head, ii);
      return 0;
   }
   DBG_PRINTF("Prev list loop: Head: %u, last prev: %u\n",
              mcast_group_use_age_head, ii);

   return 1;
}
#endif

/**
 * Return the least recently used group id.
 *
 * Since the use_age list is circular, the least recently used group
 * is the prev entry of the most recently used group id.
 */
static mcast_groupid_t mcast_group_use_age_get_oldest(void)
{
   return mcast_get_entry(mcast_group_use_age_head).prev;
}

static void mcast_group_use_age_element_remove(mcast_groupid_t group_id) {
   mcast_groupid_t next = mcast_get_entry(group_id).next;
   mcast_groupid_t prev = mcast_get_entry(group_id).prev;

   mcast_get_entry(prev).next = next;
   mcast_get_entry(next).prev = prev;
}

static void mcast_group_use_age_element_insert_head(mcast_groupid_t group_id) {
   mcast_groupid_t tail = mcast_get_entry(mcast_group_use_age_head).prev;

   mcast_get_entry(group_id).next = mcast_group_use_age_head;
   mcast_get_entry(group_id).prev = tail;
   mcast_get_entry(mcast_group_use_age_head).prev = group_id;
   mcast_get_entry(tail).next = group_id;

   mcast_group_use_age_head = group_id;
}

static void mcast_group_use_age_element_insert_new(mcast_groupid_t group_id) {
   if (mcast_group_use_age_head == 0) {
      mcast_get_entry(group_id).prev = group_id;
      mcast_get_entry(group_id).next = group_id;
      mcast_group_use_age_head = group_id;
   } else {
      mcast_group_use_age_element_insert_head(group_id);
   }
}

static void mcast_group_use_age_update(uint8_t group_id)
{
   assert(mcast_group_use_age_head != 0);

   if (group_id == mcast_group_use_age_head) {
      return;
   }

   /* Take group_id out of the use_age list */
   mcast_group_use_age_element_remove(group_id);

   /* Insert group_id at the head of the use_age list */
   mcast_group_use_age_element_insert_head(group_id);
}

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/
int mcast_group_get_nodemask_by_id(mcast_groupid_t group_id, nodemask_t nodemask)
{
  if ((group_id == 0) || group_id > MCAST_MAX_GROUPS) {
    return 0;
  }
  if (mcast_get_entry(group_id).in_use == 1) {
     nodemask_copy(nodemask, mcast_get_entry(group_id).nlist);
    return 1; /* found it */
  }
  return 0;
}

/* This is only exported for test purposes */
/** Compute the distance to change old into new by adding
 * bits. MCAST_DIST_INF means that it cannot be done.
 *
 * Ie, 0x00 - 0x00 is 0,
 *     0x10 - 0x10 is 0,
 *     0x00 - 0x01 is 1,
 *     0x01 - 0x00 is MCAST_DIST_INF,
 *     0x01 - 0x10 is MCAST_DIST_INF,
 *
 * \param old byte (what we have)
 * \param new byte (what we want)
 */
uint_least16_t mcast_byte_distance(uint8_t old, uint8_t new) {
   uint16_t dist = 0;

   if (old == new) {
      return 0;
   }
   if ((old & (~new)) != 0) {
      /* There are bits in old that are not in new. */
      return MCAST_DIST_INF;
   } else {
      /* Remove the matching bits and count what is missing using
       * Kernighan's/Wegner's algorithm.   */
      uint8_t tmp = (old ^ new);

      while (tmp > 0) {
         tmp &= tmp-1;
         dist++;
      }
      return dist;
   }
}

/* This is only exported for test purposes */
/**
 * Count how many bits have to be added to old_group to change it into new_group.
 *
 * \param old_group A group already in the database.
 * \param new_group The group we want to use.
 * \return Number of bits to change if it is possible (may be 0), MCAST_DIST_INF otherwise.
 */
uint_least16_t mcast_nodemask_distance(const nodemask_t old_group, const nodemask_t new_group) {
   uint_least16_t dist = 0;
   uint_least16_t byte_dist = 0;
   uint16_t ii;

   for (ii=0; ii<sizeof(nodemask_t); ii++) {
      byte_dist = mcast_byte_distance(old_group[ii], new_group[ii]);
      if (byte_dist == MCAST_DIST_INF) {
         return MCAST_DIST_INF;
      }
      dist = dist + byte_dist;
   }
   return dist;
}

/**
 * @}
 */

mcast_groupid_t mcast_group_get_id_by_nodemask(const nodemask_t nodemask)
{
   nodemask_t empty = {0};
   mcast_groupid_t candidate = 0;
   mcast_groupid_t first_unused = 0;
   int_least16_t lowest_dist = 0xFF;

   if (nodemask_equal(nodemask, empty) == 0) {
      return 0;
   }

   for (int ii = 0; ii < MCAST_MAX_GROUPS; ii++) {
      if (mcast_groups_list[ii].in_use) {
         uint_least16_t tmp = mcast_nodemask_distance(mcast_groups_list[ii].nlist,
                                                      nodemask);
         if (tmp < lowest_dist) {
            lowest_dist = tmp;
            candidate = ii+1;
         }
      } else {
         if (first_unused == 0) {
            first_unused = ii+1;
         }
      }
   }
   if (candidate) {
      if (lowest_dist != 0) {
         DBG_PRINTF("Expanding multicast group with id %u\n", candidate);
         nodemask_copy(mcast_get_entry(candidate).nlist, nodemask);
      }
      mcast_group_use_age_update(candidate);
      return candidate;
   }
   if (first_unused == 0) {
      /** Recycle an old group id. */
      first_unused = mcast_group_use_age_get_oldest();
      DBG_PRINTF("Deleting old multicast group with id %u\n", first_unused);
      S2_mpan_set_unused(first_unused);
      mcast_group_use_age_update(first_unused);
   } else {
      mcast_get_entry(first_unused).in_use = 1;
      mcast_group_use_age_element_insert_new(first_unused);
   }
   DBG_PRINTF("Adding multicast group with id %u\n", first_unused);
   nodemask_copy(mcast_get_entry(first_unused).nlist, nodemask);
   return first_unused;
}

void mcast_group_init(void) {
   uint8_t ii;
   for (ii = 0; ii<MCAST_MAX_GROUPS; ii++) {
      mcast_groups_list[ii].in_use = 0;
      mcast_groups_list[ii].next = 0;
      mcast_groups_list[ii].prev = 0;
      memset(mcast_groups_list[ii].nlist, 0, sizeof(mcast_groups_list[ii].nlist));
   }
   mcast_group_use_age_head = 0;
}
