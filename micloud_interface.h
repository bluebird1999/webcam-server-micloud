/*
 * micloud_interface.h
 *
 *  Created on: 2020年10月24日
 *      Author: kang
 */

#ifndef SERVER_MICLOUD_MICLOUD_INTERFACE_H_
#define SERVER_MICLOUD_MICLOUD_INTERFACE_H_


/*
 * header
 */
#include "../../manager/manager_interface.h"
#include "../../manager/global_interface.h"
/*
 * define
 */

#define		SERVER_MICLOUD_VERSION_STRING			"alpha-5.2"

#define		MSG_MICLOUD_BASE						   (SERVER_MICLOUD<<16)
#define		MSG_MICLOUD_SIGINT							MSG_MICLOUD_BASE | 0x0000
#define		MSG_MICLOUD_SIGINT_ACK						MSG_MICLOUD_BASE | 0x1000
//if video_md.config parameter change
#define		MSG_MICLOUD_CHANGE_PARA  				  	MSG_MICLOUD_BASE | 0x0010

#define		MSG_MICLOUD_VIDEO_DATA						MSG_MICLOUD_BASE | 0x0011
#define		MSG_MICLOUD_AUDIO_DATA						MSG_MICLOUD_BASE | 0x0012
//#define		MSG_MICLOUD_INIT_RESOURCE					MSG_MICLOUD_BASE | 0x0013
#define		MSG_MIIO_INIT_REPORT_ACK					MSG_MICLOUD_BASE | 0x1014
#define		MSG_MIIO_INIT_REPORT						MSG_MICLOUD_BASE | 0x0014
//control
#define		MICLOUD_CTRL_MOTION_SWITCH						0x0100		//5:1
#define 	MICLOUD_CTRL_MOTION_ALARM_INTERVAL				0x0101		//5:2
#define 	MICLOUD_CTRL_MOTION_SENSITIVITY               	0x0102		//5:3
#define 	MICLOUD_CTRL_MOTION_START           			0x0103		//5:4
#define 	MICLOUD_CTRL_MOTION_END                 		0x0104		//5:5
//#define 	MICLOUD_CTRL_CUSTOM_MICLOUD_UPLOAD_SWITCH       0x0005		//6:8
#define 	MICLOUD_CTRL_CUSTOM_WARNING_PUSH       			0x0106		//6:9


#define     MICLOUD_EVENT_TYPE_OBJECTMOTION                  			0x0107
#define     MICLOUD_EVENT_TYPE_PEOPLEMOTION                  			0x0108
//#define     CLOUD_EVENT_TYPE_BABYCRY                  				0x0109
//#define     CLOUD_EVENT_TYPE_FACE                  					0x010a
//#define     CLOUD_EVENT_TYPE_KNOWFACE                  				0x010b
/*
 * structure
 */
typedef struct micloud_pro_info_t {

	int32_t 	motion_detection_switch;
	uint32_t    alarm_interval;
	uint32_t	motion_sensitivity;
	char		motion_start[MAX_SYSTEM_STRING_SIZE];
	char		motion_end[MAX_SYSTEM_STRING_SIZE];
	int32_t		cloud_upload_switch;
	int32_t		alarm_push;
	int32_t		quality;
	char 		did[MAX_SYSTEM_STRING_SIZE];
	char 		token[2*MAX_SYSTEM_STRING_SIZE];
	char 		model[MAX_SYSTEM_STRING_SIZE];
	int32_t 	log_level;

} micloud_pro_info_t;

typedef struct  micloud_config_t {
	int								status;
	micloud_pro_info_t				profile;
} micloud_config_t;
/*
 * function
 */

int server_micloud_start(void);
int server_micloud_message(message_t *msg);
int server_micloud_video_message(message_t *msg);
int server_micloud_audio_message(message_t *msg);


#endif /* SERVER_MICLOUD_MICLOUD_INTERFACE_H_ */
