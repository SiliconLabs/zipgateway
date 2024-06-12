/* © 2019 Silicon Laboratories Inc. */

#include "command_handler.h"
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"
#include "pkgconfig.h"
#include "ZIP_Router_logging.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "zip_router_config.h"
#include "CC_Indicator.h"
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h> // basename()
#include <signal.h>
#include <errno.h>
#include <stdio.h>

/**
 * Structure used to track received indicator property values
 */
typedef struct
{
  BOOL modified;
  BYTE value;
} INDICATOR_VALUE_INFO;


/*
 * (global) Properties for the Node Identify indicator
 */
static BYTE g_on_off_period_length = 0;  /**< Period length in 1/10 second */
static BYTE g_on_off_num_cycles    = 0;  /**< Number of cycles */
static BYTE g_on_time              = 0;  /**< On time in 1/10 second */
static char g_node_identify_script[512]; /**< Sanitized copy of cfg.g_node_identify_script */
static struct ctimer g_indicator_timer;  /**< Timer used to reset indicator after n periods */


/**
 * Call external script/application to control the indicator LED.
 *
 * \param on_time_ms  ON duration (in milliseconds) for a single blink cycle.
 *                    If on_time_ms is zero the indicator should be turned off.
 *
 * \param off_time_ms OFF duration (in milliseconds) for a single blink cycle.
 *
 * \param num_cycles  Number of blink cycles. If num_cycles is zero the indicator
 *                    LED should blink "forever" or until the next time this
 *                    function is called.
 */
static void
CallExternalBlinker(unsigned int on_time_ms, unsigned int off_time_ms, unsigned int num_cycles)
{
  char *execve_args[5] = { 0 };
  char *const env[]    = { "PATH=/bin", 0 };
  char on_time_ms_arg[20]  = {0};
  char off_time_ms_arg[20] = {0};
  char num_cycles_arg[20]  = {0};
  pid_t pid            = 0;

  /* No need to use snprintf here - all input is under control */
  sprintf(on_time_ms_arg,  "%u", on_time_ms);
  sprintf(off_time_ms_arg, "%u", off_time_ms);
  sprintf(num_cycles_arg,  "%u", num_cycles);

  execve_args[0] = g_node_identify_script;
  execve_args[1] = on_time_ms_arg;
  execve_args[2] = off_time_ms_arg;
  execve_args[3] = num_cycles_arg;
  execve_args[4] = 0;

  /*
   * Unless the signal for child events are ignored the parent process
   * must call wait() to wait for the child to exit to avoid zombie/defunct
   * child processes. We do not want to delay the parent by waiting for the
   * child script to exit (even though the blink script should spawn a
   * subprocess to do the actual work and exit "quickly").
   *
   * NB: The child will generally inherit the ignored signals. POSIX does not
   * clearly state if SIGCHLD is inherited, so we'll reset the handler
   * to default in the child to be safe.
   *
   * NB: The ignored signal is a process global setting - if we ever need to
   * handle child signals in the zipgateway this function must be changed.
   */

  if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
  {
    /* Not much else to do here - log the error and keep going */
    ERR_PRINTF("Failed to ignore signals from blink script process. Call to signal(SIGCHLD, SIG_IGN) failed. errno=%d\n", errno);
  }

  pid = fork();

  switch (pid)
  {
    case -1: /* Fork failed */

      ERR_PRINTF("Cannot spawn process for blink script. Call to fork() failed. errno=%d\n", errno);
      ASSERT(FALSE);
      break;

    case 0: /* Child process */

      /* This is now the child process running here. */

      /* Restore the child signal handler for the child.
       * (potential errors are ignored - there's currently nowhere to
       * log the error from the child)
       */
      signal(SIGCHLD, SIG_DFL);

      /*
       * Invoke the external script.
       * On success execve() will never return here (the new process image takes over).
       */
      execve(g_node_identify_script, execve_args, env);
      /* We should never get here. If we do execev failed. */

#ifndef ZIPGW_UNITTEST
      /*
       * Can't mock _Exit() since it's declared as "noreturn".
       * We simply don't call it during unit testing.
       */
      _Exit(EXIT_FAILURE);
#endif
      break;

    default: /* Parent process */

      /* Nothing more to do here. Back to the usual business */
      break;
  }
}


