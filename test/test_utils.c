/* © 2018 Silicon Laboratories Inc.
 */

#include "test_utils.h"

/****************************************************************************/
/*                             Generic helpers                              */
/****************************************************************************/

int min(int a, int b)
{
    if (a < b)
    {
      return a;
    }
    return b;
}

int max(int a, int b)
{
    if (a > b)
    {
      return a;
    }
    return b;
}

