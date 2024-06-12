/* © 2014 Silicon Laboratories Inc.
 */

static int active_task_count = 0;
static void (*final_init_step)() = 0;

static unsigned char triggered;

/* Reset the init_task module and setup final task */
void init_task_setup(void (*fp)())
{
  active_task_count = 0;
  final_init_step = fp;
  triggered = 0;
}

/* Register an initialization task to wait for */
void init_task_register()
{
  active_task_count++;
}

/* Mark an initialization task complete */
void init_task_complete()
{
  if (triggered)
  {
    /* already initialized */
    return;
  }
  active_task_count--;
  if (active_task_count == 0)
  {
    if (final_init_step != 0)
    {
      triggered = 1;
      (*final_init_step)();
    }
  }
}