/**
 * Called on timeout of the timer controlling the length of the full blink sequence.
 *
 * Will reset all Node Identify properties to default values such
 * that the INDICATOR_GET command will reflect that the indicator
 * is no longer active.
 *
 * Please note: The timer is a "best effort" approximation to reflect the
 * status of the externally controlled LED. Since the external blinker is
 * running free after it has been started we do not know exactly if it has
 * blinked the requested number of times when this timeout occurs since the
 * external blink timers could have drifted a bit.
 *
 * We do not try to stop the LED from here - it's controlled by an external
 * script that we expect to terminate at the "right" time.
 * We only reset the internal state variables to reflect the the LED is
 * expected to no longer be active.
 *
 * The timer is not started if the client has requested an unlimited number
 * of blink cycles (\ref g_on_off_num_cycles == \c 0xFF). In that case there
 * will be no timeout.
 */
static void
IndicatorSequenceTimeoutCallback(void *ptr)
{
  g_on_off_period_length = 0;
  g_on_off_num_cycles    = 0;
  g_on_time              = 0;
}


/**
 * Calls \ref CallExternalBlinker after calculating the parameters to pass.
 *
 * Uses the global variables \ref g_on_off_period_length, \ref g_on_off_num_cycles
 * and \ref g_on_time (set by Indicator Command Class commands) to calculate
 * the values to pass to \ref CallExternalBlinker. In addition it updates the
 * \ref g_indicator_timer timer.
 *
 * Should be called whenever a SET command is received.
 */
static void
UpdateIndicator(void)
{
  uint32_t on_time_ms = 0;
  uint32_t off_time_ms = 0;
  uint32_t total_duration_ms = 0;
  uint32_t blinker_cycles = 0;

  /* Cancel timer (should work even if it's not running) */
  ctimer_stop(&g_indicator_timer);

  if (g_on_off_period_length > 0 && g_on_off_num_cycles > 0)
  {
    int on_off_period_length_ms = g_on_off_period_length * 100;

    /* The value 0x00 is special and means "symmetrical on/off blink" */
    if (g_on_time == 0)
    {
      on_time_ms = on_off_period_length_ms / 2;
    }
    else
    {
      on_time_ms = g_on_time * 100;
    }

    off_time_ms = on_off_period_length_ms - on_time_ms;

    /* The value 0xFF is special and means "run until stopped" */
    if (g_on_off_num_cycles != 0xFF)
    {
       total_duration_ms = g_on_off_period_length * g_on_off_num_cycles * 100;
       blinker_cycles = g_on_off_num_cycles;
    }
  }

  /* Tell the external "blinker" to start (or stop) */
  CallExternalBlinker(on_time_ms, off_time_ms, blinker_cycles);

  if (total_duration_ms != 0)
  {
    /*
     * We would like to know when the blinker is done. Since there's no
     * "finished" callback from the blinker, we set a timer to match the
     * expected blink duration.
     */
    ctimer_set(&g_indicator_timer,
               total_duration_ms * CLOCK_SECOND / 1000,
               IndicatorSequenceTimeoutCallback,
               NULL);
  }
}


/**
 * Maps a single indicator value to the three version 3 indicator properties
 * stored in the global variables \ref g_on_off_period_length,
 * \ref g_on_off_num_cycles and \ref g_on_time.
 *
 * Used for backward compatibility with the version 1 command class.
 * The single indicator value is either the version 1 indicator value
 * or the version 3 Indicator0 value.
 *
 * \param value The value of the indicator. We're assuming Indicator0 to be a
 *        binary indicator (a more advanced extension could be to map to a
 *        multilevel indicator where the value controls e.g. the blink frequency).
 *        \b value is interpreted like this:
 *        - \b  0: Turn the indicator off
 *        - \b >0: Turn the indicator on
 */
