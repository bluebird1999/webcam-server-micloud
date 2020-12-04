/*
 * config.h
 *
 *  Created on: 2020年10月24日
 *      Author: kang
 */

#ifndef SERVER_MICLOUD_CONFIG_H_
#define SERVER_MICLOUD_CONFIG_H_

#include "micloud_interface.h"

#define		CONFIG_MICLOUD_PROFILE			0

#define 	CONFIG_MICLOUD_PROFILE_PATH			"config/micloud_pro.config"
#define		CONFIG_MICLOUD_DEVICE_PATH			"device.conf"
#define		CONFIG_MICLOUD_TOKEN_PATH			"device.token"


/*
 * function
 */
int micloud_config_save(void);
int config_micloud_read(micloud_config_t *mconfig);
int config_micloud_set(int module, void *arg);
int config_micloud_get_config_status(int module);

#endif /* SERVER_MICLOUD_CONFIG_H_ */
