/* © 2014 Silicon Laboratories Inc.
 */
#include "contiki.h"
struct type_string {
  u8_t code;
  char* name;
};

static const struct type_string type_codes[] = {
    { 0x0003, "Av Control Point"},
    { 0x0004, "Display"},
    { 0x0040, "Entry Control"},
    { 0x0001, "Generic Controller"},
    { 0x0031, "Meter"},
    { 0x0030, "Meter Pulse"},
    { 0x00ff, "Non Interoperable"},
    { 0x000f, "Repeater Slave"},
    { 0x0017, "Security Panel"},
    { 0x0050, "Semi Interoperable"},
    { 0x00a1, "Sensor Alarm"},
    { 0x0020, "Sensor Binary"},
    { 0x0021, "Sensor Multilevel"},
    { 0x0002, "Static Controller"},
    { 0x0010, "Switch Binary"},
    { 0x0011, "Switch Multilevel"},
    { 0x0012, "Switch Remote"},
    { 0x0013, "Switch Toggle"},
    { 0x0008, "Thermostat"},
    { 0x0016, "Ventilation"},
    { 0x0009, "Window Covering"},
    { 0x0015, "Zip Node"},
    { 0x0018, "Wall Controller"},
    { 0x0007, "Sensor Notification"},
    {0,0},
};

/**
 * Get String description of a generic device type
 */
const char* get_gen_type_string(int gen_type) {
  u8_t i;
  for(i=0; type_codes[i].name; i++) {
    if(type_codes[i].code == gen_type) {
      return type_codes[i].name;
    }
  }
  return "(unknown)";
}