static void
SetIndicator0Value(BYTE value)
{
  if (value == 0)
  {
    g_on_off_period_length = 0;
    g_on_off_num_cycles    = 0;
    g_on_time              = 0;
  }
  else
  {
    g_on_off_period_length = 0xFF; /* Longest period possible */
    g_on_off_num_cycles    = 0xFF; /* Unlimited numer of periods (run until stopped) */
    g_on_time              = 0xFF; /* On the full period */
  }

}


/**
 * Maps the three version 3 indicator properties stored in the global variables
 * \ref g_on_off_period_length, \ref g_on_off_num_cycles and \ref g_on_time to
 * a single indicator value.
 *
 * \see SetIndicator0Value
 *
 * \return Single value representing the indicator state.
 */
static BYTE
GetIndicator0Value(void)
{
  /* We're not mapping the reported value 1:1 with the value (potentially)
   * set previously with SetIndicator0Value.
   * The indicator values could have been set with a V2 or V3 Set command
   * that provides more functionality than simply on and off.
   *
   * Here we report the indicator to be "ON" even if it's currently blinking
   * for a short time.
   */

  BYTE value = 0;

  if (g_on_off_period_length > 0 && g_on_off_num_cycles > 0)
  {
    value = 0xFF;
  }

  return value;
}


/**
 * Command handler for INDICATOR_SET (version 1) commands.
 *
 * Validates that the received (version 1) value is valid and updates the indicator LED accordingly.
 *
 * \param conn   The Z-wave connection.
 * \param frame  The incoming command frame.
 * \param length Length in bytes of the incoming frame.
 * \return \ref command_handler_codes_t representing the command handling status.
 *
 * \see IndicatorHandler.
 */
static command_handler_codes_t
IndicatorHandler_Set_V1(zwave_connection_t *conn, const uint8_t *frame, uint16_t length)
{
  const ZW_INDICATOR_SET_FRAME *set_cmd = (ZW_INDICATOR_SET_FRAME*) frame;

  if (set_cmd->value < 0x64 || set_cmd->value == 0xFF) //This field MUST be in the range 0x00..0x63 or 0xFF
  {
    SetIndicator0Value(set_cmd->value);
    UpdateIndicator();
    return COMMAND_HANDLED; //Value was accepted and applied, so we report Supervision SUCCESS.
  }
  else
  {
    return COMMAND_PARSE_ERROR; //Value was reserved/invalid, so Supervision returns FAIL.
  }
}


/**
 * Check if a Z-Wave frame was received via multicast (or broadcast).
 *
 * \param conn The Z-wave connection.
 *
 * \return \b TRUE if multicast, \b FALSE otherwise.
 */
BOOL IsZwaveMulticast(zwave_connection_t *conn)
{
  int is_multicast = FALSE;

  /* Does the remote IP address belong to a ZWave node? */
  if (nodeOfIP(&(conn->ripaddr)) != 0)
  {
    /* Are one of the broadcast or multicast flags set? */
    if (conn->rx_flags & (RECEIVE_STATUS_TYPE_BROAD | RECEIVE_STATUS_TYPE_MULTI))
    {
      is_multicast = TRUE;
    }
  }
  return is_multicast;
}


/**
 * Command handler for INDICATOR_SET (version 3) commands.
 *
 * Validates that the received (version 1) or version 2-3 values are valid and updates the indicator LED accordingly.
 *
 * \param conn   The Z-wave connection.
 * \param frame  The incoming command frame.
 * \param length Length in bytes of the incoming frame.
 * \return \ref command_handler_codes_t representing the command handling status.
 *
 * \see IndicatorHandler.
 */
