/*
 * config.c
 *
 *  Created on: 2020年10月24日
 *      Author: kang
 */

/*
 * header
 */
//system header
#include <pthread.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
//#include <dmalloc.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
//server header
#include "config.h"
#include "micloud_interface.h"

/*
 * static
 */
static pthread_rwlock_t			lock;
static int						dirty;
static micloud_config_t			micloud_config;
static config_map_t micloud_config_profile_map[] = {
	    {"enable", 			&(micloud_config.profile.motion_detection_switch), 		cfg_s32, 1,0,0,1,},
	    {"cloud_report",	&(micloud_config.profile.alarm_push),					cfg_s32, 1,0,0,1,},
	    {"alarm_interval", 	&(micloud_config.profile.alarm_interval), 				cfg_u32, 3,0,0,30,},
	    {"sensitivity",		&(micloud_config.profile.motion_sensitivity),			cfg_u32, 50,0,0,100,},
	    {"start", 			&(micloud_config.profile.motion_start), 				cfg_string, '08:00',0, 0,32,},
	    {"end",				&(micloud_config.profile.motion_end),					cfg_string, '20:00',0, 0,32,},
	    {"cloud_switch",	&(micloud_config.profile.cloud_upload_switch),			cfg_s32, 1,0, 0,1,},
	    {"quality",			&(micloud_config.profile.quality),						cfg_s32, 2,0, 0,2,},
	    {"log_level",		&(micloud_config.profile.log_level),					cfg_s32, 4,0, 0,4,},
	    {NULL,},
};



//function
static int micloud_config_device_read(void);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
int micloud_config_save(void)
{
	int ret = 0;
	message_t msg;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_MICLOUD_PROFILE_PATH);
	if( misc_get_bit(dirty, CONFIG_MICLOUD_PROFILE) ) {
		ret = write_config_file(&micloud_config_profile_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_MICLOUD_PROFILE, 0);
	}
	if( !dirty ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_REMOVE;
		msg.arg_in.handler = micloud_config_save;
		/****************************/
		manager_message(&msg);
	}
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);
	log_qcy(DEBUG_INFO,"------into micloud_config_save  --end--\n");
	return ret;
}

static int micloud_config_device_read(void)
{
	FILE *fp = NULL;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	int pos = 0;
	int len = 0;
	char *data = NULL;
	int fileSize = 0;
	int ret;
    memset(&micloud_config.profile, 0, sizeof(micloud_config));
	//read device.conf
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.miio_path, CONFIG_MICLOUD_DEVICE_PATH);
	fp = fopen(fname, "rb");
	if (fp == NULL) {
		return -1;
	}
	if (0 != fseek(fp, 0, SEEK_END)) {
		fclose(fp);
		return -1;
	}
	fileSize = ftell(fp);
    if(fileSize > 0) {
    	data = malloc(fileSize);
    	if(!data) {
    		fclose(fp);
    		return -1;
    	}
    	memset(data, 0, fileSize);
    	if(0 != fseek(fp, 0, SEEK_SET)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	if (fread(data, 1, fileSize, fp) != (fileSize)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	fclose(fp);
    	char *ptr_model = 0;
    	ptr_model = strstr(data, "model=");
    	char *p,*m;

    	p = ptr_model+6; m = micloud_config.profile.model;
    	    		while(*p!='\n' && *p!='\0') {
    	    			memcpy(m, p, 1);
    	    			m++;p++;
    	    		}
    	    		*m = '\0';

    	free(data);
    }
	fileSize = 0;
	len = 0;

	//read device.token
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.miio_path, CONFIG_MICLOUD_TOKEN_PATH);
	fp = fopen(fname, "rb");
	if (fp == NULL) {
		return -1;
	}
	if (0 != fseek(fp, 0, SEEK_END)) {
		fclose(fp);
		return -1;
	}
	fileSize = ftell(fp);
    if(fileSize > 0) {
    	data = malloc(fileSize);
    	if(!data) {
    		fclose(fp);
    		return -1;
    	}
    	memset(data, 0, fileSize);
    	if(0 != fseek(fp, 0, SEEK_SET)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	if (fread(data, 1, fileSize, fp) != (fileSize)) {
    		free(data);
    		fclose(fp);
    		return -1;
    	}
    	fclose(fp);
	    if(data[fileSize - 1] == 0xa)
		memcpy(micloud_config.profile.token,data,fileSize-1);
		memcpy(micloud_config.profile.token,data,fileSize-1);
    	free(data);
    }
    else {
		log_qcy(DEBUG_SERIOUS, "device.token -->file date err!!!\n");
        return -1;
    }
	return 0;
}



/*
 * interface
 */
int config_micloud_read(micloud_config_t *mconfig)
{
	int ret,ret1=0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	pthread_rwlock_init(&lock, NULL);
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	memset(&micloud_config, 0, sizeof(micloud_config_t));
	ret = micloud_config_device_read();
	ret1 |= ret;

	memset(fname,0,sizeof(fname));
	snprintf(fname,MAX_SYSTEM_STRING_SIZE*2,"%s%s",_config_.qcy_path, CONFIG_MICLOUD_PROFILE_PATH);
	ret = read_config_file(&micloud_config_profile_map, fname);

	if(!ret) {
		misc_set_bit(&micloud_config.status, CONFIG_MICLOUD_PROFILE,1);
	}
	else
		misc_set_bit(&micloud_config.status, CONFIG_MICLOUD_PROFILE,0);

	ret1 |= ret;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);
	ret1 |= ret;
	memcpy(mconfig, &micloud_config, sizeof(micloud_config_t));

	return ret1;
}

int config_micloud_set(int module, void *arg)
{
	int ret = 0;
	log_qcy(DEBUG_INFO, "----------into  config_micloud_set  func------\n ");
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	if(dirty==0) {
		message_t msg;
		message_arg_t arg;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_ADD;
		msg.sender = SERVER_MISS;
		msg.arg_in.cat = 30000;	//1min
		msg.arg_in.dog = 0;
		msg.arg_in.duck = 0;
		msg.arg_in.handler = &micloud_config_save;
		/****************************/
		manager_message(&msg);
	}
	misc_set_bit(&dirty, module, 1);
	if( module == CONFIG_MICLOUD_PROFILE) {
		memcpy( (micloud_pro_info_t*)(&micloud_config.profile), arg, sizeof(micloud_pro_info_t));
	}
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);

	return ret;
}

int config_micloud_get_config_status(int module)
{
	int st,ret=0;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	if(module==-1)
		st = micloud_config.status;
	else
		st = misc_get_bit(micloud_config.status, module);
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);
	return st;
}









