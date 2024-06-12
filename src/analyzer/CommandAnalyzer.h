/* © 2017 Silicon Laboratories Inc.
 */
/*
 * command_analyzer.h
 *
 *  Created on: Nov 24, 2016
 *      Author: aes
 */

#ifndef SRC_COMMANDANALYZER_H_
#define SRC_COMMANDANALYZER_H_
#include<stdint.h>

/**
 * Check is given command is a get.
 * \param cls The command class number
 * \param cmd The command number
 * \return True is the command is a Get type message. If the command is unknown, this function returns false.
 */
int CommandAnalyzerIsGet(uint8_t cls, uint8_t cmd);

/**
 * Check if a given command is a set.
 * \param cls The command class number
 * \param cmd The command number
 * \return True is the command is a Set type message. If the command is unknown, this function returns false.
 */
int CommandAnalyzerIsSet(uint8_t cls, uint8_t cmd);


/**
 * Check if a given command is a report.
 * \param cls The command class number
 * \param cmd The command number
 * \return True is the command is a Report type message. If the command is unknown, this function returns false.
 */
int CommandAnalyzerIsReport(uint8_t cls, uint8_t cmd);


/**
 * Check if a given command is a supporting command.
 * \param cls The command class number
 * \param cmd The command number
 * \return True is the command is a Supporting type message. If the command is unknown, this function returns false.
 */
int CommandAnalyzerIsSupporting(uint8_t cls, uint8_t cmd);

#endif /* SRC_COMMANDANALYZER_H_ */