static command_handler_codes_t
IndicatorHandler_Set_V3(zwave_connection_t *conn, const uint8_t *frame, uint16_t length)
{
  const ZW_INDICATOR_SET_1BYTE_V3_FRAME *set_cmd = (ZW_INDICATOR_SET_1BYTE_V3_FRAME*) frame;
  int obj_count               = 0;
  int calculated_frame_length = 0;

  const int frame_size_fixed_part = offsetof(ZW_INDICATOR_SET_1BYTE_V3_FRAME, variantgroup1);

  /* Make sure we received at least full frame before proceeding */
  if (length < frame_size_fixed_part)
  {
    return COMMAND_PARSE_ERROR;
  }
  else
  {
    obj_count               = set_cmd->properties1 & INDICATOR_OBJECT_COUNT_MASK;
    calculated_frame_length = frame_size_fixed_part +
                              obj_count * sizeof(VG_INDICATOR_SET_V3_VG);

    if (length < calculated_frame_length)
    {
      return COMMAND_PARSE_ERROR;
    }
  }
  /* Frame size ok - we can continue */

  if (obj_count == 0)
  {
    /* Only process indicator0Value here - ignore if obj_count > 0 */
    return IndicatorHandler_Set_V1(conn, frame, length);
  }
  else
  {
    const VG_INDICATOR_SET_V3_VG *vg_array = &(set_cmd->variantgroup1);

    BOOL indicator_object_error = FALSE;

    /* Default values */
    INDICATOR_VALUE_INFO on_off_period_length = {0};
    INDICATOR_VALUE_INFO on_off_num_cycles    = {0};
    INDICATOR_VALUE_INFO on_time              = {0};

    for (int i = 0; i < obj_count; i++)
    {
      if (vg_array[i].indicatorId == INDICATOR_IND_NODE_IDENTIFY)
      {
        BYTE value = vg_array[i].value;

        switch (vg_array[i].propertyId)
        {
          case INDICATOR_PROP_ON_OFF_PERIOD:
            on_off_period_length.value    = value;
            on_off_period_length.modified = TRUE;
            break;

          case INDICATOR_PROP_ON_OFF_CYCLES:
            on_off_num_cycles.value    = value;
            on_off_num_cycles.modified = TRUE;
            break;

          case INDICATOR_PROP_ON_TIME:
            on_time.value    = value;
            on_time.modified = TRUE;
            break;

          /* Ignore unknown properties, but remember that something went wrong for the Supervision Report */
          default:
            indicator_object_error = TRUE;
            break;
        }
      }
      else //IndicatorID was different than INDICATOR_IND_NODE_IDENTIFY, it is not supported
      {
        indicator_object_error = TRUE;
      }
    }

    /* Did we receive any of the minimum supported properties? */
    if (on_off_period_length.modified && on_off_num_cycles.modified)
    {
      g_on_off_period_length = on_off_period_length.value;
      g_on_off_num_cycles    = on_off_num_cycles.value;

      if (on_time.value > g_on_off_period_length)
      {
        /* Ignore (i.e. use default value) "on_time" if bigger than period length */
        g_on_time = 0;
        indicator_object_error = TRUE;
      }
      else
      {
        // If on_time was unspecified, it defaults to 0 anyway
        g_on_time = on_time.value;
      }

      UpdateIndicator();
      if(!indicator_object_error)
      {
        return COMMAND_HANDLED;
      }
      else
      {
        return COMMAND_PARSE_ERROR;
      }
    }
    else
    {
      /* Not really a parse error, but the minimum required parameters were not present */
      return COMMAND_PARSE_ERROR;
    }
  }
}


/**
 * Command handler for INDICATOR_GET (version 1) commands.
 *
 * Will send back an INDICATOR_REPORT (version 1) frame containing the value of the indicator.
 *
 * \param conn   The Z-wave connection.
 * \param frame  The incoming command frame.
 * \param length Length in bytes of the incoming frame.
 * \return \ref command_handler_codes_t representing the command handling status.
 * \see IndicatorHandler.
 */
