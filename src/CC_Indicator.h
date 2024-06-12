/* © 2019 Silicon Laboratories Inc. */

#ifndef CC_INDICATOR_H_
#define CC_INDICATOR_H_

/*
 * Local defines to get rid of context sensitive define names from ZW_classcmd.h
 */

/* Indicator IDs */
#define INDICATOR_IND_NA            INDICATOR_SET_NA_V3
#define INDICATOR_IND_NODE_IDENTIFY INDICATOR_SET_NODE_IDENTIFY_V3

/* Property IDs */
#define INDICATOR_PROP_ON_OFF_PERIOD INDICATOR_SET_ON_OFF_PERIOD_V3
#define INDICATOR_PROP_ON_OFF_CYCLES INDICATOR_SET_ON_OFF_CYCLES_V3
#define INDICATOR_PROP_ON_TIME       INDICATOR_SET_ON_TIME_V3

/* Masks etc. */
#define INDICATOR_OBJECT_COUNT_MASK INDICATOR_SET_PROPERTIES1_INDICATOR_OBJECT_COUNT_MASK_V3
#define INDICATOR_RESERVED_MASK     INDICATOR_SET_PROPERTIES1_RESERVED_MASK_V3
#define INDICATOR_RESERVED_SHIFT    INDICATOR_SET_PROPERTIES1_RESERVED_SHIFT_V3


/**
 * Initialization of the Indicator Command Class handler.
 */
void IndicatorHandler_Init(void);

/** Reset the Indicator Command Class.
 *
 * Stop the Indicator timers and activity.  Reset Indicator state.
 */
void IndicatorDefaultSet(void);

/**
 * Entry point for the Indicator Command Class handler.
 *
 * Dispatches incoming commands to the appropriate handler function.
 *
 * \param conn   The Z-wave connection.
 * \param frame  The incoming command frame.
 * \param length Length in bytes of the incoming frame.
 * \return Status of handling the command.
 */
command_handler_codes_t IndicatorHandler(zwave_connection_t *conn, uint8_t *frame, uint16_t length);


#endif /* CC_INDICATOR_H_ */
