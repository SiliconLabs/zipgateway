/* © 2014 Silicon Laboratories Inc.
 */
#ifndef INIT_TASK_H_
#define INIT_TASK_H_

/* Setup the init_task module and final task */
void init_task_setup(void (*fp)());

/* Register an initialization task to wait for */
void init_task_register();

/* Mark an initialization task complete */
void init_task_complete();

#endif /* INIT_TASK_H_ */