static command_handler_codes_t
IndicatorHandler_Get_V1(zwave_connection_t *conn, const uint8_t *frame, uint16_t length)
{
  /* Report the state of an indicator */

  ZW_INDICATOR_REPORT_FRAME report = {0};

  report.cmdClass    = COMMAND_CLASS_INDICATOR;
  report.cmd         = INDICATOR_REPORT;
  report.value       = GetIndicator0Value();

  ZW_SendDataZIP(conn, (BYTE*) &report, sizeof(report), NULL);

  return COMMAND_HANDLED;
}


/**
 * Command handler for INDICATOR_GET (version 3) commands.
 *
 * Will send back an INDICATOR_REPORT (version 3) frame containing
 * the three values of the indicator properties.
 *
 * \param conn   The Z-wave connection.
 * \param frame  The incoming command frame.
 * \param length Length in bytes of the incoming frame.
 * \return \ref command_handler_codes_t representing the command handling status.
 *
 * \see IndicatorHandler.
 */
static command_handler_codes_t
IndicatorHandler_Get_V3(zwave_connection_t *conn, const uint8_t *frame, uint16_t length)
{
  /* Report the state of an indicator */

  const ZW_INDICATOR_GET_V3_FRAME *get_cmd = (ZW_INDICATOR_GET_V3_FRAME*) frame;

  if (length < sizeof(ZW_INDICATOR_GET_V3_FRAME))
  {
    return COMMAND_PARSE_ERROR;
  }

  if (get_cmd->indicatorId == INDICATOR_IND_NODE_IDENTIFY)
  {
    ZW_INDICATOR_REPORT_3BYTE_V3_FRAME report = {0};

    report.cmdClass        = COMMAND_CLASS_INDICATOR_V3;
    report.cmd             = INDICATOR_REPORT_V3;
    report.indicator0Value = GetIndicator0Value();
    report.properties1     = 3;  /* Number of property objects */

    report.variantgroup1.indicatorId = INDICATOR_IND_NODE_IDENTIFY;
    report.variantgroup1.propertyId  = INDICATOR_PROP_ON_OFF_PERIOD;
    report.variantgroup1.value       = g_on_off_period_length;

    report.variantgroup2.indicatorId = INDICATOR_IND_NODE_IDENTIFY;
    report.variantgroup2.propertyId  = INDICATOR_PROP_ON_OFF_CYCLES;
    report.variantgroup2.value       = g_on_off_num_cycles;

    report.variantgroup3.indicatorId = INDICATOR_IND_NODE_IDENTIFY;
    report.variantgroup3.propertyId  = INDICATOR_PROP_ON_TIME;
    report.variantgroup3.value       = g_on_time;

    ZW_SendDataZIP(conn, (BYTE*) &report, sizeof(report), NULL);
  }
  else
  {
    /* Unsupported indicator ID. Send back an empty property object */

    ZW_INDICATOR_REPORT_1BYTE_V3_FRAME report = {0};

    report.cmdClass        = COMMAND_CLASS_INDICATOR_V3;
    report.cmd             = INDICATOR_REPORT_V3;
    report.indicator0Value = GetIndicator0Value();
    report.properties1     = 1;  /* Number of property objects */

    report.variantgroup1.indicatorId = INDICATOR_REPORT_NA_V3;
    report.variantgroup1.propertyId  = 0;
    report.variantgroup1.value       = 0;

    ZW_SendDataZIP(conn, (BYTE*) &report, sizeof(report), NULL);
  }

  return COMMAND_HANDLED;
}


/**
 * Command handler for INDICATOR_SUPPORTED_GET commands.
 *
 * Will send back an INDICATOR_SUPPORTED_REPORT frame containing
 * the three properties supported by the node identify indicator.
 *
 * If the command frame contains the "wildcard" indicator ID 0x00
 * used for discovery, then the Node Identify indicator ID will be
 * included as "next indicator" in the otherwise empty report.
 *
 * \param conn   The Z-wave connection.
 * \param frame  The incoming command frame.
 * \param length Length in bytes of the incoming frame.
 * \return \ref command_handler_codes_t representing the command handling status.
 *
 * \see IndicatorHandler.
 */
static command_handler_codes_t
IndicatorHandler_SupportedGet_V3(zwave_connection_t *conn, const uint8_t *frame, uint16_t length)
{
  /* Report the supported properties of an indicator */

  const ZW_INDICATOR_SUPPORTED_GET_V3_FRAME *sget_cmd = (ZW_INDICATOR_SUPPORTED_GET_V3_FRAME*) frame;

  if (length < sizeof(ZW_INDICATOR_SUPPORTED_GET_V3_FRAME))
  {
    return COMMAND_PARSE_ERROR;
  }

  if ((sget_cmd->indicatorId == INDICATOR_IND_NODE_IDENTIFY) || (sget_cmd->indicatorId == INDICATOR_IND_NA))
  {
    /*
     * If indicator ID == INDICATOR_IND_NA (0x00) the client has requested to
     * discover supported indicator IDs. Since we only support the node identify
     * indicator we advertise this in the exact same manner as if the node
     * identify indicator was requested explicitly.
     */

    static const ZW_INDICATOR_SUPPORTED_REPORT_1BYTE_V3_FRAME report =
    {
      .cmdClass                  = COMMAND_CLASS_INDICATOR_V3,
      .cmd                       = INDICATOR_SUPPORTED_REPORT_V3,
      .indicatorId               = INDICATOR_IND_NODE_IDENTIFY,
      .nextIndicatorId           = INDICATOR_IND_NA,
      .properties1               = 1,
      .propertySupportedBitMask1 = (1 << INDICATOR_PROP_ON_OFF_PERIOD) |
                                   (1 << INDICATOR_PROP_ON_OFF_CYCLES) |
                                   (1 << INDICATOR_PROP_ON_TIME)
    };

    ZW_SendDataZIP(conn, (BYTE*) &report, sizeof(report), NULL);
  }
  else
  {
    /* The indicator is not supported, send back an empty report. */

    /*
     * All the "Supported Report" structs have one or more bitmasks.
     * Here we must send one without the bitmask, so we use the struct
     * with one bitmask, but tell ZW_SendDataZIP to only send the
     * "fixed header" part (leaving out the bitmask).
     */

    ZW_INDICATOR_SUPPORTED_REPORT_1BYTE_V3_FRAME report =
    {
      .cmdClass                  = COMMAND_CLASS_INDICATOR_V3,
      .cmd                       = INDICATOR_SUPPORTED_REPORT_V3,
      .indicatorId               = INDICATOR_IND_NA,
      .nextIndicatorId           = INDICATOR_IND_NA,
      .properties1               = 0,
      .propertySupportedBitMask1 = 0
    };

    /* Leave out the bitmask when sending */
    size_t send_size = offsetof(ZW_INDICATOR_SUPPORTED_REPORT_1BYTE_V3_FRAME,
                       propertySupportedBitMask1);

    ZW_SendDataZIP(conn, (BYTE*) &report, send_size, NULL);
  }

  return COMMAND_HANDLED;
}

void IndicatorDefaultSet(void) {
   CallExternalBlinker(0, 0, 0);
   ctimer_stop(&g_indicator_timer);
   g_on_off_period_length = 0;
   g_on_off_num_cycles = 0;
   g_on_time = 0;
}

/*
 * Initialization of the Indicator Command Class handler.
 *
 * (see doxygen comments in header file)
 */
void
IndicatorHandler_Init(void)
{
  char cfg_val[512] = {0};

  /*
   * Sanitize the external indicator blink script read from zipgateway.cfg.
   * For security reasons the script must be located in INSTALL_SYSCONFDIR
   * (owned by root). We strip off path and command line arguments from
   * the script name (ideally the script name in the config file should
   * be without those elements already - but we don't know for sure).
   */

  DBG_PRINTF("Indicator blink script from config file (not sanitized): %s\n", cfg.node_identify_script);

  /* Get a modifiable copy of the script name */
  strncpy(cfg_val, cfg.node_identify_script, sizeof(cfg_val));
  ASSERT(cfg_val[sizeof(cfg_val) - 1] == 0);
  cfg_val[sizeof(cfg_val) - 1] = 0;  /* Just to be safe */

  /*
   * Strip off any arguments etc. following the script name.
   * This implies that spaces in the script name is not allowed
   */
  char * first_cfg_element = strtok(cfg_val, " ");
  if (first_cfg_element)
  {
    /* Strip off any path element - we only accept scripts in INSTALL_SYSCONFDIR */
    const char *script_basename = basename(first_cfg_element);

    if (script_basename)
    {
      int nchars = snprintf(g_node_identify_script, sizeof(g_node_identify_script),
                            "%s/%s",
                            INSTALL_SYSCONFDIR,
                            script_basename);

      ASSERT(nchars < sizeof(g_node_identify_script));
    }
    else
    {
      ERR_PRINTF("Invalid indicator blink script from zipgateway.cfg: %s\n", cfg.node_identify_script);
    }
  }

  LOG_PRINTF("Using indicator blink script: %s\n", g_node_identify_script);

  /* Ensure that the blinker is off */
  CallExternalBlinker(0, 0, 0);
}


/*
 * Entry point for the Indicator Command Class handler.
 * Dispatches incoming commands to the appropriate handler function.
 *
 * (see doxygen comments in header file)
 */
command_handler_codes_t
IndicatorHandler(zwave_connection_t *conn, uint8_t *frame, uint16_t length)
{
  command_handler_codes_t  rc       = COMMAND_NOT_SUPPORTED;
  ZW_COMMON_FRAME         *zw_frame = (ZW_COMMON_FRAME*) frame;

  /* Make sure we got the minimum required frame size */
  if (length < sizeof(ZW_COMMON_FRAME))
  {
    rc = COMMAND_NOT_SUPPORTED; //Supervision must return NO_SUPPORT if the command byte was no included in the command class.
    return rc; // Exit the function immediately and do not enter the switch()
  }


  switch (zw_frame->cmd)
  {
    case INDICATOR_SET_V3:
      if (length == sizeof(ZW_INDICATOR_SET_FRAME))
      {
        rc = IndicatorHandler_Set_V1(conn, frame, length);
      }
      else
      {
        rc = IndicatorHandler_Set_V3(conn, frame, length);
      }
      break;

    case INDICATOR_GET_V3:
      if (IsZwaveMulticast(conn))
      {
        /* Silently ignore the command if received via multicast. */
        rc = COMMAND_HANDLED;
      }
      else
      {
        if (length == sizeof(ZW_INDICATOR_GET_FRAME))
        {
          rc = IndicatorHandler_Get_V1(conn, frame, length);
        }
        else
        {
          rc = IndicatorHandler_Get_V3(conn, frame, length);
        }
      }
      break;

    case INDICATOR_SUPPORTED_GET_V3:
      if (IsZwaveMulticast(conn))
      {
        /* Silently ignore the command if received via multicast. */
        rc = COMMAND_HANDLED;
      }
      else
      {
        rc = IndicatorHandler_SupportedGet_V3(conn, frame, length);
      }
      break;

    default:
      rc = COMMAND_NOT_SUPPORTED;
      break;
  }

  return rc;
}

/**
 * Register the Indicator Command Class handler.
 *
 * \ref IndicatorHandler will then automatically be called
 * whenever a COMMAND_CLASS_INDICATOR frame is received.
 */
REGISTER_HANDLER(IndicatorHandler, IndicatorHandler_Init, COMMAND_CLASS_INDICATOR_V3, INDICATOR_VERSION_V3, NET_SCHEME);
